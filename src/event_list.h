#ifndef EVENT_LIST_H
#define EVENT_LIST_H

#include <stdbool.h>
#include <time.h>

typedef struct Event {
  int id;
  int repeat_id;
  int parent_id;
  char title[256];
  char description[1024];
  time_t start_time;
  time_t end_time;
  // bool is_repeating;
  // int repeat_count;
  struct Event *parent;
  struct Event *next;
} Event;

Event *create_event(const char *title, const char *desc, time_t start,
                    time_t end);


typedef struct EventList {
  Event *head;
  Event *tail;
  int next_id;
} EventList;

EventList *create_event_list(void);
void destroy_event_list(EventList *list);

void add_event(EventList *list, Event *event);
void remove_event(EventList *list, int id);
Event *find_event_by_id(EventList *list, int id);
void list_events(EventList *list, time_t start_date, time_t end_date);
void save_events(EventList *list, const char *filename);
void load_events(EventList *list, const char *filename);


#endif // EVENT_LIST_H
