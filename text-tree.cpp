/* text-tree.c
 *
 * Storage of reference counted text strings
 * used by keyvals.c to store the key/value strings
 */

#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include "text-tree.hpp"

namespace
{
    int text_compare(const void *pa, const void *pb, void *rb_param)
    {
        struct text_node *a = (struct text_node *)pa;
        struct text_node *b = (struct text_node *)pb;

        rb_param = NULL;
        return strcmp(a->str, b->str);
    }

    void text_free(void *pa, void *rb_param)
    {
        struct text_node *a = (struct text_node *)pa;
        rb_param = NULL;
        free(a->str);
        free(a);
    }
}

text_tree::text_tree()
{
    table = rb_create (text_compare, NULL, NULL);
    assert(table);
}

const char *text_tree::text_get(const char *text)
{
    struct text_node *node, *dupe;

    node = (struct text_node *)malloc(sizeof(*node));
    assert(node);

    node->str = strdup(text);
    assert(node->str);
    node->ref = 0;
    dupe = (struct text_node *)rb_insert(table, (void *)node);
    if (dupe) {
        free(node->str);
        free(node);
        dupe->ref++;
        return dupe->str;
    } else {
        node->ref++;
        return node->str;
    }
}

void text_tree::text_release(const char *text)
{
    struct text_node *node, find;

    find.str = (char *)text;
    find.ref = 0;
    node = (struct text_node *)rb_find(table, (void *)&find);
    if (!node) {
        fprintf(stderr, "failed to find '%s'\n", text);
        return;
    }
    node->ref--;
    if (!node->ref) {
        rb_delete (table, &find);
        free(node->str);
        free(node);
    }
}

text_tree::~text_tree()
{
    rb_destroy(table, text_free);
}
#if 0
int main(int argc, char **argv)
{
    struct tree_context *ctx = text_init();

    printf("%1$p %1$s\n", text_get(ctx, "Hello"));
    printf("%1$p %1$s\n", text_get(ctx, "Hello"));
    printf("%1$p %1$s\n", text_get(ctx, "World"));

    text_release(ctx,"Hello");
    text_release(ctx,"Hello");
    text_release(ctx,"World");
    text_release(ctx,"Hello");

    text_exit(ctx);
    return 0;
}
#endif
