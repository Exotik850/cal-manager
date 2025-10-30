# Cal Manager

A simple calendar manager written in C.

## Features

- Add, remove, and view events on specific dates.
- Save and load events from a file.
- Simple command-line interface.
- Basic error handling for invalid inputs.
- Unit tests for core functionalities.

## Compilation and Usage

### Compiling main binary

To compile the main application, run this command for the GCC compiler:

```ps
gcc ./src/*.c -o main.exe
```

For Clang, use:

```ps
clang ./src/*.c -o main.exe
```

For MSVC, use:

```ps
cl ./src\*.c /Fe:main.exe
```

Then run with

```ps
./main.exe
```

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
