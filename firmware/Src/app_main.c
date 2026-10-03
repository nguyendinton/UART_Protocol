/**
 * @file  app_main.c
 * @brief Khởi tạo và vòng lặp chính của ứng dụng.
 */
#include "main.h"
#include "app_main.h"
#include "proto_uart.h"
#include "app_cmd.h"
#include "stream.h"
#include "dht11.h"

/* UART nối với PC, khai báo trong main.c */
extern UART_HandleTypeDef huart1;

void app_setup(void)
{
    dht11_init();
    stream_init();
    puart_init(&huart1, app_cmd_on_frame);
    app_cmd_init();
}

void app_loop(void)
{
    puart_poll();       /* nhận và xử lý khung, phát hàng đợi TX */
    stream_poll();      /* đóng gói mẫu ADC thành khung STREAM_DATA */
    app_cmd_poll();     /* lệnh cần thời gian dài (DHT11) */
}
