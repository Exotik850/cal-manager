# Cal Manager

A simple calendar manager written in C.

## Features

- Add, remove, and view events on specific dates.
- Save and load events from a file.
- Simple command-line interface.
- Basic error handling for invalid inputs.
- Unit tests for core functionalities.

## Filter DSL

The program supports a small, case‑insensitive, whitespace‑insensitive DSL to express event filters. The implementation lives in src/parser.c. The parser uses recursive descent with precedence: NOT > AND > OR. Parentheses can be used for explicit grouping.

### Grammar (informal, precedence: NOT > AND > OR):
- expr       := or_expr
- or_expr    := and_expr (OR and_expr)*
- and_expr   := unary (AND unary)*
- unary      := NOT unary | primary
- primary    := '(' expr ')'
                | weekdays
                | holidays
                | 'on' day_list
                | 'before' datetime
                | 'after' datetime
                | 'spaced' duration
- day_list   := day_name (',' day_name)*
- duration   := signed_int ('minute'|'minutes'|'hour'|'hours')
- datetime   := date [time] | time
- date       := YYYY '-' M '-' D
- time       := HH ':' MM [':' SS]
- day_name   := Sunday|Monday|Tuesday|Wednesday|Thursday|Friday|Saturday

### Available keywords / meanings:
- weekdays        => Monday through Friday
- business_days   => Monday through Friday excluding holidays
- weekend         => Saturday OR Sunday
- holidays        => Matches holiday events
- business_hours  => Time between 09:00 and 17:00 (local time base, time‑only)
- on <days>       => Specific day(s) of week, e.g. on Monday,Wednesday,Friday
- before X        => Events strictly before X (date+time ⇒ datetime compare; time only ⇒ time-of-day compare)
- after  X        => Events strictly after X (same datetime vs time-of-day rule)
- spaced N[unit]  => Minimum distance (in minutes) between events; negative allowed (shifts tolerance)

### Date/time:
- Date: 2024-7-03, 2024-07-3, 2024-07-03 are all accepted (no width enforcement).
- Time: 14:30, 09:05:10. Seconds optional.

A datetime with a date produces a *DATETIME* comparison. A lone time (e.g. 08:00) produces a *TIME-OF-DAY* comparison using an epoch date placeholder.
Examples:
- before 2024-12-25
- after 2024-12-25 09:30
- before 14:00
- after 08:15:30

Duration with spaced:
- spaced 15         (15 minutes)
- spaced 2 hours    (120 minutes)
- spaced -10m       (allows negative)
- spaced 1day       (1440 minutes)

If no unit is specified, minutes are assumed. Units may be attached directly (e.g. 2hours) or separated by whitespace.

### Logical composition:
- weekdays and not holidays
- (on Monday,Tuesday or on Friday) and after 09:00 and before 17:00
- weekend or holidays
- spaced 30 and after 2024-10-01

### Error / fallback behavior:
- Unrecognized fragments inside parentheses or as primaries produce a no-op (FILTER_NONE) rather than aborting the whole parse.
- The parser does not emit diagnostics; malformed input tends to yield a broader or empty match silently.

If adding new keywords, follow the pattern in parser.c: create a parse_<keyword>() returning a Filter* and insert it into parse_primary() before the fallback FILTER_NONE.

## Compilation and Usage

### Compiling main binary

To compile the main application, run this command for the GCC compiler:

```ps
gcc ./src/*.c -o main.exe
```

Then run with

```ps
./main.exe
```

to show the help message, which lists available commands and options.

### Compiling tests

For running tests, run the following command:

```ps
gcc ./tests/test.c -o ./test.exe && ./test.exe
```

## File Structure

- `src/`: Contains the source code files for the calendar manager.
- `tests/`: Contains unit tests for the calendar manager.

```ps
+---cal-manager
|   LICENSE.md
|   README.md
|
+---src
|       calendar.c // calendar manager implementation
|       calendar.h
|       event_list.c // event list implementation
|       event_list.h
|       filter.c // filter implementation
|       filter.h
|       main.c // main application
|       parser.c // parser implementation
|       parser.h
|
+---tests
        test.c
        test_calendar.h
        test_event_list.h
        test_filter.h
        test_parse.h
```

## License

This project is licensed under the MIT License. See the [LICENSE](./LICENSE.md) file for details.
