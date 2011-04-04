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
#include <string.h>
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
    head->has_column = 0;
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

/* unlike getItem this function gives a pointer to the whole
   list item which can be used to remove the tag from the linked list
   with the removeTag function
*/
struct keyval *getTag(struct keyval *head, const char *name)
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

void removeTag(struct keyval *tag)
{
  tag->prev->next=tag->next;
  tag->next->prev=tag->prev;
  freeItem(tag);
}

struct keyval *firstItem(struct keyval *head)
{
    if (head == NULL || head == head->next)
        return NULL;

    return head->next;
}

struct keyval *nextItem(struct keyval *head, struct keyval *item)
{
    if (item->next == head)
        return NULL;

    return item->next;
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
    item->has_column=0;


#if 1
    // Add to head
    item->next = head->next;
    item->prev = head;
    head->next->prev = item;
    head->next = item;
#else
    // Add to tail
    item->prev = head->prev;
    item->next = head;
    head->prev->next = item;
    head->prev = item;
#endif
    return 0;
}

void resetList(struct keyval *head) 
{
    struct keyval *item;
	
    while((item = popItem(head))) 
        freeItem(item);
}

void cloneList( struct keyval *target, struct keyval *source )
{
  struct keyval *ptr;
  for( ptr = source->next; ptr != source; ptr=ptr->next )
    addItem( target, ptr->key, ptr->value, 0 );
}


/* create an escaped version of the string for hstore table insert */
/* make shure dst is 2*strlen(src) */
static void escape4hstore(char *dst, char *src) {
  size_t i,j;

  j=0;
  for (i=0;i<strlen(src);i++) {
    switch(src[i]) {
      case '\\':
        dst[j]='\\';j++;dst[j]='\\';j++;dst[j]='\\';j++;dst[j]='\\'; break;
      case '"':
        dst[j]='\\';j++;dst[j]='\\';j++;dst[j]='"'; break;
      case '\t':
        dst[j]='\\';j++;dst[j]='\t'; break;
      case '\r':
        dst[j]='\\';j++;dst[j]='\r'; break;
      case '\n':
        dst[j]='\\';j++;dst[j]='\n'; break;
      default:
        dst[j]=src[i];
      }
    j++;
  }
  dst[j]='\0';
}

/* print struct keyval in syntax for pgsql hstore import 
   \ and " need to be escaped
*/
void keyval2hstore(char *hstring, struct keyval *tags)
{
  keyval2hstore_manual(hstring, tags->key, tags->value);
}

void keyval2hstore_manual(char *hstring, char *key, char *value)
{
  static char* str=NULL;
  static size_t stlen=0;
  size_t len;
 
  len=strlen(value);
  if (len>stlen) {
    stlen=len;
    str=realloc(str,1+stlen*2);
  }

  len=strlen(key);
  if (len>stlen) {
    stlen=len;
    str=realloc(str,1+stlen*2);
  }

  escape4hstore(str,key);  
  hstring+=sprintf(hstring,"\"%s\"=>",str);
  escape4hstore(str,value);
  sprintf(hstring,"\"%s\"",str);  
}

