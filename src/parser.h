#ifndef PARSER_H
#define PARSER_H
#include "filter.h"

typedef struct {
  const char *s;
  size_t pos;
  size_t len;
} Parser;

Filter *parse_filter(const char *filter_str);

#endif // PARSER_H
