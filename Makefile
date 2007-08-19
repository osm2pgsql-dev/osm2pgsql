PACKAGE:=osm2pgsql
VERSION:=0.03
SVN:=$(shell date +%Y%m%d)

CFLAGS += -O2 -Wall -Wextra
CFLAGS += $(shell xml2-config --cflags)
CFLAGS += $(shell geos-config --cflags)
CFLAGS += -I$(shell pg_config --includedir)
CFLAGS += -DVERSION=\"$(VERSION)-$(SVN)\"

CXXFLAGS += -O2 -Wall -DGEOS_INLINE

LDFLAGS += $(shell xml2-config --libs) 
LDFLAGS += $(shell geos-config --libs)
LDFLAGS += -L$(shell pg_config --libdir) -lpq
LDFLAGS += -lbz2
LDFLAGS += -lproj

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

%.d: %.c
	@set -e; rm -f $@; \
	$(CC) -MM $(CPPFLAGS) $< > $@.$$$$; \
	sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' < $@.$$$$ > $@; \
	rm -f $@.$$$$

-include $(DEPS)

osm2pgsql: $(OBJS)

$(PACKAGE).spec: $(PACKAGE).spec.in
	sed -e "s/@PACKAGE@/$(PACKAGE)/g; s/@VERSION@/$(VERSION)/g; s/@SVN@/$(SVN)/g;" $^ > $@

$(PACKAGE)-$(VERSION)-$(SVN).tar.bz2: $(PACKAGE).spec
	rm -fR tmp
	mkdir -p tmp/osm2pgsql
	cp -p Makefile *.[ch] *.cpp readme.txt osm2pgsql.spec* osm2pgsql-svn.sh tmp/osm2pgsql
	cp -p osm2pgsql.spec tmp/
	tar cjf $@ -C tmp .
	rm -fR tmp

rpm: $(PACKAGE)-$(VERSION)-$(SVN).tar.bz2
	rpmbuild -ta $^
