#include "timeutil.h"
#include <stdio.h>

int64_t days_from_civil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int64_t)doe - 719468;
}

time_t parse_iso_utc(const char *s) {
  int Y, M, D, h = 0, m = 0, sec = 0;
  if (!s || sscanf(s, "%d-%d-%d%*c%d:%d:%d", &Y, &M, &D, &h, &m, &sec) < 3) return 0;
  return (time_t)(days_from_civil(Y, M, D) * 86400 + h * 3600 + m * 60 + sec);
}
