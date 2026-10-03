/**
 * @file  ringbuf.h
 * @brief Bộ đệm vòng một bên ghi – một bên đọc (SPSC), không dùng khoá.
 *
 * Bên ghi chỉ thay đổi `head`, bên đọc chỉ thay đổi `tail`, nên có thể dùng
 * giữa ngắt và vòng lặp chính mà không cần tắt ngắt. Kích thước bộ đệm phải
 * là luỹ thừa của 2.
 */
#ifndef RINGBUF_H
#define RINGBUF_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint8_t          *buf;
    uint16_t          size;         /* luỹ thừa của 2         */
    volatile uint16_t head;         /* vị trí ghi             */
    volatile uint16_t tail;         /* vị trí đọc             */
} ringbuf_t;

void     rb_init (ringbuf_t *rb, uint8_t *storage, uint16_t size_pow2);
uint16_t rb_count(const ringbuf_t *rb);
uint16_t rb_free (const ringbuf_t *rb);

/** Ghi tối đa @p n byte. @return số byte thực sự ghi được. */
uint16_t rb_write(ringbuf_t *rb, const uint8_t *d, uint16_t n);

/** Đọc 1 byte. @return 1 nếu có dữ liệu, 0 nếu rỗng. */
uint8_t  rb_read1(ringbuf_t *rb, uint8_t *out);

/**
 * Trả về vùng dữ liệu liên tục đang chờ đọc (dùng làm bộ đệm nguồn cho DMA).
 * @param[out] ptr  con trỏ tới byte đầu tiên
 * @return          số byte liên tục (0 nếu rỗng)
 */
uint16_t rb_peek_linear(const ringbuf_t *rb, const uint8_t **ptr);

/** Bỏ @p n byte đã đọc xong qua rb_peek_linear(). */
void     rb_consume(ringbuf_t *rb, uint16_t n);

#endif /* RINGBUF_H */
