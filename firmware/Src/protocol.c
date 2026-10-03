/**
 * @file  protocol.c
 * @brief Tầng khung: đóng gói khung và máy trạng thái tách khung.
 *
 * Đồng bộ lại: khi một khung bị loại (sai mã kiểm tra hoặc LEN không hợp
 * lệ), các byte đã nhận sau SOF2 được đưa trở lại đầu hàng đợi và quét lại
 * từ trạng thái chờ SOF1, vì điểm bắt đầu của khung kế tiếp có thể nằm
 * trong các byte đó. Mỗi lần đồng bộ lại loại bỏ ít nhất 2 byte SOF nên số
 * byte trong hàng đợi luôn giảm và quá trình luôn kết thúc.
 */
#include <string.h>
#include "protocol.h"
#include "checksum.h"

/* ================================================================== */
/* Hàng đợi byte chờ xử lý                                             */
/*                                                                     */
/* Byte mới được thêm vào cuối hàng đợi. Khi đồng bộ lại, các byte của */
/* khung bị loại được chèn vào đầu hàng đợi vì chúng đến trước.        */
/* ================================================================== */
#define PEND_CAP    ((uint16_t)sizeof(((proto_parser_t *)0)->pend))

static void pend_reset(proto_parser_t *p)
{
    p->pend_head = 0;
    p->pend_tail = 0;
}

static uint16_t pend_count(const proto_parser_t *p)
{
    return (uint16_t)(p->pend_tail - p->pend_head);
}

static void pend_push_back(proto_parser_t *p, uint8_t b)
{
    if (p->pend_tail >= PEND_CAP) {                 /* dồn dữ liệu về đầu mảng */
        uint16_t n = pend_count(p);
        memmove(p->pend, p->pend + p->pend_head, n);
        p->pend_head = 0;
        p->pend_tail = n;
    }
    if (p->pend_tail < PEND_CAP) {
        p->pend[p->pend_tail++] = b;
    }
}

static void pend_push_front(proto_parser_t *p, const uint8_t *d, uint16_t n)
{
    if (n == 0u) {
        return;
    }
    if (p->pend_head >= n) {                        /* còn chỗ trống phía trước */
        p->pend_head = (uint16_t)(p->pend_head - n);
        memcpy(p->pend + p->pend_head, d, n);
        return;
    }
    uint16_t rem = pend_count(p);
    if ((uint32_t)n + rem > PEND_CAP) {
        rem = (uint16_t)(PEND_CAP - n);             /* tránh tràn mảng */
    }
    memmove(p->pend + n, p->pend + p->pend_head, rem);
    memcpy(p->pend, d, n);
    p->pend_head = 0;
    p->pend_tail = (uint16_t)(n + rem);
}

/* ================================================================== */
/* Hàm nội bộ                                                          */
/* ================================================================== */

/** Lưu lại một byte của khung đang nhận. */
static void swallow(proto_parser_t *p, uint8_t b)
{
    if (p->swallowed_len < sizeof(p->swallowed)) {
        p->swallowed[p->swallowed_len++] = b;
    }
}

/** Đưa máy trạng thái về chờ SOF1. */
static void goto_idle(proto_parser_t *p)
{
    p->state         = ST_SOF1;
    p->idx           = 0;
    p->swallowed_len = 0;
}

/** Loại khung hiện tại với lỗi @p e và đưa các byte của nó vào lại hàng đợi. */
static void resync(proto_parser_t *p, proto_err_t e)
{
    p->last_err = e;
    if (p->stats) {
        p->stats->resync++;
        switch (e) {
        case ERR_CKS: p->stats->err_cks++;     break;
        case ERR_LEN: p->stats->err_len++;     break;
        default:                                break;
        }
    }
    if (p->on_error) {
        p->on_error(&p->f, e);
    }
    uint8_t  tmp[PROTO_MAX_FRAME];
    uint16_t n = p->swallowed_len;
    if (n > sizeof(tmp)) {
        n = sizeof(tmp);
    }
    memcpy(tmp, p->swallowed, n);
    goto_idle(p);
    pend_push_front(p, tmp, n);
}

/* ================================================================== */
/* Mã kiểm tra của phần LEN, SEQ, CMD, DATA                            */
/* ================================================================== */
static uint16_t body_cks(const proto_frame_t *f)
{
    uint8_t body[3 + PROTO_MAX_DATA];
    body[0] = f->len;
    body[1] = f->seq;
    body[2] = f->cmd;
    if (f->len) {
        memcpy(&body[3], f->data, f->len);
    }
    size_t n = (size_t)f->len + 3u;

#if (PROTO_CKS_MODE == PROTO_CKS_SUM8)
    return cks_sum8(body, n);
#elif (PROTO_CKS_MODE == PROTO_CKS_CRC8)
    return cks_crc8(body, n);
#else
    return cks_crc16(body, n);
#endif
}

/* ================================================================== */
/* Máy trạng thái: xử lý một byte                                      */
/* ================================================================== */
static const proto_frame_t *feed_one(proto_parser_t *p, uint8_t b)
{
    switch (p->state) {

    case ST_SOF1:
        if (b == PROTO_SOF1) {
            p->state = ST_SOF2;
        }
        /* byte khác 0xAA nằm ngoài khung: bỏ qua */
        break;

    case ST_SOF2:
        if (b == PROTO_SOF2) {
            p->swallowed_len = 0;
            p->state = ST_LEN;
        } else if (b == PROTO_SOF1) {
            /* 0xAA lặp lại: tiếp tục chờ SOF2 */
        } else {
            p->state = ST_SOF1;
        }
        break;

    case ST_LEN:
        swallow(p, b);
        if (b > PROTO_MAX_DATA) {
            resync(p, ERR_LEN);
        } else {
            p->f.len = b;
            p->state = ST_SEQ;
        }
        break;

    case ST_SEQ:
        swallow(p, b);
        p->f.seq = b;
        p->state = ST_CMD;
        break;

    case ST_CMD:
        swallow(p, b);
        p->f.cmd = b;
        p->idx   = 0;
        p->state = (p->f.len > 0u) ? ST_DATA : ST_CKS;
        break;

    case ST_DATA:
        swallow(p, b);
        p->f.data[p->idx++] = b;
        if (p->idx >= p->f.len) {
            p->idx   = 0;
            p->state = ST_CKS;
        }
        break;

    case ST_CKS:
        swallow(p, b);
        p->cks_buf[p->idx++] = b;
        if (p->idx >= PROTO_CKS_LEN) {
#if (PROTO_CKS_MODE == PROTO_CKS_CRC16)
            uint16_t got = (uint16_t)p->cks_buf[0] |
                           ((uint16_t)p->cks_buf[1] << 8);   /* little-endian */
#else
            uint16_t got = p->cks_buf[0];
#endif
            if (got == body_cks(&p->f)) {
                if (p->stats) {
                    p->stats->rx_ok++;
                }
                p->last_err = ERR_OK;
                goto_idle(p);
                return &p->f;                 /* khung hợp lệ */
            }
            resync(p, ERR_CKS);
        }
        break;

    default:
        goto_idle(p);
        break;
    }
    return NULL;
}

/* ================================================================== */
/* Hàm công khai                                                       */
/* ================================================================== */
void proto_parser_init(proto_parser_t *p, proto_stats_t *stats)
{
    memset(p, 0, sizeof(*p));
    p->state = ST_SOF1;
    p->stats = stats;
    if (stats) {
        memset(stats, 0, sizeof(*stats));
    }
}

const proto_frame_t *proto_feed(proto_parser_t *p, uint8_t b, uint32_t now_ms)
{
    p->t_last_ms = now_ms;
    if (p->stats) {
        p->stats->rx_bytes++;
    }

    /* Thêm byte vào cuối hàng đợi, xử lý tới khi có khung hoặc hết hàng đợi. */
    pend_push_back(p, b);

    while (pend_count(p) > 0u) {
        uint8_t nb = p->pend[p->pend_head++];
        if (p->pend_head == p->pend_tail) {
            pend_reset(p);
        }
        const proto_frame_t *f = feed_one(p, nb);
        if (f) {
            return f;       /* các byte còn lại được xử lý ở lần gọi sau */
        }
    }
    return NULL;
}

void proto_tick(proto_parser_t *p, uint32_t now_ms)
{
    if (p->state == ST_SOF1 && pend_count(p) == 0u) {
        return;                     /* không có khung đang nhận */
    }
    if ((uint32_t)(now_ms - p->t_last_ms) >= PROTO_IBT_MS) {
        if (p->state != ST_SOF1) {
            p->last_err = ERR_TIMEOUT;
            if (p->stats) {
                p->stats->err_timeout++;
            }
            if (p->on_error) {
                p->on_error(&p->f, ERR_TIMEOUT);
            }
        }
        /* Khung dở dang đã quá hạn: bỏ, không quét lại. */
        goto_idle(p);
        pend_reset(p);
        p->t_last_ms = now_ms;
    }
}

uint16_t proto_build(uint8_t *out, uint8_t seq, uint8_t cmd,
                     const uint8_t *data, uint8_t len)
{
    if (len > PROTO_MAX_DATA) {
        return 0;
    }
    out[0] = PROTO_SOF1;
    out[1] = PROTO_SOF2;
    out[2] = len;
    out[3] = seq;
    out[4] = cmd;
    if (len && data) {
        memcpy(&out[5], data, len);
    }

    size_t  n = (size_t)len + 3u;         /* LEN SEQ CMD DATA */
#if (PROTO_CKS_MODE == PROTO_CKS_SUM8)
    out[5 + len] = cks_sum8(&out[2], n);
#elif (PROTO_CKS_MODE == PROTO_CKS_CRC8)
    out[5 + len] = cks_crc8(&out[2], n);
#else
    uint16_t c   = cks_crc16(&out[2], n);
    out[5 + len] = (uint8_t)(c & 0xFFu);
    out[6 + len] = (uint8_t)(c >> 8);
#endif
    return (uint16_t)(PROTO_HDR_LEN + len + PROTO_CKS_LEN);
}

const char *proto_state_name(proto_state_t s)
{
    switch (s) {
    case ST_SOF1: return "WAIT_SOF1";
    case ST_SOF2: return "WAIT_SOF2";
    case ST_LEN:  return "WAIT_LEN";
    case ST_SEQ:  return "WAIT_SEQ";
    case ST_CMD:  return "WAIT_CMD";
    case ST_DATA: return "WAIT_DATA";
    case ST_CKS:  return "WAIT_CKS";
    default:      return "?";
    }
}
