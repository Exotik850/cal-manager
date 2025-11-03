#ifndef CALENDAR_H
#define CALENDAR_H

#include "event_list.h"

// A bucket for a specific year, containing pointers to the first event
typedef struct YearBucket {
  Event *days[366];
  unsigned year;
  struct YearBucket *next;
} YearBucket;

// The main calendar structure
typedef struct Calendar {
  YearBucket *years; // linked list of year buckets, sorted by year
  EventList *event_list; // master event list
} Calendar;

Calendar *create_calendar();
void free_calendar(Calendar *calendar);

// Adds an event to the calendar's event list and year buckets
// Returns pointer to the added event, or NULL on failure
Event *add_event_calendar(Calendar *calendar, const char *title,
                          const char *description, const time_t start,
                          const time_t end);

// Removes an event from the calendar's event list and year buckets
// Returns pointer to removed event, or NULL if not found
Event *remove_event_calendar(Calendar *calendar, const EventID id);

// Returns pointer to the first event on the specified date, or NULL if none
Event *get_first_event(Calendar *calendar, const unsigned year,
                       const unsigned month, const unsigned day);
// Returns pointer to the event with the specified ID, or NULL if not found
// (delegates to event list)
Event *get_event_calendar(const Calendar *calendar, const EventID id);

bool is_leap_year(const unsigned year);
unsigned days_in_month(const unsigned month, const unsigned year);

#endif // CALENDAR_H
