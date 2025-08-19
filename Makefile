CC = gcc
CFLAGS = -Wall -pthread
TARGET = main

all: $(TARGET)

$(TARGET): main.c
	$(CC) $(CFLAGS) $^ -o $@

clean:
	rm -f $(TARGET)
