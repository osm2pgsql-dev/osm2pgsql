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


keyval::keyval(const keyval &other)
{
    key = other.key;
    value = other.value;
    // the list cannot be copied, so initialise as empty
    next = this;
    prev = this;
    has_column = other.has_column;
}


unsigned int keyval::countList() const
{
    unsigned int count = 0;

    keyval *p = next;
    while(p != this) {
        count++;
        p = p->next;
    }
    return count;
}


const std::string *keyval::getItem(const std::string &name) const
{
    keyval *p = next;
    while(p != this) {
        if (!p->key.compare(name))
            return &(p->value);
        p = p->next;
    }
    return NULL;
}

/* unlike getItem this function gives a pointer to the whole
   list item which can be used to remove the tag from the linked list
   with the removeTag function
*/
struct keyval *keyval::getTag(const std::string &name)
{
    keyval *p = next;
    while(p != this) {
        if (!p->key.compare(name))
            return p;
        p = p->next;
    }
    return NULL;
}

void keyval::removeTag()
{
  prev->next = next;
  next->prev = prev;
  delete(this);
}


struct keyval *keyval::popItem()
{
    keyval *p = next;
    if (p == this)
        return NULL;

    next = p->next;
    p->next->prev = this;

    p->next = NULL;
    p->prev = NULL;

    return p;
}


void keyval::pushItem(struct keyval *item)
{
    assert(item);

    item->next = this;
    item->prev = prev;
    prev->next = item;
    prev = item;
}

int keyval::addItem(const std::string &name, const std::string &value, bool noDupe)
{
    if (noDupe) {
        keyval *item = next;
        while (item != this) {
            if (!value.compare(item->value) && !name.compare(item->key))
                return 1;
            item = item->next;
        }
    }

    new keyval(name, value, this);

    return 0;
}

void keyval::resetList()
{
    struct keyval *item;

    while((item = popItem()))
        delete(item);
}

void keyval::cloneList(struct keyval *target)
{
  for(keyval *ptr = firstItem(); ptr; ptr = nextItem(ptr))
    target->addItem(ptr->key, ptr->value, false);
}

void keyval::moveList(keyval *target)
{
    target->resetList();

    if (listHasData()) {
        target->next = next;
        target->prev = prev;
        next->prev = target;
        prev->next = target;

        next = this;
        prev = this;
    }
}
