#include "../src/parser.c"
#include <stdio.h>
#include <string.h>

void expect(bool condition, const char *message);
void expect_eq(int a, int b, const char *message);

inline static void expect_day_filter(Filter *filter, int expected_day) {
  expect(filter != NULL, "Filter should not be NULL");
  expect(filter->type == FILTER_DAY_OF_WEEK,
         "Filter type should be DAY_OF_WEEK");
  expect_eq(filter->data.day_of_week, expected_day,
            "Day of week should match expected");
}

static void test_parse_day(const char *input, const int expected) {
  Filter *filter = parse_filter(input);
  expect_day_filter(filter, expected);
  destroy_filter(filter);
}

static void test_parse_weekdays() {
  // Test each weekday
  test_parse_day("on sunday", 0);
  test_parse_day("on monday", 1);
  test_parse_day("on tuesday", 2);
  test_parse_day("on wednesday", 3);
  test_parse_day("on thursday", 4);
  test_parse_day("on friday", 5);
  test_parse_day("on saturday", 6);

  // Test day list
  const char *input = "on monday, wednesday, friday";
  Filter *filter = parse_filter(input);
  expect(filter != NULL, "Filter should not be NULL");
  expect(filter->type == FILTER_OR, "Filter type should be OR");
  // Left should be Monday, Wednesday, Right should be Friday

  expect(filter->data.logical.left->type == FILTER_OR,
         "Left operand should be OR");
  expect_day_filter(filter->data.logical.left->data.logical.left, 1); // Monday
  expect_day_filter(filter->data.logical.left->data.logical.right,
                    3); // Wednesday

  expect_day_filter(filter->data.logical.right, 5); // Friday
  destroy_filter(filter);
}

static void test_parse_unary() {
  const char *input = "not weekdays";
  Filter *filter = parse_filter(input);
  expect(filter != NULL, "Filter should not be NULL");
  expect(filter->type == FILTER_NOT, "Filter type should be NOT");
  expect(filter->data.operand != NULL, "Operand should not be NULL");
  expect(filter->data.operand->type == FILTER_OR, "Operand type should be OR");

  int count = 0;
  Filter *current = filter->data.operand;
  while (current) {
    if (current->type == FILTER_DAY_OF_WEEK) {
      count++;
      current = NULL; // No more ORs
    } else if (current->type == FILTER_OR) {
      count++;
      current = current->data.logical.left;
    } else {
      break;
    }
  }
  expect_eq(count, 5, "There should be 5 weekdays in the OR filter");
  destroy_filter(filter);
}

static void test_invalid_filter() {
  const char *input = "foobar";
  Filter *filter = parse_filter(input);
  expect(filter != NULL, "Filter should not be NULL for invalid input");
  expect(filter->type == FILTER_NONE,
         "Filter type should be NONE for invalid input");
  destroy_filter(filter);
}

static void test_or_parsing() {
  const char *input = "on monday or on wednesday";
  Filter *filter = parse_filter(input);
  expect(filter != NULL, "Filter should not be NULL");
  expect(filter->type == FILTER_OR, "Filter type should be OR");
  expect_day_filter(filter->data.logical.left, 1);  // Monday
  expect_day_filter(filter->data.logical.right, 3); // Wednesday
  destroy_filter(filter);
}

static void test_and_parsing() {
  const char *input = "on tuesday and not on friday";
  Filter *filter = parse_filter(input);
  expect(filter != NULL, "Filter should not be NULL");
  expect(filter->type == FILTER_AND, "Filter type should be AND");
  expect_day_filter(filter->data.logical.left, 2); // Tuesday
  expect(filter->data.logical.right->type == FILTER_NOT,
         "Right operand should be NOT");
  expect_day_filter(filter->data.logical.right->data.operand, 5); // Friday
  destroy_filter(filter);
}

static void test_grouped_parsing() {
  const char *input = "not (on saturday or on sunday)";
  Filter *filter = parse_filter(input);
  expect(filter != NULL, "Filter should not be NULL");
  expect(filter->type == FILTER_NOT, "Filter type should be NOT");
  Filter *inside = filter->data.operand;
  expect(inside != NULL, "Inside filter should not be NULL");
  expect(inside->type == FILTER_OR, "Inside filter type should be OR");
  expect_day_filter(inside->data.logical.left, 6);  // Saturday
  expect_day_filter(inside->data.logical.right, 0); // Sunday
  destroy_filter(filter);
}

static void test_parse_holiday() {
  const char *input = "holidays";
  Filter *filter = parse_filter(input);
  expect(filter != NULL, "Filter should not be NULL");
  expect(filter->type == FILTER_HOLIDAY, "Filter type should be HOLIDAY");
  destroy_filter(filter);
}

static inline void run_parse_tests() {
  puts("Running parser tests...");
  test_parse_weekdays();
  test_parse_unary();
  test_invalid_filter();
  test_or_parsing();
  test_and_parsing();
  test_grouped_parsing();
  test_parse_holiday();
}
