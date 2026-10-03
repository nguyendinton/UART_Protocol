/**
 * @file  dht11.c
 * @brief Driver DHT11/DHT22: tạo xung khởi động, đo độ rộng 40 bit dữ liệu
 *        bằng bộ đếm chu kỳ DWT và kiểm tra checksum của cảm biến.
 */
#include "main.h"
#include "dht11.h"

/* Chân DATA của cảm biến */
#define DHT_PORT    GPIOA
#define DHT_PIN     GPIO_PIN_8

/* ------------------------------------------------------------------ */
/* Đo thời gian mức micro giây bằng bộ đếm chu kỳ DWT                  */
/* ------------------------------------------------------------------ */
static uint32_t s_cyc_per_us;

void dht11_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->LAR          = 0xC5ACCE55u;      /* mở khoá truy cập DWT */
    DWT->CYCCNT       = 0;
    DWT->CTRL        |= DWT_CTRL_CYCCNTENA_Msk;
    s_cyc_per_us      = HAL_RCC_GetHCLKFreq() / 1000000u;
}

static inline void delay_us(uint32_t us)
{
    uint32_t t0 = DWT->CYCCNT;
    uint32_t n  = us * s_cyc_per_us;
    while ((DWT->CYCCNT - t0) < n) { }
}

/* ------------------------------------------------------------------ */
/* Đổi chiều chân DATA                                                 */
/* ------------------------------------------------------------------ */
static void pin_output(void)
{
    GPIO_InitTypeDef g = {0};
    g.Pin   = DHT_PIN;
    g.Mode  = GPIO_MODE_OUTPUT_OD;        /* open-drain, trở kéo lên 4,7k ngoài */
    g.Pull  = GPIO_PULLUP;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(DHT_PORT, &g);
}

static void pin_input(void)
{
    GPIO_InitTypeDef g = {0};
    g.Pin  = DHT_PIN;
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(DHT_PORT, &g);
}

/** Chờ chân DATA đạt mức @p lvl trong tối đa @p max_us.
 *  @return thời gian đã chờ (us), hoặc -1 nếu quá hạn. */
static int32_t wait_level(GPIO_PinState lvl, uint32_t max_us)
{
    uint32_t t0 = DWT->CYCCNT;
    uint32_t lim = max_us * s_cyc_per_us;
    while (HAL_GPIO_ReadPin(DHT_PORT, DHT_PIN) != lvl) {
        if ((DWT->CYCCNT - t0) > lim) {
            return -1;
        }
    }
    return (int32_t)((DWT->CYCCNT - t0) / s_cyc_per_us);
}

/* ================================================================== */
proto_err_t dht11_read(dht11_data_t *out)
{
    uint8_t raw[5] = {0};

    /* 1. Xung khởi động: kéo DATA xuống thấp ít nhất 18 ms rồi nhả */
    pin_output();
    HAL_GPIO_WritePin(DHT_PORT, DHT_PIN, GPIO_PIN_RESET);
    HAL_Delay(20);
    HAL_GPIO_WritePin(DHT_PORT, DHT_PIN, GPIO_PIN_SET);
    delay_us(30);
    pin_input();

    /* 2. Cảm biến phản hồi: 80 us mức thấp, 80 us mức cao */
    if (wait_level(GPIO_PIN_RESET, 100) < 0) { return ERR_SENSOR; }
    if (wait_level(GPIO_PIN_SET,   120) < 0) { return ERR_SENSOR; }
    if (wait_level(GPIO_PIN_RESET, 120) < 0) { return ERR_SENSOR; }

    /* 3. 40 bit dữ liệu: 50 us mức thấp, sau đó mức cao 26 us (bit 0)
          hoặc 70 us (bit 1) */
    for (int i = 0; i < 40; i++) {
        if (wait_level(GPIO_PIN_SET, 100) < 0) { return ERR_TIMEOUT; }
        int32_t high_us = wait_level(GPIO_PIN_RESET, 150);
        if (high_us < 0) { return ERR_TIMEOUT; }
        raw[i / 8] = (uint8_t)((raw[i / 8] << 1) | ((high_us > 45) ? 1u : 0u));
    }

    /* 4. Checksum của cảm biến: tổng 4 byte đầu */
    uint8_t sum = (uint8_t)(raw[0] + raw[1] + raw[2] + raw[3]);
    if (sum != raw[4]) {
        return ERR_SENSOR;
    }

    /* 5. Quy đổi: DHT11 có byte phần lẻ bằng 0, DHT22 trả giá trị 16 bit x10 */
    if (raw[1] == 0u && raw[3] == 0u) {          /* DHT11 */
        out->hum_x10  = (uint16_t)(raw[0] * 10u);
        out->temp_x10 = (int16_t) (raw[2] * 10);
    } else {                                     /* DHT22 */
        out->hum_x10  = (uint16_t)(((uint16_t)raw[0] << 8) | raw[1]);
        int16_t t     = (int16_t)((((uint16_t)(raw[2] & 0x7Fu)) << 8) | raw[3]);
        out->temp_x10 = (raw[2] & 0x80u) ? (int16_t)(-t) : t;
    }
    return ERR_OK;
}
