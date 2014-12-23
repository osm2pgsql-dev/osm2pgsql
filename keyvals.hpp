/* Common key-value list processing
 *
 * Used as a small general purpose store for
 * tags, segment lists etc
 *
 */

#ifndef KEYVAL_H
#define KEYVAL_H

#include "text-tree.hpp"

#include <boost/shared_ptr.hpp>

struct keyval {
    char *key;
    char *value;
    /* if a hstore column is requested we need a flag to store if a key
       has its own column because it should not be added to the hstore
       in this case
    */
    int has_column;
    struct keyval *next;
    struct keyval *prev;
    boost::shared_ptr<text_tree> tree_ctx;

    keyval();
    keyval(const keyval &other);
    ~keyval();

    static void freeItem(struct keyval *p);
    static unsigned int countList(const struct keyval *head);
    static int listHasData(struct keyval *head);
    static char *getItem(const struct keyval *head, const char *name);
    static struct keyval *getTag(struct keyval *head, const char *name);
    static const struct keyval *getTag(const struct keyval *head, const char *name);
    static void removeTag(struct keyval *tag);
    static struct keyval *firstItem(struct keyval *head);
    static struct keyval *nextItem(struct keyval *head, struct keyval *item);
    static struct keyval *popItem(struct keyval *head);
    static void pushItem(struct keyval *head, struct keyval *item);
    static int addItem(struct keyval *head, const char *name, const char *value, int noDupe);
    static void resetList(struct keyval *head);
    static struct keyval *getMatches(struct keyval *head, const char *name);
    static void updateItem(struct keyval *head, const char *name, const char *value);
    static void cloneList( struct keyval *target, struct keyval *source );
};


#endif
