/* text-tree.c
 *
 * Storage of reference counted text strings
 * used by keyvals.c to store the key/value strings
 */
#define _GNU_SOURCE
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include "text-tree.h"

struct tree_context *tree_ctx = NULL;

int text_compare(const void *pa, const void *pb, void *rb_param)
{
    struct text_node *a = (struct text_node *)pa;
    struct text_node *b = (struct text_node *)pb;

    rb_param = NULL;
    return strcmp(a->str, b->str);
}

struct tree_context *text_init(void)
{
    struct tree_context *context;
    struct rb_table *table = rb_create (text_compare, NULL, NULL);

    assert(table);
    context = calloc(1, sizeof(*context));
    assert(context);
    context->table = table;
    tree_ctx = context;
    return context;
}

void text_free(void *pa, void *rb_param)
{
    struct text_node *a = (struct text_node *)pa;
    rb_param = NULL;
    free(a->str);
    free(a);
}

const char *text_get(struct tree_context *context, const char *text)
{
    struct text_node *node, *dupe;

    node = malloc(sizeof(*node));
    assert(node);

    node->str = strdup(text);
    assert(node->str);
    node->ref = 0;
    dupe = rb_insert(context->table, (void *)node);
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


void text_release(struct tree_context *context, const char *text)
{
    struct text_node *node, find;

    find.str = (char *)text;
    find.ref = 0;
    node = rb_find(context->table, (void *)&find);
    if (!node) {
        fprintf(stderr, "failed to find '%s'\n", text);
        return;
    }
    node->ref--;
    if (!node->ref) {
        rb_delete (context->table, &find);
        free(node->str);
        free(node);
    }
}

void text_exit(void)
{
    struct tree_context *context = tree_ctx;
    rb_destroy(context->table, text_free);
    free(context);
    tree_ctx = NULL;
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
