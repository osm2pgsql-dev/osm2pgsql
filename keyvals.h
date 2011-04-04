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
    /* if a hstore column is requested we need a flag to store if a key
       has its own column because it should not be added to the hstore 
       in this case
    */
    int has_column;
    struct keyval *next;
    struct keyval *prev;
};

void initList(struct keyval *head);
void freeItem(struct keyval *p);
unsigned int countList(struct keyval *head);
int listHasData(struct keyval *head);
char *getItem(struct keyval *head, const char *name);
struct keyval *getTag(struct keyval *head, const char *name);
void removeTag(struct keyval *tag);
struct keyval *firstItem(struct keyval *head);
struct keyval *nextItem(struct keyval *head, struct keyval *item);
struct keyval *popItem(struct keyval *head);
void pushItem(struct keyval *head, struct keyval *item);
int addItem(struct keyval *head, const char *name, const char *value, int noDupe);
void resetList(struct keyval *head);
struct keyval *getMatches(struct keyval *head, const char *name);
void updateItem(struct keyval *head, const char *name, const char *value);
void cloneList( struct keyval *target, struct keyval *source );
void keyval2hstore(char *hstring, struct keyval *tags);
void keyval2hstore_manual(char *hstring, char *key, char *value);
#endif
