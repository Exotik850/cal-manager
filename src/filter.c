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
  return (days_ahead - 1) * 1440 + (1440 - minutes_today);
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

int get_next_invalid_minutes(const Filter *filter, const time_t candidate,
                             const Calendar *calendar) {
  if (!filter || filter->type == FILTER_NONE)
    return -1;
  struct tm *tm_candidate = localtime(&candidate);

  switch (filter->type) {
  case FILTER_DAY_OF_WEEK: {
    if (tm_candidate->tm_wday != filter->data.day_of_week) {
      return 0; // Already invalid
    }
    // It's valid today. It becomes invalid at midnight.
    int minutes_today = tm_candidate->tm_hour * 60 + tm_candidate->tm_min;
    return 1440 - minutes_today;
  }

  case FILTER_AFTER_DATETIME:
    if (candidate <= filter->data.time_value) {
      return 0; // Already invalid
    }
    return -1; // Will never be invalid again

  case FILTER_BEFORE_DATETIME: {
    if (candidate >= filter->data.time_value) {
      return 0; // Already invalid
    }
    // It's valid now. It becomes invalid at the filter's time_value.
    return (int)(difftime(filter->data.time_value, candidate) / 60);
  }

  case FILTER_AFTER_TIME: {
    struct tm tm_candidate_saved = *tm_candidate;
    struct tm *tm_limit = localtime(&filter->data.time_value);
    tm_candidate_saved.tm_hour = tm_limit->tm_hour;
    tm_candidate_saved.tm_min = tm_limit->tm_min;
    tm_candidate_saved.tm_sec = 0;
    time_t limit_time = mktime(&tm_candidate_saved); // same day at limit time
    if (candidate < limit_time) {
      return 0; // Already invalid
    }
    // It's valid now. It becomes invalid at the limit time tomorrow.
    tm_candidate_saved.tm_mday += 1;
    tm_candidate_saved.tm_isdst = -1;
    time_t next_limit_time = mktime(&tm_candidate_saved);
    return (int)(difftime(next_limit_time, candidate) / 60);
  }

  case FILTER_BEFORE_TIME: {
    struct tm tm_candidate_saved = *tm_candidate;
    struct tm *tm_limit = localtime(&filter->data.time_value);
    tm_candidate_saved.tm_hour = tm_limit->tm_hour;
    tm_candidate_saved.tm_min = tm_limit->tm_min;
    tm_candidate_saved.tm_sec = 0;
    time_t limit_time = mktime(&tm_candidate_saved); // same day at limit time
    if (candidate >= limit_time) {
      return 0; // Already invalid
    }
    // It's valid now. It becomes invalid at the limit time today.
    return (int)(difftime(limit_time, candidate) / 60);
  }

  case FILTER_MIN_DISTANCE:
    // This filter is only used to find valid times.
    // An "invalid" time for this filter is one that is too close to an event.
    // The logic to find the "next invalid" time is complex and not required.
    // We can treat it as always valid from an "invalid" perspective.
    return -1;

  case FILTER_HOLIDAY:
    if (days_until_holiday(tm_candidate) != 0) {
      return 0; // Not a holiday, so it's invalid.
    }
    // It's a holiday. It becomes not-a-holiday at midnight.
    int minutes_today = tm_candidate->tm_hour * 60 + tm_candidate->tm_min;
    return 1440 - minutes_today;

  case FILTER_AND: {
    int left_dist = get_next_invalid_minutes(filter->data.logical.left,
                                             candidate, calendar);
    int right_dist = get_next_invalid_minutes(filter->data.logical.right,
                                              candidate, calendar);

    if (left_dist == 0 || right_dist == 0) {
      return 0; // Already invalid if either part is invalid.
    }
    if (left_dist < 0)
      return right_dist;
    if (right_dist < 0)
      return left_dist;
    // Return the sooner of the two invalid times.
    return left_dist < right_dist ? left_dist : right_dist;
  }

  case FILTER_OR: {
    int left_dist = get_next_invalid_minutes(filter->data.logical.left,
                                             candidate, calendar);
    int right_dist = get_next_invalid_minutes(filter->data.logical.right,
                                              candidate, calendar);

    if (left_dist < 0 || right_dist < 0) {
      return -1; // Never invalid if either part is never invalid.
    }
    // Return the later of the two invalid times.
    return left_dist > right_dist ? left_dist : right_dist;
  }

  case FILTER_NOT: {
    // The time is invalid for a NOT filter if the operand is valid.
    // get_next_valid_minutes returns 0 if the operand is currently valid.
    int valid_minutes =
        get_next_valid_minutes(filter->data.operand, candidate, calendar);
    return valid_minutes;
  }

  default:
    return -1; // Should not happen, but assume always valid.
  }
}

int get_next_valid_minutes(const Filter *filter, const time_t candidate,
                           const Calendar *calendar) {
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

  case FILTER_AFTER_DATETIME:
    if (candidate > filter->data.time_value) {
      return 0;
    }
    return (int)(difftime(filter->data.time_value, candidate) / 60) + 1;

  case FILTER_BEFORE_DATETIME: {
    if (candidate < filter->data.time_value) {
      return 0;
    }
    return -1; // No valid time
  }

  case FILTER_AFTER_TIME: {
    // Filter should be valid if HH:MM of candidate is > HH:MM of filter time
    struct tm tm_candidate_saved = *tm_candidate;
    struct tm *tm_limit = localtime(&filter->data.time_value);
    tm_candidate_saved.tm_hour = tm_limit->tm_hour;
    tm_candidate_saved.tm_min = tm_limit->tm_min;
    tm_candidate_saved.tm_sec = 0;
    time_t limit_time = mktime(&tm_candidate_saved); // same day at limit time
    if (candidate >= limit_time) {
      return 0;
    }
    // Add 1 to ensure we reach the target time
    return (int)(difftime(limit_time, candidate) / 60) + 1;
  }
  case FILTER_BEFORE_TIME: {
    // Filter should be valid if HH:MM of candidate is < HH:MM of filter time
    struct tm tm_candidate_saved = *tm_candidate;
    struct tm tm_limit_copy;
    {
      struct tm *tm_limit = localtime(&filter->data.time_value);
      tm_limit_copy = *tm_limit;
    }
    tm_candidate_saved.tm_hour = tm_limit_copy.tm_hour;
    tm_candidate_saved.tm_min = tm_limit_copy.tm_min;
    tm_candidate_saved.tm_sec = 0;
    time_t limit_time = mktime(&tm_candidate_saved); // same day at limit time
    if (candidate < limit_time) {
      return 0;
    }

    // If we're at or past the threshold, wait until midnight (start of next
    // day) This allows other filters (like FILTER_AFTER_TIME) to properly
    // constrain the next valid period
    tm_candidate_saved.tm_hour = 0;
    tm_candidate_saved.tm_min = 0;
    tm_candidate_saved.tm_sec = 0;
    tm_candidate_saved.tm_isdst = -1; // let mktime figure it out
    tm_candidate_saved.tm_mday += 1;  // next day (mktime will normalize)
    time_t next_midnight = mktime(&tm_candidate_saved);
    // Add 1 to ensure we're past midnight, not at 23:59
    return (int)(difftime(next_midnight, candidate) / 60) + 1;
  }

  case FILTER_MIN_DISTANCE:
    return minutes_until_min_distance(candidate, filter->data.minutes,
                                      calendar);

  case FILTER_HOLIDAY:
    unsigned days = days_until_holiday(tm_candidate);
    if (days == 0) {
      return 0;
    }
    int minutes_today = tm_candidate->tm_hour * 60 + tm_candidate->tm_min;
    return (days - 1) * 1440 + (1440 - minutes_today);

  case FILTER_AND: {
    int left_dist =
        get_next_valid_minutes(filter->data.logical.left, candidate, calendar);
    int right_dist =
        get_next_valid_minutes(filter->data.logical.right, candidate, calendar);

    if (left_dist < 0 || right_dist < 0)
      return -1;

    if (left_dist == 0 && right_dist == 0)
      return 0;

    return left_dist > right_dist ? left_dist : right_dist;
  }

  case FILTER_OR: {
    int left_dist =
        get_next_valid_minutes(filter->data.logical.left, candidate, calendar);
    int right_dist =
        get_next_valid_minutes(filter->data.logical.right, candidate, calendar);

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
    int minutes = get_next_invalid_minutes(filter->data.operand, candidate, calendar);
    return minutes;
  }

  default:
    return 0;
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
