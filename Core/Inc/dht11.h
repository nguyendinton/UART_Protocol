/**
 * @file  dht11.h
 * @brief Driver cảm biến nhiệt độ – độ ẩm DHT11/DHT22 (giao tiếp một dây).
 *
 * Bit 0 và bit 1 của cảm biến phân biệt bằng độ rộng xung (khoảng 26 us và
 * 70 us), nên thời gian được đo bằng bộ đếm chu kỳ DWT của Cortex-M4.
 * dht11_read() chặn khoảng 20 ms; chỉ gọi từ vòng lặp chính.
 */
#ifndef DHT11_H
#define DHT11_H

#include "protocol.h"

typedef struct {
    int16_t  temp_x10;      /* nhiệt độ  x10, ví dụ 275 = 27.5 °C */
    uint16_t hum_x10;       /* độ ẩm     x10, ví dụ 620 = 62.0 %  */
} dht11_data_t;

/** Bật bộ đếm chu kỳ DWT. Gọi một lần khi khởi động. */
void        dht11_init(void);

/** Đọc nhiệt độ và độ ẩm. @return ERR_OK, ERR_SENSOR hoặc ERR_TIMEOUT. */
proto_err_t dht11_read(dht11_data_t *out);

#endif /* DHT11_H */
