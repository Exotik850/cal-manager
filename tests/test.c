#include "test_calendar.h"
#include "test_event_list.h"
#include "test_filter.h"
#include "test_parse.h"
#include <stdio.h>


static unsigned assertions = 0;
static unsigned failures = 0;

void expect(const bool condition, const char *message) {
  assertions++;
  if (!condition) {
    failures++;
    printf("FAIL: %s\n", message);
  }
}

void expect_eq(const int real, const int expected, const char *message) {
  assertions++;
  if (real != expected) {
    failures++;
    printf("FAIL: %s (expected %d, got %d)\n", message, expected, real);
  }
}

int main() {
  // Run test suites
  run_calendar_tests();
  run_filter_tests();
  run_event_list_tests();
  run_parse_tests();

  printf("Out of %u assertions, %u failed\n", assertions, failures);
  printf("Success rate: %.2f%%\n",
         (assertions > 0) ? 100.0 * (1.0 - (double)failures / assertions)
                          : 0.0);
  return failures == 0 ? 0 : 1;
}
