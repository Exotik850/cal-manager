#include "calendar.h"
#include "event_list.h"
#include <stdlib.h>

Calendar *create_calendar() {
  Calendar *calendar = (Calendar *)calloc(1, sizeof(Calendar));
  if (!calendar) {
    return NULL;
  }
  calendar->event_list = create_event_list();
  if (!calendar->event_list) {
    free(calendar);
    return NULL;
  }
  return calendar;
}

static YearBucket *create_year_bucket(const unsigned year) {
  YearBucket *year_bucket = (YearBucket *)calloc(1, sizeof(YearBucket));
  if (!year_bucket) {
    return NULL;
  }
  year_bucket->year = year;
  return year_bucket;
}

static void free_years(YearBucket *year) {
  while (year) {
    YearBucket *next_year = year->next;
    free(year);
    year = next_year;
  }
}

void free_calendar(Calendar *calendar) {
  if (!calendar) {
    return;
  }
  free_years(calendar->years);
  destroy_event_list(calendar->event_list);
  free(calendar);
}

// Returns true if the given year is a leap year
// Year is the full year (e.g., 2024)
bool is_leap_year(const unsigned year) {
  return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

// Returns the number of days in a given month of a given year
// Month is 1-12
// Year is the full year (e.g., 2024)
unsigned days_in_month(const unsigned month, const unsigned year) {
  switch (month) {
  case 1:
  case 3:
  case 5:
  case 7:
  case 8:
  case 10:
  case 12:
    return 31;
  case 4:
  case 6:
  case 9:
  case 11:
    return 30;
  case 2:
    return is_leap_year(year) ? 29 : 28;
  default:
    return 0; // Invalid month
  }
}

// Returns the day of the year (1-365 or 1-366 for leap years)
// for the given date
//
// Returns -1 for invalid dates
static size_t get_day_of_year_date(const unsigned year, const unsigned month,
                                   const unsigned day) {
  size_t out = 0;
  if (month < 1 || month > 12) {
    return -1; // Invalid month
  }
  if (day < 1 || day > days_in_month(month, year)) {
    return -1; // Invalid day
  }
  for (unsigned m = 1; m < month; ++m) {
    out += days_in_month(m, year);
  }
  out += day;
  return out;
}

typedef struct YearDay {
  unsigned year;
  unsigned day_of_year;
} YearDay;

static YearDay *get_year_day_from_event(const Event *event) {
  if (!event) {
    return NULL;
  }
  struct tm *tm_info = localtime(&event->start_time);
  if (!tm_info) {
    return NULL;
  }
  YearDay *yd = malloc(sizeof(YearDay));
  if (!yd) {
    return NULL;
  }
  yd->year = tm_info->tm_year + 1900;
  yd->day_of_year =
      get_day_of_year_date(yd->year, tm_info->tm_mon + 1, tm_info->tm_mday);
  if (yd->day_of_year == -1) {
    free(yd);
    return NULL; // Invalid date, should not happen
  }
  return yd;
}

void add_event_cal_(Calendar *calendar, Event *event) {
  YearDay *year_day = get_year_day_from_event(event);
  if (!year_day) {
    return; // Failed to get year/day, should not happen
  }
  unsigned year = year_day->year;
  size_t day_of_year = year_day->day_of_year;
  free(year_day);

  // Find or create the year bucket
  YearBucket *current_year = calendar->years;
  YearBucket *prev_year = NULL;
  while (current_year && current_year->year < year) {
    prev_year = current_year;
    current_year = current_year->next;
  }
  if (!current_year || current_year->year != year) {
    YearBucket *new_year = create_year_bucket(year);
    if (!new_year) {
      // Failed to create year bucket, remove event and return NULL
      remove_event(calendar->event_list, event->id);
      return;
    }
    new_year->next = current_year;
    if (prev_year) {
      prev_year->next = new_year;
    } else {
      calendar->years = new_year;
    }
    current_year = new_year;
  }
  // Insert the event into the correct day bucket if it's the first event of the
  // day
  if (!current_year->days[day_of_year - 1]) {
    current_year->days[day_of_year - 1] = event;
    return;
  }
  if (event->start_time < current_year->days[day_of_year - 1]->start_time) {
    current_year->days[day_of_year - 1] = event;
  }
}

Event *add_event_calendar(Calendar *calendar, const char *title,
                          const char *description, const time_t start,
                          const time_t end) {
  if (!calendar || !calendar->event_list) {
    return NULL;
  }
  Event *event =
      add_event_to_list(calendar->event_list, title, description, start, end);
  if (!event) {
    return NULL;
  }
  add_event_cal_(calendar, event);
  return event;
}

Event *remove_event_calendar(Calendar *calendar, const EventID id) {
  if (!calendar || !calendar->event_list) {
    return NULL;
  }
  Event *event = remove_event(calendar->event_list, id);
  if (!event) {
    return NULL; // Not found
  }
  YearDay *year_day = get_year_day_from_event(event);
  if (!year_day) {
    return event; // Failed to get year/day, should not happen
  }
  unsigned year = year_day->year;
  size_t day_of_year = year_day->day_of_year;
  free(year_day);
  YearBucket *current_year = calendar->years;
  while (current_year) {
    if (current_year->year < year) {
      current_year = current_year->next;
      continue;
    }
    if (current_year->year > year) {
      return event; // Year not found, but event already removed (should not
                    // happen)
    }
    if (current_year->days[day_of_year - 1] != event) {
      return event; // Event not the first on this day, nothing to update
    }
    // Need to find the next event for that day
    Event *next_event = event->next;
    if (!next_event) {
      current_year->days[day_of_year - 1] = NULL;
      return event;
    }
    YearDay *next_year_day = get_year_day_from_event(next_event);
    if (!next_year_day) {
      current_year->days[day_of_year - 1] = NULL;
      return event; // Failed to get year/day, should not happen
    }
    unsigned next_year = next_year_day->year;
    size_t next_day_of_year = next_year_day->day_of_year;
    free(next_year_day);
    if (next_year != year || next_day_of_year != day_of_year) {
      // No more events on this day
      current_year->days[day_of_year - 1] = NULL;
      current_year = current_year->next;
    } else {
      current_year->days[day_of_year - 1] = next_event;
      return event;
    }
  }
  return event; // Year not found, but event already removed
}

Event *get_event_calendar(const Calendar *calendar, const EventID id) {
  if (!calendar || !calendar->event_list) {
    return NULL;
  }
  return find_event_by_id(calendar->event_list, id);
}

Event *get_first_event(Calendar *calendar, const unsigned year,
                       const unsigned month, const unsigned day) {
  if (!calendar || !calendar->event_list) {
    return NULL;
  }
  size_t day_of_year = get_day_of_year_date(year, month, day);
  if (day_of_year == -1) {
    return NULL; // Invalid date
  }
  YearBucket *current_year = calendar->years;
  while (current_year) {
    if (current_year->year == year) {
      return current_year->days[day_of_year - 1];
    } else if (current_year->year > year) {
      return NULL; // Year not found
    }
    current_year = current_year->next;
  }
  return NULL; // Year not found
}

void load_calendar_events(Calendar *cal, const char *filename) {
  if (!cal || !cal->event_list) {
    return;
  }
  load_events(cal->event_list, filename);
  // Rebuild year buckets
  cal->years = NULL;
  Event *current = cal->event_list->head;
  while (current) {
    add_event_cal_(cal, current);
    current = current->next;
  }
}
