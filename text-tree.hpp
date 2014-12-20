#ifndef TEXT_TREE_H
#define TEXT_TREE_H

#include "rb.hpp"

struct text_tree {
    struct rb_table *table;

    text_tree();
    ~text_tree();
    const char *text_get(const char *text);
    void text_release(const char *text);
};

struct text_node {
    char *str;
    int ref;
};


#endif
