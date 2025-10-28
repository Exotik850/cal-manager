#include "calendar.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

EventList *create_event_list(void) {
  EventList *list = malloc(sizeof(EventList));
  list->head = NULL;
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

Event *create_event(const char *title, const char *desc, time_t start,
                    time_t end) {
  Event *event = malloc(sizeof(Event));
  event->id = 0;
  event->repeat_id = -1;
  event->parent_id = -1;
  strncpy(event->title, title, 255);
  event->title[255] = '\0';
  strncpy(event->description, desc, 1023);
  event->description[1023] = '\0';
  event->start_time = start;
  event->end_time = end;
  // event->is_repeating = false;
  // event->repeat_count = 0;
  event->next = NULL;
  event->parent = NULL;
  return event;
}

void add_event(EventList *list, Event *event) {
  event->id = list->next_id++;

  // If this event should be the new head
  if (!list->head || event->start_time < list->head->start_time) {
    event->next = list->head;
    if (list->head)
      list->head->parent = event;
    list->head = event;
    event->parent = NULL;
    return;
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
}

void remove_event(EventList *list, int id) {
  if (!list->head)
    return;
  // If the head is to be removed
  if (list->head->id == id) {
    Event *temp = list->head;
    list->head->next->parent = NULL;
    list->head = list->head->next;
    free(temp);
    return;
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
    free(temp);
    return;
  }
}

Event *find_event_by_id(EventList *list, int id) {
  Event *current = list->head;
  while (current) {
    if (current->id == id)
      return current;
    current = current->next;
  }
  return NULL;
}

void list_events(EventList *list, time_t start_date, time_t end_date) {
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

void save_events(EventList *list, const char *filename) {
  FILE *file = fopen(filename, "w");
  if (!file)
    return;

  Event *current = list->head;
  while (current) {
    fprintf(file, "%d|%d|%d|%s|%s|%lld|%lld\n", current->id, current->repeat_id,
            current->parent_id, current->title, current->description,
            current->start_time, current->end_time);
    current = current->next;
  }

  fclose(file);
}

void load_events(EventList *list, const char *filename) {
  FILE *file = fopen(filename, "r");
  if (!file)
    return;

  char line[2048];
  while (fgets(line, sizeof(line), file)) {
    Event *event = malloc(sizeof(Event));
    char *token = strtok(line, "|");
    event->id = atoi(token);

    token = strtok(NULL, "|");
    event->repeat_id = atoi(token);

    token = strtok(NULL, "|");
    event->parent_id = atoi(token);

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

    // token = strtok(NULL, "|");
    // event->is_repeating = atoi(token);

    // token = strtok(token, "|");
    // event->repeat_count = atoi(token);

    event->next = NULL;

    if (event->id >= list->next_id) {
      list->next_id = event->id + 1;
    }

    if (!list->head) {
      list->head = event;
    } else {
      Event *current = list->head;
      while (current->next) {
        current = current->next;
      }
      current->next = event;
    }
  }

  fclose(file);
}
