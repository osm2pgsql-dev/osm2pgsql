/* Common output layer interface */

/* Each output layer must provide methods for 
 * storing:
 * - Nodes (Points of interest etc)
 * - Way geometries
 * Associated tags: name, type etc. 
*/

#ifndef OUTPUT_H
#define OUTPUT_H

#include "keyvals.h"
#include "middle.h"

struct output_t {
    int (*start)(const char *db, int append);
    void (*stop)(int append);
    void (*cleanup)(void);
    void (*process)(struct middle_t *mid);
    int (*node)(int id, struct keyval *tags, double node_lat, double node_lon);
    int (*way)(int id, struct keyval *tags, struct osmNode *nodes, int count);
};

#endif
