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
//               | 'before' date | 'after' date | 'spaced' duration
//   day_list   := day_name (',' day_name)*
//   duration   := signed_int ('minute'|'minutes'|'hour'|'hours')
//   date       := YYYY '-' M '-' D
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
  return and_filter(acc, make_filter(FILTER_HOLIDAY));
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
    return NULL; // unknown unit
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

  if ((f = parse_weekdays(p)))
    return f;
  p->pos = save;

  if ((f = parse_holidays(p)))
    return f;
  p->pos = save;

  if ((f = parse_on(p)))
    return f;
  p->pos = save;

  if ((f = parse_before(p)))
    return f;
  p->pos = save;

  if ((f = parse_after(p)))
    return f;
  p->pos = save;

  if ((f = parse_spaced(p)))
    return f;
  p->pos = save;
  if ((f = parse_business_days(p)))
    return f;
  p->pos = save;
  if ((f = parse_weekend(p)))
    return f;
  p->pos = save;
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

static Filter *parse_expr(Parser *p) { return parse_or(p); }

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
