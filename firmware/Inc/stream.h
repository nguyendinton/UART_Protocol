/**
 * @file  stream.h
 * @brief Truyền dữ liệu liên tục (streaming) từ ADC về PC.
 *
 *   TIM2 (TRGO)  ->  ADC1  ->  DMA circular  ->  bộ đệm đôi
 *                                                  |
 *                         ngắt nửa đầy / đầy  ->  cờ  ->  vòng lặp chính
 *                                                  |
 *                                    gom N mẫu vào một khung STREAM_DATA
 *                                                  |
 *                                    hàng đợi TX  ->  UART TX DMA
 *
 * Tần số lấy mẫu do TIM2 quyết định. Mỗi khung mang N mẫu để giảm số khung
 * phải gửi: ở 100 Hz với N = 20, board gửi 5 khung/giây.
 */
#ifndef STREAM_H
#define STREAM_H

#include "protocol.h"

/* Nguồn dữ liệu */
#define STREAM_SRC_ADC1CH   0u  /* 1 kênh ADC (PA0)                        */
#define STREAM_SRC_ADC2CH   1u  /* 2 kênh ADC xen kẽ (PA0, PA1)            */
#define STREAM_SRC_TEST     2u  /* sóng sin tạo bằng phần mềm              */

#define STREAM_BATCH_MAX    60u     /* số mẫu tối đa trong một khung       */
#define STREAM_RATE_MAX     20000u  /* tần số lấy mẫu tối đa (Hz)          */

void        stream_init(void);

/**
 * Bắt đầu truyền liên tục.
 * @param src      nguồn dữ liệu (STREAM_SRC_*)
 * @param rate_hz  tần số lấy mẫu, 1..STREAM_RATE_MAX
 * @param batch    số mẫu trong một khung, 1..STREAM_BATCH_MAX
 * @return ERR_OK hoặc mã lỗi.
 */
proto_err_t stream_start(uint8_t src, uint16_t rate_hz, uint8_t batch);

void        stream_stop(void);
uint8_t     stream_is_running(void);

/** Đóng gói các mẫu đã sẵn sàng thành khung. Gọi trong vòng lặp chính. */
void        stream_poll(void);

/** Số khung đã bị bỏ do hàng đợi TX đầy. */
uint32_t    stream_dropped(void);

#endif /* STREAM_H */
