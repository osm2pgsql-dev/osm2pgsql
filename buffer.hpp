#ifndef BUFFER_HPP
#define BUFFER_HPP

#include <boost/noncopyable.hpp>
#include <cstddef>

struct buffer : public boost::noncopyable {
    buffer();
    ~buffer();

    size_t printf(const char *format, ...);
    size_t aprintf(const char *format, ...);
    size_t cpy(const char *);
    void reserve(size_t);

    size_t len() const;
    size_t capacity() const;

    char *buf;

private:
    size_t m_len, m_capacity;

    void truncate();
    void realloc(size_t);
};

#endif /* BUFFER_HPP */
