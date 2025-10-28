#ifndef FILTER_H
#define FILTER_H

#include "calendar.h"
#include <stdbool.h>
#include <time.h>

typedef enum {
  FILTER_DAY_OF_WEEK,
  FILTER_AFTER_TIME,
  FILTER_BEFORE_TIME,
  FILTER_MIN_DISTANCE,
  FILTER_NOT_HOLIDAY,
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

Filter *parse_filter(const char *filter_str);
bool evaluate_filter(Filter *filter, time_t candidate, EventList *list);
int get_next_valid_minutes(Filter *filter, time_t candidate, EventList *list);
time_t find_optimal_time(EventList *list, int duration_minutes, Filter *filter);
void destroy_filter(Filter *filter);

#endif // FILTER_H
