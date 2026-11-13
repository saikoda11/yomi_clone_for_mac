CC = clang++
CFLAGS = -std=c++17 -Wall -I/opt/homebrew/opt/raylib/include -L/opt/homebrew/opt/raylib/lib -lraylib -framework OpenGL -framework Cocoa -framework IOKit
FILES = camera.cpp main.cpp
GAME_FILES = game.cpp sim.cpp ai.cpp draw.cpp

all: build game

build:
	$(CC) $(CFLAGS) -o main $(FILES)

game:
	$(CC) $(CFLAGS) -O2 -o secondguess $(GAME_FILES)

run:
	./main

play: game
	./secondguess

clean:
	rm -rf main secondguess

.PHONY: all build game run play clean
