/**
 * @file  app_cmd.c
 * @brief Tầng ứng dụng: giải mã tham số, thực thi lệnh (LED, PWM, ADC,
 *        DHT11, streaming, thống kê) và gửi phản hồi hoặc NACK.
 */
#include <string.h>
#include "main.h"
#include "app_cmd.h"
#include "proto_uart.h"
#include "stream.h"
#include "dht11.h"

/* Handle ngoại vi khai báo trong main.c */
extern ADC_HandleTypeDef  hadc1;
extern TIM_HandleTypeDef  htim3;      /* PWM: TIM3 CH1, CH2 */

/* ------------------------------------------------------------------ */
/* Bảng chân LED                                                       */
/* ------------------------------------------------------------------ */
typedef struct { GPIO_TypeDef *port; uint16_t pin; } led_t;

static const led_t LEDS[] = {
    { GPIOC, GPIO_PIN_13 },   /* LED0: LED trên board, tích cực mức thấp */
    { GPIOB, GPIO_PIN_12 },   /* LED1 */
    { GPIOB, GPIO_PIN_13 },   /* LED2 */
    { GPIOB, GPIO_PIN_14 },   /* LED3 */
};
#define LED_COUNT   (sizeof(LEDS) / sizeof(LEDS[0]))
#define LED0_ACTIVE_LOW   1

static uint8_t  s_led_mask;
static uint8_t  s_dht_pending;        /* có lệnh DHT_READ chờ xử lý   */
static proto_frame_t s_dht_req;

/* ------------------------------------------------------------------ */
/* Đọc/ghi số nhiều byte dạng little-endian                            */
/* ------------------------------------------------------------------ */
static inline void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)(v >> 8);
}
static inline void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v);       p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static inline uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

/* ------------------------------------------------------------------ */
/* Điều khiển ngoại vi                                                 */
/* ------------------------------------------------------------------ */
static void led_write(uint8_t id, uint8_t on)
{
    GPIO_PinState st = on ? GPIO_PIN_SET : GPIO_PIN_RESET;
#if LED0_ACTIVE_LOW
    if (id == 0u) {
        st = on ? GPIO_PIN_RESET : GPIO_PIN_SET;   /* PC13 tích cực mức thấp */
    }
#endif
    HAL_GPIO_WritePin(LEDS[id].port, LEDS[id].pin, st);
    if (on) { s_led_mask |=  (uint8_t)(1u << id); }
    else    { s_led_mask &= (uint8_t)~(1u << id); }
}

/* Đọc một lần một kênh ADC. Không dùng khi đang streaming. */
static uint8_t adc_read_once(uint8_t ch, uint16_t *raw)
{
    ADC_ChannelConfTypeDef c = {0};
    c.Channel      = (ch == 0u) ? ADC_CHANNEL_0 : ADC_CHANNEL_1;
    c.Rank         = 1;
    c.SamplingTime = ADC_SAMPLETIME_84CYCLES;
    if (HAL_ADC_ConfigChannel(&hadc1, &c) != HAL_OK) {
        return 0;
    }
    if (HAL_ADC_Start(&hadc1) != HAL_OK) {
        return 0;
    }
    if (HAL_ADC_PollForConversion(&hadc1, 10) != HAL_OK) {
        HAL_ADC_Stop(&hadc1);
        return 0;
    }
    *raw = (uint16_t)HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);
    return 1;
}

/* ================================================================== */
void app_cmd_init(void)
{
    s_led_mask    = 0;
    s_dht_pending = 0;
    for (uint8_t i = 0; i < LED_COUNT; i++) {
        led_write(i, 0);
    }
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_2);
}

/* ================================================================== */
/* Xử lý lệnh                                                          */
/* ================================================================== */
void app_cmd_on_frame(const proto_frame_t *f)
{
    uint8_t r[40];

    switch (f->cmd) {

    /* -------------------------------------------------- hệ thống -- */
    case CMD_PING:
        puart_reply(f, 0, 0);
        break;

    case CMD_GET_VERSION:
        r[0] = PROTO_VERSION;
        r[1] = PROTO_CKS_MODE;
        r[2] = FW_VERSION_MAJOR;
        r[3] = FW_VERSION_MINOR;
        puart_reply(f, r, 4);
        break;

    case CMD_ECHO:
        puart_reply(f, f->data, f->len);
        break;

    /* ------------------------------------------------------ LED --- */
    case CMD_LED_SET:
        if (f->len != 2u || f->data[0] >= LED_COUNT || f->data[1] > 2u) {
            puart_nack(f, ERR_PARAM);
            break;
        }
        if (f->data[1] == 2u) {                      /* 2 = đảo trạng thái */
            led_write(f->data[0],
                      (uint8_t)!((s_led_mask >> f->data[0]) & 1u));
        } else {
            led_write(f->data[0], f->data[1]);
        }
        r[0] = s_led_mask;
        puart_reply(f, r, 1);
        break;

    case CMD_LED_GET:
        r[0] = s_led_mask;
        puart_reply(f, r, 1);
        break;

    /* ------------------------------------------------------ PWM --- */
    case CMD_PWM_SET: {
        if (f->len != 3u) {
            puart_nack(f, ERR_PARAM);
            break;
        }
        uint8_t  ch   = f->data[0];
        uint16_t duty = get_u16(&f->data[1]);        /* 0..1000 phần nghìn */
        if (ch > 1u || duty > 1000u) {
            puart_nack(f, ERR_PARAM);
            break;
        }
        uint32_t arr = __HAL_TIM_GET_AUTORELOAD(&htim3);
        uint32_t ccr = ((arr + 1u) * duty) / 1000u;
        __HAL_TIM_SET_COMPARE(&htim3,
                              (ch == 0u) ? TIM_CHANNEL_1 : TIM_CHANNEL_2, ccr);
        r[0] = ch;
        put_u16(&r[1], duty);
        puart_reply(f, r, 3);
        break;
    }

    /* ------------------------------------------------------ ADC --- */
    case CMD_ADC_READ: {
        if (f->len != 1u || f->data[0] > 1u) {
            puart_nack(f, ERR_PARAM);
            break;
        }
        if (stream_is_running()) {
            /* ADC đang được tầng streaming sử dụng qua DMA. */
            puart_nack(f, ERR_BUSY);
            break;
        }
        uint16_t raw = 0;
        if (!adc_read_once(f->data[0], &raw)) {
            puart_nack(f, ERR_SENSOR);
            break;
        }
        r[0] = f->data[0];
        put_u16(&r[1], raw);
        put_u16(&r[3], (uint16_t)(((uint32_t)raw * 3300u) / 4095u));  /* mV */
        puart_reply(f, r, 5);
        break;
    }

    /* ---------------------------------------------------- DHT11 --- */
    case CMD_DHT_READ:
        if (s_dht_pending) {
            puart_nack(f, ERR_BUSY);
            break;
        }
        /* Đọc DHT11 chặn khoảng 20 ms nên chỉ ghi nhận yêu cầu ở đây;
           việc đọc và phản hồi thực hiện trong app_cmd_poll(). */
        s_dht_req     = *f;
        s_dht_pending = 1;
        break;

    /* ------------------------------------------------- streaming -- */
    case CMD_STREAM_START: {
        if (f->len != 4u) {
            puart_nack(f, ERR_PARAM);
            break;
        }
        uint8_t  src   = f->data[0];
        uint16_t rate  = get_u16(&f->data[1]);
        uint8_t  batch = f->data[3];
        proto_err_t e  = stream_start(src, rate, batch);
        if (e == ERR_OK) { puart_reply(f, 0, 0); }
        else             { puart_nack(f, e);     }
        break;
    }

    case CMD_STREAM_STOP:
        stream_stop();
        puart_reply(f, 0, 0);
        break;

    /* ------------------------------------------------ thống kê ---- */
    case CMD_STATS_GET: {
        const proto_stats_t *s = puart_stats();
        put_u32(&r[0],  s->rx_bytes);
        put_u32(&r[4],  s->rx_ok);
        put_u32(&r[8],  s->err_cks);
        put_u32(&r[12], s->err_len);
        put_u32(&r[16], s->err_timeout);
        put_u32(&r[20], s->resync);
        put_u32(&r[24], s->tx_frames);
        put_u32(&r[28], s->tx_drop);
        puart_reply(f, r, 32);
        break;
    }

    case CMD_STATS_RESET:
        memset(puart_stats(), 0, sizeof(proto_stats_t));
        puart_reply(f, 0, 0);
        break;

    default:
        puart_nack(f, ERR_UNKNOWN_CMD);
        break;
    }
}

/* ================================================================== */
void app_cmd_poll(void)
{
    if (s_dht_pending) {
        dht11_data_t d;
        uint8_t r[5];
        proto_err_t e = dht11_read(&d);
        r[0] = (uint8_t)e;
        if (e == ERR_OK) {
            put_u16(&r[1], (uint16_t)d.temp_x10);
            put_u16(&r[3], d.hum_x10);
        } else {
            put_u16(&r[1], 0);
            put_u16(&r[3], 0);
        }
        puart_reply(&s_dht_req, r, 5);
        s_dht_pending = 0;
    }
}
