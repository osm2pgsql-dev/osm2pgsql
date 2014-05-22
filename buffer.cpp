#include "buffer.hpp"
#include "util.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstdarg>
#include <cstdio>
#include <cstring>

buffer::buffer() 
    : buf(NULL), m_len(0), m_capacity(0) {
}

buffer::~buffer() {
    if (buf != NULL) {
        free(buf);
        buf = NULL;
        m_len = m_capacity = 0;
    }
}

size_t buffer::printf(const char *format, ...) {
    va_list ap;
    int status = 0;
    size_t num_chars = 0;

    // basically, we reset m_len to zero when not appending
    truncate();

    // try to print the format into whatever buffer is available.
    va_start(ap, format);
    status = vsnprintf(buf, m_capacity, format, ap);
    va_end(ap);
    
    // negative return means an error occurred.
    if (status < 0) {
        fprintf(stderr, "Error returned from vsnprintf.");
        util::exit_nicely();
    }
    // otherwise, must be positive and is a number of characters
    num_chars = status;

    // vsnprintf returns the number of chars it would have wanted to
    // write, so we can use that to detect if we need to reallocate.
    if (num_chars >= m_capacity) {
        realloc(num_chars + 1);

        // try printing again
        va_start(ap, format);
        status = vsnprintf(buf, m_capacity, format, ap);
        va_end(ap);

        // negative return means an error occurred.
        if (status < 0) {
            fprintf(stderr, "Error returned from vsnprintf.");
            util::exit_nicely();
        }
        num_chars = status;
    }

    m_len += num_chars;
    return num_chars;
}

size_t buffer::aprintf(const char *format, ...) {
    va_list ap;
    int status = 0;
    size_t num_chars = 0;

    // try to print the format into whatever buffer is available.
    va_start(ap, format);
    status = vsnprintf(&buf[m_len], (m_capacity - m_len), format, ap);
    va_end(ap);
    
    // negative return means an error occurred.
    if (status < 0) {
        fprintf(stderr, "Error returned from vsnprintf.");
        util::exit_nicely();
    }
    // otherwise, must be positive and is a number of characters
    num_chars = status;

    // vsnprintf returns the number of chars it would have wanted to
    // write, so we can use that to detect if we need to reallocate.
    if (num_chars >= (m_capacity - m_len)) {
        realloc(num_chars + m_len + 1);

        // try printing again
        va_start(ap, format);
        status = vsnprintf(&buf[m_len], (m_capacity - m_len), format, ap);
        va_end(ap);

        // negative return means an error occurred.
        if (status < 0) {
            fprintf(stderr, "Error returned from vsnprintf.");
            util::exit_nicely();
        }
        num_chars = status;
    }

    m_len += num_chars;
    return num_chars;
}

size_t buffer::cpy(const char *str) {
    size_t len = strlen(str);
    if (len >= m_capacity) {
        realloc(len + 1);
    }
    strncpy(buf, str, m_capacity);
    buf[len] = '\0';
    m_len = len;
    return len;
}

void buffer::reserve(size_t sz) {
    truncate(); // we drop any existing content
    if (sz > m_capacity) {
        realloc(sz);
    }
}

size_t buffer::len() const { return m_len; }
size_t buffer::capacity() const { return m_capacity; }

void buffer::truncate() {
    m_len = 0;
    if (m_capacity > 0) {
        buf[0] = '\0';
    }
}

void buffer::realloc(size_t len) {
    // allocate either double what we have or the size we need,
    // whichever is larger. this should help to reduce the number
    // of allocations by requiring at least some increase in size
    // before needing to run again.
    size_t new_size = std::max(2 * m_capacity, len);
    
    // allocate a new buffer
    char *new_buf = (char *)malloc(new_size);
    if (new_buf == NULL) {
        fprintf(stderr, "Unable to allocate new temporary buffer.");
        util::exit_nicely();
    }
    
    // copy and free old buffer, if one was ever allocated, so that
    // we retain any content which was in the old buffer.
    if (buf != NULL) {
        if (m_len > 0) { memcpy(new_buf, buf, m_len); }
        new_buf[m_len] = '\0';
        free(buf);
    }
    m_capacity = new_size;
    buf = new_buf;
}
