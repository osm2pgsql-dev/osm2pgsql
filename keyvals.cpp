/* Common key-value list processing
 *
 * Used as a small general purpose store for
 * tags, segment lists etc
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <string.h>
#include "keyvals.hpp"

#include <algorithm>

keyval::keyval()
{
    tree_ctx.reset(new text_tree());
    keyval::initList(this);
}

keyval::~keyval()
{
    //keyval::resetList(this);
}

void keyval::initList(struct keyval *head)
{
    assert(head);

    head->next = head;
    head->prev = head;
    head->key = NULL;
    head->value = NULL;
    head->has_column = 0;
}

void keyval::freeItem(struct keyval *p)
{
    if (!p)
        return;

    p->tree_ctx->text_release(p->key);
    p->tree_ctx->text_release(p->value);
    delete p;
}


unsigned int keyval::countList(const struct keyval *head)
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

int keyval::listHasData(struct keyval *head)
{
    if (!head)
        return 0;

    return (head->next != head);
}


char *keyval::getItem(const struct keyval *head, const char *name)
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

/* unlike getItem this function gives a pointer to the whole
   list item which can be used to remove the tag from the linked list
   with the removeTag function
*/
struct keyval *keyval::getTag(struct keyval *head, const char *name)
{
    struct keyval *p;

    if (!head)
        return NULL;

    p = head->next;
    while(p != head) {
        if (!strcmp(p->key, name))
            return p;
        p = p->next;
    }
    return NULL;
}

void keyval::removeTag(struct keyval *tag)
{
  tag->prev->next=tag->next;
  tag->next->prev=tag->prev;
  freeItem(tag);
}

struct keyval *keyval::firstItem(struct keyval *head)
{
    if (head == NULL || head == head->next)
        return NULL;

    return head->next;
}

struct keyval *keyval::nextItem(struct keyval *head, struct keyval *item)
{
    if (item->next == head)
        return NULL;

    return item->next;
}

/* Pulls all items from list which match this prefix
 * note: they are removed from the original list an returned in a new one
 */
struct keyval *keyval::getMatches(struct keyval *head, const char *name)
{
    struct keyval *out = NULL;
    struct keyval *p;

    if (!head)
        return NULL;

    //TODO: properly copy the tree_ctx from the keyval passed in
    out = new keyval();
    if (!out)
        return NULL;

    p = head->next;
    while(p != head) {
        struct keyval *next = p->next;
        if (!strncmp(p->key, name, strlen(name))) {
            p->next->prev = p->prev;
            p->prev->next = p->next;
            pushItem(out, p);
        }
        p = next;
    }

    if (listHasData(out))
        return out;

    delete out;
    return NULL;
}

void keyval::updateItem(struct keyval *head, const char *name, const char *value)
{
    struct keyval *item;

    if (!head)
        return;

    item = head->next;
    while(item != head) {
        if (!strcmp(item->key, name)) {
            head->tree_ctx->text_release(item->value);
            item->value = (char *)head->tree_ctx->text_get(value);
            return;
        }
        item = item->next;
    }
    addItem(head, name, value, 0);
}


struct keyval *keyval::popItem(struct keyval *head)
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


void keyval::pushItem(struct keyval *head, struct keyval *item)
{

    assert(head);
    assert(item);

    item->next = head;
    item->prev = head->prev;
    head->prev->next = item;
    head->prev = item;
}

int keyval::addItem(struct keyval *head, const char *name, const char *value, int noDupe)
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

    //TODO: properly implement a copy constructor and do the
    //shallow copy there instead of relying on implicit one?
    item = new keyval(*head);

    item->key   = (char *)head->tree_ctx->text_get(name);
    item->value = (char *)head->tree_ctx->text_get(value);
    item->has_column=0;

    /* Add to head */
    item->next = head->next;
    item->prev = head;
    head->next->prev = item;
    head->next = item;
    return 0;
}

void keyval::resetList(struct keyval *head)
{
    struct keyval *item;

    while((item = popItem(head)))
        freeItem(item);
}

void keyval::cloneList( struct keyval *target, struct keyval *source )
{
  struct keyval *ptr;
  for( ptr = source->next; ptr != source; ptr=ptr->next )
    addItem( target, ptr->key, ptr->value, 0 );
}
