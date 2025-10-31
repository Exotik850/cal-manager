#ifndef TEST_EVENT_LIST_H
#define TEST_EVENT_LIST_H

#include "../src/event_list.c"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void expect(bool condition, const char *message);
void expect_eq(int expected, int actual, const char *message);

// Helpers
static time_t tc_mktime(int year, int mon, int mday, int hour, int min) {
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

// 1) create_event_list initializes empty list and id counter
static void test_create_event_list_initial_state(void) {
  EventList *list = create_event_list();
  expect(list != NULL, "create_event_list should return non-NULL");
  expect(list->head == NULL, "new EventList should have NULL head");
  expect(list->next_id == 1, "new EventList should start next_id at 1");
  destroy_event_list(list);
}

// 2) create_event sets fields and no ID yet
static void test_create_event_sets_fields(void) {
  time_t s = tc_mktime(2025, 10, 22, 9, 0);
  time_t e = tc_mktime(2025, 10, 22, 10, 0);
  Event *ev = create_event("Title", "Desc", s, e);
  expect(ev != NULL, "create_event should return non-NULL");
  expect_eq(0, ev->id, "new Event should have id=0 before add_event");
  expect(ev->repeat_id == -1, "new Event repeat_id should be -1");
  expect(ev->parent_id == -1, "new Event parent_id should be -1");
  // expect(ev->is_repeating == false, "new Event is_repeating should be
  // false"); expect(ev->repeat_count == 0, "new Event repeat_count should be
  // 0");
  expect(ev->start_time == s && ev->end_time == e,
         "create_event should set start/end times");
  expect(ev->next == NULL, "new Event next should be NULL");
  free(ev);
}

// 3) add_event assigns incremental IDs and inserts at head if earlier
static void test_add_event_assigns_ids_and_orders(void) {
  EventList *list = create_event_list();
  time_t s1 = tc_mktime(2025, 10, 22, 10, 0);
  time_t e1 = tc_mktime(2025, 10, 22, 11, 0);
  time_t s2 = tc_mktime(2025, 10, 22, 9, 0);
  time_t e2 = tc_mktime(2025, 10, 22, 9, 30);

  Event *a = add_event_to_list(list, "A", "", s1, e1);
  Event *b = add_event_to_list(list, "B", "", s2, e2);

  expect_eq(1, a->id, "first added event should get id=1");
  expect_eq(2, b->id, "second added event should get id=2");
  expect(list->head == b && list->head->next == a,
         "events should be ordered by ascending start_time");
  expect(list->tail == a, "tail should point to last event");
  expect_eq(a->parent->id, b->id, "parent pointer should be set correctly");
  destroy_event_list(list);
}

// 4) remove_event deletes head safely
static void test_remove_event_removes_head(void) {
  EventList *list = create_event_list();
  Event *a = add_event_to_list(list, "A", "", tc_mktime(2025, 10, 22, 9, 0),
                               tc_mktime(2025, 10, 22, 10, 0));
  Event *b = add_event_to_list(list, "B", "", tc_mktime(2025, 10, 22, 11, 0),
                               tc_mktime(2025, 10, 22, 12, 0));
  // Head should be 'a' (earlier start). Remove it.
  expect(list->head == a, "earliest event should be at head before removal");
  remove_event(list, a->id);
  expect(list->head == b, "after removing head, next node should become head");
  expect(list->head->next == NULL,
         "after removal, only one node should remain");
  destroy_event_list(list);
}

// 5) remove_event deletes middle node and preserves links
static void test_remove_event_middle_node(void) {
  EventList *list = create_event_list();

  Event *e1 = add_event_to_list(list, "1", "", tc_mktime(2025, 10, 22, 8, 0),
                                tc_mktime(2025, 10, 22, 8, 30));
  Event *e2 = add_event_to_list(list, "2", "", tc_mktime(2025, 10, 22, 9, 0),
                                tc_mktime(2025, 10, 22, 9, 30));
  Event *e3 = add_event_to_list(list, "3", "", tc_mktime(2025, 10, 22, 10, 0),
                                tc_mktime(2025, 10, 22, 10, 30));

  expect(list->tail == e3, "tail should point to last node");

  remove_event(list, e2->id);
  expect(find_event_by_id(list, e2->id) == NULL,
         "removed node should not be found by id anymore");
  expect(list->head != NULL && list->head->next != NULL,
         "list should still have two nodes after removing middle");
  expect(list->head->next == e3,
         "after removing middle, first node's next should point to last");
  expect(e3->parent == list->head,
         "after removing middle, last node's parent should point to first");
  expect(list->tail == e3, "tail should still point to last node");
  destroy_event_list(list);
}

// 6) find_event_by_id returns correct pointer
static void test_find_event_by_id_finds_correct(void) {
  EventList *list = create_event_list();

  Event *e1 = add_event_to_list(list, "A", "", tc_mktime(2025, 10, 22, 9, 0),
                                tc_mktime(2025, 10, 22, 10, 0));
  Event *e2 = add_event_to_list(list, "B", "", tc_mktime(2025, 10, 22, 11, 0),
                                tc_mktime(2025, 10, 22, 12, 0));

  Event *f1 = find_event_by_id(list, e1->id);
  Event *f2 = find_event_by_id(list, e2->id);
  expect(f1 == e1, "find_event_by_id should return pointer to first event");
  expect(f2 == e2, "find_event_by_id should return pointer to second event");
  expect(find_event_by_id(list, 99999) == NULL,
         "find_event_by_id should return NULL for missing id");
  destroy_event_list(list);
}

// 7) save_events writes to file and load_events reconstructs list
static void test_save_and_load_events_roundtrip(void) {
  const char *fname = "cal_test_tmp.txt";
  EventList *list = create_event_list();
  Event *a =
      add_event_to_list(list, "A", "alpha", tc_mktime(2025, 10, 22, 9, 0),
                        tc_mktime(2025, 10, 22, 10, 0));
  Event *b =
      add_event_to_list(list, "B", "beta", tc_mktime(2025, 10, 22, 11, 0),
                        tc_mktime(2025, 10, 22, 12, 0));
  save_events(list, fname);

  EventList *loaded = create_event_list();
  load_events(loaded, fname);

  // After loading, next_id should be > highest id
  expect(loaded->next_id > 0, "loaded list should have next_id set");
  Event *l1 = find_event_by_id(loaded, a->id);
  Event *l2 = find_event_by_id(loaded, b->id);
  expect(l1 != NULL && l2 != NULL,
         "loaded list should contain two events with ids 1 and 2");
  if (l1) {
    expect_eq(0, strcmp(l1->title, "A"), "first loaded event title matches");
    expect_eq(0, strcmp(l1->description, "alpha"),
              "first loaded event description matches");
    expect_eq(l1->start_time, tc_mktime(2025, 10, 22, 9, 0),
              "first loaded event start time matches");
    expect_eq(l1->end_time, tc_mktime(2025, 10, 22, 10, 0),
              "first loaded event end time matches");
  }
  if (l2) {
    expect_eq(0, strcmp(l2->title, "B"), "second loaded event title matches");
    expect_eq(0, strcmp(l2->description, "beta"),
              "second loaded event description matches");
    expect_eq(l2->start_time, tc_mktime(2025, 10, 22, 11, 0),
              "second loaded event start time matches");
    expect_eq(l2->end_time, tc_mktime(2025, 10, 22, 12, 0),
              "second loaded event end time matches");
  }
  // cleanup
  destroy_event_list(list);
  destroy_event_list(loaded);
  remove(fname);
}

// Aggregate runner
static inline void run_event_list_tests(void) {
  puts("Running event list tests...");
  test_create_event_list_initial_state();
  test_create_event_sets_fields();
  test_add_event_assigns_ids_and_orders();
  test_remove_event_removes_head();
  test_remove_event_middle_node();
  test_find_event_by_id_finds_correct();
  test_save_and_load_events_roundtrip();
  puts("Event list tests completed.");
}

#endif // TEST_EVENT_LIST_H
