#ifndef TEST_CALENDAR_H
#define TEST_CALENDAR_H

#include "../src/calendar.c"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

void expect(bool condition, const char *message);
void expect_eq(int expected, int actual, const char *message);

// Helper function to create time_t values
static time_t tca_mktime(int year, int mon, int mday, int hour, int min) {
  struct tm t = {0};
  t.tm_year = year - 1900;
  t.tm_mon = mon - 1;
  t.tm_mday = mday;
  t.tm_hour = hour;
  t.tm_min = min;
  t.tm_sec = 0;
  t.tm_isdst = -1;
  return mktime(&t);
}

// 1) create_calendar initializes with empty years and valid event list
static void test_create_calendar_initial_state(void) {
  Calendar *cal = create_calendar();
  expect(cal != NULL, "create_calendar should return non-NULL");
  expect(cal->years == NULL, "new calendar should have NULL years");
  expect(cal->event_list != NULL, "new calendar should have valid event_list");
  expect(cal->event_list->head == NULL,
         "new calendar event_list should be empty");
  free_calendar(cal);
}

// 2) add_event_calendar creates year bucket and stores event
static void test_add_event_creates_year_bucket(void) {
  Calendar *cal = create_calendar();
  time_t start = tca_mktime(2025, 10, 22, 9, 0);
  time_t end = tca_mktime(2025, 10, 22, 10, 0);

  Event *event =
      add_event_calendar(cal, "Meeting", "Daily standup", start, end);
  expect(event != NULL, "add_event_calendar should return non-NULL");
  expect(cal->years != NULL, "adding event should create year bucket");
  expect_eq(2025, cal->years->year, "year bucket should be for 2025");
  free_calendar(cal);
}

// 3) add_event_calendar stores event in correct day slot
static void test_add_event_stores_in_correct_day(void) {
  Calendar *cal = create_calendar();
  time_t start = tca_mktime(2025, 3, 15, 10, 0);
  time_t end = tca_mktime(2025, 3, 15, 11, 0);

  Event *event = add_event_calendar(cal, "Event", "Description", start, end);
  Event *first = get_first_event(cal, 2025, 3, 15);
  expect(first == event, "get_first_event should return the added event");
  expect(first != NULL && strcmp(first->title, "Event") == 0,
         "retrieved event should have correct title");
  free_calendar(cal);
}

// 4) add_event_calendar maintains earliest event as first for each day
static void test_add_event_maintains_earliest_first(void) {
  Calendar *cal = create_calendar();
  time_t start1 = tca_mktime(2025, 5, 10, 14, 0);
  time_t end1 = tca_mktime(2025, 5, 10, 15, 0);
  time_t start2 = tca_mktime(2025, 5, 10, 9, 0);
  time_t end2 = tca_mktime(2025, 5, 10, 10, 0);

  Event *later = add_event_calendar(cal, "Later", "", start1, end1);
  Event *earlier = add_event_calendar(cal, "Earlier", "", start2, end2);

  Event *first = get_first_event(cal, 2025, 5, 10);
  expect(first == earlier,
         "get_first_event should return earlier event of the day");
  expect(first != NULL && strcmp(first->title, "Earlier") == 0,
         "first event should be the one with earlier start time");
  expect(earlier->next == later,
         "earlier event's next should point to later event");
  free_calendar(cal);
}

// 5) add_event_calendar handles multiple years in sorted order
static void test_add_event_multiple_years_sorted(void) {
  Calendar *cal = create_calendar();
  add_event_calendar(cal, "2026", "", tca_mktime(2026, 1, 1, 10, 0),
                     tca_mktime(2026, 1, 1, 11, 0));
  add_event_calendar(cal, "2024", "", tca_mktime(2024, 1, 1, 10, 0),
                     tca_mktime(2024, 1, 1, 11, 0));
  add_event_calendar(cal, "2025", "", tca_mktime(2025, 1, 1, 10, 0),
                     tca_mktime(2025, 1, 1, 11, 0));

  expect(cal->years != NULL && cal->years->year == 2024,
         "first year bucket should be 2024");
  expect(cal->years->next != NULL && cal->years->next->year == 2025,
         "second year bucket should be 2025");
  expect(cal->years->next->next != NULL && cal->years->next->next->year == 2026,
         "third year bucket should be 2026");
  free_calendar(cal);
}

// 6) get_first_event returns NULL for dates with no events
static void test_get_first_event_returns_null_when_empty(void) {
  Calendar *cal = create_calendar();
  Event *event = get_first_event(cal, 2025, 10, 22);
  expect(event == NULL,
         "get_first_event should return NULL for date with no events");
  free_calendar(cal);
}

// 7) get_first_event returns NULL for invalid dates
static void test_get_first_event_invalid_date(void) {
  Calendar *cal = create_calendar();
  add_event_calendar(cal, "Event", "", tca_mktime(2025, 2, 15, 10, 0),
                     tca_mktime(2025, 2, 15, 11, 0));

  Event *bad_month = get_first_event(cal, 2025, 13, 1);
  Event *bad_day = get_first_event(cal, 2025, 2, 30);
  expect(bad_month == NULL,
         "get_first_event should return NULL for invalid month");
  expect(bad_day == NULL, "get_first_event should return NULL for invalid day");
  free_calendar(cal);
}

// 8) get_first_event handles leap year correctly
static void test_get_first_event_leap_year(void) {
  Calendar *cal = create_calendar();
  // 2024 is a leap year
  time_t start = tca_mktime(2024, 2, 29, 10, 0);
  time_t end = tca_mktime(2024, 2, 29, 11, 0);
  Event *event = add_event_calendar(cal, "Leap Day", "", start, end);

  Event *found = get_first_event(cal, 2024, 2, 29);
  expect(found == event, "get_first_event should find event on leap day");

  // 2025 is not a leap year
  Event *invalid = get_first_event(cal, 2025, 2, 29);
  expect(invalid == NULL,
         "get_first_event should return NULL for Feb 29 in non-leap year");
  free_calendar(cal);
}

// 9) remove_event_calendar removes event and returns pointer
static void test_remove_event_returns_event(void) {
  Calendar *cal = create_calendar();
  Event *event =
      add_event_calendar(cal, "To Remove", "", tca_mktime(2025, 7, 4, 10, 0),
                         tca_mktime(2025, 7, 4, 11, 0));
  EventID id = event->id;

  Event *removed = remove_event_calendar(cal, id);
  expect(removed != NULL, "remove_event_calendar should return removed event");
  expect_eq(id, removed->id, "removed event should have correct id");
  free_calendar(cal);
}

// 10) remove_event_calendar updates day pointer when removing first event
static void test_remove_event_updates_day_pointer(void) {
  Calendar *cal = create_calendar();
  time_t start1 = tca_mktime(2025, 6, 15, 9, 0);
  time_t end1 = tca_mktime(2025, 6, 15, 10, 0);
  time_t start2 = tca_mktime(2025, 6, 15, 14, 0);
  time_t end2 = tca_mktime(2025, 6, 15, 15, 0);

  Event *first = add_event_calendar(cal, "First", "", start1, end1);
  Event *second = add_event_calendar(cal, "Second", "", start2, end2);
  EventID first_id = first->id;

  remove_event_calendar(cal, first_id);
  Event *now_first = get_first_event(cal, 2025, 6, 15);
  expect(now_first == second,
         "after removing first event, second event should become first");
  free_calendar(cal);
}

// 11) remove_event_calendar sets day pointer to NULL when removing last event
static void test_remove_event_clears_day_when_last(void) {
  Calendar *cal = create_calendar();
  Event *event =
      add_event_calendar(cal, "Only", "", tca_mktime(2025, 8, 20, 10, 0),
                         tca_mktime(2025, 8, 20, 11, 0));
  EventID id = event->id;

  remove_event_calendar(cal, id);
  Event *found = get_first_event(cal, 2025, 8, 20);
  expect(found == NULL,
         "after removing only event of the day, get_first_event should return "
         "NULL");
  free_calendar(cal);
}

// 12) remove_event_calendar returns NULL for non-existent event
static void test_remove_event_nonexistent_returns_null(void) {
  Calendar *cal = create_calendar();
  Event *removed = remove_event_calendar(cal, 99999);
  expect(removed == NULL,
         "remove_event_calendar should return NULL for non-existent id");
  free_calendar(cal);
}

// 13) multiple events on same day are all in event list
static void test_multiple_events_same_day_in_list(void) {
  Calendar *cal = create_calendar();
  add_event_calendar(cal, "Event1", "", tca_mktime(2025, 4, 10, 9, 0),
                     tca_mktime(2025, 4, 10, 10, 0));
  add_event_calendar(cal, "Event2", "", tca_mktime(2025, 4, 10, 11, 0),
                     tca_mktime(2025, 4, 10, 12, 0));
  add_event_calendar(cal, "Event3", "", tca_mktime(2025, 4, 10, 13, 0),
                     tca_mktime(2025, 4, 10, 14, 0));

  expect(cal->event_list->head != NULL, "event list should contain events");
  int count = 0;
  Event *curr = cal->event_list->head;
  while (curr) {
    count++;
    curr = curr->next;
  }
  expect_eq(3, count, "event list should contain all 3 events");
  free_calendar(cal);
}

// 14) add_event_calendar handles events spanning midnight
static void test_add_event_spanning_midnight(void) {
  Calendar *cal = create_calendar();
  time_t start = tca_mktime(2025, 9, 30, 23, 0);
  time_t end = tca_mktime(2025, 10, 1, 1, 0);

  Event *event = add_event_calendar(cal, "Late Night", "", start, end);
  expect(event != NULL,
         "add_event_calendar should handle midnight-spanning events");

  // Event should be stored on its start date
  Event *found = get_first_event(cal, 2025, 9, 30);
  expect(found == event,
         "event spanning midnight should be stored on start date");
  free_calendar(cal);
}

// 15) free_calendar handles NULL safely
static void test_free_calendar_handles_null(void) {
  free_calendar(NULL);
  // Should not crash
  expect(true, "free_calendar should handle NULL safely");
}

// 16) add_event_calendar handles different months in same year
static void test_add_event_different_months_same_year(void) {
  Calendar *cal = create_calendar();
  Event *jan =
      add_event_calendar(cal, "January", "", tca_mktime(2025, 1, 15, 10, 0),
                         tca_mktime(2025, 1, 15, 11, 0));
  Event *jun =
      add_event_calendar(cal, "June", "", tca_mktime(2025, 6, 15, 10, 0),
                         tca_mktime(2025, 6, 15, 11, 0));
  Event *dec =
      add_event_calendar(cal, "December", "", tca_mktime(2025, 12, 15, 10, 0),
                         tca_mktime(2025, 12, 15, 11, 0));

  expect(get_first_event(cal, 2025, 1, 15) == jan,
         "should retrieve January event");
  expect(get_first_event(cal, 2025, 6, 15) == jun,
         "should retrieve June event");
  expect(get_first_event(cal, 2025, 12, 15) == dec,
         "should retrieve December event");

  // All should be in same year bucket
  expect(cal->years != NULL && cal->years->next == NULL,
         "all events in same year should share one year bucket");
  free_calendar(cal);
}

// 17) remove_event_calendar doesn't affect other days
static void test_remove_event_preserves_other_days(void) {
  Calendar *cal = create_calendar();
  Event *day1 =
      add_event_calendar(cal, "Day1", "", tca_mktime(2025, 5, 1, 10, 0),
                         tca_mktime(2025, 5, 1, 11, 0));
  Event *day2 =
      add_event_calendar(cal, "Day2", "", tca_mktime(2025, 5, 2, 10, 0),
                         tca_mktime(2025, 5, 2, 11, 0));
  Event *day3 =
      add_event_calendar(cal, "Day3", "", tca_mktime(2025, 5, 3, 10, 0),
                         tca_mktime(2025, 5, 3, 11, 0));

  remove_event_calendar(cal, day2->id);

  expect(get_first_event(cal, 2025, 5, 1) == day1,
         "removing event from day 2 should not affect day 1");
  expect(get_first_event(cal, 2025, 5, 2) == NULL,
         "day 2 should have no events after removal");
  expect(get_first_event(cal, 2025, 5, 3) == day3,
         "removing event from day 2 should not affect day 3");

  expect(day1->next == day3,
         "day 1's next should point to day 3 after removing day 2 event");
  free_calendar(cal);
}

// 18) get_first_event returns NULL for year that doesn't exist
static void test_get_first_event_nonexistent_year(void) {
  Calendar *cal = create_calendar();
  add_event_calendar(cal, "2025", "", tca_mktime(2025, 1, 1, 10, 0),
                     tca_mktime(2025, 1, 1, 11, 0));

  Event *found = get_first_event(cal, 2026, 1, 1);
  expect(found == NULL,
         "get_first_event should return NULL for year with no events");
  free_calendar(cal);
}

// Aggregate runner for all calendar tests
static inline void run_calendar_tests(void) {
  puts("Running calendar tests...");
  test_create_calendar_initial_state();
  test_add_event_creates_year_bucket();
  test_add_event_stores_in_correct_day();
  test_add_event_maintains_earliest_first();
  test_add_event_multiple_years_sorted();
  test_get_first_event_returns_null_when_empty();
  test_get_first_event_invalid_date();
  test_get_first_event_leap_year();
  test_remove_event_returns_event();
  test_remove_event_updates_day_pointer();
  test_remove_event_clears_day_when_last();
  test_remove_event_nonexistent_returns_null();
  test_multiple_events_same_day_in_list();
  test_add_event_spanning_midnight();
  test_free_calendar_handles_null();
  test_add_event_different_months_same_year();
  test_remove_event_preserves_other_days();
  test_get_first_event_nonexistent_year();
  puts("Calendar tests completed.");
}

#endif // TEST_CALENDAR_H
