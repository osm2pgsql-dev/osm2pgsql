/* Common key-value list processing
 *
 * Used as a small general purpose store for 
 * tags, segment lists etc 
 *
 */

#ifndef KEYVAL_H
#define KEYVAL_H

struct keyval {
    char *key;
    char *value;
    struct keyval *next;
    struct keyval *prev;
};

void initList(struct keyval *head);
void freeItem(struct keyval *p);
unsigned int countList(struct keyval *head);
int listHasData(struct keyval *head);
char *getItem(struct keyval *head, const char *name);
struct keyval *popItem(struct keyval *head);
void pushItem(struct keyval *head, struct keyval *item);
int addItem(struct keyval *head, const char *name, const char *value, int noDupe);
void resetList(struct keyval *head);
struct keyval *getMatches(struct keyval *head, const char *name);
void updateItem(struct keyval *head, const char *name, const char *value);

#endif
