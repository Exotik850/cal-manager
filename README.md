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

Then run with

```ps
./main.exe
```

### Compiling tests

For running tests, run the following command:

```ps
gcc ./tests/test.c -o ./test.exe && ./test.exe
```
