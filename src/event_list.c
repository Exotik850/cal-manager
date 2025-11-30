#include "event_list.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

EventList *create_event_list(void) {
  EventList *list = malloc(sizeof(EventList));
  if (!list)
    return NULL;
  list->head = NULL;
  list->tail = NULL;
  list->next_id = 1;
  return list;
}

void destroy_event_list(EventList *list) {
  Event *current = list->head;
  while (current) {
    Event *next = current->next;
    free(current);
    current = next;
  }
  free(list);
}

static Event *create_event(const char *title, const char *desc, time_t start,
                           time_t end) {
  Event *event = malloc(sizeof(Event));
  if (!event)
    return NULL;
  event->id = 0;
  strncpy(event->title, title, 255);
  event->title[255] = '\0';
  strncpy(event->description, desc, 1023);
  event->description[1023] = '\0';
  event->start_time = start;
  event->end_time = end;
  event->next = NULL;
  event->parent = NULL;
  return event;
}

Event *add_event_to_list(EventList *list, const char *title, const char *desc,
                         const time_t start, const time_t end) {
  Event *event = create_event(title, desc, start, end);
  event->id = list->next_id++;

  // If this event should be the new head
  if (!list->head || event->start_time < list->head->start_time) {
    event->next = list->head;
    if (list->head)
      list->head->parent = event;
    list->head = event;
    event->parent = NULL;
    if (!list->tail) {
      list->tail = event;
    }
    return event;
  }

  // If this event should be the new tail, safe because we know there are no
  // more events after tail
  if (list->tail && event->start_time >= list->tail->start_time) {
    list->tail->next = event;
    event->parent = list->tail;
    list->tail = event;
    return event;
  }

  // Find the correct insertion point
  Event *current = list->head;
  while (current->next && current->next->start_time <= event->start_time) {
    current = current->next;
  }

  // Insert the event
  event->next = current->next;
  if (current->next)
    current->next->parent = event;
  current->next = event;
  event->parent = current;
  return event;
}

// Removes the event with the specified ID from the list
// Returns pointer to removed event, or NULL if not found
Event *remove_event(EventList *list, const EventID id) {
  if (!list->head)
    return NULL;

  // If the head is to be removed
  if (list->head->id == id) {
    Event *temp = list->head;
    list->head = list->head->next;
    if (list->head)
      list->head->parent = NULL;
    return temp;
  }

  // If the tail is to be removed
  if (list->tail && list->tail->id == id) {
    Event *tail = list->tail;
    tail->parent->next =
        NULL; // safe: tail is not head here, gauranteed to have parent
    list->tail = tail->parent;
    return tail;
  }

  // Search for the event to remove
  Event *current = list->head;
  while (current->next) {
    if (current->next->id != id) {
      current = current->next;
      continue;
    }
    // Remove the event
    Event *temp = current->next;
    if (current->next->next)
      current->next->next->parent = current;
    current->next = current->next->next;
    return temp;
  }
  return NULL; // Not found
}

Event *find_event_by_id(const EventList *list, const EventID id) {
  Event *current = list->head;
  while (current) {
    if (current->id == id)
      return current;
    current = current->next;
  }
  return NULL;
}

void list_events(const EventList *list, const time_t start_date,
                 const time_t end_date) {
  Event *current = list->head;
  char start_buf[64], end_buf[64];
  printf("Events from %s to %s:\n", ctime(&start_date), ctime(&end_date));
  while (current) {
    if (current->start_time >= start_date && current->start_time <= end_date) {
      strftime(start_buf, 64, "%Y-%m-%d %H:%M",
               localtime(&current->start_time));
      strftime(end_buf, 64, "%Y-%m-%d %H:%M", localtime(&current->end_time));
      printf("ID: %d | %s | %s - %s\n", current->id, current->title, start_buf,
             end_buf);
      if (current->description[0])
        printf("  %s\n", current->description);
    }
    current = current->next;
  }
}

bool save_events(const EventList *list, const char *filename) {
  if (!list->head)
    return false;
  FILE *file = fopen(filename, "w");
  if (!file)
    return false;
  Event *current = list->head;
  while (current) {
    fprintf(file, "%d|%s|%s|%lld|%lld\n", current->id, current->title,
            current->description, current->start_time, current->end_time);
    current = current->next;
  }
  fclose(file);
  return true;
}

bool load_events(EventList *list, const char *filename) {
  FILE *file = fopen(filename, "r");
  if (!file)
    return false;

  char line[2048];
  while (fgets(line, sizeof(line), file)) {
    Event *event = malloc(sizeof(Event));
    if (!event) {
      printf("Memory allocation failed while loading events.\n");
      fclose(file);
      return false;
    }
    char *token = strtok(line, "|");
    event->id = atoi(token);

    token = strtok(NULL, "|");
    strncpy(event->title, token, 255);
    event->title[255] = '\0';

    token = strtok(NULL, "|");
    strncpy(event->description, token, 1023);
    event->description[1023] = '\0';

    token = strtok(NULL, "|");
    event->start_time = atol(token);

    token = strtok(NULL, "|");
    event->end_time = atol(token);

    event->next = NULL;

    if (event->id >= list->next_id) {
      list->next_id = event->id + 1;
    }

    // assume events are already sorted in the file
    if (!list->head) {
      list->head = event;
      list->tail = event;
      event->parent = NULL;
    } else {
      list->tail->next = event;
      event->parent = list->tail;
      list->tail = event;
    }
  }

  fclose(file);
  return true;
}
