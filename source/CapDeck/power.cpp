// CapDeck - power: Dim -> Screen off -> Doze -> Spore mode.
//
// The screen is only the part you see. The real saving is the radios and the chip:
//   Dim         screen timeout reached: backlight drops, CPU slows to 80 MHz.
//   Screen off  15 s later: display + LED off.
//   Doze        if nothing needs to stay awake: Wi-Fi and LoRa go down and the ESP32 light-sleeps.
//               It resumes exactly where it was (no reboot). The wake key is checked in hardware.
//   Spore mode  after a long doze (or at critical battery): deep sleep, near-zero draw. Waking reboots.
//
// Wake keys come straight from the keyboard chip (TCA8418): it keeps every press in its own buffer and holds
// its interrupt line (GPIO11) low until that buffer is read. So a press can wake the chip, and we can read
// *which* key it was - even after deep sleep, before the keyboard driver starts (and flushes the buffer).
#include "core.h"
#include <RadioLib.h>
#include <Wire.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <sys/time.h>

const char *WAKE_KEY_NAMES[] = {"Enter", "Any key", "Fn + Enter"};

#define KB_INT GPIO_NUM_11
#define TCA_ADDR 0x34
#define TCA_INT_STAT 0x02
#define TCA_KEY_LCK_EC 0x03
#define TCA_KEY_EVENT_A 0x04

enum Stage : uint8_t { AWAKE, DIM, OFF };
static Stage stage = AWAKE;
static uint32_t lastActivity = 0;
static uint32_t lockUntil = 0;
static uint32_t alarmAt = 0;  // unix seconds
static String alarmWhat;
static bool bootAlarm = false;
RTC_DATA_ATTR static uint32_t rtcAlarmAt = 0;  // survives deep sleep
RTC_DATA_ATTR static char rtcAlarmWhat[64];
RTC_DATA_ATTR static bool rtcFnDown = false;   // Fn held across a deep-sleep check (Fn+Enter mode)

// ------------------------------------------------------------------ keyboard chip, read directly
// Raw TCA8418 event -> the library's (row, col) on the 4x14 key map. Enter is (2,13), Fn is (2,0).
static void tcaKey(uint8_t ev, int &row, int &col) {
  int b = (ev & 0x7F) - 1;
  int rr = b / 10, rc = b % 10;
  col = rr * 2 + (rc > 3 ? 1 : 0);
  row = rc % 4;
}

struct WakeScan { bool anyPress = false, match = false; };

// Drain the chip's buffer; decide whether the wake key was pressed. readReg/writeReg abstract the I2C driver
// (Wire before boot, M5Unified's internal bus after).
template <typename RD, typename WR>
static WakeScan scanKeys(uint8_t rule, bool &fnDown, RD readReg, WR writeReg) {
  WakeScan w;
  for (int guard = 0; guard < 12; guard++) {
    uint8_t count = readReg(TCA_KEY_LCK_EC) & 0x0F;
    if (!count) break;
    for (int i = 0; i < count; i++) {
      uint8_t ev = readReg(TCA_KEY_EVENT_A);
      if (!ev) continue;
      bool press = ev & 0x80;
      int row, col;
      tcaKey(ev, row, col);
      bool isEnter = row == 2 && col == 13, isFn = row == 2 && col == 0;
      if (isFn) fnDown = press;
      if (!press) continue;
      w.anyPress = true;
      if (rule == 1) w.match = true;
      else if (rule == 0 && isEnter) w.match = true;
      else if (rule == 2 && isEnter && fnDown) w.match = true;
    }
  }
  writeReg(TCA_INT_STAT, 0x01);  // release the interrupt line
  return w;
}

static WakeScan scanKeysNow(uint8_t rule, bool &fnDown) {
  return scanKeys(
      rule, fnDown, [](uint8_t r) { return m5::In_I2C.readRegister8(TCA_ADDR, r, 400000); },
      [](uint8_t r, uint8_t v) { m5::In_I2C.writeRegister8(TCA_ADDR, r, v, 400000); });
}

static uint8_t wireRead(uint8_t reg) {
  Wire.beginTransmission(TCA_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return 0;
  if (Wire.requestFrom((int)TCA_ADDR, 1) != 1) return 0;
  return Wire.read();
}
static void wireWrite(uint8_t reg, uint8_t v) {
  Wire.beginTransmission(TCA_ADDR);
  Wire.write(reg);
  Wire.write(v);
  Wire.endTransmission();
}

static void armDeepSleepAndGo() {
  rtc_gpio_pullup_en(KB_INT);
  rtc_gpio_pulldown_dis(KB_INT);
  esp_sleep_enable_ext0_wakeup(KB_INT, 0);
  if (rtcAlarmAt) {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    if (rtcAlarmAt > (uint32_t)tv.tv_sec) esp_sleep_enable_timer_wakeup((uint64_t)(rtcAlarmAt - tv.tv_sec) * 1000000ULL);
  }
  esp_deep_sleep_start();
}

// ------------------------------------------------------------------ boot
void powerBootCheck() {
  auto cause = esp_sleep_get_wakeup_cause();
  if (cause == ESP_SLEEP_WAKEUP_TIMER) {
    bootAlarm = rtcAlarmAt != 0;
    return;
  }
  if (cause != ESP_SLEEP_WAKEUP_EXT0) return;
  rtc_gpio_deinit(KB_INT);  // hand the pin back to the keyboard driver
  Preferences p;
  p.begin("capdeck", true);
  uint8_t rule = p.getUChar("wakekey", 0);
  p.end();
  if (rule == 1) return;  // any key wakes
  Wire.begin(8, 9, 400000);
  bool fn = rtcFnDown;
  WakeScan w = scanKeys(rule, fn, wireRead, wireWrite);
  Wire.end();
  rtcFnDown = fn;
  // Fail open: if the buffer was empty (something else read it), wake rather than risk never waking.
  if (w.match || !w.anyPress) return;
  armDeepSleepAndGo();  // a stray pocket press - straight back to sleep
}

static void fireAlarm();

void powerAfterBoot() {
  lastActivity = millis();
  if (bootAlarm && rtcAlarmAt) {
    alarmAt = rtcAlarmAt;
    alarmWhat = rtcAlarmWhat;
    fireAlarm();
  }
}

// ------------------------------------------------------------------ screen stages
void wakeScreen() {
  lastActivity = millis();
  if (stage == AWAKE) return;
  if (stage == OFF) M5Cardputer.Display.wakeup();
  stage = AWAKE;
  setCpuFrequencyMhz(240);
  applyBrightness();
  ledApply();
}

bool powerOnKey() {
  bool wasAsleep = stage != AWAKE;
  wakeScreen();
  return wasAsleep;
}

bool screenIsOff() { return stage == OFF; }

void stayAwake() { lockUntil = millis() + 4000; }

static bool mustStayAwake() {
  if ((int32_t)(lockUntil - millis()) > 0) return true;
  if (portalActive()) return true;
  if (cfg.meshListen && radioMode == RM_MESH && loraPresent) return true;  // Mycelium is listening
  if (forageRunning || rhizoRunning) return true;  // relaying keeps the radio up even with the screen off      // a foray keeps the radios up even with the screen off (pocket mode)
  if (batteryCharging()) return true;  // plugged in: the battery isn't the concern
  return false;
}

// ------------------------------------------------------------------ alarms (Nightcap's ISS chime)
void powerSetAlarm(uint32_t unixTime, const char *what) {
  alarmAt = unixTime;
  alarmWhat = what ? what : "";
  rtcAlarmAt = unixTime;
  strncpy(rtcAlarmWhat, alarmWhat.c_str(), sizeof rtcAlarmWhat - 1);
  rtcAlarmWhat[sizeof rtcAlarmWhat - 1] = 0;
}

static uint32_t unixNow() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return tv.tv_sec;
}

static bool alarmDue() { return alarmAt && timeValid() && unixNow() >= alarmAt; }

void skyAlarmFired();  // nightcap.cpp: chime, then schedule the next one

static void fireAlarm() {
  String what = alarmWhat;
  alarmAt = 0;
  rtcAlarmAt = 0;
  wakeScreen();
  for (int i = 0; i < 3; i++) {  // a little three-note chime
    beep(1760, 90);
    delay(130);
    beep(2349, 90);
    delay(130);
    beep(2960, 160);
    delay(380);
  }
  ledPulse(0);
  toast(what.length() ? what : String("Nightcap alarm"), col::PORTAL, 6000);
  skyAlarmFired();
}

// ------------------------------------------------------------------ doze: light sleep with the radios down
static void doze() {
  bool wifiWas = netState() == NetState::UP || netState() == NetState::CONNECTING;
  if (wifiWas) wifiStop();
  if (loraPresent && loraRadio()) loraRadio()->sleep(true);  // warm sleep keeps its settings
  rgbLedWrite(PIN_LED, 0, 0, 0);
  uint32_t dozeStart = millis();
  bool fnDown = false;
  bool wake = false, alarm = false;
  while (!wake) {
    bool tmp = fnDown;
    scanKeysNow(cfg.wakeKey, tmp);  // the interrupt line must be idle, or we'd wake at once
    gpio_intr_disable(KB_INT);      // no ISR storm on the level-triggered wake
    gpio_wakeup_enable(KB_INT, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
    uint64_t us = 30ULL * 1000000ULL;
    if (alarmAt && timeValid()) {
      uint32_t now = unixNow();
      if (alarmAt <= now) us = 1000;
      else if ((uint64_t)(alarmAt - now) * 1000000ULL < us) us = (uint64_t)(alarmAt - now) * 1000000ULL;
    }
    esp_sleep_enable_timer_wakeup(us);
    esp_light_sleep_start();
    gpio_wakeup_disable(KB_INT);
    gpio_set_intr_type(KB_INT, GPIO_INTR_ANYEDGE);
    auto cause = esp_sleep_get_wakeup_cause();
    if (cause == ESP_SLEEP_WAKEUP_GPIO) {
      WakeScan w = scanKeysNow(cfg.wakeKey, fnDown);
      if (w.match) wake = true;
    } else if (cause == ESP_SLEEP_WAKEUP_TIMER) {
      if (alarmDue()) { wake = true; alarm = true; }
      int lvl = M5Cardputer.Power.getBatteryLevel();
      if (lvl >= 0 && lvl <= 3 && !batteryCharging()) { gpio_intr_enable(KB_INT); sporeMode("Battery critical"); }
      if (cfg.sporeMins && millis() - dozeStart > cfg.sporeMins * 60000UL) { gpio_intr_enable(KB_INT); sporeMode("Long nap"); }
    } else {
      wake = true;
    }
    gpio_intr_enable(KB_INT);
  }
  if (loraPresent && loraRadio()) loraSetMode(radioMode);  // back to listening
  if (wifiWas) wifiStart();  // reconnects in the background; the status icons pop back one by one
  wakeScreen();
  if (alarm) fireAlarm();
}

void powerIdle() {
  uint32_t now = millis();
  if (!lastActivity) lastActivity = now;
  static uint32_t lastBatt = 0;
  static uint8_t crit = 0;
  if (now - lastBatt > 20000) {  // critical battery: three low readings in a row, then Spore mode
    lastBatt = now;
    int lvl = M5Cardputer.Power.getBatteryLevel();
    crit = (lvl >= 0 && lvl <= 3 && !batteryCharging()) ? crit + 1 : 0;
    if (crit >= 3) sporeMode("Battery critical");
  }
  if (alarmDue()) fireAlarm();
  if (!cfg.dimSecs) return;  // screen timeout "Always on"
  uint32_t idle = now - lastActivity;
  if (stage == AWAKE && idle > cfg.dimSecs * 1000UL) {
    stage = DIM;
    M5Cardputer.Display.setBrightness(max(8, cfg.brightness / 8));
    if (!mustStayAwake()) setCpuFrequencyMhz(80);
  }
  if (stage == DIM && idle > (cfg.dimSecs + 15) * 1000UL) {
    stage = OFF;
    M5Cardputer.Display.setBrightness(0);
    M5Cardputer.Display.sleep();
  }
  if (stage == OFF && cfg.doze && !mustStayAwake()) doze();
}

// ------------------------------------------------------------------ Spore mode: deep sleep
void sporeMode(const char *why) {
  // a short goodbye: the cap closes over the spores
  M5Cardputer.Display.wakeup();
  applyBrightness();
  for (int f = 0; f <= 12; f++) {
    canvas.fillScreen(col::BG);
    int r = 26 - f;
    canvas.fillArc(SCR_W / 2, 70, 0, max(4, r), 180, 360, accent());
    canvas.fillRect(SCR_W / 2 - 3, 70, 6, max(2, 18 - f), col::DIM);
    canvas.setFont(&fonts::FreeSansBold9pt7b);
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(col::TEXT);
    canvas.drawString("Spore mode", SCR_W / 2, 100);
    canvas.setFont(&fonts::Font0);
    canvas.setTextColor(col::DIM);
    canvas.drawString(String(why) + "  -  " + WAKE_KEY_NAMES[cfg.wakeKey % 3] + " wakes", SCR_W / 2, 118);
    pushFrame();
    delay(f == 0 ? 900 : 60);
  }
  meshSaveIfDirty();
  wifiStop();
  if (loraPresent && loraRadio()) loraRadio()->sleep(false);
  M5Cardputer.Display.setBrightness(0);
  M5Cardputer.Display.sleep();
  rgbLedWrite(PIN_LED, 0, 0, 0);
  {  // persist the wake rule where the boot check can read it before NVS-backed settings load
    Preferences p;
    p.begin("capdeck", false);
    p.putUChar("wakekey", cfg.wakeKey);
    p.end();
  }
  bool fn = false;
  scanKeysNow(cfg.wakeKey, fn);  // the interrupt line must be high, or we'd wake immediately
  rtcFnDown = false;
  armDeepSleepAndGo();
}
