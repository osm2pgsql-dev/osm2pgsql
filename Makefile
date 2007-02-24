CFLAGS += -g 
CFLAGS += -O2 -Wall
CFLAGS += $(shell xml2-config --cflags)
CXXFLAGS += -g -O2 -Wall -DGEOS_INLINE -I/opt/geos/include
LDFLAGS += $(shell xml2-config --libs) -L/opt/geos/lib -lgeos

APPS:=osm2pgsql

.PHONY: all clean

all: $(APPS)

clean: 
	rm -f  $(APPS) osm2pgsql.o bst.o avl.o build_geometry.o

osm2pgsql: osm2pgsql.o bst.o avl.o build_geometry.o

