/* Data types to hold OSM node, segment, way data */

#ifndef OSMTYPES_H
#define OSMTYPES_H


struct osmNode {
    double lon;
    double lat;
};

/* exit_nicely - called to cleanup after fatal error */
void exit_nicely(void);

#endif
