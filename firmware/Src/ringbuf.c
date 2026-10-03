/**
 * @file  ringbuf.c
 * @brief Bộ đệm vòng một bên ghi – một bên đọc.
 */
#include <string.h>
#include "ringbuf.h"

void rb_init(ringbuf_t *rb, uint8_t *storage, uint16_t size_pow2)
{
    rb->buf  = storage;
    rb->size = size_pow2;
    rb->head = 0;
    rb->tail = 0;
}

uint16_t rb_count(const ringbuf_t *rb)
{
    return (uint16_t)((rb->head - rb->tail) & (rb->size - 1u));
}

uint16_t rb_free(const ringbuf_t *rb)
{
    return (uint16_t)(rb->size - 1u - rb_count(rb));
}

uint16_t rb_write(ringbuf_t *rb, const uint8_t *d, uint16_t n)
{
    uint16_t space = rb_free(rb);
    if (n > space) {
        n = space;
    }
    uint16_t h    = rb->head;
    uint16_t mask = (uint16_t)(rb->size - 1u);
    uint16_t first = (uint16_t)(rb->size - h);
    if (first > n) {
        first = n;
    }
    memcpy(&rb->buf[h], d, first);
    if (n > first) {
        memcpy(&rb->buf[0], d + first, (size_t)(n - first));
    }
    rb->head = (uint16_t)((h + n) & mask);
    return n;
}

uint8_t rb_read1(ringbuf_t *rb, uint8_t *out)
{
    if (rb->head == rb->tail) {
        return 0;
    }
    *out = rb->buf[rb->tail];
    rb->tail = (uint16_t)((rb->tail + 1u) & (rb->size - 1u));
    return 1;
}

uint16_t rb_peek_linear(const ringbuf_t *rb, const uint8_t **ptr)
{
    uint16_t h = rb->head;
    uint16_t t = rb->tail;
    if (h == t) {
        *ptr = 0;
        return 0;
    }
    *ptr = &rb->buf[t];
    return (h > t) ? (uint16_t)(h - t) : (uint16_t)(rb->size - t);
}

void rb_consume(ringbuf_t *rb, uint16_t n)
{
    rb->tail = (uint16_t)((rb->tail + n) & (rb->size - 1u));
}
