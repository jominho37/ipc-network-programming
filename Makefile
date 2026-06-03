CC = gcc
CFLAGS = -Wall
TARGET = webserver

all: $(TARGET)

$(TARGET): webserver.c
	$(CC) $(CFLAGS) -o $(TARGET) webserver.c

clean:
	rm -f $(TARGET)
