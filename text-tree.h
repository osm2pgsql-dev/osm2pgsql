#ifndef TEXT_TREE_H
#define TEXT_TREE_H

#include "rb.h"

struct tree_context {
    struct rb_table *table;
};

extern struct tree_context *tree_ctx;

struct text_node {
    char *str;
    int ref;
};

struct tree_context *text_init(void);
void text_exit(void);
const char *text_get(struct tree_context *context, const char *text);
void text_release(struct tree_context *context, const char *text);
#endif
