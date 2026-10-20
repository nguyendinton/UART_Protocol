/**
 * @file    proto_cfg.h
 * @brief   Cấu hình biên dịch của giao thức khung UART.
 *
 * Mọi tham số có thể thay đổi của giao thức được gom tại đây: kiểu mã kiểm
 * tra, chế độ nhận UART, kích thước bộ đệm và các mốc thời gian.
 */
#ifndef PROTO_CFG_H
#define PROTO_CFG_H

/* ---------------------------------------------------------------------------
 * 1. Kiểu mã kiểm tra (checksum)
 * ------------------------------------------------------------------------- */
#define PROTO_CKS_SUM8      0   /* Sum-8 bù 2          — 1 byte              */
#define PROTO_CKS_CRC8      1   /* CRC-8/ATM           — 1 byte              */
#define PROTO_CKS_CRC16     2   /* CRC-16/CCITT-FALSE  — 2 byte (mặc định)   */

#ifndef PROTO_CKS_MODE
#define PROTO_CKS_MODE      PROTO_CKS_CRC16
#endif

/* ---------------------------------------------------------------------------
 * 2. Chế độ nhận UART
 * ------------------------------------------------------------------------- */
#define PROTO_RX_IT         0   /* HAL_UART_Receive_IT 1 byte + ring buffer  */
#define PROTO_RX_DMA        1   /* DMA circular + quét NDTR (mặc định)       */

#ifndef PROTO_RX_MODE
#define PROTO_RX_MODE       PROTO_RX_DMA
#endif

/* ---------------------------------------------------------------------------
 * 3. Kích thước
 * ------------------------------------------------------------------------- */
/* LEN tối đa mà bộ tách khung chấp nhận.
 * Khung dài nhất của ứng dụng là STREAM_DATA: 5 byte đầu + 60 mẫu 16 bit =
 * 125 byte. Giới hạn LEN sát với nhu cầu thực tế để khi bắt nhầm một cặp
 * 0xAA 0x55 nằm trong nhiễu, bộ tách khung đi lạc tối đa 128 byte.          */
#define PROTO_MAX_DATA      128u
#define PROTO_RX_DMA_SIZE   512u    /* bộ đệm vòng DMA RX (luỹ thừa của 2)   */
#define PROTO_TX_RB_SIZE    2048u   /* hàng đợi TX (luỹ thừa của 2)          */

/* ---------------------------------------------------------------------------
 * 4. Thời gian
 * ------------------------------------------------------------------------- */
#define PROTO_IBT_MS        50u     /* inter-byte timeout: quá 50 ms giữa 2
                                       byte trong cùng khung -> huỷ khung    */

/* ---------------------------------------------------------------------------
 * 5. Phiên bản firmware / giao thức
 * ------------------------------------------------------------------------- */
#define PROTO_VERSION       0x11u   /* v1.1 */
#define FW_VERSION_MAJOR    1u
#define FW_VERSION_MINOR    0u

#endif /* PROTO_CFG_H */
