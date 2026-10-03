/**
 * @file  proto_uart.h
 * @brief Tầng vận chuyển: ghép bộ tách khung với UART của STM32 (HAL).
 *
 *   - Nhận: ngắt từng byte hoặc DMA circular, đưa byte vào bộ tách khung
 *     và gọi callback khi có khung hợp lệ.
 *   - Gửi: khung được xếp vào hàng đợi vòng rồi phát bằng DMA, không chặn
 *     vòng lặp chính.
 */
#ifndef PROTO_UART_H
#define PROTO_UART_H

#include "protocol.h"

struct __UART_HandleTypeDef;

/** Hàm xử lý khung hợp lệ. */
typedef void (*proto_frame_cb_t)(const proto_frame_t *f);

/** Khởi tạo và bắt đầu nhận. Gọi sau khi UART đã được khởi tạo. */
void puart_init(struct __UART_HandleTypeDef *huart, proto_frame_cb_t cb);

/** Xử lý byte đã nhận, kiểm tra timeout và phát hàng đợi TX. Gọi trong vòng lặp chính. */
void puart_poll(void);

/**
 * Đóng gói và xếp một khung vào hàng đợi gửi (không chặn).
 * @return 1 nếu xếp được, 0 nếu hàng đợi đầy (tăng stats.tx_drop).
 */
uint8_t puart_send(uint8_t cmd, const uint8_t *data, uint8_t len);

/** Gửi phản hồi cho khung @p req: giữ nguyên SEQ, CMD | 0x80. */
uint8_t puart_reply(const proto_frame_t *req, const uint8_t *data, uint8_t len);

/** Gửi NACK cho khung @p req: CMD = 0xFF, DATA = [CMD gốc][mã lỗi]. */
uint8_t puart_nack(const proto_frame_t *req, proto_err_t err);

/** Gửi khung với SEQ tự tăng (dùng cho STREAM_DATA). */
uint8_t puart_send_auto(uint8_t cmd, const uint8_t *data, uint8_t len);

/** Số byte còn trống trong hàng đợi TX. */
uint16_t puart_tx_space(void);

/** Con trỏ tới bộ đếm thống kê. */
proto_stats_t *puart_stats(void);

#endif /* PROTO_UART_H */
