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

static time_t until_day_of_week(const time_t t, const int target_day) {
  struct tm *tm_time = localtime(&t);
  int current_day = tm_time->tm_wday;
  if (current_day == target_day) {
    return 0;
  }
  time_t days_ahead = (target_day - current_day + 7) % 7;
  if (days_ahead == 0)
    days_ahead = 7;
  time_t today =
      tm_time->tm_hour * 60 * 60 + tm_time->tm_min * 60 + tm_time->tm_sec;
  return days_ahead * 1440 * 60 - today;
}

// Returns the time in seconds until the next holiday from time t.
static time_t until_holiday(const struct tm *time) {
  // Today is a holiday
  for (size_t i = 0; i < num_holidays; ++i) {
    if (time->tm_mon + 1 == holidays[i].month &&
        time->tm_mday == holidays[i].day) {
      return 0;
    }
  }

  // Build a time_t for "now"
  struct tm now_tm = *time;
  now_tm.tm_isdst = -1;
  time_t now_ts = mktime(&now_tm);

  // Find the next holiday in the current year at 00:00:00
  time_t best_diff = (time_t)-1;
  for (size_t i = 0; i < num_holidays; ++i) {
    struct tm target = *time;
    target.tm_year = time->tm_year; // same year
    target.tm_mon = holidays[i].month - 1;
    target.tm_mday = holidays[i].day;
    target.tm_hour = 0;
    target.tm_min = 0;
    target.tm_sec = 0;
    target.tm_isdst = -1;
    time_t target_ts = mktime(&target);
    double diff = difftime(target_ts, now_ts);
    if (diff >= 0) {
      if (best_diff == (time_t)-1 || (time_t)diff < best_diff) {
        best_diff = (time_t)diff;
      }
    }
  }

  // If none in current year, take the earliest in the next year
  if (best_diff == (time_t)-1) {
    time_t next_best = (time_t)-1;
    for (size_t i = 0; i < num_holidays; ++i) {
      struct tm target = *time;
      target.tm_year = time->tm_year + 1; // next year
      target.tm_mon = holidays[i].month - 1;
      target.tm_mday = holidays[i].day;
      target.tm_hour = 0;
      target.tm_min = 0;
      target.tm_sec = 0;
      target.tm_isdst = -1;
      time_t target_ts = mktime(&target);
      double diff = difftime(target_ts, now_ts);
      if (diff >= 0) {
        if (next_best == (time_t)-1 || (time_t)diff < next_best) {
          next_best = (time_t)diff;
        }
      }
    }
    return next_best == (time_t)-1 ? 0 : next_best;
  }

  return best_diff;
}

// Helper to find time until candidate is at least dist
// away from any event boundary (start or end).
// Negative distance allowed to permit overlaps.
static time_t time_til_distance(const time_t start, const time_t duration,
                                const time_t dist, const Calendar *calendar) {
  if (!calendar || !calendar->event_list || !calendar->event_list->head ||
      dist <= 0) {
    return 0;
  }
  EventList *list = calendar->event_list;
  time_t guess = start;
  const time_t pad = dist * 60; // minutes -> seconds

  for (Event *current = list->head; current; current = current->next) {
    const time_t s = current->start_time;
    const time_t e = current->end_time;
    // If we're at least pad before this event's start, we're done.
    if (guess + duration + pad <= s) {
      break;
    }
    // Too close to (or inside) this event; move to just after it with
    // padding.
    if (guess < e + pad) {
      guess = e + pad;
    }
  }
  if (guess <= start)
    return 0;

  return guess - start;
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

// Helper to calculate time until midnight.
static time_t until_midnight(const struct tm *tm_time) {
  return 1440 * 60 -
         (tm_time->tm_hour * 60 * 60 + tm_time->tm_min * 60 + tm_time->tm_sec);
}

static time_t until_invalid(const Filter *filter, const time_t candidate,
                            const time_t duration, const Calendar *calendar) {
  if (!filter || filter->type == FILTER_NONE) {
    return -1; // Always valid, never invalid.
  }

  // A time is invalid if it's not valid.
  // If it's already valid (next valid is 0), we need to find when it
  // becomes invalid. Otherwise, it's already invalid (next valid > 0).
  if (until_valid(filter, candidate, duration, calendar) > 0) {
    return 0; // Already invalid.
  }

  // The candidate time is currently valid. Find when it becomes invalid.
  struct tm *tm_candidate = localtime(&candidate);

  switch (filter->type) {
  case FILTER_DAY_OF_WEEK:
  case FILTER_HOLIDAY:
    // Valid today, becomes invalid at midnight.
    return until_midnight(tm_candidate);

  case FILTER_AFTER_DATETIME:
    return -1; // Valid now, will be valid forever.

  case FILTER_BEFORE_DATETIME:
    // Valid now, becomes invalid at the filter's time.
    return difftime(filter->data.time_value, candidate);

  case FILTER_AFTER_TIME: {
    // Valid now. Becomes invalid at the filter time tomorrow.
    time_t limit_today =
        get_time_on_date(tm_candidate, filter->data.time_value);
    struct tm tm_tomorrow = *localtime(&limit_today);
    tm_tomorrow.tm_mday += 1;
    tm_tomorrow.tm_isdst = -1;
    time_t limit_tomorrow = mktime(&tm_tomorrow);
    return difftime(limit_tomorrow, candidate);
  }

  case FILTER_BEFORE_TIME: {
    // Valid now. Becomes invalid at the filter time today.
    time_t limit_today =
        get_time_on_date(tm_candidate, filter->data.time_value);
    return difftime(limit_today, candidate);
  }

  case FILTER_MIN_DISTANCE:
    // This filter is about finding valid slots, not continuous validity.
    // We treat it as always valid from an "invalid" perspective.
    return -1;

  case FILTER_AND: {
    int left_dist =
        until_invalid(filter->data.logical.left, candidate, duration, calendar);
    int right_dist = until_invalid(filter->data.logical.right, candidate,
                                   duration, calendar);
    if (left_dist < 0)
      return right_dist;
    if (right_dist < 0)
      return left_dist;
    return left_dist < right_dist ? left_dist : right_dist;
  }

  case FILTER_OR: {
    int left_dist =
        until_invalid(filter->data.logical.left, candidate, duration, calendar);
    int right_dist = until_invalid(filter->data.logical.right, candidate,
                                   duration, calendar);
    if (left_dist < 0 || right_dist < 0)
      return -1;
    return left_dist > right_dist ? left_dist : right_dist;
  }

  case FILTER_NOT:
    return until_valid(filter->data.operand, candidate, duration, calendar);

  default:
    return -1; // Should not happen.
  }
}

time_t until_valid(const Filter *filter, const time_t candidate,
                   const time_t duration, const Calendar *calendar) {
  if (!filter || filter->type == FILTER_NONE) {
    return 0;
  }

  struct tm *cand_tm = localtime(&candidate);
  if (!cand_tm) {
    return -1; // Invalid time
  }
  struct tm tm_candidate = *cand_tm;

  switch (filter->type) {
  case FILTER_DAY_OF_WEEK:
    if (tm_candidate.tm_wday == filter->data.day_of_week)
      return 0;
    return until_day_of_week(candidate, filter->data.day_of_week);

  case FILTER_HOLIDAY:
    return until_holiday(&tm_candidate);

  case FILTER_AFTER_DATETIME:
    if (candidate > filter->data.time_value)
      return 0;
    return difftime(filter->data.time_value, candidate) + 1;

  case FILTER_BEFORE_DATETIME:
    return (candidate < filter->data.time_value) ? 0 : -1;

  case FILTER_AFTER_TIME: {
    time_t limit_today =
        get_time_on_date(&tm_candidate, filter->data.time_value);
    if (candidate >= limit_today)
      return 0;
    return difftime(limit_today, candidate) + 1;
  }

  case FILTER_BEFORE_TIME: {
    time_t limit_today =
        get_time_on_date(&tm_candidate, filter->data.time_value);
    if (candidate < limit_today)
      return 0;
    // Past the time today. Next valid time is start of next day.
    return until_midnight(&tm_candidate);
  }

  case FILTER_MIN_DISTANCE:
    return time_til_distance(candidate, duration, filter->data.minutes,
                             calendar);

  case FILTER_AND: {
    time_t left_dist =
        until_valid(filter->data.logical.left, candidate, duration, calendar);
    time_t right_dist =
        until_valid(filter->data.logical.right, candidate, duration, calendar);
    if (left_dist < 0 || right_dist < 0)
      return -1;
    return left_dist > right_dist ? left_dist : right_dist;
  }

  case FILTER_OR: {
    time_t left_dist =
        until_valid(filter->data.logical.left, candidate, duration, calendar);
    time_t right_dist =
        until_valid(filter->data.logical.right, candidate, duration, calendar);
    if (left_dist < 0 && right_dist < 0)
      return -1;
    if (left_dist < 0)
      return right_dist;
    if (right_dist < 0)
      return left_dist;
    return left_dist < right_dist ? left_dist : right_dist;
  }

  case FILTER_NOT: {
    // NOT is valid when the operand is invalid now.
    time_t op_valid =
        until_valid(filter->data.operand, candidate, duration, calendar);
    if (op_valid != 0) {
      return 0; // operand invalid now -> NOT valid now
    }
    // Operand is valid now; NOT becomes valid when operand becomes invalid.
    return until_invalid(filter->data.operand, candidate, duration, calendar);
  }

  default:
    return 0; // Should not happen, assume valid.
  }
}

bool evaluate_filter(const Filter *filter, const time_t candidate,
                     const time_t duration, const Calendar *calendar) {
  return until_valid(filter, candidate, duration, calendar) == 0;
}

Filter *make_filter(FilterType type) {
  Filter *f = malloc(sizeof(Filter));
  if (!f)
    return NULL;
  f->type = type;
  return f;
}

// unsafe: assume type is logical
static Filter *combine(Filter *left, Filter *right, FilterType type) {
  Filter *f = malloc(sizeof(Filter));
  if (!f)
    return NULL;
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

time_t find_optimal_time(const Calendar *calendar, const Filter *filter,
                         const time_t start_time, const time_t duration) {

  if (!filter) {
    return start_time; // No filter means now is valid
  }
  time_t candidate = start_time;
  time_t effective_duration = duration;
  if (start_time == 0 && duration > 1000000000) {
    candidate = duration;
    effective_duration = 0;
  }
  int max_iterations = 365 * 24 * 60 / 15;
  int iterations = 0;

  while (iterations < max_iterations) {
    iterations++;
    time_t skip_seconds =
        until_valid(filter, candidate, effective_duration, calendar);

    if (skip_seconds < 0) {
      return -1; // No valid time found within filter constraints
    }

    if (skip_seconds > 0) {
      candidate += skip_seconds;
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
