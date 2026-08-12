#include <unity.h>

#include "day_progress.h"

void test_midnight_is_zero_percent() {
  TEST_ASSERT_DOUBLE_WITHIN(0.000001, 0.0,
                            dayProgressPercent(0, 0, 0, 0));
}

void test_noon_is_fifty_percent() {
  TEST_ASSERT_DOUBLE_WITHIN(0.000001, 50.0,
                            dayProgressPercent(12, 0, 0, 0));
}

void test_quarter_day_is_twenty_five_percent() {
  TEST_ASSERT_DOUBLE_WITHIN(0.000001, 25.0,
                            dayProgressPercent(6, 0, 0, 0));
}

void test_fractional_seconds_are_included() {
  const double expected = 0.5 * 100.0 / 86400.0;
  TEST_ASSERT_DOUBLE_WITHIN(0.000001, expected,
                            dayProgressPercent(0, 0, 0, 500000));
}

void setUp() {}
void tearDown() {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_midnight_is_zero_percent);
  RUN_TEST(test_noon_is_fifty_percent);
  RUN_TEST(test_quarter_day_is_twenty_five_percent);
  RUN_TEST(test_fractional_seconds_are_included);
  return UNITY_END();
}
