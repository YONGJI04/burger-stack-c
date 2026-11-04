CC = gcc
CPPFLAGS = -Iinclude
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -Werror -O2
LDLIBS = -lm
SOURCES = src/main.c src/game.c src/ranking.c src/terminal.c src/art.c src/ui.c
HEADERS = $(wildcard include/*.h)
CORE = src/game.c src/ranking.c src/terminal.c src/art.c

.PHONY: all run test sanitize clean demo windows soak
all: burger

burger: $(SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SOURCES) -o $@ $(LDLIBS)

build:
	mkdir -p build

build/test_game: tests/test_game.c $(CORE) $(HEADERS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_game.c $(CORE) -o $@ $(LDLIBS)

test: burger build/test_game
	./build/test_game
	python3 tests/test_terminal.py ./burger

build/test_sanitize: tests/test_game.c $(CORE) $(HEADERS) | build
	$(CC) $(CPPFLAGS) -std=c11 -Wall -Wextra -Wpedantic -Werror -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined tests/test_game.c $(CORE) -o $@ $(LDLIBS)

build/burger_sanitize: $(SOURCES) $(HEADERS) | build
	$(CC) $(CPPFLAGS) -std=c11 -Wall -Wextra -Wpedantic -Werror -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined $(SOURCES) -o $@ $(LDLIBS)

sanitize: build/test_sanitize build/burger_sanitize
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 ./build/test_sanitize
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 python3 tests/test_terminal.py ./build/burger_sanitize

run: burger
	./burger

demo: burger
	./burger --demo

windows: | build
	x86_64-w64-mingw32-gcc $(CPPFLAGS) $(CFLAGS) -static $(SOURCES) -o build/burger.exe $(LDLIBS)

soak: burger build/test_game
	python3 scripts/soak.py --hours 10

clean:
	rm -f burger
	rm -rf build
