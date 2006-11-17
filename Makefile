#CFLAGS += -g
CFLAGS += -O2
CFLAGS += $(shell xml2-config --cflags)
LDFLAGS += $(shell xml2-config --libs)

APPS:=osm2pgsql

.PHONY: all clean

all: $(APPS)

clean: 
	rm -f  $(APPS)
