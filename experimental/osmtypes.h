/* Data types to hold OSM node, segment, way data */

#ifndef OSMTYPES_H
#define OSMTYPES_H


struct osmNode {
    double lon;
    double lat;
};

struct osmSegment {
    int from;
    int to;
};

struct osmSegLL {
    double lon0;
    double lat0;
    double lon1;
    double lat1;
};

/* exit_nicely - called to cleanup after fatal error */
void exit_nicely(void);

#endif
