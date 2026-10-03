# Cấu hình phần cứng và STM32CubeMX

Board: STM32F411CEU6 (Blackpill), thạch anh HSE 25 MHz. Với Nucleo-F411RE chỉ
cần đổi chân LED và cổng USART.

## 1. Sơ đồ chân

| Chân | Chức năng | Nối với |
|---|---|---|
| PA9 | USART1_TX | RXD của mạch USB-UART |
| PA10 | USART1_RX | TXD của mạch USB-UART |
| GND | — | GND của mạch USB-UART |
| PA0 | ADC1_IN0 | Chân giữa biến trở 10 kΩ (hai đầu nối 3V3 và GND) |
| PA1 | ADC1_IN1 | Biến trở thứ hai hoặc cảm biến tương tự |
| PA6 | TIM3_CH1 (PWM) | LED qua điện trở 330 Ω |
| PA7 | TIM3_CH2 (PWM) | LED qua điện trở 330 Ω |
| PA8 | GPIO open-drain | DATA của DHT11, điện trở kéo lên 4,7 kΩ tới 3V3 |
| PC13 | GPIO output | LED trên board (tích cực mức thấp) |
| PB12, PB13, PB14 | GPIO output | LED1, LED2, LED3 qua điện trở 330 Ω |

Điện áp vào chân ADC không vượt quá 3,3 V.

## 2. Cấu hình CubeMX

| Ngoại vi | Cấu hình |
|---|---|
| RCC | HSE: Crystal/Ceramic Resonator; HCLK 96 MHz, APB1 48 MHz, APB2 96 MHz |
| SYS | Debug: Serial Wire |
| USART1 | Asynchronous, 115200-8-N-1; bật USART1 global interrupt |
| DMA cho USART1 | RX: DMA2 Stream2, Circular, Byte; TX: DMA2 Stream7, Normal, Byte |
| ADC1 | IN0, IN1; 12 bit, căn phải; External Trigger: Timer 2 Trigger Out event, Rising edge |
| DMA cho ADC1 | DMA2 Stream0, Circular, Half Word; bật ADC1 global interrupt |
| TIM2 | Internal Clock; Trigger Event Selection (TRGO): Update Event; bật TIM2 global interrupt |
| TIM3 | PWM Generation CH1, CH2; Prescaler 95, Counter Period 999 (PWM 1 kHz) |
| GPIO | PC13, PB12, PB13, PB14: Output Push-Pull; PA8: Output Open-Drain, Pull-up |

Prescaler và Counter Period của TIM2 được firmware đặt lại khi nhận lệnh
`STREAM_START`.

## 3. Đưa mã nguồn vào project CubeIDE

1. Chép `firmware/Inc/*.h` vào `Core/Inc/` và `firmware/Src/*.c` vào `Core/Src/`.
2. Trong `Core/Src/main.c` thêm ba dòng vào các vùng `USER CODE`:

```c
/* USER CODE BEGIN Includes */
#include "app_main.h"
/* USER CODE END Includes */

  /* USER CODE BEGIN 2 */
  app_setup();
  /* USER CODE END 2 */

  /* USER CODE BEGIN WHILE */
  while (1)
  {
    app_loop();
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
```

Vòng lặp chính không dùng `HAL_Delay`; các module đều xử lý theo kiểu không chặn
(trừ lệnh `DHT_READ`, xem mục Giới hạn trong đặc tả giao thức).
