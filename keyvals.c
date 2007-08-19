/* Common key-value list processing
 *
 * Used as a small general purpose store for 
 * tags, segment lists etc 
 *
 */
#define USE_TREE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "keyvals.h"

#ifdef USE_TREE
#include "text-tree.h"
#endif

void initList(struct keyval *head)
{
    assert(head);

    head->next = head;
    head->prev = head;
    head->key = NULL;
    head->value = NULL;
}

void freeItem(struct keyval *p)
{
    if (!p) 
        return;

#ifdef USE_TREE
    text_release(tree_ctx, p->key);
    text_release(tree_ctx, p->value);
#else
    free(p->key);
    free(p->value);
#endif
    free(p);
}


unsigned int countList(struct keyval *head) 
{
    struct keyval *p;
    unsigned int count = 0;	

    if (!head) 
        return 0;

    p = head->next;
    while(p != head) {
        count++;
        p = p->next;
    }
    return count;
}

int listHasData(struct keyval *head) 
{
    if (!head) 
        return 0;

    return (head->next != head);
}


char *getItem(struct keyval *head, const char *name)
{
    struct keyval *p;

    if (!head) 
        return NULL;

    p = head->next;
    while(p != head) {
        if (!strcmp(p->key, name))
            return p->value;
        p = p->next;
    }
    return NULL;
}	

/* Pulls all items from list which match this prefix
 * note: they are removed from the original list an returned in a new one
 */
struct keyval *getMatches(struct keyval *head, const char *name)
{
    struct keyval *out = NULL;
    struct keyval *p;

    if (!head) 
        return NULL;

    out = malloc(sizeof(struct keyval));
    if (!out)
        return NULL;

    initList(out);
    p = head->next;
    while(p != head) {
        struct keyval *next = p->next;
        if (!strncmp(p->key, name, strlen(name))) {
            //printf("match: %s=%s\n", p->key, p->value);
            p->next->prev = p->prev;
            p->prev->next = p->next;
            pushItem(out, p);
        }
        p = next;
    }

    if (listHasData(out))
        return out;

    free(out);
    return NULL;
}

void updateItem(struct keyval *head, const char *name, const char *value)
{
    struct keyval *item;

    if (!head) 
        return;

    item = head->next;
    while(item != head) {
        if (!strcmp(item->key, name)) {
#ifdef USE_TREE
            text_release(tree_ctx, item->value);
            item->value = (char *)text_get(tree_ctx,value);
#else
            free(item->value);
            item->value = strdup(value);
#endif
            return;
        }
        item = item->next;
    }
    addItem(head, name, value, 0);
}


struct keyval *popItem(struct keyval *head)
{
    struct keyval *p;

    if (!head) 
        return NULL;
 
    p = head->next;
    if (p == head)
        return NULL;

    head->next = p->next;
    p->next->prev = head;

    p->next = NULL;
    p->prev = NULL;

    return p;
}	


void pushItem(struct keyval *head, struct keyval *item)
{
    assert(head);
    assert(item);
 
    item->next = head;
    item->prev = head->prev;
    head->prev->next = item;
    head->prev = item;
}	

int addItem(struct keyval *head, const char *name, const char *value, int noDupe)
{
    struct keyval *item;

    assert(head);
    assert(name);
    assert(value);

    if (noDupe) {
        item = head->next;
        while (item != head) {
            if (!strcmp(item->value, value) && !strcmp(item->key, name))
                return 1;
            item = item->next;
        }
    }

    item = malloc(sizeof(struct keyval));

    if (!item) {
        fprintf(stderr, "Error allocating keyval\n");
        return 2;
    }

#ifdef USE_TREE
    item->key   = (char *)text_get(tree_ctx,name);
    item->value = (char *)text_get(tree_ctx,value);
#else
    item->key   = strdup(name);
    item->value = strdup(value);
#endif
    
    item->next = head->next;
    item->prev = head;
    head->next->prev = item;
    head->next = item;

    return 0;
}

void resetList(struct keyval *head) 
{
    struct keyval *item;
	
    while((item = popItem(head))) 
        freeItem(item);
}

