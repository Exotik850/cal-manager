#include "filter.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
  char month;
  char day;
} Holiday;

static const Holiday holidays[] = {
    {1, 1},   // New Year's Day
    {7, 4},   // Independence Day
    {12, 25}, // Christmas Day
};
const size_t num_holidays = sizeof(holidays) / sizeof(holidays[0]);

static bool is_holiday(time_t t) {
  struct tm *tm_time = localtime(&t);
  for (size_t i = 0; i < num_holidays; i++) {
    if (tm_time->tm_mon + 1 == holidays[i].month &&
        tm_time->tm_mday == holidays[i].day) {
      return true;
    }
  }
  return false;
}

static int minutes_until_day_of_week(time_t t, int target_day) {
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
// TODO: This is the naive implementation that only checks the current day,
// can be improved to find the actual next holiday.
static int minutes_until_next_holiday(time_t t) {
  if (is_holiday(t)) {
    return 0;
  }
  return 1;
}

// Helper to find minutes until current is outside the limit time
static int minutes_until_outside_range(time_t current, time_t limit) {
  if (current > limit)
    return 0;
  return (int)(difftime(limit, current) / 60) + 1;
}

// Helper to find minutes until candidate is at least min_minutes
// away from any event boundary.
// Negative minutes allowed to permit overlaps.
static int minutes_until_min_distance(time_t candidate, int min_minutes,
                                      EventList *list) {
  if (!list || !list->head)
    return 0;
  int min_seconds = min_minutes * 60;
  Event *current = list->head;
  int max_skip = 0;
  while (current) {
    for (int i = 0; i < 2; i++) {
      time_t boundary = i == 0 ? current->start_time : current->end_time;
      if (candidate >= boundary - min_seconds &&
          candidate < boundary + min_seconds) {
        int skip = (int)(difftime(boundary + min_seconds, candidate) / 60);
        if (skip > max_skip)
          max_skip = skip;
      }
    }
    current = current->next;
  }
  return max_skip;
}

int get_next_valid_minutes(Filter *filter, time_t candidate, EventList *list) {
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

  case FILTER_AFTER_TIME:
    return minutes_until_outside_range(candidate, filter->data.time_value);

  case FILTER_BEFORE_TIME: {
    if (candidate < filter->data.time_value) {
      return 0;
    }
    return -1; // No valid time
  }

  case FILTER_MIN_DISTANCE:
    return minutes_until_min_distance(candidate, filter->data.minutes, list);

  case FILTER_HOLIDAY:
    return minutes_until_next_holiday(candidate);

  case FILTER_AND: {
    int left_dist =
        get_next_valid_minutes(filter->data.logical.left, candidate, list);
    int right_dist =
        get_next_valid_minutes(filter->data.logical.right, candidate, list);

    if (left_dist < 0 || right_dist < 0)
      return -1;

    if (left_dist == 0 && right_dist == 0)
      return 0;

    return left_dist > right_dist ? left_dist : right_dist;
  }

  case FILTER_OR: {
    int left_dist =
        get_next_valid_minutes(filter->data.logical.left, candidate, list);
    int right_dist =
        get_next_valid_minutes(filter->data.logical.right, candidate, list);

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
    int minutes = get_next_valid_minutes(filter->data.operand, candidate, list);
    // If operand says valid in N minutes, we are invalid for N minutes
    return minutes < 0 ? 0 : minutes + 1;
  }

  default:
    return 0;
  }
}

bool evaluate_filter(Filter *filter, time_t candidate, EventList *list) {
  return get_next_valid_minutes(filter, candidate, list) == 0;
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


// Parser combinator implementation for filter expressions
// Grammar (informal, precedence: NOT > AND > OR):
//   expr       := or_expr
//   or_expr    := and_expr (OR and_expr)*
//   and_expr   := unary (AND unary)*
//   unary      := NOT unary | primary
//   primary    := '(' expr ')' | weekdays | holidays | 'on' day_list
//               | 'before' date | 'after' date | 'spaced' duration
//   day_list   := day_name (',' day_name)*
//   duration   := signed_int ('minute'|'minutes'|'hour'|'hours')
//   date       := YYYY '-' M '-' D
//   day_name   := Sunday|Monday|Tuesday|Wednesday|Thursday|Friday|Saturday

typedef struct {
  const char *s;
  size_t pos;
  size_t len;
} Parser;

static char lower_char(char c) {
  if (c >= 'A' && c <= 'Z')
    return (char)(c - 'A' + 'a');
  return c;
}

static bool is_space(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static void skip_ws(Parser *p) {
  while (p->pos < p->len && is_space(p->s[p->pos])) {
    p->pos++;
  }
}

static bool match_char(Parser *p, char c) {
  skip_ws(p);
  if (p->pos < p->len && p->s[p->pos] == c) {
    p->pos++;
    return true;
  }
  return false;
}

static bool peek_char(Parser *p, char c) {
  skip_ws(p);
  return (p->pos < p->len && p->s[p->pos] == c);
}

// Match a word case-insensitively and ensure a reasonable boundary after it.
static bool match_word(Parser *p, const char *word) {
  skip_ws(p);
  size_t start = p->pos;
  size_t i = 0;
  while (word[i] != '\0') {
    if (start + i >= p->len)
      return false;
    if (lower_char(p->s[start + i]) != lower_char(word[i]))
      return false;
    i++;
  }
  // Boundary check for what follows the word
  char next = (start + i < p->len) ? p->s[start + i] : '\0';
  if (!(next == '\0' || is_space(next) || next == '(' || next == ')' || next == ',')) {
    return false;
  }
  p->pos = start + i;
  return true;
}

static bool match_ci(Parser *p, const char *lit) {
  // Same as match_word but without boundary requirements (for units like minutes/hours attached to numbers)
  size_t start = p->pos;
  size_t i = 0;
  while (lit[i] != '\0') {
    if (start + i >= p->len)
      return false;
    if (lower_char(p->s[start + i]) != lower_char(lit[i]))
      return false;
    i++;
  }
  p->pos = start + i;
  return true;
}

static bool parse_int(Parser *p, int *out) {
  skip_ws(p);
  int val = 0;
  bool have = false;
  while (p->pos < p->len) {
    char c = p->s[p->pos];
    if (c >= '0' && c <= '9') {
      have = true;
      val = val * 10 + (c - '0');
      p->pos++;
    } else {
      break;
    }
  }
  if (have)
    *out = val;
  return have;
}

static bool parse_signed_int(Parser *p, int *out) {
  skip_ws(p);
  int sign = 1;
  if (p->pos < p->len && p->s[p->pos] == '-') {
    sign = -1;
    p->pos++;
  }
  int val = 0;
  if (!parse_int(p, &val))
    return false;
  *out = sign * val;
  return true;
}

static time_t make_date_time(int year, int month, int day) {
  struct tm tmv;
  memset(&tmv, 0, sizeof(tmv));
  tmv.tm_year = year - 1900;
  tmv.tm_mon = month - 1;
  tmv.tm_mday = day;
  tmv.tm_hour = 0;
  tmv.tm_min = 0;
  tmv.tm_sec = 0;
  tmv.tm_isdst = -1;
  return mktime(&tmv);
}

static bool parse_date(Parser *p, time_t *out) {
  skip_ws(p);
  int y = 0, m = 0, d = 0;
  size_t save = p->pos;
  if (!parse_int(p, &y))
    return false;
  if (!(p->pos < p->len && p->s[p->pos] == '-')) {
    p->pos = save;
    return false;
  }
  p->pos++; // skip '-'
  if (!parse_int(p, &m)) {
    p->pos = save;
    return false;
  }
  if (!(p->pos < p->len && p->s[p->pos] == '-')) {
    p->pos = save;
    return false;
  }
  p->pos++; // skip '-'
  if (!parse_int(p, &d)) {
    p->pos = save;
    return false;
  }
  *out = make_date_time(y, m, d);
  return true;
}

static int day_name_to_wday(Parser *p) {
  skip_ws(p);
  if (match_word(p, "Sunday")) return 0;
  if (match_word(p, "Monday")) return 1;
  if (match_word(p, "Tuesday")) return 2;
  if (match_word(p, "Wednesday")) return 3;
  if (match_word(p, "Thursday")) return 4;
  if (match_word(p, "Friday")) return 5;
  if (match_word(p, "Saturday")) return 6;
  // Lowercase variants
  if (match_word(p, "sunday")) return 0;
  if (match_word(p, "monday")) return 1;
  if (match_word(p, "tuesday")) return 2;
  if (match_word(p, "wednesday")) return 3;
  if (match_word(p, "thursday")) return 4;
  if (match_word(p, "friday")) return 5;
  if (match_word(p, "saturday")) return 6;
  return -1;
}

static Filter *day_filter(int wday) {
  Filter *f = make_filter(FILTER_DAY_OF_WEEK);

  f->data.day_of_week = wday;
  return f;
}

static Filter *parse_on(Parser *p) {
  if (!match_word(p, "on"))
    return NULL;
  Filter *acc = NULL;
  // Expect at least one day
  int w = day_name_to_wday(p);
  if (w < 0)
    return NULL;
  acc = day_filter(w);
  while (1) {
    skip_ws(p);
    if (peek_char(p, ',')) {
      match_char(p, ',');

      int w2 = day_name_to_wday(p);
      if (w2 < 0) break;
      acc = or_filter(acc, day_filter(w2));
      continue;
    }

    break;

  }
  return acc;
}

static Filter *parse_weekdays(Parser *p) {
  if (!match_word(p, "weekdays"))
    return NULL;
  Filter *acc = NULL;
  for (int i = 1; i <= 5; i++) {
    Filter *d = day_filter(i);
    if (!acc)
      acc = d;
    else
      acc = or_filter(acc, d);
  }
  return acc;
}


static Filter *parse_business_days(Parser *p) {

  if (!match_word(p, "business_days"))
    return NULL;

  Filter *acc = NULL;
  for (int i = 1; i <= 5; i++) {
    Filter *d = day_filter(i);
    if (!acc)
      acc = d;
    else
      acc = or_filter(acc, d);
  }
  // Backward-compatible behavior from legacy parse_filter:
  // weekdays AND holiday
  return and_filter(acc, make_filter(FILTER_HOLIDAY));

}

static Filter *parse_avoid_friday(Parser *p) {
  if (!match_word(p, "avoid_friday"))
    return NULL;
  return not_filter(day_filter(5));
}

static Filter *parse_avoid_weekend(Parser *p) {
  if (!match_word(p, "avoid_weekend"))
    return NULL;
  Filter *sat = day_filter(6);
  Filter *sun = day_filter(0);
  return not_filter(or_filter(sat, sun));
}

static Filter *parse_holidays(Parser *p) {
  if (!match_word(p, "holidays"))
    return NULL;
  return make_filter(FILTER_HOLIDAY);
}


static Filter *parse_before(Parser *p) {
  if (!match_word(p, "before"))
    return NULL;
  time_t t;
  if (!parse_date(p, &t))
    return NULL;
  Filter *f = make_filter(FILTER_BEFORE_TIME);
  f->data.time_value = t;
  return f;
}

static Filter *parse_after(Parser *p) {
  if (!match_word(p, "after"))
    return NULL;
  time_t t;
  if (!parse_date(p, &t))
    return NULL;
  Filter *f = make_filter(FILTER_AFTER_TIME);
  f->data.time_value = t;
  return f;
}


static Filter *parse_spaced(Parser *p) {

  if (!match_word(p, "spaced"))

    return NULL;

  int val = 0;

  if (!parse_signed_int(p, &val))

    return NULL;

  // Allow optional whitespace between number and unit, and also accept attached units
  skip_ws(p);
  if (match_ci(p, "minutes") || match_ci(p, "minute")) {

    // val already in minutes

  } else if (match_ci(p, "hours") || match_ci(p, "hour")) {

    val *= 60;

  } else {

    // Unknown unit

    return NULL;

  }

  Filter *f = make_filter(FILTER_MIN_DISTANCE);

  f->data.minutes = val;

  return f;

}


static Filter *parse_expr(Parser *p); // forward

static Filter *parse_primary(Parser *p) {
  skip_ws(p);
  if (match_char(p, '(')) {
    Filter *inside = parse_expr(p);
    match_char(p, ')'); // best-effort close
    return inside ? inside : make_filter(FILTER_NONE);
  }
  {
    
        size_t save = p->pos;

        Filter *f = NULL;

        if ((f = parse_weekdays(p))) return f;

        p->pos = save;

        if ((f = parse_holidays(p))) return f;

        p->pos = save;

        if ((f = parse_on(p))) return f;

        p->pos = save;

        if ((f = parse_before(p))) return f;

        p->pos = save;

        if ((f = parse_after(p))) return f;

        p->pos = save;

        if ((f = parse_spaced(p))) return f;
        p->pos = save;
        if ((f = parse_business_days(p))) return f;
        p->pos = save;
        if ((f = parse_avoid_friday(p))) return f;
        p->pos = save;
        if ((f = parse_avoid_weekend(p))) return f;

  }
  return make_filter(FILTER_NONE);

}

static Filter *parse_unary(Parser *p) {
  skip_ws(p);
  if (match_word(p, "not")) {
    Filter *operand = parse_unary(p);
    return not_filter(operand ? operand : make_filter(FILTER_NONE));
  }
  return parse_primary(p);
}

static Filter *parse_and(Parser *p) {
  Filter *left = parse_unary(p);
  while (1) {
    size_t save = p->pos;
    if (match_word(p, "and")) {
      Filter *right = parse_unary(p);
      left = and_filter(left, right ? right : make_filter(FILTER_NONE));
    } else {
      p->pos = save;
      break;
    }
  }
  return left;
}

static Filter *parse_or(Parser *p) {
  Filter *left = parse_and(p);
  while (1) {
    size_t save = p->pos;
    if (match_word(p, "or")) {
      Filter *right = parse_and(p);
      left = or_filter(left, right ? right : make_filter(FILTER_NONE));
    } else {
      p->pos = save;
      break;
    }
  }
  return left;
}

static Filter *parse_expr(Parser *p) {
  return parse_or(p);
}

Filter *parse_filter(const char *filter_str) {
  if (!filter_str || strlen(filter_str) == 0) {
    return make_filter(FILTER_NONE);
  }
  Parser parser;
  parser.s = filter_str;
  parser.pos = 0;
  parser.len = strlen(filter_str);
  Filter *result = parse_expr(&parser);
  if (!result) {
    return make_filter(FILTER_NONE);
  }
  return result;
}


time_t find_optimal_time(EventList *list, int duration_minutes,
                         Filter *filter) {
  time_t now = time(NULL);
  time_t candidate = now;
  int duration_seconds = duration_minutes * 60;
  int max_iterations = 365 * 24 * 60 / 15;
  int iterations = 0;

  while (iterations < max_iterations) {
    iterations++;

    if (filter) {
      int skip_minutes = get_next_valid_minutes(filter, candidate, list);

      if (skip_minutes < 0) {
        return -1; // No valid time found within filter constraints
      }

      if (skip_minutes > 0) {
        candidate += skip_minutes * 60;
        continue;
      }
    }

    bool conflict = false;
    Event *current = list->head;
    int next_event_minutes = -1;

    while (current) {
      if ((candidate >= current->start_time && candidate < current->end_time) ||
          (candidate + duration_seconds > current->start_time &&
           candidate + duration_seconds <= current->end_time) ||
          (candidate <= current->start_time &&
           candidate + duration_seconds >= current->end_time)) {
        conflict = true;

        int minutes_to_end =
            (int)(difftime(current->end_time, candidate) / 60) + 1;
        if (next_event_minutes < 0 || minutes_to_end < next_event_minutes) {
          next_event_minutes = minutes_to_end;
        }
      }
      current = current->next;
    }

    if (!conflict) {
      return candidate;
    }

    if (next_event_minutes > 0) {
      candidate += next_event_minutes * 60;
    } else {
      candidate += 900; // 15 minutes
    }
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
