#include "filter.h"
#include "calendar.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
  char month;
  char day;
} month_day;

static const month_day holidays[] = {
    {1, 1},   // New Year's Day
    {7, 4},   // Independence Day
    {12, 25}, // Christmas Day
    {12, 31}, // New Year's Eve
};
const size_t num_holidays = sizeof(holidays) / sizeof(holidays[0]);

static int minutes_until_day_of_week(const time_t t, const int target_day) {
  struct tm *tm_time = localtime(&t);
  int current_day = tm_time->tm_wday;
  if (current_day == target_day) {
    return 0;
  }
  int days_ahead = (target_day - current_day + 7) % 7;
  if (days_ahead == 0)
    days_ahead = 7;
  int minutes_today = tm_time->tm_hour * 60 + tm_time->tm_min;
  return days_ahead * 1440 - minutes_today;
}

// Returns the minutes until the next holiday from time t, or an underestimate
// thereof.
static unsigned days_until_holiday(const struct tm *time) {
  // Return 0 if today is a holiday
  unsigned month = time->tm_mon + 1;
  unsigned min_dist = -1;
  for (size_t i = 0; i < num_holidays; ++i) {
    if (time->tm_mon + 1 == holidays[i].month &&
        time->tm_mday == holidays[i].day) {
      return 0;
    }
    unsigned dist = 0;
    int month_diff = holidays[i].month - month;
    if (month_diff < 0) {
      month_diff += 12;
    }
    while (month_diff) {
      dist += days_in_month(month + month_diff, time->tm_year + 1900);
      month_diff--;
    }
    dist += holidays[i].day - time->tm_mday;
    if (min_dist == -1 || dist < min_dist) {
      min_dist = dist;
    }
  }
  return (min_dist < 0) ? 0 : min_dist;
}

// Helper to find minutes until candidate is at least min_minutes
// away from any event boundary (start or end).
// Negative minutes allowed to permit overlaps.
static unsigned minutes_until_min_distance(const time_t candidate,
                                           const int min_minutes,
                                           const Calendar *calendar) {
  if (!calendar || !calendar->event_list || !calendar->event_list->head) {
    return 0;
  }
  EventList *list = calendar->event_list;
  time_t guess = candidate;
  const time_t pad = (time_t)min_minutes * 60;

  for (Event *current = list->head; current; current = current->next) {
    const time_t s = current->start_time;
    const time_t e = current->end_time;
    // If we're at least pad before this event's start, we're done.
    if (guess + pad <= s) {
      break;
    }
    // Too close to (or inside) this event; move to just after it with
    // padding.
    if (guess < e + pad) {
      guess = e + pad;
    }
  }
  if (guess <= candidate)
    return 0;
  // Round up to full minutes.
  time_t delta = guess - candidate;
  return (unsigned)((delta + 59) / 60);
}

// Helper to create a time_t for today with the time from a filter.
static time_t get_time_on_date(const struct tm *date_tm, time_t time_val) {
  struct tm temp_tm = *date_tm;
  struct tm *time_tm = localtime(&time_val);
  temp_tm.tm_hour = time_tm->tm_hour;
  temp_tm.tm_min = time_tm->tm_min;
  temp_tm.tm_sec = time_tm->tm_sec;
  temp_tm.tm_isdst = -1; // Let mktime figure out DST
  return mktime(&temp_tm);
}

// Helper to calculate minutes until midnight.
static int minutes_until_midnight(const struct tm *tm_time) {
  return 1440 - (tm_time->tm_hour * 60 + tm_time->tm_min);
}

// Helper to calculate minutes between two times.
static int minutes_between(time_t future, time_t past) {
  if (future <= past) {
    return 0;
  }
  return (int)(difftime(future, past) / 60);
}

static int get_next_invalid_minutes(const Filter *filter, const time_t candidate,
                             const Calendar *calendar) {
  if (!filter || filter->type == FILTER_NONE) {
    return -1; // Always valid, never invalid.
  }

  // A time is invalid if it's not valid.
  // If it's already valid (next valid is 0), we need to find when it
  // becomes invalid. Otherwise, it's already invalid (next valid > 0).
  if (get_next_valid_minutes(filter, candidate, calendar) > 0) {
    return 0; // Already invalid.
  }

  // The candidate time is currently valid. Find when it becomes invalid.
  struct tm *tm_candidate = localtime(&candidate);

  switch (filter->type) {
  case FILTER_DAY_OF_WEEK:
  case FILTER_HOLIDAY:
    // Valid today, becomes invalid at midnight.
    return minutes_until_midnight(tm_candidate);

  case FILTER_AFTER_DATETIME:
    return -1; // Valid now, will be valid forever.

  case FILTER_BEFORE_DATETIME:
    // Valid now, becomes invalid at the filter's time.
    return minutes_between(filter->data.time_value, candidate);

  case FILTER_AFTER_TIME: {
    // Valid now. Becomes invalid at the filter time tomorrow.
    time_t limit_today =
        get_time_on_date(tm_candidate, filter->data.time_value);
    struct tm tm_tomorrow = *localtime(&limit_today);
    tm_tomorrow.tm_mday += 1;
    tm_tomorrow.tm_isdst = -1;
    time_t limit_tomorrow = mktime(&tm_tomorrow);
    return minutes_between(limit_tomorrow, candidate);
  }

  case FILTER_BEFORE_TIME: {
    // Valid now. Becomes invalid at the filter time today.
    time_t limit_today =
        get_time_on_date(tm_candidate, filter->data.time_value);
    return minutes_between(limit_today, candidate);
  }

  case FILTER_MIN_DISTANCE:
    // This filter is about finding valid slots, not continuous validity.
    // We treat it as always valid from an "invalid" perspective.
    return -1;

  case FILTER_AND: {
    int left_dist = get_next_invalid_minutes(filter->data.logical.left,
                                             candidate, calendar);
    int right_dist = get_next_invalid_minutes(filter->data.logical.right,
                                              candidate, calendar);
    // Becomes invalid as soon as either sub-filter becomes invalid.
    if (left_dist < 0) return right_dist;
    if (right_dist < 0) return left_dist;
    return left_dist < right_dist ? left_dist : right_dist; // min
  }

  case FILTER_OR: {
    int left_dist = get_next_invalid_minutes(filter->data.logical.left,
                                             candidate, calendar);
    int right_dist = get_next_invalid_minutes(filter->data.logical.right,
                                              candidate, calendar);
    // Becomes invalid only when both sub-filters become invalid.
    if (left_dist < 0 || right_dist < 0) return -1; // One is always valid.
    return left_dist > right_dist ? left_dist : right_dist; // max
  }

  case FILTER_NOT:
    // Becomes invalid when the operand becomes valid.
    return get_next_valid_minutes(filter->data.operand, candidate, calendar);

  default:
    return -1; // Should not happen.
  }
}

int get_next_valid_minutes(const Filter *filter, const time_t candidate,
                           const Calendar *calendar) {
  if (!filter || filter->type == FILTER_NONE) {
    return 0;
  }

  struct tm *tm_candidate = localtime(&candidate);

  switch (filter->type) {
  case FILTER_DAY_OF_WEEK:
    if (tm_candidate->tm_wday == filter->data.day_of_week) return 0;
    return minutes_until_day_of_week(candidate, filter->data.day_of_week);

  case FILTER_HOLIDAY: {
    unsigned days = days_until_holiday(tm_candidate);
    if (days == 0) return 0;
    return (days - 1) * 1440 + minutes_until_midnight(tm_candidate);
  }

  case FILTER_AFTER_DATETIME:
    if (candidate > filter->data.time_value) return 0;
    return minutes_between(filter->data.time_value, candidate) + 1;

  case FILTER_BEFORE_DATETIME:
    return (candidate < filter->data.time_value) ? 0 : -1;

  case FILTER_AFTER_TIME: {
    time_t limit_today =
        get_time_on_date(tm_candidate, filter->data.time_value);
    if (candidate >= limit_today) return 0;
    return minutes_between(limit_today, candidate) + 1;
  }

  case FILTER_BEFORE_TIME: {
    time_t limit_today =
        get_time_on_date(tm_candidate, filter->data.time_value);
    if (candidate < limit_today) return 0;
    // Past the time today. Next valid time is start of next day.
    struct tm tm_midnight = *tm_candidate;
    tm_midnight.tm_hour = 0;
    tm_midnight.tm_min = 0;
    tm_midnight.tm_sec = 0;
    tm_midnight.tm_mday += 1;
    tm_midnight.tm_isdst = -1;
    time_t next_midnight = mktime(&tm_midnight);
    return minutes_between(next_midnight, candidate);
  }

  case FILTER_MIN_DISTANCE:
    return minutes_until_min_distance(candidate, filter->data.minutes,
                                      calendar);

  case FILTER_AND: {
    int left_dist =
        get_next_valid_minutes(filter->data.logical.left, candidate, calendar);
    int right_dist =
        get_next_valid_minutes(filter->data.logical.right, candidate, calendar);

    if (left_dist < 0 || right_dist < 0) return -1; // Not possible if one part is never valid.
    // To be valid, both must be valid. Wait for the later of the two.
    return left_dist > right_dist ? left_dist : right_dist; // max
  }

  case FILTER_OR: {
    int left_dist =
        get_next_valid_minutes(filter->data.logical.left, candidate, calendar);
    int right_dist =
        get_next_valid_minutes(filter->data.logical.right, candidate, calendar);

    if (left_dist < 0 && right_dist < 0) return -1;
    if (left_dist < 0) return right_dist;
    if (right_dist < 0) return left_dist;
    // To be valid, either can be valid. Wait for the sooner of the two.
    return left_dist < right_dist ? left_dist : right_dist; // min
  }

  case FILTER_NOT:
    // Becomes valid when the operand becomes invalid.
    return get_next_invalid_minutes(filter->data.operand, candidate, calendar);

  default:
    return 0; // Should not happen, assume valid.
  }
}

bool evaluate_filter(Filter *filter, time_t candidate,
                     const Calendar *calendar) {
  return get_next_valid_minutes(filter, candidate, calendar) == 0;
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

time_t find_optimal_time(const Calendar *calendar, const int duration_minutes,
                         const Filter *filter) {

  time_t now = time(NULL);
  if (!filter) {
    return now; // No filter means now is valid
  }
  time_t candidate = now;
  int duration_seconds = duration_minutes * 60;
  int max_iterations = 365 * 24 * 60 / 15;
  int iterations = 0;

  while (iterations < max_iterations) {
    iterations++;
    int skip_minutes = get_next_valid_minutes(filter, candidate, calendar);
    if (skip_minutes < 0) {
      return -1; // No valid time found within filter constraints
    }
    if (skip_minutes > 0) {
      candidate += skip_minutes * 60;
      continue;
    }
    // Now is a valid time
    return candidate;
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
