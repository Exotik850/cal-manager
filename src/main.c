#include "calendar.h"
#include "filter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void print_usage(const char *prog_name) {
  printf("Usage: %s [options] <command>\n", prog_name);
  printf("Options:\n");
  printf("  -f <file>    Use persistent storage file\n");
  printf("\nCommands:\n");
  printf("  list [start] [end]           List events\n");
  printf("  add <title> <desc> <start> <end>  Add event\n");
  printf("  find <duration> [filter]     Find optimal time\n");
  printf("  remove <id>                  Remove event\n");
  printf("\nTime format: YYYY-MM-DD-HH:MM\n");
  printf("Filters: business_days, avoid_friday\n");
}

static time_t parse_time(const char *str) {
  struct tm tm = {0};
  if (sscanf(str, "%d-%d-%d-%d:%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
             &tm.tm_hour, &tm.tm_min) == 5) {
    tm.tm_year -= 1900;
    tm.tm_mon -= 1;
    return mktime(&tm);
  }
  printf("Warning: invalid time format '%s', using current time\n", str);
  return time(NULL);
}

int main(int argc, char *argv[]) {
  EventList *list = create_event_list();
  char *filename = NULL;
  int arg_offset = 1;

  if (argc > 2 && strcmp(argv[1], "-f") == 0) {
    filename = argv[2];
    arg_offset = 3;
    load_events(list, filename);
  }

  if (argc <= arg_offset) {
    print_usage(argv[0]);
    destroy_event_list(list);
    return 1;
  }

  const char *command = argv[arg_offset];

  if (strcmp(command, "list") == 0) {
    time_t start =
        (arg_offset + 1 < argc) ? parse_time(argv[arg_offset + 1]) : time(NULL);
    time_t end = (arg_offset + 2 < argc) ? parse_time(argv[arg_offset + 2])
                                         : start + 86400 * 30;
    list_events(list, start, end);

  } else if (strcmp(command, "add") == 0) {
    if (argc < arg_offset + 5) {
      printf("Error: add requires title, description, start, end\n");
      destroy_event_list(list);
      return 1;
    }

    const char *title = argv[arg_offset + 1];
    const char *desc = argv[arg_offset + 2];
    time_t start = parse_time(argv[arg_offset + 3]);
    time_t end = parse_time(argv[arg_offset + 4]);

    Event *event = create_event(title, desc, start, end);
    add_event(list, event);
    printf("Event added with ID: %d\n", event->id);

    if (filename)
      save_events(list, filename);

  } else if (strcmp(command, "find") == 0) {
    if (argc < arg_offset + 2) {
      printf("Error: find requires duration in minutes\n");
      destroy_event_list(list);
      return 1;
    }

    int duration = atoi(argv[arg_offset + 1]);
    const char *filter_str =
        (arg_offset + 2 < argc) ? argv[arg_offset + 2] : "";

    Filter *filter = parse_filter(filter_str);

    if (!filter) {
      printf("Error: invalid filter\n");
      destroy_event_list(list);
      return 1;
    }


    time_t optimal = find_optimal_time(list, duration, filter);

    char buf[64];
    strftime(buf, 64, "%Y-%m-%d %H:%M", localtime(&optimal));
    printf("Optimal time: %s\n", buf);

    destroy_filter(filter);

  } else if (strcmp(command, "remove") == 0) {
    if (argc < arg_offset + 2) {
      printf("Error: remove requires event ID\n");
      destroy_event_list(list);
      return 1;
    }

    int id = atoi(argv[arg_offset + 1]);
    remove_event(list, id);
    printf("Event %d removed\n", id);

    if (filename)
      save_events(list, filename);

  } else {
    print_usage(argv[0]);
    destroy_event_list(list);
    return 1;
  }

  destroy_event_list(list);
  return 0;
}
