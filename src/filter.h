#ifndef FILTER_H
#define FILTER_H

#include "calendar.h"
#include <stdbool.h>
#include <time.h>

typedef enum {
  FILTER_DAY_OF_WEEK,
  FILTER_AFTER_DATETIME,
  FILTER_BEFORE_DATETIME,
  FILTER_BEFORE_TIME, // time of day
  FILTER_AFTER_TIME,
  FILTER_MIN_DISTANCE,
  FILTER_HOLIDAY,
  FILTER_AND,
  FILTER_OR,
  FILTER_NOT,
  FILTER_NONE
} FilterType;

typedef struct Filter {
  FilterType type;
  union {
    int day_of_week;
    time_t time_value;
    int minutes;
    struct {
      struct Filter *left;
      struct Filter *right;
    } logical;
    struct Filter *operand;
  } data;
} Filter;

// combinators

// Creates a basic filter of the specified type
Filter *make_filter(FilterType type);
// Combines two filters with a logical OR
Filter *or_filter(Filter *left, Filter *right);
// Combines two filters with a logical AND
Filter *and_filter(Filter *left, Filter *right);
// Negates a filter
Filter *not_filter(Filter *operand);

// main functions

// Parses a filter string into a Filter structure
Filter *parse_filter(const char *filter_str);

// Evaluates whether a candidate time satisfies the filter conditions
bool evaluate_filter(Filter *filter, time_t candidate, const Calendar *calendar);

// Returns minutes to skip to reach a valid time according to the filter, or an underestimate thereof.
//
// Returns 0 if candidate is valid now.
// Returns -1 if no valid time can be found.
int get_next_valid_minutes(const Filter *filter, const time_t candidate, const Calendar *calendar);

// Finds the earliest time slot that fits the duration and satisfies the filter
time_t find_optimal_time(const Calendar *calendar, const int duration_minutes, const Filter *filter);

// Frees a Filter structure and its sub-filters
void destroy_filter(Filter *filter);

#endif // FILTER_H
