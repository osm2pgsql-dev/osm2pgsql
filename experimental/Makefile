CFLAGS += -g -O2 -Wall -Wextra -Wsign-compare -Wpointer-sign 
CFLAGS += $(shell xml2-config --cflags)
CFLAGS += $(shell geos-config --cflags)

CXXFLAGS += -g -O2 -Wall -DGEOS_INLINE

LDFLAGS += $(shell xml2-config --libs) 
LDFLAGS += $(shell geos-config --libs) 
LDFLAGS += -lpq -g
LDFLAGS += -lbz2
LDFLAGS += -lproj

SRCS:=$(wildcard *.c) $(wildcard *.cpp)
OBJS:=$(SRCS:.c=.o)
OBJS:=$(OBJS:.cpp=.o)
DEPS:=$(SRCS:.c=.d)
DEPS:=$(DEPS:.cpp=.d)

APPS:=osm2pgsql

.PHONY: all clean

all: $(APPS)

clean: 
	rm -f  $(APPS) $(OBJS) $(DEPS)

%.d: %.c
	@set -e; rm -f $@; \
	$(CC) -MM $(CPPFLAGS) $< > $@.$$$$; \
	sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' < $@.$$$$ > $@; \
	rm -f $@.$$$$

-include $(DEPS)
	
osm2pgsql: osm2pgsql.o build_geometry.o middle-pgsql.o keyvals.o output-pgsql.o middle-ram.o input.o UTF8sanitizer.o reprojection.o

