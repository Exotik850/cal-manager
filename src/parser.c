#include "parser.h"
#include <stdbool.h>
#include <string.h>
#include <time.h>

// Parser combinator implementation for filter expressions
// Grammar (informal, precedence: NOT > AND > OR):
//   expr       := or_expr
//   or_expr    := and_expr (OR and_expr)*
//   and_expr   := unary (AND unary)*
//   unary      := NOT unary | primary
//   primary    := '(' expr ')' | weekdays | holidays | 'on' day_list
//               | 'before' datetime | 'after' datetime | 'spaced' duration
//   day_list   := day_name (',' day_name)*
//   duration   := signed_int ('minute'|'minutes'|'hour'|'hours')
//   datetime   := date [time] | time
//   date       := YYYY '-' M '-' D
//   time       := HH ':' MM [':' SS]
//   day_name   := Sunday|Monday|Tuesday|Wednesday|Thursday|Friday|Saturday

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
  if (!(next == '\0' || is_space(next) || next == '(' || next == ')' ||
        next == ',')) {
    return false;
  }
  p->pos = start + i;
  return true;
}

static bool match_ci(Parser *p, const char *lit) {
  // Same as match_word but without boundary requirements (for units like
  // minutes/hours attached to numbers)
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

static time_t make_date_time(int year, int month, int day, int hour, int minute,
                             int second) {
  struct tm tmv;
  memset(&tmv, 0, sizeof(tmv));
  tmv.tm_year = year - 1900;
  tmv.tm_mon = month - 1;
  tmv.tm_mday = day;
  tmv.tm_hour = hour;
  tmv.tm_min = minute;
  tmv.tm_sec = second;
  tmv.tm_isdst = -1;
  return mktime(&tmv);
}

// Parse date in the form YYYY-MM-DD
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
  *out = make_date_time(y, m, d, 0, 0, 0);
  return true;
}

// Parse time in the form HH:MM[:SS]
static bool parse_time(Parser *p, int *hour, int *minute, int *second) {
  skip_ws(p);
  size_t save = p->pos;
  int h = 0, m = 0, s = 0;

  if (!parse_int(p, &h))
    return false;
  if (!match_char(p, ':')) {
    p->pos = save;
    return false;
  }
  if (!parse_int(p, &m)) {
    p->pos = save;
    return false;
  }
  if (peek_char(p, ':')) {
    size_t save2 = p->pos++;
    if (!parse_int(p, &s)) {
      p->pos = save2;
      s = 0;
    }
  }
  if (hour)
    *hour = h;
  if (minute)
    *minute = m;
  if (second)
    *second = s;
  return true;
}

// Parse datetime as either `date [time]` or `time`
static bool parse_datetime(Parser *p, time_t *out, bool *has_date) {
  size_t save = p->pos;
  int h = 0, m = 0, s = 0;
  time_t date_t;
  if (parse_date(p, &date_t)) {
    if (has_date)
      *has_date = true;
    size_t save_time = p->pos;
    if (parse_time(p, &h, &m, &s)) {
      date_t += h * 3600 + m * 60 + s;
    }
    p->pos = save_time;
    *out = date_t;
    return true;
  }

  p->pos = save;
  if (!parse_time(p, &h, &m, &s))
    return false;
  if (has_date)
    *has_date = false;

  // Use epoch date for time-only
  *out = make_date_time(1970, 1, 1, h, m, s);
  return true;
}

static int day_name_to_wday(Parser *p) {
  skip_ws(p);
  if (match_word(p, "Sunday"))
    return 0;
  if (match_word(p, "Monday"))
    return 1;
  if (match_word(p, "Tuesday"))
    return 2;
  if (match_word(p, "Wednesday"))
    return 3;
  if (match_word(p, "Thursday"))
    return 4;
  if (match_word(p, "Friday"))
    return 5;
  if (match_word(p, "Saturday"))
    return 6;
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
      if (w2 < 0)
        break;
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
  return and_filter(acc, not_filter(make_filter(FILTER_HOLIDAY)));
}

static Filter *parse_weekend(Parser *p) {
  if (!match_word(p, "weekend"))
    return NULL;
  Filter *sat = day_filter(6);
  Filter *sun = day_filter(0);
  return or_filter(sat, sun);
}

static Filter *parse_holidays(Parser *p) {
  if (!match_word(p, "holidays"))
    return NULL;
  return make_filter(FILTER_HOLIDAY);
}

static Filter *parse_business_hours(Parser *p) {
  if (!match_word(p, "business_hours"))
    return NULL;
  Filter *after_nine = make_filter(FILTER_AFTER_TIME);
  after_nine->data.time_value = make_date_time(1970, 1, 1, 9, 0, 0);
  Filter *before_five = make_filter(FILTER_BEFORE_TIME);
  before_five->data.time_value = make_date_time(1970, 1, 1, 17, 0, 0);
  return and_filter(after_nine, before_five);
}

static Filter *parse_before(Parser *p) {
  if (!match_word(p, "before"))
    return NULL;

  time_t t;
  bool has_date = false;
  if (!parse_datetime(p, &t, &has_date))
    return NULL;

  Filter *f =
      make_filter(has_date ? FILTER_BEFORE_DATETIME : FILTER_BEFORE_TIME);
  f->data.time_value = t;
  return f;
}

static Filter *parse_after(Parser *p) {
  if (!match_word(p, "after"))
    return NULL;
  
  time_t t;
  bool has_date = false;
  if (!parse_datetime(p, &t, &has_date))
    return NULL;

  Filter *f = make_filter(has_date ? FILTER_AFTER_DATETIME : FILTER_AFTER_TIME);
  f->data.time_value = t;
  return f;
}

static Filter *parse_spaced(Parser *p) {
  if (!match_word(p, "spaced"))
    return NULL;

  int val = 0;
  if (!parse_signed_int(p, &val))
    return NULL;
  // Allow optional whitespace between number and unit, and also accept attached
  // units
  skip_ws(p);
  if (match_ci(p, "minutes") || match_ci(p, "minute") || match_ci(p, "mins") ||
      match_ci(p, "min") || match_ci(p, "m")) {
    // already in minutes
  } else if (match_ci(p, "hours") || match_ci(p, "hour") ||
             match_ci(p, "hrs") || match_ci(p, "hr") || match_ci(p, "h")) {
    val *= 60;
  } else if (match_ci(p, "days") || match_ci(p, "day") || match_ci(p, "d")) {
    val *= 1440;
  } else {
    // Assume minutes if no unit specified
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

  size_t save = p->pos;
  Filter *f = NULL;

#define parse(name)                                                            \
  if ((f = parse_##name(p)))                                                   \
    return f;                                                                  \
  p->pos = save;
  parse(weekdays);
  parse(holidays);
  parse(on);
  parse(before);
  parse(after);
  parse(spaced);
  parse(business_days);
  parse(business_hours);
  parse(weekend);
#undef parse

  return make_filter(FILTER_NONE);
}

static Filter *parse_unary(Parser *p) {
  skip_ws(p);
  if (!match_word(p, "not")) {
    return parse_primary(p);
  }
  Filter *operand = parse_unary(p);
  return not_filter(operand ? operand : make_filter(FILTER_NONE));
}

static Filter *parse_and(Parser *p) {
  Filter *left = parse_unary(p);
  while (1) {
    size_t save = p->pos;
    if (!match_word(p, "and")) {
      p->pos = save;
      break;
    }
    Filter *right = parse_unary(p);
    left = and_filter(left, right ? right : make_filter(FILTER_NONE));
  }
  return left;
}

static Filter *parse_or(Parser *p) {
  Filter *left = parse_and(p);
  while (1) {
    size_t save = p->pos;
    if (!match_word(p, "or")) {
      p->pos = save;
      break;
    }
    Filter *right = parse_and(p);
    left = or_filter(left, right ? right : make_filter(FILTER_NONE));
  }
  return left;
}

static Filter *parse_expr(Parser *p) { return parse_or(p); }

Filter *parse_filter(const char *filter_str) {
  size_t len = strlen(filter_str);
  if (!filter_str || len == 0) {
    return make_filter(FILTER_NONE);
  }
  Parser parser = {filter_str, 0, len};
  return parse_expr(&parser);
}
