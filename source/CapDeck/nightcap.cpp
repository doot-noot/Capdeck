// CapDeck - Nightcap: the sky above you.
// A live planetarium for wherever you're standing. Stars by brightness, the classical planets drawn
// as their old glyphs, the Moon with its real phase, and satellites as Tracers streaking overhead
// from the GPS module's own data. The ISS is propagated from its orbit so CapDeck can chime before
// it passes - even while the device sleeps.
//
// The Summer Triangle - Vega, Deneb and Altair - is picked out in gold, because someone asked.
//
// Palette: Bruise Blue. The night isn't black; it's the deep blue-violet of a day-old bruise, going
// greenish at the horizon where the last light drains away.
#include "core.h"
#include <Preferences.h>
#include <math.h>

#ifndef DEG2RAD
#define DEG2RAD (M_PI / 180.0)
#define RAD2DEG (180.0 / M_PI)
#endif

// ------------------------------------------------------------------ Bruise Blue palette
static uint16_t BRUISE_ZENITH, BRUISE_HORIZON, BRUISE_TWI, STAR_WARM;
static void bruiseInit() {
  BRUISE_ZENITH = canvas.color565(0x10, 0x08, 0x24);   // deep violet overhead
  BRUISE_HORIZON = canvas.color565(0x12, 0x2A, 0x2E);  // bruised green at the rim
  BRUISE_TWI = canvas.color565(0x3A, 0x1E, 0x40);       // twilight band
  STAR_WARM = canvas.color565(0xFF, 0xF2, 0xD8);
}

// ------------------------------------------------------------------ time / coordinates
static double julianDate(time_t t) { return (double)t / 86400.0 + 2440587.5; }
static double gmstDeg(double jd) {
  double d = jd - 2451545.0;
  double g = 280.46061837 + 360.98564736629 * d;
  g = fmod(g, 360.0);
  return g < 0 ? g + 360.0 : g;
}
// equatorial (RA hours, Dec deg) -> horizontal (alt, az deg), given observer lat/lon and LST
static void eqToHoriz(double raH, double decDeg, double latDeg, double lstDeg, double &alt, double &az) {
  double ha = lstDeg - raH * 15.0;
  double haR = ha * DEG2RAD, decR = decDeg * DEG2RAD, latR = latDeg * DEG2RAD;
  double sinAlt = sin(decR) * sin(latR) + cos(decR) * cos(latR) * cos(haR);
  alt = asin(fmax(-1.0, fmin(1.0, sinAlt))) * RAD2DEG;
  double y = -sin(haR);
  double x = tan(decR) * cos(latR) - sin(latR) * cos(haR);
  az = atan2(y, x) * RAD2DEG;
  if (az < 0) az += 360.0;
}

// ------------------------------------------------------------------ star catalogue (J2000)
struct Star { float ra, dec, mag; const char *name; };
// A compact bright-star set: enough to recognise the sky. RA in hours, Dec in degrees.
static const Star STARS[] = {
  // J2000 positions (RA hours, Dec degrees), checked against the Yale Bright Star catalogue
  {6.752f, -16.716f, -1.46f, "Sirius"},   {6.399f, -52.696f, -0.74f, "Canopus"},
  {14.261f, 19.182f, -0.05f, "Arcturus"}, {18.616f, 38.784f, 0.03f, "Vega"},
  {5.278f, 45.998f, 0.08f, "Capella"},    {5.242f, -8.202f, 0.13f, "Rigel"},
  {7.655f, 5.225f, 0.34f, "Procyon"},     {5.919f, 7.407f, 0.42f, "Betelgeuse"},
  {1.629f, -57.237f, 0.46f, "Achernar"},  {19.846f, 8.868f, 0.77f, "Altair"},
  {4.599f, 16.509f, 0.85f, "Aldebaran"},  {16.490f, -26.432f, 0.96f, "Antares"},
  {13.420f, -11.161f, 0.98f, "Spica"},    {7.755f, 28.026f, 1.14f, "Pollux"},
  {22.961f, -29.622f, 1.16f, "Fomalhaut"},{20.690f, 45.280f, 1.25f, "Deneb"},
  {12.795f, -59.689f, 1.25f, "Mimosa"},   {10.139f, 11.967f, 1.35f, "Regulus"},
  {7.577f, 31.888f, 1.58f, "Castor"},     {12.443f, -63.099f, 0.77f, "Acrux"},
  {17.560f, -37.104f, 1.62f, "Shaula"},   {5.604f, -1.202f, 1.69f, "Alnilam"},
  {5.419f, 6.350f, 1.64f, "Bellatrix"},   {0.140f, 29.090f, 2.07f, "Alpheratz"},
  {2.120f, 23.462f, 2.00f, "Hamal"},      {5.679f, -1.943f, 1.77f, "Alnitak"},
  {5.533f, -0.299f, 2.23f, "Mintaka"},    {3.405f, 49.861f, 1.79f, "Mirfak"},
  {11.062f, 61.751f, 1.81f, "Dubhe"},     {12.900f, 55.960f, 1.77f, "Alioth"},
  {13.399f, 54.925f, 2.23f, "Mizar"},     {11.031f, 56.383f, 2.37f, "Merak"},
  {11.897f, 53.695f, 2.44f, "Phecda"},    {12.257f, 57.033f, 3.31f, "Megrez"},
  {13.792f, 49.313f, 1.86f, "Alkaid"},    {0.221f, 15.184f, 2.83f, "Algenib"},
  {2.530f, 89.264f, 1.98f, "Polaris"},    {22.137f, -46.961f, 1.74f, "Alnair"},
  {20.370f, 40.257f, 2.23f, "Sadr"},      {18.403f, -34.384f, 1.85f, "Kaus Australis"},
  {16.006f, -22.622f, 2.29f, "Dschubba"}, {14.845f, 74.156f, 2.07f, "Kochab"},
  {9.133f, -43.433f, 2.21f, "Suhail"},    {8.375f, -59.510f, 1.86f, "Avior"},
  {3.791f, 24.105f, 2.87f, "Pleiades"},   {0.675f, 56.537f, 2.24f, "Schedar"},
  {0.153f, 59.150f, 2.28f, "Caph"},       {0.945f, 60.717f, 2.47f, "Navi"},
  {1.430f, 60.235f, 2.68f, "Ruchbah"},    {21.310f, 62.585f, 2.45f, "Alderamin"},
  {5.796f, -9.670f, 2.06f, "Saiph"},      {1.162f, 35.621f, 2.07f, "Mirach"},
  {23.079f, 15.205f, 2.49f, "Markab"},    {23.063f, 28.083f, 2.42f, "Scheat"},
  {19.512f, 27.960f, 3.05f, "Albireo"},   {20.770f, 33.970f, 2.48f, "Gienah Cyg"},
  {19.750f, 45.131f, 2.87f, "Fawaris"},
};
static const int NSTAR = sizeof(STARS) / sizeof(STARS[0]);
// indices of the Summer Triangle (favourites): Vega, Altair, Deneb
static const int TRI_VEGA = 3, TRI_ALTAIR = 9, TRI_DENEB = 15;  // indices in STARS above - keep in sync

// constellation lines as pairs of star-name lookups resolved once
struct Line { const char *a, *b; };
static const Line LINES[] = {
  {"Dubhe", "Merak"}, {"Merak", "Phecda"}, {"Phecda", "Megrez"}, {"Megrez", "Dubhe"},   // the Big Dipper bowl
  {"Megrez", "Alioth"}, {"Alioth", "Mizar"}, {"Mizar", "Alkaid"},                       // ...and handle
  {"Betelgeuse", "Bellatrix"}, {"Betelgeuse", "Alnitak"}, {"Bellatrix", "Mintaka"},     // Orion
  {"Alnitak", "Alnilam"}, {"Alnilam", "Mintaka"}, {"Alnitak", "Saiph"}, {"Mintaka", "Rigel"},
  {"Caph", "Schedar"}, {"Schedar", "Navi"}, {"Navi", "Ruchbah"},                         // Cassiopeia's W
  {"Deneb", "Sadr"}, {"Sadr", "Albireo"}, {"Sadr", "Gienah Cyg"}, {"Sadr", "Fawaris"},    // Cygnus, the swan
  {"Alpheratz", "Scheat"}, {"Scheat", "Markab"}, {"Markab", "Algenib"}, {"Algenib", "Alpheratz"}, // Great Square
  {"Castor", "Pollux"}, {"Acrux", "Mimosa"},
};
static const int NLINE = sizeof(LINES) / sizeof(LINES[0]);
static int starByName(const char *n) {
  for (int i = 0; i < NSTAR; i++) if (!strcmp(STARS[i].name, n)) return i;
  return -1;
}

// ------------------------------------------------------------------ Sun, Moon, planets (low precision, 1800-2050)
// returns geocentric ecliptic lon/lat (deg) and distance; good to ~arcminutes for naked-eye use.
struct Elem { double a, e, i, L, lp, node; double da, de, di, dL, dlp, dnode; };
// a(AU), e, i(deg), L mean longitude, longitude of perihelion, longitude of node; rates per century (JPL 1800-2050)
static const Elem PLAN[] = {
  {0.38709927,0.20563593,7.00497902,252.25032350,77.45779628,48.33076593, 0.00000037,0.00001906,-0.00594749,149472.67411175,0.16047689,-0.12534081}, // Mercury
  {0.72333566,0.00677672,3.39467605,181.97909950,131.60246718,76.67984255, 0.00000390,-0.00004107,-0.00078890,58517.81538729,0.00268329,-0.27769418}, // Venus
  {1.00000261,0.01671123,-0.00001531,100.46457166,102.93768193,0.0, 0.00000562,-0.00004392,-0.01294668,35999.37244981,0.32327364,0.0}, // Earth
  {1.52371034,0.09339410,1.84969142,-4.55343205,-23.94362959,49.55953891, 0.00001847,0.00007882,-0.00813131,19140.30268499,0.44441088,-0.29257343}, // Mars
  {5.20288700,0.04838624,1.30439695,34.39644051,14.72847983,100.47390909,-0.00011607,-0.00013253,-0.00183714,3034.74612775,0.21252668,0.20469106}, // Jupiter
  {9.53667594,0.05386179,2.48599187,49.95424423,92.59887831,113.66242448,-0.00125060,-0.00050991,0.00193609,1222.49362201,-0.41897216,-0.28867794}, // Saturn
};
static void solveKepler(double M, double e, double &E) {
  M = fmod(M, 2 * M_PI); if (M < -M_PI) M += 2 * M_PI; if (M > M_PI) M -= 2 * M_PI;
  E = M;
  for (int i = 0; i < 8; i++) { double d = (E - e * sin(E) - M) / (1 - e * cos(E)); E -= d; if (fabs(d) < 1e-7) break; }
}
// heliocentric ecliptic rectangular (AU) for planet p at centuries T
static void helioXYZ(const Elem &el, double T, double &x, double &y, double &z) {
  double a = el.a + el.da * T, e = el.e + el.de * T;
  double I = (el.i + el.di * T) * DEG2RAD;
  double L = el.L + el.dL * T, lp = el.lp + el.dlp * T, node = (el.node + el.dnode * T);
  double w = (lp - node) * DEG2RAD, O = node * DEG2RAD;
  double M = (L - lp) * DEG2RAD, E; solveKepler(M, e, E);
  double xp = a * (cos(E) - e), yp = a * sqrt(1 - e * e) * sin(E);
  double cw = cos(w), sw = sin(w), cO = cos(O), sO = sin(O), ci = cos(I), si = sin(I);
  x = (cw * cO - sw * sO * ci) * xp + (-sw * cO - cw * sO * ci) * yp;
  y = (cw * sO + sw * cO * ci) * xp + (-sw * sO + cw * cO * ci) * yp;
  z = (sw * si) * xp + (cw * si) * yp;
}
// geocentric RA/Dec for a planet index (0 Mer,1 Ven,3 Mar,4 Jup,5 Sat) - 2 (Earth) is observer
static void planetRaDec(int p, double jd, double &ra, double &dec) {
  double T = (jd - 2451545.0) / 36525.0;
  double ex, ey, ez, px, py, pz;
  helioXYZ(PLAN[2], T, ex, ey, ez);
  helioXYZ(PLAN[p], T, px, py, pz);
  double x = px - ex, y = py - ey, z = pz - ez;
  double eps = 23.43929 * DEG2RAD;
  double xe = x, ye = y * cos(eps) - z * sin(eps), ze = y * sin(eps) + z * cos(eps);
  ra = atan2(ye, xe) * RAD2DEG / 15.0; if (ra < 0) ra += 24;
  dec = atan2(ze, sqrt(xe * xe + ye * ye)) * RAD2DEG;
}
static void sunRaDec(double jd, double &ra, double &dec, double &eclLon) {
  double n = jd - 2451545.0;
  double L = fmod(280.460 + 0.9856474 * n, 360.0); if (L < 0) L += 360;
  double g = fmod(357.528 + 0.9856003 * n, 360.0) * DEG2RAD;
  double lam = (L + 1.915 * sin(g) + 0.020 * sin(2 * g)); eclLon = lam;
  double lamR = lam * DEG2RAD, eps = 23.439 * DEG2RAD;
  ra = atan2(cos(eps) * sin(lamR), cos(lamR)) * RAD2DEG / 15.0; if (ra < 0) ra += 24;
  dec = asin(sin(eps) * sin(lamR)) * RAD2DEG;
}
// Moon: simplified Schlyter; returns RA/Dec and phase (0 new .. 1 full)
static void moonRaDec(double jd, double &ra, double &dec, double &phase, double &illum) {
  double d = jd - 2451543.5;
  double N = (125.1228 - 0.0529538083 * d) * DEG2RAD;
  double i = 5.1454 * DEG2RAD;
  double w = (318.0634 + 0.1643573223 * d) * DEG2RAD;
  double a = 60.2666, e = 0.054900;
  double M = fmod(115.3654 + 13.0649929509 * d, 360.0) * DEG2RAD;
  double E = M + e * sin(M) * (1 + e * cos(M));
  for (int k = 0; k < 6; k++) E = E - (E - e * sin(E) - M) / (1 - e * cos(E));
  double xv = a * (cos(E) - e), yv = a * sqrt(1 - e * e) * sin(E);
  double v = atan2(yv, xv), r = sqrt(xv * xv + yv * yv);
  double xh = r * (cos(N) * cos(v + w) - sin(N) * sin(v + w) * cos(i));
  double yh = r * (sin(N) * cos(v + w) + cos(N) * sin(v + w) * cos(i));
  double zh = r * (sin(v + w) * sin(i));
  double eps = 23.4393 * DEG2RAD;
  double xe = xh, ye = yh * cos(eps) - zh * sin(eps), ze = yh * sin(eps) + zh * cos(eps);
  ra = atan2(ye, xe) * RAD2DEG / 15.0; if (ra < 0) ra += 24;
  dec = atan2(ze, sqrt(xe * xe + ye * ye)) * RAD2DEG;
  double sra, sdec, slon; sunRaDec(jd, sra, sdec, slon);
  double moonLon = atan2(ye, xe) * RAD2DEG;
  double elong = fmod(moonLon - slon + 360.0, 360.0);
  phase = elong / 360.0;  // 0 new, 0.5 full
  illum = (1 - cos(elong * DEG2RAD)) / 2;
}

// ------------------------------------------------------------------ ISS (TLE -> J2 secular propagation)
struct Tle {
  bool ok = false;
  double epoch;      // unix seconds
  double incl, raan, ecc, argp, M0, n0;  // rad, rad, -, rad, rad, rad/s
  double ndot;       // rad/s^2
} iss;
static const double MU = 398600.4418, RE = 6378.137, J2 = 1.08262998905e-3;

static double tleField(const char *s, int a, int b) { char buf[20]; int n = b - a; memcpy(buf, s + a, n); buf[n] = 0; return atof(buf); }
void skyReloadTle() {
  iss.ok = false;
  if (!sdOK || !SD.exists("/capdeck/sky/iss.tle")) return;
  File f = SD.open("/capdeck/sky/iss.tle");
  String l1, l2;
  while (f.available()) {
    String ln = f.readStringUntil('\n'); ln.trim();
    if (ln.startsWith("1 ")) l1 = ln;
    else if (ln.startsWith("2 ")) l2 = ln;
  }
  f.close();
  if (l1.length() < 60 || l2.length() < 60) return;
  // epoch: cols 18-19 year, 20-31 day
  int yy = (int)tleField(l1.c_str(), 18, 20);
  double doy = tleField(l1.c_str(), 20, 32);
  int year = yy < 57 ? 2000 + yy : 1900 + yy;
  // Unix time of Jan 1 00:00 UTC of `year` (no timegm() on this libc)
  long days = 0;
  for (int y = 1970; y < year; y++) days += ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) ? 366 : 365;
  double base = days * 86400.0;
  iss.epoch = base + (doy - 1.0) * 86400.0;
  // ndot (rev/day^2) cols 33-43
  iss.ndot = 2.0 * tleField(l1.c_str(), 33, 43) * 2 * M_PI / (86400.0 * 86400.0);  // the TLE field is ndot/2 (rev/day^2)
  iss.incl = tleField(l2.c_str(), 8, 16) * DEG2RAD;
  iss.raan = tleField(l2.c_str(), 17, 25) * DEG2RAD;
  char eb[10] = "0."; strncpy(eb + 2, l2.c_str() + 26, 7); eb[9] = 0; iss.ecc = atof(eb);  // implied leading decimal point
  iss.argp = tleField(l2.c_str(), 34, 42) * DEG2RAD;
  iss.M0 = tleField(l2.c_str(), 43, 51) * DEG2RAD;
  double revday = tleField(l2.c_str(), 52, 63);
  iss.n0 = revday * 2 * M_PI / 86400.0;  // rad/s
  iss.ok = true;
}
// ECI position (km) at time t
static bool issEci(time_t t, double &x, double &y, double &z) {
  if (!iss.ok) return false;
  double dt = (double)t - iss.epoch;
  double n = iss.n0 + iss.ndot * dt;
  double a = pow(MU / (n * n), 1.0 / 3.0);
  double e = iss.ecc, p = a * (1 - e * e);
  double cosi = cos(iss.incl);
  double factor = 1.5 * J2 * (RE / p) * (RE / p) * n;
  double raanDot = -factor * cosi;
  double argpDot = factor * (2.0 - 2.5 * sin(iss.incl) * sin(iss.incl));
  double M = iss.M0 + n * dt + 0.5 * iss.ndot * dt * dt;
  double raan = iss.raan + raanDot * dt;
  double argp = iss.argp + argpDot * dt;
  double E = M, d;
  for (int i = 0; i < 10; i++) { d = (E - e * sin(E) - M) / (1 - e * cos(E)); E -= d; if (fabs(d) < 1e-8) break; }
  double xo = a * (cos(E) - e), yo = a * sqrt(1 - e * e) * sin(E);
  double cw = cos(argp), sw = sin(argp), cO = cos(raan), sO = sin(raan), ci = cosi, si = sin(iss.incl);
  x = (cw * cO - sw * sO * ci) * xo + (-sw * cO - cw * sO * ci) * yo;
  y = (cw * sO + sw * cO * ci) * xo + (-sw * sO + cw * cO * ci) * yo;
  z = (sw * si) * xo + (cw * si) * yo;
  return true;
}
// topocentric alt/az of the ISS for observer lat/lon
static bool issAltAz(time_t t, double lat, double lon, double &alt, double &az, bool &sunlit) {
  double x, y, z;
  if (!issEci(t, x, y, z)) return false;
  double jd = julianDate(t), gmst = gmstDeg(jd) * DEG2RAD;
  // observer ECI
  double latR = lat * DEG2RAD, lst = gmst + lon * DEG2RAD;
  double ox = RE * cos(latR) * cos(lst), oy = RE * cos(latR) * sin(lst), oz = RE * sin(latR);
  double rx = x - ox, ry = y - oy, rz = z - oz;
  // rotate to topocentric SEZ
  double slat = sin(latR), clat = cos(latR), slst = sin(lst), clst = cos(lst);
  double south = slat * clst * rx + slat * slst * ry - clat * rz;
  double east = -slst * rx + clst * ry;
  double up = clat * clst * rx + clat * slst * ry + slat * rz;
  double rng = sqrt(rx * rx + ry * ry + rz * rz);
  alt = asin(up / rng) * RAD2DEG;
  az = atan2(east, -south) * RAD2DEG; if (az < 0) az += 360;
  // sunlit: is the satellite outside Earth's cylindrical shadow?
  double sra, sdec, slon; sunRaDec(jd, sra, sdec, slon);
  double eps = 23.439 * DEG2RAD, lam = slon * DEG2RAD;
  double sx = cos(lam), sy = sin(lam) * cos(eps), sz = sin(lam) * sin(eps);  // sun unit vector (ECI-ish)
  double dot = x * sx + y * sy + z * sz;
  double perp = sqrt((x * x + y * y + z * z) - dot * dot);
  sunlit = (dot > 0) || (perp > RE);
  return true;
}

// ------------------------------------------------------------------ dome projection
static const int DOME_R = 62;
static int domeCX, domeCY;
// alt/az -> screen; returns false if below horizon
static bool project(double alt, double az, int &sx, int &sy) {
  if (alt < 0) return false;
  double r = (90.0 - alt) / 90.0 * DOME_R;
  double a = az * DEG2RAD;  // 0=N up, clockwise through E(right)
  sx = domeCX + (int)lround(r * sin(a));
  sy = domeCY - (int)lround(r * cos(a));
  return true;
}

// ------------------------------------------------------------------ Tracers: GPS satellites with trails
struct Trail { char sys; uint8_t prn; uint8_t len; uint8_t head; uint8_t ax[10], ay[10]; uint32_t last; };
static Trail trails[32];
static int nTrails = 0;
static void trailAdd(char sys, uint8_t prn, int sx, int sy) {
  Trail *t = nullptr;
  for (int i = 0; i < nTrails; i++) if (trails[i].sys == sys && trails[i].prn == prn) { t = &trails[i]; break; }
  if (!t && nTrails < 32) { t = &trails[nTrails++]; t->sys = sys; t->prn = prn; t->len = 0; t->head = 0; }
  if (!t) return;
  if (t->len && t->last && millis() - t->last < 8000) return;  // sample ~every 8 s
  t->ax[t->head] = sx; t->ay[t->head] = sy;
  t->head = (t->head + 1) % 10;
  if (t->len < 10) t->len++;
  t->last = millis();
}

// ------------------------------------------------------------------ settings
struct SkyCfg { bool stars = true, planets = true, moon = true, sats = true, iss = true, lines = true, labels = true;
                bool issAlert = true, issNightOnly = true; uint8_t lead1 = 10, lead2 = 5; } sky;
static void skyLoad() {
  Preferences p; p.begin("sky", true);
  sky.stars = p.getBool("st", true); sky.planets = p.getBool("pl", true); sky.moon = p.getBool("mo", true);
  sky.sats = p.getBool("sa", true); sky.iss = p.getBool("is", true); sky.lines = p.getBool("ln", true);
  sky.labels = p.getBool("lb", true); sky.issAlert = p.getBool("al", true); sky.issNightOnly = p.getBool("no", true);
  sky.lead1 = p.getUChar("l1", 10); sky.lead2 = p.getUChar("l2", 5);
  p.end();
}
static void skySave() {
  Preferences p; p.begin("sky", false);
  p.putBool("st", sky.stars); p.putBool("pl", sky.planets); p.putBool("mo", sky.moon);
  p.putBool("sa", sky.sats); p.putBool("is", sky.iss); p.putBool("ln", sky.lines);
  p.putBool("lb", sky.labels); p.putBool("al", sky.issAlert); p.putBool("no", sky.issNightOnly);
  p.putUChar("l1", sky.lead1); p.putUChar("l2", sky.lead2);
  p.end();
}

// ------------------------------------------------------------------ hand-drawn glyphs
// classical planet symbols, drawn with primitives so they're crisp at any size
static void glyph(LovyanGFX &g, int p, int x, int y, uint16_t c) {
  // p: 0 Sun 1 Moon 2 Mercury 3 Venus 4 Mars 5 Jupiter 6 Saturn
  switch (p) {
    case 0: g.drawCircle(x, y, 4, c); g.fillCircle(x, y, 1, c); break;                         // Sun: circle + dot
    case 1: g.drawCircle(x + 1, y, 4, c); g.fillCircle(x + 2, y, 4, col::BG); break;           // Moon: crescent
    case 2: g.drawCircle(x, y, 3, c); g.drawLine(x, y + 3, x, y + 6, c);                        // Mercury
            g.drawLine(x - 2, y + 4, x + 2, y + 4, c); g.drawCircle(x, y - 5, 2, c); break;     // ...with horns
    case 3: g.drawCircle(x, y - 1, 3, c); g.drawLine(x, y + 2, x, y + 6, c);                    // Venus
            g.drawLine(x - 2, y + 4, x + 2, y + 4, c); break;
    case 4: g.drawCircle(x - 1, y + 1, 3, c); g.drawLine(x + 1, y - 1, x + 5, y - 5, c);        // Mars
            g.drawLine(x + 5, y - 5, x + 2, y - 5, c); g.drawLine(x + 5, y - 5, x + 5, y - 2, c); break;
    case 5: g.drawLine(x - 3, y - 4, x - 3, y + 4, c); g.drawLine(x - 3, y - 2, x + 3, y - 2, c);  // Jupiter (stylized 4)
            g.drawLine(x + 3, y - 4, x + 3, y + 4, c); break;
    case 6: g.drawLine(x - 2, y - 4, x - 2, y + 2, c); g.drawLine(x - 4, y - 3, x, y - 3, c);   // Saturn (stylized h+curl)
            g.drawLine(x - 2, y + 2, x + 3, y + 2, c); g.drawCircle(x + 3, y + 4, 2, c); break;
  }
}

// moon with its real phase, and a faint watching eye (the app's motif)
static void drawMoonFace(LovyanGFX &g, int x, int y, int r, double illum, double phase, uint16_t c) {
  g.fillCircle(x, y, r, blend565(c, col::BG, 0.25f));
  // terminator: shade the dark limb
  int off = (int)((illum * 2 - 1) * r);
  bool waxing = phase < 0.5;
  for (int dy = -r; dy <= r; dy++) {
    int w = (int)sqrt((double)(r * r - dy * dy));
    int lit = (int)(w * (illum * 2 - 1));
    if (waxing) g.drawFastHLine(x - w, y + dy, w - lit > 0 ? w - lit : 0, col::BG);
    else g.drawFastHLine(x + lit, y + dy, lit < w ? w - lit : 0, col::BG);
  }
  g.drawCircle(x, y, r, c);
  // the eye
  g.drawEllipse(x, y, r / 2, r / 3, blend565(c, col::BG, 0.5f));
  g.fillCircle(x, y, max(1, r / 6), c);
}

// ------------------------------------------------------------------ ISS pass search + alerts
struct Pass { time_t start, peak, end; int maxAlt; };
static bool nextIssPass(time_t from, double lat, double lon, Pass &out, bool needNight) {
  if (!iss.ok) return false;
  bool up = false; time_t rise = 0; int peak = 0; time_t peakT = 0;
  for (time_t t = from; t < from + 36 * 3600; t += 30) {
    double alt, az; bool lit;
    if (!issAltAz(t, lat, lon, alt, az, lit)) return false;
    if (needNight) {
      double sa, sd, sl; sunRaDec(julianDate(t), sa, sd, sl);
      double salt, saz; eqToHoriz(sa, sd, lat, gmstDeg(julianDate(t)) + lon, salt, saz);
      if (salt > -6) { if (up) { up = false; } continue; }  // daytime: a pass isn't visible
      if (!lit) { if (up) { up = false; } continue; }        // ISS in shadow: not visible
    }
    if (alt > 10) {
      if (!up) { up = true; rise = t; peak = 0; }
      if (alt > peak) { peak = (int)alt; peakT = t; }
    } else if (up) {
      out.start = rise; out.peak = peakT; out.end = t; out.maxAlt = peak;
      return true;
    }
  }
  return false;
}

// called by power.cpp when an ISS alarm fires (even from sleep)
void skyAlarmFired() {
  // the alarm itself chimes in power.cpp; here we re-arm the next lead time / next pass
  skyLoad();
  double lat, lon;
  if (!iss.ok || !timeValid() || !myPosition(lat, lon)) return;
  Pass p;
  time_t now = time(nullptr);
  if (!nextIssPass(now + 60, lat, lon, p, sky.issNightOnly)) { powerSetAlarm(0, ""); return; }
  time_t l1 = p.start - sky.lead1 * 60, l2 = p.start - sky.lead2 * 60;
  time_t next = l1 > now + 5 ? l1 : (l2 > now + 5 ? l2 : 0);
  if (next) powerSetAlarm(next, "ISS pass soon");
}
// arm the soonest lead alarm for the next pass
static void armIssAlert() {
  if (!sky.issAlert || !iss.ok || !timeValid()) { return; }
  double lat, lon; if (!myPosition(lat, lon)) return;
  Pass p; time_t now = time(nullptr);
  if (!nextIssPass(now + 60, lat, lon, p, sky.issNightOnly)) return;
  time_t l1 = p.start - sky.lead1 * 60, l2 = p.start - sky.lead2 * 60;
  time_t next = l1 > now + 30 ? l1 : (l2 > now + 30 ? l2 : 0);
  if (next) powerSetAlarm(next, "ISS pass soon");
}

// ------------------------------------------------------------------ sky background gradient
static void drawSkyBackdrop(double sunAlt) {
  // night->twilight->day tints the whole dome; Bruise Blue by default
  float day = (float)fmax(0.0, fmin(1.0, (sunAlt + 18) / 24.0));  // 0 deep night .. 1 full day
  uint16_t zen = blend565(canvas.color565(0x6C,0x9A,0xD8), BRUISE_ZENITH, 1 - day * day);
  uint16_t hor = blend565(canvas.color565(0x9A,0xB0,0xC8), BRUISE_HORIZON, 1 - day);
  for (int y = BAR_H; y < SCR_H; y++) {
    float f = (float)(y - BAR_H) / (SCR_H - BAR_H);
    canvas.drawFastHLine(0, y, SCR_W, blend565(hor, zen, f));
  }
}

// ------------------------------------------------------------------ the app
void appNightcap() {
  bruiseInit();
  skyLoad();
  domeCX = SCR_W / 2 + 28;
  domeCY = BAR_H + (SCR_H - BAR_H) / 2;
  double lat, lon;
  bool havePos = myPosition(lat, lon);
  if (!havePos) { lat = 42.3; lon = -85.6; }  // a default so the sky still draws
  int32_t timeOffset = 0;  // seconds of "time travel"
  int sel = -1;  // selected object for details (-1 none)
  uint32_t lastDraw = 0;
  armIssAlert();

  while (true) {
    Key k = readKey();
    stayAwake();
    if (k.back) break;
    if (k.left) timeOffset -= 600;   // 10-minute steps
    if (k.right) timeOffset += 600;
    if (k.up) timeOffset += 3600;
    if (k.down) timeOffset -= 3600;
    if (k.ch == '0' || k.space) timeOffset = 0;
    if (k.ch == 't') {  // toggles menu
      while (true) {
        std::vector<ListItem> it = {
          {"Stars", sky.stars ? "on" : "off", 255, sky.stars ? col::OK : col::DIM},
          {"Constellation lines", sky.lines ? "on" : "off", 255, sky.lines ? col::OK : col::DIM},
          {"Planets", sky.planets ? "on" : "off", 255, sky.planets ? col::OK : col::DIM},
          {"Moon", sky.moon ? "on" : "off", 255, sky.moon ? col::OK : col::DIM},
          {"Satellites (Tracers)", sky.sats ? "on" : "off", 255, sky.sats ? col::OK : col::DIM},
          {"ISS", sky.iss ? "on" : "off", 255, sky.iss ? col::OK : col::DIM},
          {"Labels", sky.labels ? "on" : "off", 255, sky.labels ? col::OK : col::DIM},
          {"ISS alerts...", "", ICON_CLOCK, col::PORTAL},
        };
        int s2 = 0;
        int r = listMenu("Show in the sky", col::PORTAL, it, s2, ICON_NIGHTCAP);
        if (r < 0) break;
        switch (r) {
          case 0: sky.stars = !sky.stars; break; case 1: sky.lines = !sky.lines; break;
          case 2: sky.planets = !sky.planets; break; case 3: sky.moon = !sky.moon; break;
          case 4: sky.sats = !sky.sats; break; case 5: sky.iss = !sky.iss; break;
          case 6: sky.labels = !sky.labels; break;
          case 7: {
            std::vector<ListItem> a = {
              {"Alerts", sky.issAlert ? "on" : "off", 255, sky.issAlert ? col::OK : col::DIM},
              {"Night passes only", sky.issNightOnly ? "on" : "off", 255, sky.issNightOnly ? col::OK : col::DIM},
              {"First chime", String(sky.lead1) + " min before", 255, 0},
              {"Second chime", String(sky.lead2) + " min before", 255, 0},
            };
            int s3 = 0; int r2 = listMenu("ISS alerts", col::PORTAL, a, s3, ICON_CLOCK);
            if (r2 == 0) sky.issAlert = !sky.issAlert;
            else if (r2 == 1) sky.issNightOnly = !sky.issNightOnly;
            else if (r2 == 2) sky.lead1 = sky.lead1 >= 20 ? 5 : sky.lead1 + 5;
            else if (r2 == 3) sky.lead2 = sky.lead2 >= 15 ? 2 : sky.lead2 + 3;
            break;
          }
        }
        skySave();
        armIssAlert();
      }
      lastDraw = 0;
    }

    if (millis() - lastDraw < 250 && !k.any) { delay(8); continue; }
    lastDraw = millis();

    time_t now = (timeValid() ? time(nullptr) : 1762300000) + timeOffset;
    double jd = julianDate(now), gmst = gmstDeg(jd), lst = gmst + lon;

    double sunRa, sunDec, sunLon; sunRaDec(jd, sunRa, sunDec, sunLon);
    double sunAlt, sunAz; eqToHoriz(sunRa, sunDec, lat, lst, sunAlt, sunAz);
    drawSkyBackdrop(sunAlt);
    drawStatusBar("NIGHTCAP", col::PORTAL);

    // dome rim + cardinal marks
    canvas.drawCircle(domeCX, domeCY, DOME_R, blend565(col::TEXT, col::BG, 0.35f));
    canvas.setFont(&fonts::Font0); canvas.setTextColor(blend565(col::TEXT, col::BG, 0.5f));
    canvas.setTextDatum(middle_center);
    canvas.drawString("N", domeCX, domeCY - DOME_R - 4);
    canvas.drawString("S", domeCX, domeCY + DOME_R + 4);
    canvas.drawString("E", domeCX + DOME_R + 4, domeCY);
    canvas.drawString("W", domeCX - DOME_R - 4, domeCY);

    bool night = sunAlt < -6;
    int sx, sy;

    // constellation lines (behind stars)
    if (sky.lines && night) {
      for (int i = 0; i < NLINE; i++) {
        int ia = starByName(LINES[i].a), ib = starByName(LINES[i].b);
        if (ia < 0 || ib < 0) continue;
        double a1, z1, a2, z2; int x1, y1, x2, y2;
        eqToHoriz(STARS[ia].ra, STARS[ia].dec, lat, lst, a1, z1);
        eqToHoriz(STARS[ib].ra, STARS[ib].dec, lat, lst, a2, z2);
        if (project(a1, z1, x1, y1) && project(a2, z2, x2, y2))
          canvas.drawLine(x1, y1, x2, y2, blend565(col::PORTAL, col::BG, 0.4f));
      }
      // Summer Triangle in gold (favourites)
      int tri[3] = {TRI_VEGA, TRI_ALTAIR, TRI_DENEB};
      int tx[3], ty[3]; bool allUp = true;
      for (int i = 0; i < 3; i++) {
        double a, z; eqToHoriz(STARS[tri[i]].ra, STARS[tri[i]].dec, lat, lst, a, z);
        if (!project(a, z, tx[i], ty[i])) allUp = false;
      }
      if (allUp) {
        uint16_t gold = canvas.color565(0xFF, 0xD2, 0x60);
        for (int i = 0; i < 3; i++) canvas.drawLine(tx[i], ty[i], tx[(i + 1) % 3], ty[(i + 1) % 3], blend565(gold, col::BG, 0.45f));
      }
    }

    // stars
    if (sky.stars && night) {
      for (int i = 0; i < NSTAR; i++) {
        double a, z; eqToHoriz(STARS[i].ra, STARS[i].dec, lat, lst, a, z);
        if (!project(a, z, sx, sy)) continue;
        int br = STARS[i].mag < 0.5f ? 2 : STARS[i].mag < 1.6f ? 1 : 0;
        bool fav = (i == TRI_VEGA || i == TRI_ALTAIR || i == TRI_DENEB);
        uint16_t c = fav ? canvas.color565(0xFF, 0xD2, 0x60) : STAR_WARM;
        if (br >= 2) canvas.fillCircle(sx, sy, 2, c);
        else if (br == 1) canvas.fillCircle(sx, sy, 1, c);
        else canvas.drawPixel(sx, sy, blend565(c, col::BG, 0.6f));
        if (fav) { canvas.drawCircle(sx, sy, 4, blend565(c, col::BG, 0.4f)); }
        if (sky.labels && (STARS[i].mag < 0.3f || fav))
          { canvas.setTextDatum(bottom_left); canvas.setTextColor(blend565(c, col::BG, 0.5f)); canvas.drawString(STARS[i].name, sx + 3, sy - 1); }
      }
    }

    // planets (always, day or night, if above horizon)
    if (sky.planets) {
      const int pidx[5] = {0, 1, 3, 4, 5};  // Mercury, Venus, Mars, Jupiter, Saturn
      const int pg[5] = {2, 3, 4, 5, 6};    // glyph ids
      for (int j = 0; j < 5; j++) {
        double ra, dec; planetRaDec(pidx[j], jd, ra, dec);
        double a, z; eqToHoriz(ra, dec, lat, lst, a, z);
        if (!project(a, z, sx, sy)) continue;
        uint16_t c = canvas.color565(0xFF, 0xE8, 0xC0);
        glyph(canvas, pg[j], sx, sy, c);
      }
    }

    // Moon
    if (sky.moon) {
      double mra, mdec, phase, illum; moonRaDec(jd, mra, mdec, phase, illum);
      double a, z;
      eqToHoriz(mra, mdec, lat, lst, a, z);
      if (project(a, z, sx, sy)) drawMoonFace(canvas, sx, sy, 7, illum, phase, canvas.color565(0xF0, 0xF0, 0xE0));
    }

    // Satellites (Tracers) from the GPS module - real az/el, with trails
    if (sky.sats) {
      std::vector<SatInfo> sats; gpsSatList(sats);
      for (auto &s : sats) {
        if (s.elev <= 0) continue;
        if (!project(s.elev, s.az, sx, sy)) continue;
        trailAdd(s.sys, s.prn, sx, sy);
      }
      // draw trails then heads
      for (int i = 0; i < nTrails; i++) {
        Trail &t = trails[i];
        if (millis() - t.last > 60000) continue;
        for (int j = 1; j < t.len; j++) {
          int a = (t.head - j + 20) % 10, b = (t.head - j - 1 + 20) % 10;
          canvas.drawLine(t.ax[a], t.ay[a], t.ax[b], t.ay[b], blend565(col::GPS, col::BG, (float)(t.len - j) / t.len * 0.5f));
        }
        int h = (t.head - 1 + 10) % 10;
        canvas.fillCircle(t.ax[h], t.ay[h], 1, col::GPS);
      }
    }

    // ISS live
    if (sky.iss && iss.ok) {
      double a, z; bool lit;
      if (issAltAz(now, lat, lon, a, z, lit) && project(a, z, sx, sy)) {
        uint16_t c = lit ? canvas.color565(0xFF, 0xFF, 0xFF) : blend565(col::TEXT, col::BG, 0.4f);
        canvas.drawLine(sx - 3, sy, sx + 3, sy, c);
        canvas.drawLine(sx, sy - 3, sx, sy + 3, c);
        canvas.fillCircle(sx, sy, 1, c);
        if (sky.labels) { canvas.setTextDatum(top_left); canvas.setTextColor(c); canvas.drawString("ISS", sx + 4, sy); }
      }
    }

    // left info panel
    canvas.setTextDatum(top_left);
    canvas.setFont(&fonts::Font0);
    int px = 4, py = BAR_H + 4;
    canvas.setTextColor(col::TEXT);
    struct tm tmv; time_t nn = now; localtime_r(&nn, &tmv);
    char tb[24]; strftime(tb, sizeof tb, "%H:%M", &tmv);
    canvas.drawString(timeOffset ? String(tb) + "*" : String(tb), px, py); py += 10;
    canvas.setTextColor(col::DIM);
    canvas.drawString(night ? "night" : sunAlt < 0 ? "twilight" : "day", px, py); py += 10;
    if (!havePos) { havePos = myPosition(lat, lon); canvas.setTextColor(col::WARN); canvas.drawString("no GPS: guess", px, py); }
    py += 12;
    // next ISS pass (searching 36 h is heavy: refresh once a minute)
    static Pass cachedPass;
    static bool cachedOk = false;
    static uint32_t cachedAt = 0;
    static bool cachedNight = true;
    if (iss.ok && timeValid() && (!cachedAt || millis() - cachedAt > 60000 || cachedNight != sky.issNightOnly ||
                                  (cachedOk && cachedPass.end < time(nullptr)))) {
      cachedOk = nextIssPass(time(nullptr), lat, lon, cachedPass, sky.issNightOnly);
      cachedAt = millis();
      cachedNight = sky.issNightOnly;
    }
    if (iss.ok && timeValid()) {
      Pass p = cachedPass;
      if (cachedOk) {
        long mins = (p.start - time(nullptr)) / 60;
        canvas.setTextColor(col::PORTAL);
        canvas.drawString("ISS pass", px, py); py += 9;
        canvas.setTextColor(col::DIM);
        canvas.drawString(mins < 60 ? String("in " + String(mins) + "m") : String("in " + String(mins / 60) + "h"), px, py); py += 9;
        canvas.drawString("max " + String(p.maxAlt) + (char)247 + " alt", px, py); py += 9;
      } else { canvas.setTextColor(col::DIM); canvas.drawString("no ISS pass", px, py); py += 9; canvas.drawString("in 36h", px, py); py += 9; }
    } else if (!iss.ok) {
      canvas.setTextColor(col::WARN); canvas.drawString("no orbit", px, py); py += 9;
      canvas.setTextColor(col::DIM); canvas.drawString("get it in", px, py); py += 9; canvas.drawString("Mycorrhiza", px, py); py += 9;
    }
    py = SCR_H - 10;
    canvas.setTextColor(col::FAINT);
    canvas.drawString(fitText("<>hr " + String((char)247) + " T toggles", 80), px, py);
    pushFrame();
  }
  armIssAlert();  // leave an alarm set for the next pass
}
