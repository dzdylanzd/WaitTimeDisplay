// Simulator timezone implementation: unlike the old stub (which always
// showed the host clock), this evaluates the park's POSIX TZ rules from the
// shared TZ_TABLE so the sim shows real park-local time — including DST —
// exactly like the device does via configTzTime().
#include "../src/tzhelper.h"
#include "../src/tzposix.h"
#include <ctime>
#include <cstdio>
#include <cstdlib>

namespace {

struct DstRule { int mon = 0, week = 0, dow = 0, hour = 2; };

struct ZoneSpec {
  long    stdOff = 0;     // seconds EAST of UTC (POSIX offsets are west, negated here)
  long    dstOff = 0;
  bool    hasDst = false;
  DstRule start, end;
};

const char* skipName(const char* p) {
  while (*p && ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z'))) p++;
  return p;
}

// POSIX offset is hours WEST of UTC ("CET-1" = UTC+1), so negate to east.
const char* parseOffset(const char* p, long& outSecEast) {
  int sign = 1;
  if (*p == '+') p++;
  else if (*p == '-') { sign = -1; p++; }
  long h = 0, m = 0;
  while (*p >= '0' && *p <= '9') h = h * 10 + (*p++ - '0');
  if (*p == ':') { p++; while (*p >= '0' && *p <= '9') m = m * 10 + (*p++ - '0'); }
  outSecEast = -sign * (h * 3600 + m * 60);
  return p;
}

// "M3.5.0/2" — month.week.weekday at local hour (default 2). week 5 = last.
bool parseRule(const char*& p, DstRule& r) {
  if (*p != 'M') return false;
  p++;
  r.mon = atoi(p);  while (*p && *p != '.') p++;  if (*p != '.') return false;  p++;
  r.week = atoi(p); while (*p && *p != '.') p++;  if (*p != '.') return false;  p++;
  r.dow = atoi(p);  while (*p >= '0' && *p <= '9') p++;
  r.hour = 2;
  if (*p == '/') { p++; r.hour = atoi(p); while (*p && *p != ',') p++; }
  if (*p == ',') p++;
  return true;
}

ZoneSpec parseZone(const char* posix) {
  ZoneSpec z;
  const char* p = skipName(posix);
  p = parseOffset(p, z.stdOff);
  const char* q = skipName(p);
  if (q != p) {                       // a DST zone name follows
    z.dstOff = z.stdOff + 3600;       // default: one hour ahead of standard
    if (*q == '+' || *q == '-' || (*q >= '0' && *q <= '9'))
      q = parseOffset(q, z.dstOff);
    if (*q == ',') {
      q++;
      z.hasDst = parseRule(q, z.start) && parseRule(q, z.end);
    }
  }
  return z;
}

int ruleDayOfMonth(int year, const DstRule& r) {
  std::tm t{};
  t.tm_year = year - 1900; t.tm_mon = r.mon - 1; t.tm_mday = 1; t.tm_hour = 12;
  time_t tt = _mkgmtime(&t);
  std::tm f; gmtime_s(&f, &tt);
  int day = 1 + ((r.dow - f.tm_wday + 7) % 7) + (r.week - 1) * 7;
  static const int mdays[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  int dim = mdays[r.mon - 1] +
            ((r.mon == 2 && (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0))) ? 1 : 0);
  while (day > dim) day -= 7;         // week 5 = last occurrence
  return day;
}

// UTC instant of a transition rule (rule hour is local wall time at offSecEast).
time_t ruleUtc(int year, const DstRule& r, long offSecEast) {
  std::tm t{};
  t.tm_year = year - 1900; t.tm_mon = r.mon - 1;
  t.tm_mday = ruleDayOfMonth(year, r); t.tm_hour = r.hour;
  return _mkgmtime(&t) - offSecEast;
}

long activeOffset(const ZoneSpec& z, time_t nowUtc) {
  if (!z.hasDst) return z.stdOff;
  std::tm g; gmtime_s(&g, &nowUtc);
  int year = g.tm_year + 1900;
  time_t startU = ruleUtc(year, z.start, z.stdOff);  // start is in std time
  time_t endU   = ruleUtc(year, z.end,   z.dstOff);  // end is in DST time
  bool dst = (startU <= endU)
      ? (nowUtc >= startU && nowUtc < endU)          // northern hemisphere
      : (nowUtc >= startU || nowUtc < endU);         // southern (wraps new year)
  return dst ? z.dstOff : z.stdOff;
}

ZoneSpec g_zone;                       // applied park zone; default = UTC

std::tm zoneTm(const ZoneSpec& z) {
  time_t now = time(nullptr);
  time_t shifted = now + activeOffset(z, now);
  std::tm out; gmtime_s(&out, &shifted);
  return out;
}

}  // namespace

void applyTimeZone(const String& ianaTz) {
  const char* posix = lookupPosixTZ(ianaTz);
  g_zone = posix ? parseZone(posix) : ZoneSpec{};   // unknown zone → UTC (device parity)
}

void resetTimeCache() {}

String getLocalTimeString() {
  std::tm t = zoneTm(g_zone);
  char buf[6];
  snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
  return String(buf);
}

bool getLocalMinutesOfDay(int& outMinutes) {
  std::tm t = zoneTm(g_zone);
  outMinutes = t.tm_hour * 60 + t.tm_min;
  return true;
}

String getLocalDateString() {
  std::tm t = zoneTm(g_zone);
  char buf[11];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
           t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
  return String(buf);
}

bool getMinutesOfDayInTz(const String& ianaTz, int& outMinutes) {
  const char* posix = lookupPosixTZ(ianaTz);
  if (!posix) return false;
  std::tm t = zoneTm(parseZone(posix));
  outMinutes = t.tm_hour * 60 + t.tm_min;
  return true;
}
