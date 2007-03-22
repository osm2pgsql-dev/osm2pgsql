CFLAGS += -g 
CFLAGS += -O2 -Wall
CFLAGS += $(shell xml2-config --cflags)
CFLAGS += $(shell geos-config --cflags)
CXXFLAGS += -g -O2 -Wall -DGEOS_INLINE
LDFLAGS += $(shell xml2-config --libs) 
LDFLAGS += $(shell geos-config --libs) 

APPS:=osm2pgsql

.PHONY: all clean

all: $(APPS)

clean: 
	rm -f  $(APPS) osm2pgsql.o build_geometry.o

osm2pgsql: osm2pgsql.o build_geometry.o
