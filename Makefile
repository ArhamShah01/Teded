# Makefile for teded – a terminal text editor
#
# Usage:
#   make          – build the editor (default target)
#   make clean    – remove compiled objects and the binary
#   make run      – build and run without a file (empty buffer)
#   make test     – build and open this Makefile as a test file

CC      := gcc
CFLAGS  := -Wall -Wextra -Wpedantic -std=c99 -g
TARGET  := teded
SRCS    := editor.c buffer.c file.c input.c screen.c
OBJS    := $(SRCS:.c=.o)
HEADERS := editor.h

# ---- Default target: link the binary ----------------------------------- #
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

# ---- Compile each source file into an object file --------------------- #
%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c -o $@ $<

# ---- Phony targets ----------------------------------------------------- #
.PHONY: clean run test

clean:
	rm -f $(OBJS) $(TARGET)

run: $(TARGET)
	./$(TARGET)

test: $(TARGET)
	./$(TARGET) Makefile
