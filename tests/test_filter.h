#ifndef TEST_FILTER_H
#define TEST_FILTER_H

#include "../src/filter.c"
#include <stdbool.h>
#include <stdio.h>
#include <time.h>

void expect(const bool condition, const char *message);
void expect_eq(const int actual, const int expected, const char *message);
void increment_assertions();
void increment_failures();

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

static void expect_time_eq(const time_t actual, const time_t expected,
                           const char *message) {
  increment_assertions();
  if (actual != expected) {
    char actual_buf[100];
    char expected_buf[100];
    strftime(actual_buf, sizeof(actual_buf), "%Y-%m-%d %H:%M:%S",
             localtime(&actual));
    strftime(expected_buf, sizeof(expected_buf), "%Y-%m-%d %H:%M:%S",
             localtime(&expected));
    printf("FAIL: %s. Expected %s, got %s\n", message, expected_buf,
           actual_buf);
    increment_failures();
  }
}

static void test_filter_none(void) {
  Filter *f = make_filter(FILTER_NONE);
  time_t now = time(NULL);
  expect(evaluate_filter(f, now, 0, NULL), "FILTER_NONE is always true");
  expect_eq(until_valid(f, now, 0, NULL), 0, "FILTER_NONE next valid is 0");
  destroy_filter(f);
}

static void test_filter_day_of_week(void) {
  Filter *f = make_filter(FILTER_DAY_OF_WEEK);
  f->data.day_of_week = 1; // Monday

  // 2025-10-20 is a Monday
  time_t monday = tf_mktime(2025, 10, 20, 9, 0);
  expect(evaluate_filter(f, monday, 0, NULL),
         "FILTER_DAY_OF_WEEK matches Monday");
  expect_eq(until_valid(f, monday, 0, NULL), 0,
            "FILTER_DAY_OF_WEEK on Monday is valid now");

  // 2025-10-21 is Tuesday
  time_t tuesday = tf_mktime(2025, 10, 21, 9, 0);
  expect(!evaluate_filter(f, tuesday, 0, NULL),
         "FILTER_DAY_OF_WEEK does not match Tuesday");
  // It's Tuesday 9:00. Next Monday is 6 days away.
  // 6*24*60 - (9*60) = 8640 - 540 = 8100 minutes
  expect_eq(until_valid(f, tuesday, 0, NULL), 8100 * 60,
            "FILTER_DAY_OF_WEEK from Tuesday to Monday");

  destroy_filter(f);
}

static void test_filter_after_datetime(void) {
  Filter *f = make_filter(FILTER_AFTER_DATETIME);
  f->data.time_value = tf_mktime(2025, 10, 22, 10, 0);

  time_t before = tf_mktime(2025, 10, 22, 9, 59);
  time_t equal = tf_mktime(2025, 10, 22, 10, 0);
  time_t after = tf_mktime(2025, 10, 22, 10, 1);

  expect(!evaluate_filter(f, before, 0, NULL),
         "AFTER_DATETIME: before is false");
  expect(!evaluate_filter(f, equal, 0, NULL), "AFTER_DATETIME: equal is false");
  expect(evaluate_filter(f, after, 0, NULL), "AFTER_DATETIME: after is true");

  expect_eq(until_valid(f, before, 0, NULL), 61,
            "AFTER_DATETIME: next valid from before");
  expect_eq(until_valid(f, equal, 0, NULL), 1,
            "AFTER_DATETIME: next valid from equal");
  expect_eq(until_valid(f, after, 0, NULL), 0,
            "AFTER_DATETIME: next valid from after");
  destroy_filter(f);
}

static void test_filter_before_datetime(void) {
  Filter *f = make_filter(FILTER_BEFORE_DATETIME);
  f->data.time_value = tf_mktime(2025, 10, 22, 15, 0);

  time_t before = tf_mktime(2025, 10, 22, 14, 59);
  time_t equal = tf_mktime(2025, 10, 22, 15, 0);
  time_t after = tf_mktime(2025, 10, 22, 15, 1);

  expect(evaluate_filter(f, before, 0, NULL),
         "BEFORE_DATETIME: before is true");
  expect(!evaluate_filter(f, equal, 0, NULL),
         "BEFORE_DATETIME: equal is false");
  expect(!evaluate_filter(f, after, 0, NULL),
         "BEFORE_DATETIME: after is false");

  expect_eq(until_valid(f, before, 0, NULL), 0,
            "BEFORE_DATETIME: next valid from before");
  expect_eq(until_valid(f, equal, 0, NULL), -1,
            "BEFORE_DATETIME: next valid from equal is never");
  expect_eq(until_valid(f, after, 0, NULL), -1,
            "BEFORE_DATETIME: next valid from after is never");
  destroy_filter(f);
}

static void test_filter_after_time(void) {
  Filter *f = make_filter(FILTER_AFTER_TIME);
  f->data.time_value = tf_mktime(2025, 1, 1, 10, 0); // Date part is ignored

  time_t before = tf_mktime(2025, 10, 22, 9, 0);
  time_t equal = tf_mktime(2025, 10, 22, 10, 0);
  time_t after = tf_mktime(2025, 10, 22, 11, 0);

  expect(!evaluate_filter(f, before, 0, NULL), "AFTER_TIME: before is false");
  expect(evaluate_filter(f, equal, 0, NULL), "AFTER_TIME: equal is true");
  expect(evaluate_filter(f, after, 0, NULL), "AFTER_TIME: after is true");

  expect_eq(until_valid(f, before, 0, NULL), 3601,
            "AFTER_TIME: next valid from before");
  expect_eq(until_valid(f, equal, 0, NULL), 0,
            "AFTER_TIME: next valid from equal");
  expect_eq(until_valid(f, after, 0, NULL), 0,
            "AFTER_TIME: next valid from after");
  destroy_filter(f);
}

static void test_filter_before_time(void) {
  Filter *f = make_filter(FILTER_BEFORE_TIME);
  f->data.time_value = tf_mktime(2025, 1, 1, 12, 0); // Date part is ignored

  time_t before = tf_mktime(2025, 10, 22, 11, 0);
  time_t equal = tf_mktime(2025, 10, 22, 12, 0);
  time_t after = tf_mktime(2025, 10, 22, 13, 0);

  expect(evaluate_filter(f, before, 0, NULL), "BEFORE_TIME: before is true");
  expect(!evaluate_filter(f, equal, 0, NULL), "BEFORE_TIME: equal is false");
  expect(!evaluate_filter(f, after, 0, NULL), "BEFORE_TIME: after is false");

  expect_eq(until_valid(f, before, 0, NULL), 0,
            "BEFORE_TIME: next valid from before");
  // 12 hours until midnight
  expect_eq(until_valid(f, equal, 0, NULL), 12 * 60 * 60,
            "BEFORE_TIME: next valid from equal is next day");
  // 11 hours until midnight
  expect_eq(until_valid(f, after, 0, NULL), 11 * 60 * 60,
            "BEFORE_TIME: next valid from after is next day");
  destroy_filter(f);
}

static void test_filter_holiday(void) {
  Filter *f = make_filter(FILTER_HOLIDAY);

  time_t christmas = tf_mktime(2025, 12, 25, 10, 0);
  expect(evaluate_filter(f, christmas, 0, NULL), "FILTER_HOLIDAY on Christmas");

  time_t not_holiday = tf_mktime(2025, 12, 26, 10, 0);
  expect(!evaluate_filter(f, not_holiday, 0, NULL),
         "FILTER_HOLIDAY on day after Christmas");

  time_t before_christmas = tf_mktime(2025, 12, 24, 10, 0);
  // 14 hours until midnight, then it's Christmas
  expect_eq(until_valid(f, before_christmas, 0, NULL), 14 * 60 * 60,
            "FILTER_HOLIDAY: minutes until Christmas");

  time_t after_christmas = tf_mktime(2025, 12, 26, 10, 0);
  // 5 days to NYE (Dec 31), plus 14 hours to get to midnight on the 26th.
  // (31-26-1)*24*60 + 14*60 = 4*24*60 + 14*60 = 5760 + 840 = 6600
  expect_eq(until_valid(f, after_christmas, 0, NULL), 6600 * 60,
            "FILTER_HOLIDAY: minutes until New Year's Eve");

  destroy_filter(f);
}

static void test_filter_min_distance(void) {
  Calendar *cal = create_calendar();
  add_event_calendar(cal, "Event 1", "", tf_mktime(2025, 10, 22, 9, 0),
                     tf_mktime(2025, 10, 22, 10, 0));
  add_event_calendar(cal, "Event 2", "", tf_mktime(2025, 10, 22, 14, 0),
                     tf_mktime(2025, 10, 22, 15, 0));

  Filter *f = make_filter(FILTER_MIN_DISTANCE);
  f->data.minutes = 30;

  time_t duration = 30 * 60;

  // Before all events
  time_t way_before = tf_mktime(2025, 10, 22, 8, 0);
  expect_eq(until_valid(f, way_before, duration, cal), 0,
            "MIN_DISTANCE: valid far before any event");

  // Too close before Event 1
  time_t close_before = tf_mktime(2025, 10, 22, 8, 45);
  // Should jump to 30 mins after Event 1 (10:30)
  // 10:30 - 8:45 = 1h 45m = 105 mins
  expect_eq(until_valid(f, close_before, duration, cal), 105 * 60,
            "MIN_DISTANCE: invalid close before event");

  // In between events
  time_t between = tf_mktime(2025, 10, 22, 11, 0);
  expect_eq(until_valid(f, between, duration, cal), 0,
            "MIN_DISTANCE: valid between events");

  // Too close after Event 1
  time_t close_after = tf_mktime(2025, 10, 22, 13, 15);
  // Should jump to 30 mins after Event 2 (15:30)
  // 15:30 - 13:15 = 2h 15m = 135 mins
  expect_eq(until_valid(f, close_after, duration, cal), 135 * 60,
            "MIN_DISTANCE: invalid close after event");

  // During an event
  time_t during = tf_mktime(2025, 10, 22, 9, 30);
  // Should jump to 30 mins after Event 1 (10:30)
  // 10:30 - 9:30 = 1h = 60 mins
  expect_eq(until_valid(f, during, duration, cal), 60 * 60,
            "MIN_DISTANCE: invalid during event");

  // Negative distance (overlap allowed)
  f->data.minutes = -15;
  time_t overlap_candidate = tf_mktime(2025, 10, 22, 9, 45);
  expect_eq(until_valid(f, overlap_candidate, duration, cal), 0,
            "MIN_DISTANCE: negative distance allows overlap");

  destroy_filter(f);
  free_calendar(cal);
}

static void test_filter_and(void) {
  Filter *after = make_filter(FILTER_AFTER_DATETIME);
  after->data.time_value = tf_mktime(2025, 10, 22, 9, 0);
  Filter *before = make_filter(FILTER_BEFORE_DATETIME);
  before->data.time_value = tf_mktime(2025, 10, 22, 17, 0);
  Filter *andF = and_filter(after, before);

  time_t early = tf_mktime(2025, 10, 22, 8, 59);
  time_t inside = tf_mktime(2025, 10, 22, 10, 0);
  time_t late = tf_mktime(2025, 10, 22, 17, 1);

  expect(!evaluate_filter(andF, early, 0, NULL), "FILTER_AND: before is false");
  expect(evaluate_filter(andF, inside, 0, NULL), "FILTER_AND: inside is true");
  expect(!evaluate_filter(andF, late, 0, NULL), "FILTER_AND: after is false");

  expect_eq(until_valid(andF, early, 0, NULL), 61,
            "FILTER_AND: next valid from early");
  expect_eq(until_valid(andF, inside, 0, NULL), 0,
            "FILTER_AND: next valid from inside");
  expect_eq(until_valid(andF, late, 0, NULL), -1,
            "FILTER_AND: next valid from late is never");

  // Note: and_filter transfers ownership, so only destroy the top-level filter
  destroy_filter(andF);
}

static void test_filter_or(void) {
  Filter *before = make_filter(FILTER_BEFORE_DATETIME);
  before->data.time_value = tf_mktime(2025, 10, 22, 9, 0);
  Filter *after = make_filter(FILTER_AFTER_DATETIME);
  after->data.time_value = tf_mktime(2025, 10, 22, 17, 0);
  Filter *orF = or_filter(before, after);

  time_t early = tf_mktime(2025, 10, 22, 8, 0);
  time_t middle = tf_mktime(2025, 10, 22, 12, 0);
  time_t late = tf_mktime(2025, 10, 22, 18, 0);

  expect(evaluate_filter(orF, early, 0, NULL), "FILTER_OR: early is true");
  expect(!evaluate_filter(orF, middle, 0, NULL), "FILTER_OR: middle is false");
  expect(evaluate_filter(orF, late, 0, NULL), "FILTER_OR: late is true");

  expect_eq(until_valid(orF, early, 0, NULL), 0,
            "FILTER_OR: next valid from early");
  expect_eq(until_valid(orF, middle, 0, NULL), 300 * 60 + 1,
            "FILTER_OR: next valid from middle");
  expect_eq(until_valid(orF, late, 0, NULL), 0,
            "FILTER_OR: next valid from late");

  destroy_filter(orF);
}

static void test_filter_not(void) {
  Filter *base = make_filter(FILTER_BEFORE_DATETIME);
  base->data.time_value = tf_mktime(2025, 10, 22, 12, 0);
  Filter *notF = not_filter(base);

  time_t eleven = tf_mktime(2025, 10, 22, 11, 0);
  time_t twelve = tf_mktime(2025, 10, 22, 12, 0);
  time_t thirteen = tf_mktime(2025, 10, 22, 13, 0);

  expect(!evaluate_filter(notF, eleven, 0, NULL), "FILTER_NOT: inverts true");
  expect(evaluate_filter(notF, twelve, 0, NULL),
         "FILTER_NOT: inverts false equal to");
  expect(evaluate_filter(notF, thirteen, 0, NULL),
         "FILTER_NOT: inverts false greater than");

  // `not before 12:00` is `after or equal 12:00`
  expect_eq(until_valid(notF, eleven, 0, NULL), 60 * 60,
            "FILTER_NOT: next valid from before");
  expect_eq(until_valid(notF, twelve, 0, NULL), 0,
            "FILTER_NOT: next valid from equal");
  expect_eq(until_valid(notF, thirteen, 0, NULL), 0,
            "FILTER_NOT: next valid from after");

  destroy_filter(notF);
}

static void test_find_optimal_time(void) {
  Calendar *cal = create_calendar();
  add_event_calendar(cal, "Blocker", "", tf_mktime(2025, 11, 13, 10, 0),
                     tf_mktime(2025, 11, 13, 11, 0));

  // Filter: After 9am, with 30min distance from events
  Filter *after9am = make_filter(FILTER_AFTER_TIME);
  after9am->data.time_value = tf_mktime(2025, 1, 1, 9, 0);
  Filter *min_dist = make_filter(FILTER_MIN_DISTANCE);
  min_dist->data.minutes = 30;
  Filter *f = and_filter(after9am, min_dist);

  // Current time is 2025-11-13 00:00:00
  time_t now = tf_mktime(2025, 11, 13, 0, 0);
  time_t optimal = find_optimal_time(cal, f, now, 0);

  // Expected: after 9am, and >= 30 mins before 10am event -> 9:00:01
  time_t expected = tf_mktime(2025, 11, 13, 9, 0) + 1;
  expect_time_eq(optimal, expected,
                 "find_optimal_time finds slot before event");

  // Expected: if we start searching from 11:00, we should get 11:30:00
  // (30 mins after event end)
  time_t start_search = tf_mktime(2025, 11, 13, 11, 0);
  optimal = find_optimal_time(cal, f, start_search, 0);
  expected = tf_mktime(2025, 11, 13, 11, 30);
  expect_time_eq(optimal, expected,
                 "find_optimal_time finds slot after event end");

  destroy_filter(f);
  free_calendar(cal);
}

// Aggregate runner for all filter tests
static inline void run_filter_tests(void) {
  puts("Running filter tests...");
  test_filter_none();
  test_filter_day_of_week();
  test_filter_after_datetime();
  test_filter_before_datetime();
  test_filter_after_time();
  test_filter_before_time();
  test_filter_holiday();
  test_filter_min_distance();
  test_filter_and();
  test_filter_or();
  test_filter_not();
  test_find_optimal_time();
  puts("Filter tests completed.");
}

#endif // TEST_FILTER_H
