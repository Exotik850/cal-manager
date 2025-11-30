#ifndef EVENT_LIST_H
#define EVENT_LIST_H

#include <stdbool.h>
#include <time.h>

typedef unsigned EventID;

typedef struct Event {
  EventID id;
  char title[256];
  char description[1024];
  time_t start_time;
  time_t end_time;
  struct Event *parent;
  struct Event *next;
} Event;

typedef struct EventList {
  Event *head;
  Event *tail;
  EventID next_id;
} EventList;

EventList *create_event_list(void);
void destroy_event_list(EventList *list);

Event *add_event_to_list(EventList *list, const char *title, const char *desc,
                         const time_t start, const time_t end);
Event *remove_event(EventList *list, const EventID id);
Event *find_event_by_id(const EventList *list, const EventID id);
void list_events(const EventList *list, const time_t start_date,
                 const time_t end_date);
bool save_events(const EventList *list, const char *filename);
bool load_events(EventList *list, const char *filename);

#endif // EVENT_LIST_H
