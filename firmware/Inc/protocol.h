/**
 * @file  protocol.h
 * @brief Tầng khung: định dạng khung, bảng lệnh, mã lỗi và bộ tách khung.
 *
 * Định dạng khung (v1.1):
 *
 *   +------+------+-----+-----+-----+----------+----------+
 *   | SOF1 | SOF2 | LEN | SEQ | CMD | DATA[LEN]|   CKS    |
 *   | 0xAA | 0x55 |     |     |     |          | 1 / 2 B  |
 *   +------+------+-----+-----+-----+----------+----------+
 *      0      1     2     3     4     5..         ...
 *
 *   - CKS tính trên LEN, SEQ, CMD, DATA (không gồm 2 byte SOF).
 *   - CRC-16 truyền little-endian (byte thấp trước).
 *   - Dữ liệu nhiều byte trong DATA dùng little-endian.
 *
 * Không phụ thuộc HAL, biên dịch được trên PC để kiểm thử.
 */
#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>
#include <stddef.h>
#include "proto_cfg.h"

/* ------------------------------------------------------------------ */
/* Hằng số khung                                                       */
/* ------------------------------------------------------------------ */
#define PROTO_SOF1          0xAAu
#define PROTO_SOF2          0x55u
#define PROTO_HDR_LEN       5u      /* SOF1 SOF2 LEN SEQ CMD */

#if (PROTO_CKS_MODE == PROTO_CKS_CRC16)
#  define PROTO_CKS_LEN     2u
#else
#  define PROTO_CKS_LEN     1u
#endif

#define PROTO_MAX_FRAME     (PROTO_HDR_LEN + PROTO_MAX_DATA + PROTO_CKS_LEN)

/* ------------------------------------------------------------------ */
/* Bảng lệnh                                                           */
/* ------------------------------------------------------------------ */
/* Lệnh từ PC có mã < 0x80; phản hồi dùng mã lệnh OR 0x80 và giữ nguyên SEQ.
 * 0xFF là NACK, 0xF0 là STREAM_DATA do board chủ động gửi.
 * Cột chú thích: "->" là DATA của lệnh, "<-" là DATA của phản hồi.        */
#define CMD_RESP_FLAG       0x80u

typedef enum {
    CMD_PING          = 0x01,   /* -> (rỗng)                 <- (rỗng)      */
    CMD_GET_VERSION   = 0x02,   /* -> (rỗng)                 <- 4 byte      */
    CMD_ECHO          = 0x05,   /* -> N byte                 <- N byte      */

    CMD_LED_SET       = 0x10,   /* -> [id][state]            <- [mask]      */
    CMD_LED_GET       = 0x11,   /* -> (rỗng)                 <- [mask]      */
    CMD_PWM_SET       = 0x12,   /* -> [ch][duty_lo][duty_hi] <- [ch][duty]  */

    CMD_ADC_READ      = 0x20,   /* -> [ch]        <- [ch][raw16][mv16]      */
    CMD_DHT_READ      = 0x21,   /* -> (rỗng)      <- [st][temp16][hum16]    */

    CMD_STREAM_START  = 0x30,   /* -> [src][rate16][batch]   <- (rỗng)      */
    CMD_STREAM_STOP   = 0x31,   /* -> (rỗng)                 <- (rỗng)      */

    CMD_STATS_GET     = 0x40,   /* -> (rỗng)      <- 8 x uint32 (32 byte)   */
    CMD_STATS_RESET   = 0x41,   /* -> (rỗng)                 <- (rỗng)      */

    CMD_STREAM_DATA   = 0xF0,   /* <- [idx32][n][mẫu u16 x n]               */
    CMD_NACK          = 0xFF    /* <- [CMD gốc][mã lỗi]                     */
} proto_cmd_t;

/* ------------------------------------------------------------------ */
/* Mã lỗi                                                              */
/* ------------------------------------------------------------------ */
typedef enum {
    ERR_OK            = 0x00,
    ERR_CKS           = 0x01,   /* sai mã kiểm tra                         */
    ERR_LEN           = 0x02,   /* LEN vượt PROTO_MAX_DATA                 */
    ERR_UNKNOWN_CMD   = 0x03,   /* mã lệnh không tồn tại                   */
    ERR_PARAM         = 0x04,   /* tham số ngoài miền cho phép             */
    ERR_BUSY          = 0x05,   /* tài nguyên đang được sử dụng            */
    ERR_TIMEOUT       = 0x06,   /* quá thời gian chờ giữa hai byte         */
    ERR_NOT_SUPPORTED = 0x07,   /* cấu hình ngoại vi thất bại              */
    ERR_SENSOR        = 0x08,   /* cảm biến không phản hồi / sai checksum  */
    ERR_OVERRUN       = 0x09    /* hàng đợi TX đầy                         */
} proto_err_t;

/* ------------------------------------------------------------------ */
/* Khung đã giải mã                                                    */
/* ------------------------------------------------------------------ */
typedef struct {
    uint8_t seq;
    uint8_t cmd;
    uint8_t len;
    uint8_t data[PROTO_MAX_DATA];
} proto_frame_t;

/* ------------------------------------------------------------------ */
/* Bộ đếm thống kê                                                     */
/* ------------------------------------------------------------------ */
typedef struct {
    uint32_t rx_bytes;      /* số byte đã nhận                             */
    uint32_t rx_ok;         /* số khung hợp lệ                             */
    uint32_t err_cks;       /* số khung sai mã kiểm tra                    */
    uint32_t err_len;       /* số khung có LEN không hợp lệ                */
    uint32_t err_timeout;   /* số khung dở dang bị huỷ do timeout          */
    uint32_t resync;        /* số lần đồng bộ lại                          */
    uint32_t tx_frames;     /* số khung đã gửi                             */
    uint32_t tx_drop;       /* số khung bị bỏ do hàng đợi TX đầy           */
} proto_stats_t;

/* ------------------------------------------------------------------ */
/* Bộ tách khung (máy trạng thái)                                      */
/* ------------------------------------------------------------------ */
typedef enum {
    ST_SOF1 = 0, ST_SOF2, ST_LEN, ST_SEQ, ST_CMD, ST_DATA, ST_CKS
} proto_state_t;

typedef struct {
    proto_state_t state;
    proto_frame_t f;            /* khung đang nhận                         */
    uint8_t  idx;               /* chỉ số trong DATA hoặc trong CKS        */
    uint8_t  cks_buf[2];

    /* các byte đã nhận sau SOF2 của khung hiện tại (dùng khi đồng bộ lại) */
    uint8_t  swallowed[PROTO_MAX_FRAME];
    uint16_t swallowed_len;

    /* hàng đợi byte chờ xử lý                                             */
    uint8_t  pend[2u * PROTO_MAX_FRAME];
    uint16_t pend_head;             /* vị trí đọc  */
    uint16_t pend_tail;             /* vị trí ghi  */

    uint32_t t_last_ms;         /* thời điểm nhận byte gần nhất            */
    proto_err_t last_err;
    proto_stats_t *stats;       /* có thể NULL                             */

    /* Gọi khi một khung bị loại; có thể NULL. Các trường trong @p partial
       thuộc về khung lỗi nên không bảo đảm đúng. */
    void (*on_error)(const proto_frame_t *partial, proto_err_t e);
} proto_parser_t;

/* ------------------------------------------------------------------ */
/* API                                                                 */
/* ------------------------------------------------------------------ */

/** Khởi tạo bộ tách khung. @p stats có thể NULL. */
void proto_parser_init(proto_parser_t *p, proto_stats_t *stats);

/**
 * Đưa một byte vào bộ tách khung.
 * @return con trỏ tới khung hợp lệ vừa nhận xong, hoặc NULL.
 *         Con trỏ chỉ có hiệu lực tới lần gọi proto_feed() kế tiếp.
 */
const proto_frame_t *proto_feed(proto_parser_t *p, uint8_t b, uint32_t now_ms);

/** Kiểm tra timeout giữa hai byte. Gọi định kỳ trong vòng lặp chính. */
void proto_tick(proto_parser_t *p, uint32_t now_ms);

/**
 * Đóng gói một khung vào @p out.
 * @param out phải có ít nhất PROTO_MAX_FRAME byte.
 * @return tổng số byte của khung, hoặc 0 nếu len không hợp lệ.
 */
uint16_t proto_build(uint8_t *out, uint8_t seq, uint8_t cmd,
                     const uint8_t *data, uint8_t len);

/** Tên trạng thái dạng chuỗi (phục vụ gỡ lỗi). */
const char *proto_state_name(proto_state_t s);

#endif /* PROTOCOL_H */
