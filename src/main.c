#include "calendar.h"
#include "event_list.h"
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
  printf("  list [start] [end]           List events in date range\n");
  printf("  add <title> <desc> <start> <end>  Add event\n");
  printf("  find <duration> [filter]     Find optimal time slot\n");
  printf(
      "  find <duration> [filter] --add <title> <desc>  Find and add event\n");
  printf("  remove <id>                  Remove event by ID\n");
  printf("\nTime format: YYYY-MM-DD-HH:MM\n");
  printf("Date format (filters): YYYY-M-D\n");
  printf("\nFilter keywords:\n");
  printf("  weekdays, weekend, holidays, business_days\n");
  printf("  on <day>[,<day>...]         (e.g., on Monday, Friday)\n");
  printf("  before <date>, after <date>\n");
  printf("  spaced <N> <unit>           (units: minutes/hours/days)\n");
  printf("  not, and, or                (logical operators)\n");
  printf("\nExamples:\n");
  printf("  weekdays and not holidays\n");
  printf("  on Monday, Wednesday or weekend\n");
  printf("  after 2024-1-1 and spaced 30 minutes\n");
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
  Calendar *cal = create_calendar();
  char *filename = NULL;
  int arg_offset = 1;

  if (argc > 2 && strcmp(argv[1], "-f") == 0) {
    filename = argv[2];
    arg_offset = 3;
    load_calendar_events(cal, filename);
  }

  if (argc <= arg_offset) {
    print_usage(argv[0]);
    free_calendar(cal);
    return 1;
  }

  const char *command = argv[arg_offset];

  if (strcmp(command, "list") == 0) {
    time_t start =
        (arg_offset + 1 < argc) ? parse_time(argv[arg_offset + 1]) : time(NULL);
    time_t end = (arg_offset + 2 < argc) ? parse_time(argv[arg_offset + 2])
                                         : start + 86400 * 30;
    list_events(cal->event_list, start, end);

  } else if (strcmp(command, "add") == 0) {
    if (argc < arg_offset + 5) {
      printf("Error: add requires title, description, start, end\n");
      free_calendar(cal);
      return 1;
    }

    const char *title = argv[arg_offset + 1];
    const char *desc = argv[arg_offset + 2];
    time_t start = parse_time(argv[arg_offset + 3]);
    time_t end = parse_time(argv[arg_offset + 4]);

    Event *ev = add_event_calendar(cal, title, desc, start, end);
    printf("Event added with ID: %d\n", ev->id);

    if (filename)
      save_events(cal->event_list, filename);

  } else if (strcmp(command, "find") == 0) {
    // Check for --add option
    // If present, we will add the event after finding the optimal time
    bool do_add = false;
    const char *add_title = NULL;
    const char *add_desc = NULL;
    int duration = 0;
    for (int i = arg_offset + 2; i < argc; i++) {
      if (strcmp(argv[i], "--add") == 0) {
        do_add = true;
        if (i + 3 >= argc) {
          printf("Error: --add requires title, description, and duration\n");
          free_calendar(cal);
          return 1;
        }
        add_title = argv[i + 1];
        add_desc = argv[i + 2];
        duration = atoi(argv[i + 3]);
        // Adjust argc to ignore the --add part for filter parsing
        argc = i;
        break;
      }
    }

    const char *filter_str =
        (arg_offset + 1 < argc) ? argv[arg_offset + 1] : "";

    Filter *filter = parse_filter(filter_str);

    if (!filter) {
      printf("Error: invalid filter\n");
      free_calendar(cal);
      return 1;
    }

    time_t optimal = find_optimal_time(cal, filter);
    if (optimal == -1) {
      printf("No valid time slot found within constraints\n");
      destroy_filter(filter);
      free_calendar(cal);
      return 1;
    }

    char buf[64];
    strftime(buf, 64, "%Y-%m-%d %H:%M", localtime(&optimal));
    printf("Optimal time: %s\n", buf);

    if (do_add) {
      time_t end_time = optimal + duration * 60;
      Event *ev =
          add_event_calendar(cal, add_title, add_desc, optimal, end_time);
      printf("Event added with ID: %d\n", ev->id);

      if (filename)
        save_events(cal->event_list, filename);
    }

    destroy_filter(filter);

  } else if (strcmp(command, "remove") == 0) {
    if (argc < arg_offset + 2) {
      printf("Error: remove requires event ID\n");
      free_calendar(cal);
      return 1;
    }

    int id = atoi(argv[arg_offset + 1]);
    remove_event_calendar(cal, id);
    printf("Event %d removed\n", id);

    if (filename)
      save_events(cal->event_list, filename);

  } else {
    print_usage(argv[0]);
    free_calendar(cal);
    return 1;
  }

  free_calendar(cal);
  return 0;
}
