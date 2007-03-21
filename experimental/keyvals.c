/* Common key-value list processing
 *
 * Used as a small general purpose store for 
 * tags, segment lists etc 
 *
 */
#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include <assert.h>
#include "keyvals.h"


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

    free(p->key);
    free(p->value);
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

    item->key   = strdup(name);
    item->value = strdup(value);

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

