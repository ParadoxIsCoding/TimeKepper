#include "day_progress.h"

namespace {
constexpr double kSecondsPerDay = 24.0 * 60.0 * 60.0;

int clamp(int value, int minimum, int maximum) {
  if (value < minimum) {
    return minimum;
  }
  if (value > maximum) {
    return maximum;
  }
  return value;
}
}  // namespace

double dayProgressPercent(int hour, int minute, int second, int microsecond) {
  const int safeHour = clamp(hour, 0, 23);
  const int safeMinute = clamp(minute, 0, 59);
  const int safeSecond = clamp(second, 0, 59);
  const int safeMicrosecond = clamp(microsecond, 0, 999999);

  const double elapsedSeconds = safeHour * 3600.0 + safeMinute * 60.0 +
                                safeSecond + safeMicrosecond / 1000000.0;
  return elapsedSeconds * 100.0 / kSecondsPerDay;
}
