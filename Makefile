CC=gcc
CFLAGS= -Wall -I/usr/include/libxml2/ `xml2-config --cflags`
LIBS= `xml2-config --libs` -lpthread

all: url-engine 
	
url-engine: url_engine.o
	$(CC) $(LIBS) -o url-engine url_engine.o 

url_engine.o: url_engine.c
	$(CC) -c $(CFLAGS) url_engine.c

clean:
	rm -rf *.o url-engine 


