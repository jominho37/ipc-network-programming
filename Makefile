CC = gcc
CFLAGS = -Wall

all: webserver web_app_server

webserver: webserver.c
	$(CC) $(CFLAGS) -o webserver webserver.c -lpthread

web_app_server: web_app_server.c
	$(CC) $(CFLAGS) -o web_app_server web_app_server.c -lpthread

clean:
	rm -f webserver web_app_server
