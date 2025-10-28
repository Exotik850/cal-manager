#ifndef TEST_FILTER_H
#define TEST_FILTER_H

#include "../src/filter.c"
#include <stdbool.h>
#include <stdio.h>
#include <time.h>

void expect(bool condition, const char *message);
void expect_eq(int expected, int actual, const char *message);

// Helpers
static time_t tf_mktime(int year, int mon, int mday, int hour, int min) {
  // mon is 1-12; struct tm expects 0-11
  struct tm t = {0};
  t.tm_year = year - 1900;
  t.tm_mon = mon - 1;
  t.tm_mday = mday;
  t.tm_hour = hour;
  t.tm_min = min;
  t.tm_sec = 0;
  t.tm_isdst = -1; // let mktime determine DST
  return mktime(&t);
}

static void test_filter_holiday(void) {
  Filter *f = make_filter(FILTER_HOLIDAY);
  // 2025-12-25 is Christmas
  time_t christmas = tf_mktime(2025, 12, 25, 10, 0);
  expect(evaluate_filter(f, christmas, NULL) == true,
         "FILTER_HOLIDAY should evaluate to true on holiday");

  // Set candidate to 2025-12-26 10:00 (not a holiday)
  time_t not_holiday = tf_mktime(2025, 12, 26, 10, 0);
  expect(evaluate_filter(f, not_holiday, NULL) == false,
         "FILTER_HOLIDAY should evaluate to false on non-holiday");
}

// 1) FILTER_NONE always allows any candidate
static void test_filter_none_always_true(void) {
  Filter *f = make_filter(FILTER_NONE);
  time_t c = tf_mktime(2025, 10, 22, 12, 0);
  expect(evaluate_filter(f, c, NULL) == true,
         "FILTER_NONE should always evaluate to true");
}

// 2) FILTER_DAY_OF_WEEK matches a specific weekday (tm_wday: 0=Sun..6=Sat)
static void test_filter_day_of_week_matches_monday(void) {
  // 2025-10-20 is a Monday
  time_t monday = tf_mktime(2025, 10, 20, 9, 0);
  Filter f = {.type = FILTER_DAY_OF_WEEK};
  f.data.day_of_week = 1; // Monday
  expect(evaluate_filter(&f, monday, NULL) == true,
         "FILTER_DAY_OF_WEEK should match Monday for tm_wday=1");

  // 2025-10-21 is Tuesday
  time_t tuesday = tf_mktime(2025, 10, 21, 9, 0);
  expect(evaluate_filter(&f, tuesday, NULL) == false,
         "FILTER_DAY_OF_WEEK should not match non-Monday day");
}

// 3) FILTER_AFTER_TIME is strictly after a threshold (candidate > threshold)
static void test_filter_after_time_strict_greater(void) {
  time_t threshold = tf_mktime(2025, 10, 22, 10, 0);
  Filter f = {.type = FILTER_AFTER_TIME};
  f.data.time_value = threshold;

  time_t before = tf_mktime(2025, 10, 22, 9, 59);
  time_t equal = tf_mktime(2025, 10, 22, 10, 0);
  time_t after = tf_mktime(2025, 10, 22, 10, 1);

  expect(evaluate_filter(&f, before, NULL) == false,
         "FILTER_AFTER_TIME: before threshold should be false");
  expect(evaluate_filter(&f, equal, NULL) == false,
         "FILTER_AFTER_TIME: equal to threshold should be false (strict)\n");
  expect(evaluate_filter(&f, after, NULL) == true,
         "FILTER_AFTER_TIME: strictly after threshold should be true");
}

// 4) FILTER_BEFORE_TIME is strictly before a threshold (candidate < threshold)
static void test_filter_before_time_strict_less(void) {
  time_t threshold = tf_mktime(2025, 10, 22, 15, 0);
  Filter f = {.type = FILTER_BEFORE_TIME};
  f.data.time_value = threshold;

  time_t before = tf_mktime(2025, 10, 22, 14, 59);
  time_t equal = tf_mktime(2025, 10, 22, 15, 0);
  time_t after = tf_mktime(2025, 10, 22, 15, 1);

  expect(evaluate_filter(&f, before, NULL) == true,
         "FILTER_BEFORE_TIME: strictly before threshold should be true");
  expect(evaluate_filter(&f, equal, NULL) == false,
         "FILTER_BEFORE_TIME: equal to threshold should be false (strict)");
  expect(evaluate_filter(&f, after, NULL) == false,
         "FILTER_BEFORE_TIME: after threshold should be false");
}

// 5) FILTER_AND combines two child filters (window: after A AND before B)
static void test_filter_and_window_between_times(void) {
  Filter afterF = {.type = FILTER_AFTER_TIME};
  afterF.data.time_value = tf_mktime(2025, 10, 22, 9, 0);
  Filter beforeF = {.type = FILTER_BEFORE_TIME};
  beforeF.data.time_value = tf_mktime(2025, 10, 22, 17, 0);

  Filter *andF = and_filter(&afterF, &beforeF);

  time_t inside = tf_mktime(2025, 10, 22, 10, 0);
  time_t early = tf_mktime(2025, 10, 22, 8, 59);
  time_t late = tf_mktime(2025, 10, 22, 17, 1);

  expect(evaluate_filter(andF, inside, NULL) == true,
         "FILTER_AND: time within (A,B) window should be true");
  expect(evaluate_filter(andF, early, NULL) == false,
         "FILTER_AND: time before lower bound should be false");
  expect(evaluate_filter(andF, late, NULL) == false,
         "FILTER_AND: time after upper bound should be false");
}

// 6) FILTER_OR returns true if either condition is true
static void test_filter_or_either_condition(void) {
  Filter beforeF = {.type = FILTER_BEFORE_TIME};
  beforeF.data.time_value = tf_mktime(2025, 10, 22, 9, 0);
  Filter afterF = {.type = FILTER_AFTER_TIME};
  afterF.data.time_value = tf_mktime(2025, 10, 22, 17, 0);

  Filter *orF = or_filter(&beforeF, &afterF);

  time_t early = tf_mktime(2025, 10, 22, 8, 0);
  time_t middle = tf_mktime(2025, 10, 22, 12, 0);
  time_t late = tf_mktime(2025, 10, 22, 18, 0);

  expect(evaluate_filter(orF, early, NULL) == true,
         "FILTER_OR: true when first condition is true");
  expect(evaluate_filter(orF, middle, NULL) == false,
         "FILTER_OR: false when neither condition is true");
  expect(evaluate_filter(orF, late, NULL) == true,
         "FILTER_OR: true when second condition is true");
}

// 7) FILTER_NOT negates the operand
static void test_filter_not_inverts_result(void) {
  Filter base = {.type = FILTER_BEFORE_TIME};
  base.data.time_value = tf_mktime(2025, 10, 22, 12, 0);
  Filter notF = {.type = FILTER_NOT};
  notF.data.operand = &base;

  time_t eleven = tf_mktime(2025, 10, 22, 11, 0);
  time_t thirteen = tf_mktime(2025, 10, 22, 13, 0);

  expect(evaluate_filter(&notF, eleven, NULL) == false,
         "FILTER_NOT: invert true -> false");
  expect(evaluate_filter(&notF, thirteen, NULL) == true,
         "FILTER_NOT: invert false -> true");
}

// 8) FILTER_MIN_DISTANCE enforces a buffer from existing events
//    Assumed behavior: candidate must be at least N minutes away from any
//    event boundary (start or end).
static void test_filter_min_distance_respects_buffer_after_event(void) {
  EventList *list = create_event_list();
  time_t ev_start = tf_mktime(2025, 10, 22, 9, 0);
  time_t ev_end = tf_mktime(2025, 10, 22, 10, 0);
  add_event(list, create_event("Meeting", "", ev_start, ev_end));

  Filter f = {.type = FILTER_MIN_DISTANCE};
  f.data.minutes = 30;

  time_t fifteen_after = tf_mktime(2025, 10, 22, 10, 15);
  time_t fortyfive_after = tf_mktime(2025, 10, 22, 10, 45);

  expect(evaluate_filter(&f, fifteen_after, list) == false,
         "FILTER_MIN_DISTANCE: 15m after end (need 30m) should be false");
  expect(evaluate_filter(&f, fortyfive_after, list) == true,
         "FILTER_MIN_DISTANCE: 45m after end (>=30m) should be true");

  destroy_event_list(list);
}

// Negative distance passed into filter allows overlaps for events
static void test_filter_min_distance_negative() {
  EventList *list = create_event_list();
  time_t ev_start = tf_mktime(2025, 10, 22, 11, 0);
  time_t ev_end = tf_mktime(2025, 10, 22, 12, 0);
  add_event(list, create_event("Lunch", "", ev_start, ev_end));

  Filter f = {.type = FILTER_MIN_DISTANCE};
  f.data.minutes = -10; // negative buffer

  time_t during_event = tf_mktime(2025, 10, 22, 11, 30);

  expect(evaluate_filter(&f, during_event, list) == true,
         "FILTER_MIN_DISTANCE: negative buffer should allow overlaps");

  destroy_event_list(list);
}

static void test_filter_min_distance_respects_buffer_before_event(void) {
  EventList *list = create_event_list();
  time_t ev_start = tf_mktime(2025, 10, 22, 14, 0);
  time_t ev_end = tf_mktime(2025, 10, 22, 15, 0);
  add_event(list, create_event("Call", "", ev_start, ev_end));

  Filter f = {.type = FILTER_MIN_DISTANCE};
  f.data.minutes = 45;

  time_t thirty_before = tf_mktime(2025, 10, 22, 13, 30);
  time_t sixty_before = tf_mktime(2025, 10, 22, 13, 0);

  expect(evaluate_filter(&f, thirty_before, list) == false,
         "FILTER_MIN_DISTANCE: 30m before start (need 45m) should be false");
  expect(evaluate_filter(&f, sixty_before, list) == true,
         "FILTER_MIN_DISTANCE: 60m before start (>=45m) should be true");

  destroy_event_list(list);
}

// 9) get_next_valid_minutes for AFTER_TIME suggests waiting until threshold
static void test_get_next_valid_minutes_after_time_boundary(void) {
  time_t threshold = tf_mktime(2025, 10, 22, 16, 0);
  Filter f = {.type = FILTER_AFTER_TIME};
  f.data.time_value = threshold;

  time_t candidate = tf_mktime(2025, 10, 22, 15, 30);
  int delta = get_next_valid_minutes(&f, candidate, NULL);
  expect(delta >= 30 && delta <= 31,
         "get_next_valid_minutes(AFTER_TIME): ~30 minutes to threshold");

  // Already valid -> expect 0
  time_t valid = tf_mktime(2025, 10, 22, 16, 1);
  int delta2 = get_next_valid_minutes(&f, valid, NULL);
  expect_eq(0, delta2,
            "get_next_valid_minutes(AFTER_TIME): already valid -> 0");
}

// Aggregate runner for all filter tests
static inline void run_filter_tests(void) {
  puts("Running filter tests...");
  test_filter_holiday();
  test_filter_none_always_true();
  test_filter_day_of_week_matches_monday();
  test_filter_after_time_strict_greater();
  test_filter_before_time_strict_less();
  test_filter_and_window_between_times();
  test_filter_or_either_condition();
  test_filter_not_inverts_result();
  test_filter_min_distance_respects_buffer_after_event();
  test_filter_min_distance_respects_buffer_before_event();
  test_filter_min_distance_negative();
  test_get_next_valid_minutes_after_time_boundary();
}

#endif // TEST_FILTER_H
