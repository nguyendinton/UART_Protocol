/**
 * @file  app_main.h
 * @brief Điểm vào của ứng dụng, gọi từ main.c do STM32CubeIDE sinh ra.
 *
 * Trong main.c:
 *   - gọi app_setup() một lần sau các hàm MX_xxx_Init();
 *   - gọi app_loop() trong vòng lặp while (1).
 */
#ifndef APP_MAIN_H
#define APP_MAIN_H

/** Khởi tạo các module của ứng dụng. */
void app_setup(void);

/** Một vòng xử lý của ứng dụng (không chặn). */
void app_loop(void);

#endif /* APP_MAIN_H */
