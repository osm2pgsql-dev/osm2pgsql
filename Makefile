PACKAGE:=osm2pgsql
VERSION:=0.65
SVN:=$(shell svnversion)

CC = gcc
CXX = g++

CFLAGS += -g -O2 -Wall -Wextra
CFLAGS += $(shell xml2-config --cflags)
CFLAGS += $(shell geos-config --cflags)
CFLAGS += -I$(shell pg_config --includedir)
CFLAGS += -DVERSION=\"$(VERSION)-$(SVN)\"
CFLAGS += -DHAVE_PTHREAD
CC=gcc
CXX=g++

CXXFLAGS += -g -O2 -Wall -DGEOS_INLINE $(CFLAGS)
CXXFLAGS += $(shell geos-config --cflags)

LDFLAGS += $(shell xml2-config --libs) 
LDFLAGS += $(shell geos-config --libs)
LDFLAGS += -L$(shell pg_config --libdir) -lpq
LDFLAGS += -lbz2 -lz
LDFLAGS += -g -lproj
LDFLAGS += -lstdc++
LDFLAGS += -lpthread

SRCS:=$(wildcard *.c) $(wildcard *.cpp)
OBJS:=$(SRCS:.c=.o)
OBJS:=$(OBJS:.cpp=.o)
DEPS:=$(SRCS:.c=.d)
DEPS:=$(DEPS:.cpp=.d)

APPS:=osm2pgsql

.PHONY: all clean $(PACKAGE).spec

all: $(APPS)

clean: 
	rm -f $(APPS) $(OBJS) $(DEPS)
	rm -f $(PACKAGE)-*.tar.bz2
	rm -f osm2pgsql.spec

%.d: %.c
	@set -e; rm -f $@; \
	$(CC) -MM $(CFLAGS) $< > $@.$$$$; \
	sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' < $@.$$$$ > $@; \
	rm -f $@.$$$$

-include $(DEPS)

osm2pgsql: $(OBJS)
	$(CXX) -o $@ $^ $(LDFLAGS)

$(PACKAGE).spec: $(PACKAGE).spec.in
	sed -e "s/@PACKAGE@/$(PACKAGE)/g; s/@VERSION@/$(VERSION)/g; s/@SVN@/$(SVN)/g;" $^ > $@

$(PACKAGE)-$(VERSION)-$(SVN).tar.bz2: $(PACKAGE).spec
	rm -fR tmp
	mkdir -p tmp/osm2pgsql
	cp -p Makefile *.[ch] *.cpp README.txt osm2pgsql-svn.sh tmp/osm2pgsql
	cp -p osm2pgsql.spec tmp/
	tar cjf $@ -C tmp .
	rm -fR tmp

rpm: $(PACKAGE)-$(VERSION)-$(SVN).tar.bz2
	rpmbuild -ta $^
