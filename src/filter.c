#include "filter.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
  char month;
  char day;
} Holiday;

static const Holiday holidays[] = {
    {1, 1},   // New Year's Day
    {7, 4},   // Independence Day
    {12, 25}, // Christmas Day
};
const size_t num_holidays = sizeof(holidays) / sizeof(holidays[0]);

static bool is_holiday(time_t t) {
  struct tm *tm_time = localtime(&t);
  for (size_t i = 0; i < num_holidays; i++) {
    if (tm_time->tm_mon + 1 == holidays[i].month &&
        tm_time->tm_mday == holidays[i].day) {
      return true;
    }
  }
  return false;
}

static int minutes_until_day_of_week(time_t t, int target_day) {
  struct tm *tm_time = localtime(&t);
  int current_day = tm_time->tm_wday;

  if (current_day == target_day) {
    return 0;
  }

  int days_ahead = (target_day - current_day + 7) % 7;
  if (days_ahead == 0)
    days_ahead = 7;

  int minutes_today = tm_time->tm_hour * 60 + tm_time->tm_min;
  return (days_ahead - 1) * 1440 + (1440 - minutes_today);
}

static int minutes_until_next_non_holiday(time_t t) {
  if (!is_holiday(t))
    return 0;

  time_t candidate = t + 86400;
  for (int days = 1; days <= 7; days++) {
    if (!is_holiday(candidate)) {
      return days * 1440;
    }
    candidate += 86400;
  }
  return 1440;
}

// Helper to find minutes until current is outside the limit time
static int minutes_until_outside_range(time_t current, time_t limit) {
  if (current > limit)
    return 0;
  return (int)(difftime(limit, current) / 60) + 1;
}

// Helper to find minutes until candidate is at least min_minutes
// away from any event boundary.
// Negative minutes allowed to permit overlaps.
static int minutes_until_min_distance(time_t candidate, int min_minutes,
                                      EventList *list) {
  if (!list || !list->head)
    return 0;
  int min_seconds = min_minutes * 60;
  Event *current = list->head;
  int max_skip = 0;
  while (current) {
    for (int i = 0; i < 2; i++) {
      time_t boundary = i == 0 ? current->start_time : current->end_time;
      if (candidate >= boundary - min_seconds &&
          candidate < boundary + min_seconds) {
        int skip = (int)(difftime(boundary + min_seconds, candidate) / 60);
        if (skip > max_skip)
          max_skip = skip;
      }
    }
    current = current->next;
  }
  return max_skip;
}

int get_next_valid_minutes(Filter *filter, time_t candidate, EventList *list) {
  if (!filter || filter->type == FILTER_NONE)
    return 0;

  struct tm *tm_candidate = localtime(&candidate);

  switch (filter->type) {
  case FILTER_DAY_OF_WEEK: {
    if (tm_candidate->tm_wday == filter->data.day_of_week) {
      return 0;
    }
    return minutes_until_day_of_week(candidate, filter->data.day_of_week);
  }

  case FILTER_AFTER_TIME:
    return minutes_until_outside_range(candidate, filter->data.time_value);

  case FILTER_BEFORE_TIME: {
    if (candidate < filter->data.time_value) {
      return 0;
    }
    return -1; // No valid time
  }

  case FILTER_MIN_DISTANCE:
    return minutes_until_min_distance(candidate, filter->data.minutes, list);

  case FILTER_NOT_HOLIDAY:
    return minutes_until_next_non_holiday(candidate);

  case FILTER_AND: {
    int left_dist =
        get_next_valid_minutes(filter->data.logical.left, candidate, list);
    int right_dist =
        get_next_valid_minutes(filter->data.logical.right, candidate, list);

    if (left_dist < 0 || right_dist < 0)
      return -1;

    if (left_dist == 0 && right_dist == 0)
      return 0;

    return left_dist > right_dist ? left_dist : right_dist;
  }

  case FILTER_OR: {
    int left_dist =
        get_next_valid_minutes(filter->data.logical.left, candidate, list);
    int right_dist =
        get_next_valid_minutes(filter->data.logical.right, candidate, list);

    if (left_dist < 0 && right_dist < 0)
      return -1;
    if (left_dist < 0)
      return right_dist;
    if (right_dist < 0)
      return left_dist;

    if (left_dist == 0 || right_dist == 0)
      return 0;

    return left_dist < right_dist ? left_dist : right_dist;
  }

  case FILTER_NOT: {
    if (!evaluate_filter(filter->data.operand, candidate, list)) {
      return 0;
    }

    int sub_dist =
        get_next_valid_minutes(filter->data.operand, candidate, list);
    if (sub_dist <= 0) {
      struct tm tm_next = *tm_candidate;

      if (filter->data.operand->type == FILTER_DAY_OF_WEEK) {
        tm_next.tm_mday += 1;
        mktime(&tm_next);
        return (int)(difftime(mktime(&tm_next), candidate) / 60);
      }

      return 1;
    }

    return 1;
  }

  default:
    return 0;
  }
}

bool evaluate_filter(Filter *filter, time_t candidate, EventList *list) {
  return get_next_valid_minutes(filter, candidate, list) == 0;
}

Filter *make_filter(FilterType type) {
  Filter *f = malloc(sizeof(Filter));
  f->type = type;
  return f;
}

// unsafe: assume type is logical
static Filter *combine(Filter *left, Filter *right, FilterType type) {
  Filter *f = malloc(sizeof(Filter));
  f->type = type;
  f->data.logical.left = left;
  f->data.logical.right = right;
  return f;
}

Filter *or_filter(Filter *left, Filter *right) {
  return combine(left, right, FILTER_OR);
}

Filter *and_filter(Filter *left, Filter *right) {
  return combine(left, right, FILTER_AND);
}

Filter *not_filter(Filter *operand) {
  Filter *f = make_filter(FILTER_NOT);
  f->data.operand = operand;
  return f;
}

Filter *parse_filter(const char *filter_str) {
  if (!filter_str || strlen(filter_str) == 0) {
    return make_filter(FILTER_NONE);
  }

  if (strstr(filter_str, "business_days")) {
    Filter *prev = NULL;

    for (int i = 1; i <= 5; i++) {
      Filter *day_filter = make_filter(FILTER_DAY_OF_WEEK);
      day_filter->data.day_of_week = i;
      if (!prev) {
        prev = day_filter;
      } else {
        prev = or_filter(prev, day_filter);
      }
    }
    return and_filter(prev, make_filter(FILTER_NOT_HOLIDAY));
  }

  if (strstr(filter_str, "avoid_friday")) {
    Filter *f = make_filter(FILTER_DAY_OF_WEEK);
    f->data.day_of_week = 5;
    return not_filter(f);
  }

  if (strstr(filter_str, "avoid_weekend")) {
    Filter *sat_filter = make_filter(FILTER_DAY_OF_WEEK);
    sat_filter->data.day_of_week = 6;
    Filter *sun_filter = make_filter(FILTER_DAY_OF_WEEK);
    sun_filter->data.day_of_week = 0;
    Filter *weekend_filter = or_filter(sat_filter, sun_filter);
    return not_filter(weekend_filter);
  }
  return make_filter(FILTER_NONE);
}

time_t find_optimal_time(EventList *list, int duration_minutes,
                         Filter *filter) {
  time_t now = time(NULL);
  time_t candidate = now;
  int duration_seconds = duration_minutes * 60;
  int max_iterations = 365 * 24 * 60 / 15;
  int iterations = 0;

  while (iterations < max_iterations) {
    iterations++;

    if (filter) {
      int skip_minutes = get_next_valid_minutes(filter, candidate, list);

      if (skip_minutes < 0) {
        return -1; // No valid time found within filter constraints
      }

      if (skip_minutes > 0) {
        candidate += skip_minutes * 60;
        continue;
      }
    }

    bool conflict = false;
    Event *current = list->head;
    int next_event_minutes = -1;

    while (current) {
      if ((candidate >= current->start_time && candidate < current->end_time) ||
          (candidate + duration_seconds > current->start_time &&
           candidate + duration_seconds <= current->end_time) ||
          (candidate <= current->start_time &&
           candidate + duration_seconds >= current->end_time)) {
        conflict = true;

        int minutes_to_end =
            (int)(difftime(current->end_time, candidate) / 60) + 1;
        if (next_event_minutes < 0 || minutes_to_end < next_event_minutes) {
          next_event_minutes = minutes_to_end;
        }
      }
      current = current->next;
    }

    if (!conflict) {
      return candidate;
    }

    if (next_event_minutes > 0) {
      candidate += next_event_minutes * 60;
    } else {
      candidate += 900; // 15 minutes
    }
  }

  return -1;
}

void destroy_filter(Filter *filter) {
  if (!filter)
    return;

  if (filter->type == FILTER_AND || filter->type == FILTER_OR) {
    destroy_filter(filter->data.logical.left);
    destroy_filter(filter->data.logical.right);
  } else if (filter->type == FILTER_NOT) {
    destroy_filter(filter->data.operand);
  }

  free(filter);
}
