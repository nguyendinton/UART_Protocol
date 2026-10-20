/**
 * @file  stream.c
 * @brief Truyền liên tục: TIM2 kích ADC1, DMA ghi vào bộ đệm đôi, vòng lặp
 *        chính đóng gói từng nửa bộ đệm thành khung STREAM_DATA.
 */
#include <string.h>
#include "main.h"
#include "stream.h"
#include "proto_uart.h"

/* Handle ngoại vi khai báo trong main.c */
extern ADC_HandleTypeDef hadc1;
extern TIM_HandleTypeDef htim2;      /* nhịp lấy mẫu, TRGO = Update */

/* ------------------------------------------------------------------ */
/* Biến nội bộ                                                         */
/* ------------------------------------------------------------------ */
static uint16_t s_buf[2u * STREAM_BATCH_MAX];   /* bộ đệm đôi            */
static volatile uint8_t  s_half_ready;          /* nửa đầu sẵn sàng      */
static volatile uint8_t  s_full_ready;          /* nửa sau sẵn sàng      */
static volatile uint16_t s_test_widx;           /* vị trí ghi (nguồn TEST) */

static uint8_t   s_running;
static uint8_t   s_src;
static uint8_t   s_batch;
static uint16_t  s_rate;
static uint32_t  s_index;                       /* chỉ số mẫu kế tiếp    */
static uint32_t  s_drop;
static uint32_t  s_phase;                       /* pha của sóng sin      */

/* ------------------------------------------------------------------ */
/* Đặt tần số cập nhật của TIM2                                        */
/* ------------------------------------------------------------------ */
static void tim2_set_rate(uint16_t hz)
{
    /* TIM2 thuộc APB1. Khi hệ số chia APB1 khác 1, clock của timer bằng
       2 lần PCLK1. */
    uint32_t pclk1 = HAL_RCC_GetPCLK1Freq();
    uint32_t tclk  = (((RCC->CFGR & RCC_CFGR_PPRE1) == 0u) ? pclk1
                                                           : (pclk1 * 2u));
    /* TIM2 là timer 32 bit nên không cần prescaler với hz >= 1. */
    uint32_t arr = tclk / hz;
    if (arr == 0u) {
        arr = 1u;
    }
    __HAL_TIM_SET_PRESCALER(&htim2, 0);
    __HAL_TIM_SET_AUTORELOAD(&htim2, arr - 1u);
    __HAL_TIM_SET_COUNTER(&htim2, 0);
}

/* ------------------------------------------------------------------ */
/* Cấu hình ADC: kích bằng TIM2 TRGO, DMA circular                     */
/* ------------------------------------------------------------------ */
static proto_err_t adc_setup_stream(uint8_t nch, uint16_t total_len)
{
    ADC_ChannelConfTypeDef c = {0};

    HAL_ADC_Stop_DMA(&hadc1);

    hadc1.Init.ScanConvMode          = (nch > 1u) ? ENABLE : DISABLE;
    hadc1.Init.ContinuousConvMode    = DISABLE;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConv      = ADC_EXTERNALTRIGCONV_T2_TRGO;
    hadc1.Init.ExternalTrigConvEdge  = ADC_EXTERNALTRIGCONVEDGE_RISING;
    hadc1.Init.DMAContinuousRequests = ENABLE;
    hadc1.Init.NbrOfConversion       = nch;
    hadc1.Init.EOCSelection          = ADC_EOC_SINGLE_CONV;
    if (HAL_ADC_Init(&hadc1) != HAL_OK) {
        return ERR_NOT_SUPPORTED;
    }

    c.SamplingTime = ADC_SAMPLETIME_15CYCLES;
    c.Channel = ADC_CHANNEL_0; c.Rank = 1;
    if (HAL_ADC_ConfigChannel(&hadc1, &c) != HAL_OK) {
        return ERR_NOT_SUPPORTED;
    }
    if (nch > 1u) {
        c.Channel = ADC_CHANNEL_1; c.Rank = 2;
        if (HAL_ADC_ConfigChannel(&hadc1, &c) != HAL_OK) {
            return ERR_NOT_SUPPORTED;
        }
    }
    if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)s_buf, total_len) != HAL_OK) {
        return ERR_NOT_SUPPORTED;
    }
    return ERR_OK;
}

/* ------------------------------------------------------------------ */
/* Callback của ADC (DMA nửa bộ đệm / đầy bộ đệm)                      */
/* ------------------------------------------------------------------ */
void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *h)
{
    if (h == &hadc1 && s_running) {
        s_half_ready = 1;
    }
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *h)
{
    if (h == &hadc1 && s_running) {
        s_full_ready = 1;
    }
}

/* ------------------------------------------------------------------ */
/* Nguồn TEST: sóng sin 64 điểm, cập nhật trong ngắt Update của TIM2   */
/* ------------------------------------------------------------------ */
static const uint16_t SINE64[64] = {
    2048,2248,2447,2642,2831,3013,3185,3347,3496,3631,3750,3854,3940,4008,
    4057,4087,4095,4087,4057,4008,3940,3854,3750,3631,3496,3347,3185,3013,
    2831,2642,2447,2248,2048,1847,1648,1453,1264,1082, 910, 748, 599, 464,
     345, 241, 155,  87,  38,   8,   0,   8,  38,  87, 155, 241, 345, 464,
     599, 748, 910,1082,1264,1453,1648,1847
};

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *h)
{
    if (h != &htim2 || !s_running || s_src != STREAM_SRC_TEST) {
        return;
    }
    s_buf[s_test_widx++] = SINE64[s_phase & 63u];
    s_phase++;
    if (s_test_widx == s_batch)            { s_half_ready = 1; }
    else if (s_test_widx >= 2u * s_batch)  { s_full_ready = 1; s_test_widx = 0; }
}

/* ================================================================== */
/* Hàm công khai                                                       */
/* ================================================================== */
void stream_init(void)
{
    s_running = 0;
    s_drop    = 0;
}

proto_err_t stream_start(uint8_t src, uint16_t rate_hz, uint8_t batch)
{
    if (batch == 0u || batch > STREAM_BATCH_MAX)          { return ERR_PARAM; }
    if (rate_hz == 0u || rate_hz > STREAM_RATE_MAX)       { return ERR_PARAM; }
    if (src > STREAM_SRC_TEST)                            { return ERR_PARAM; }
    if (s_running)                                        { return ERR_BUSY; }

    s_src        = src;
    s_batch      = batch;
    s_rate       = rate_hz;
    s_index      = 0;
    s_phase      = 0;
    s_test_widx  = 0;
    s_half_ready = 0;
    s_full_ready = 0;

    tim2_set_rate(rate_hz);

    if (src == STREAM_SRC_TEST) {
        s_running = 1;
        HAL_TIM_Base_Start_IT(&htim2);
    } else {
        uint8_t nch = (src == STREAM_SRC_ADC2CH) ? 2u : 1u;
        proto_err_t e = adc_setup_stream(nch, (uint16_t)(2u * batch));
        if (e != ERR_OK) {
            return e;
        }
        s_running = 1;
        HAL_TIM_Base_Start(&htim2);          /* chỉ cần TRGO, không cần ngắt */
    }
    return ERR_OK;
}

void stream_stop(void)
{
    if (!s_running) {
        return;
    }
    s_running = 0;
    if (s_src == STREAM_SRC_TEST) {
        HAL_TIM_Base_Stop_IT(&htim2);
    } else {
        HAL_TIM_Base_Stop(&htim2);
        HAL_ADC_Stop_DMA(&hadc1);
    }
    s_half_ready = 0;
    s_full_ready = 0;
}

uint8_t  stream_is_running(void) { return s_running; }
uint32_t stream_dropped(void)    { return s_drop;    }

/* ------------------------------------------------------------------ */
/* Đóng gói n mẫu thành một khung STREAM_DATA                          */
/* DATA = [chỉ số mẫu đầu: u32][số mẫu: u8][mẫu: u16 x n]              */
/* ------------------------------------------------------------------ */
static void emit(const uint16_t *src, uint8_t n)
{
    uint8_t d[5 + 2u * STREAM_BATCH_MAX];
    d[0] = (uint8_t)(s_index);
    d[1] = (uint8_t)(s_index >> 8);
    d[2] = (uint8_t)(s_index >> 16);
    d[3] = (uint8_t)(s_index >> 24);
    d[4] = n;
    for (uint8_t i = 0; i < n; i++) {
        d[5 + 2u * i]      = (uint8_t)(src[i] & 0xFFu);
        d[5 + 2u * i + 1u] = (uint8_t)(src[i] >> 8);
    }
    uint8_t len = (uint8_t)(5u + 2u * n);

    /* Hàng đợi TX không đủ chỗ cho cả khung thì bỏ khung này. Phía PC phát
       hiện khung bị mất qua chỉ số mẫu không liên tục. */
    if (puart_tx_space() < (uint16_t)(len + PROTO_HDR_LEN + PROTO_CKS_LEN)) {
        s_drop++;
    } else {
        puart_send_auto(CMD_STREAM_DATA, d, len);
    }
    s_index += n;
}

void stream_poll(void)
{
    if (!s_running) {
        return;
    }
    if (s_half_ready) {
        s_half_ready = 0;
        emit(&s_buf[0], s_batch);
    }
    if (s_full_ready) {
        s_full_ready = 0;
        emit(&s_buf[s_batch], s_batch);
    }
}
