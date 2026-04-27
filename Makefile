CC = clang++
CFLAGS = -std=c++17 -Wall -I/opt/homebrew/opt/raylib/include -L/opt/homebrew/opt/raylib/lib -lraylib -framework OpenGL -framework Cocoa -framework IOKit
FILES = main.cpp

all: build run

build:
	$(CC) $(CFLAGS) -o main $(FILES)

run:
	./main

clean:
	rm -rf main
