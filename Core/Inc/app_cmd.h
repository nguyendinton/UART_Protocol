/**
 * @file  app_cmd.h
 * @brief Tầng ứng dụng: thực thi lệnh và gửi phản hồi ACK/NACK.
 */
#ifndef APP_CMD_H
#define APP_CMD_H

#include "protocol.h"

/** Khởi tạo tầng ứng dụng. Gọi sau puart_init(). */
void app_cmd_init(void);

/** Xử lý một khung lệnh hợp lệ (callback đăng ký với puart_init()). */
void app_cmd_on_frame(const proto_frame_t *f);

/** Xử lý các lệnh cần thời gian dài (đọc DHT11). Gọi trong vòng lặp chính. */
void app_cmd_poll(void);

#endif /* APP_CMD_H */
