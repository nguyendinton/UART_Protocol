/**
 * @file  checksum.c
 * @brief Cài đặt Sum-8, CRC-8/ATM và CRC-16/CCITT-FALSE (tính từng bit).
 */
#include "checksum.h"

/* -------------------------------------------------------------------------
 * Sum-8 bù 2
 * ---------------------------------------------------------------------- */
uint8_t cks_sum8(const uint8_t *d, size_t n)
{
    uint8_t s = 0;
    while (n--) {
        s = (uint8_t)(s + *d++);
    }
    return (uint8_t)(~s + 1u);      /* bù 2: tổng dữ liệu + checksum = 0 */
}

/* -------------------------------------------------------------------------
 * CRC-8/ATM  (poly 0x07, init 0x00)
 * ---------------------------------------------------------------------- */
uint8_t cks_crc8(const uint8_t *d, size_t n)
{
    uint8_t crc = 0x00u;
    while (n--) {
        crc ^= *d++;
        for (int i = 0; i < 8; i++) {
            crc = (uint8_t)((crc & 0x80u) ? (((unsigned)crc << 1) ^ 0x07u)
                                          :  ((unsigned)crc << 1));
        }
    }
    return crc;
}

/* -------------------------------------------------------------------------
 * CRC-16/CCITT-FALSE  (poly 0x1021, init 0xFFFF, không đảo bit)
 * Còn gọi là CRC-16/IBM-3740. Phát hiện mọi lỗi 1, 2, 3 bit và mọi lỗi
 * chùm dài không quá 16 bit.
 * ---------------------------------------------------------------------- */
uint16_t cks_crc16_update(uint16_t crc, uint8_t b)
{
    crc ^= (uint16_t)b << 8;
    for (int i = 0; i < 8; i++) {
        crc = (uint16_t)((crc & 0x8000u) ? (((unsigned)crc << 1) ^ 0x1021u)
                                         :  ((unsigned)crc << 1));
    }
    return crc;
}

uint16_t cks_crc16(const uint8_t *d, size_t n)
{
    uint16_t crc = 0xFFFFu;
    while (n--) {
        crc = cks_crc16_update(crc, *d++);
    }
    return crc;
}
