#include "../src/calendar.c"
#include "../src/filter.c"
#include "test_calendar.h"
#include "test_filter.h"
#include <stdio.h>


static unsigned assertions = 0;
static unsigned failures = 0;

void expect(bool condition, const char *message) {
  assertions++;
  if (!condition) {
    failures++;
    printf("FAIL: %s\n", message);
  }
}

void expect_eq(int a, int b, const char *message) {
  assertions++;
  if (a != b) {
    failures++;
    printf("FAIL: %s (expected %d, got %d)\n", message, a, b);
  }
}

int main() {
  // Run test suites
  run_filter_tests();
  run_calendar_tests();

  printf("Out of %u assertions, %u failed\n", assertions, failures);
  printf("Success rate: %.2f%%\n",
         (assertions > 0) ? 100.0 * (1.0 - (double)failures / assertions)
                          : 0.0);
  return failures == 0 ? 0 : 1;
}