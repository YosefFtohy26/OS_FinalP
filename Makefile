CC      = gcc
CFLAGS  = -Wall -Wextra -pedantic -std=c11
TARGET  = myShell

.PHONY: all clean

all: $(TARGET)

$(TARGET): myShell.c
	$(CC) $(CFLAGS) -o $(TARGET) myShell.c

clean:
	rm -f $(TARGET)
