CFLAGS += -g
CFLAGS += -O2 -Wall
CFLAGS += $(shell xml2-config --cflags)
LDFLAGS += $(shell xml2-config --libs)

APPS:=osm2pgsql

.PHONY: all clean

all: $(APPS)

clean: 
	rm -f  $(APPS) osm2pgsql.o bst.o avl.o

osm2pgsql: osm2pgsql.o bst.o avl.o
