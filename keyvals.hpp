/* Common key-value list processing
 *
 * Used as a small general purpose store for
 * tags, segment lists etc
 *
 */

#ifndef KEYVAL_H
#define KEYVAL_H

#include <boost/shared_ptr.hpp>

struct keyval {
    std::string key;
    std::string value;
    /* if a hstore column is requested we need a flag to store if a key
       has its own column because it should not be added to the hstore
       in this case
    */
    int has_column;
private:
    keyval *next;
    keyval *prev;

public:
    keyval() : has_column(0), next(this), prev(this) {}

    keyval(const std::string &name_, const std::string &value_)
    : key(name_), value(value_), has_column(0), next(NULL), prev(NULL)
    {}

    keyval(const keyval &other);
    ~keyval() {}

    unsigned int countList() const;
    bool listHasData() const { return next != this; }
    const std::string *getItem(const std::string &name) const;
    keyval *getTag(const std::string &name);
    void removeTag();
    keyval *firstItem() const { return listHasData() ? next : NULL; }
    keyval *nextItem(const keyval *item) const {
        return item->next == this ? NULL : item->next;
    }
    keyval *popItem();
    void pushItem(keyval *item);
    int addItem(const std::string &name, const std::string &value, bool noDupe);
    void resetList();
    void cloneList(keyval *target);
    void moveList(keyval *target);

private:
    keyval(const std::string &key_, const std::string &value_, keyval *after)
    : key(key_), value(value_), has_column(0)
    {
        next = after->next;
        prev = after;
        after->next->prev = this;
        after->next = this;
    }

};


#endif
