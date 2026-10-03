# Đặc tả giao thức khung UART (v1.1)

Giao thức truyền lệnh và dữ liệu giữa PC và vi điều khiển STM32F4 qua UART.

## 1. Tổng quan

| Hạng mục | Giá trị |
|---|---|
| Định dạng khung | `[0xAA][0x55][LEN][SEQ][CMD][DATA…][CRC16_L][CRC16_H]` |
| Mã kiểm tra | CRC-16/CCITT-FALSE (poly `0x1021`, init `0xFFFF`) trên `LEN, SEQ, CMD, DATA` |
| LEN tối đa | 128 byte (khung dài nhất 135 byte) |
| UART | 115200-8-N-1 |
| Nhận trên board | DMA circular, đọc vị trí ghi qua `NDTR` (có thể chọn nhận bằng ngắt) |
| Gửi trên board | Hàng đợi vòng 2 KB, phát bằng DMA |
| Kiểu hoạt động | Hỏi – đáp: mỗi lệnh có một phản hồi hoặc một NACK |
| Streaming | TIM2 → ADC1 → DMA circular, gom N mẫu vào một khung |

Kiểu mã kiểm tra chọn lúc biên dịch bằng `PROTO_CKS_MODE` trong `proto_cfg.h`:
SUM-8 (1 byte), CRC-8 (1 byte) hoặc CRC-16 (2 byte, mặc định).

## 2. Định dạng khung

```
 byte:   0      1      2      3      4      5 .. 4+LEN    5+LEN   6+LEN
      +------+------+------+------+------+-------------+-------+-------+
      | SOF1 | SOF2 | LEN  | SEQ  | CMD  |  DATA[LEN]  | CRC_L | CRC_H |
      | 0xAA | 0x55 |0..128|0..255|      |             |       |       |
      +------+------+------+------+------+-------------+-------+-------+
                     \_____________________________________/
                               phạm vi tính CRC
```

| Trường | Kích thước | Ý nghĩa |
|---|---|---|
| `SOF1`, `SOF2` | 2 byte | Dấu bắt đầu khung, luôn là `0xAA 0x55` |
| `LEN` | 1 byte | Số byte của `DATA`; lớn hơn 128 là khung lỗi |
| `SEQ` | 1 byte | Số thứ tự do bên gửi đặt, tăng dần và quay vòng |
| `CMD` | 1 byte | Mã lệnh |
| `DATA` | `LEN` byte | Tham số hoặc dữ liệu; số nhiều byte dùng little-endian |
| `CRC` | 2 byte | CRC-16, byte thấp gửi trước |

`SEQ` dùng để ghép phản hồi với lệnh: phản hồi mang cùng `SEQ` với lệnh. Điều
này cần thiết khi đang streaming, vì các khung `STREAM_DATA` có thể xen giữa
lệnh và phản hồi.

Quy ước về chiều truyền:

| Chiều | Quy ước |
|---|---|
| PC → board | `CMD` < `0x80` |
| Board → PC, phản hồi | `CMD` của lệnh OR `0x80`, cùng `SEQ` |
| Board → PC, báo lỗi | `CMD = 0xFF` (NACK), `DATA = [CMD gốc][mã lỗi]` |
| Board → PC, tự gửi | `CMD = 0xF0` (`STREAM_DATA`), `SEQ` tăng riêng |

Lệnh thực hiện thành công mà không có dữ liệu trả về được phản hồi bằng khung
`CMD | 0x80` với `LEN = 0` (ACK).

## 3. Bảng lệnh

### 3.1 Hệ thống

| CMD | Tên | DATA của lệnh | DATA của phản hồi |
|---|---|---|---|
| `0x01` | `PING` | — | — |
| `0x02` | `GET_VERSION` | — | `[ver][cks_mode][fw_major][fw_minor]` |
| `0x05` | `ECHO` | N byte | N byte nhận được |

`ver = 0x11` là giao thức v1.1. `cks_mode`: 0 = SUM-8, 1 = CRC-8, 2 = CRC-16.

### 3.2 LED và PWM

| CMD | Tên | DATA của lệnh | DATA của phản hồi |
|---|---|---|---|
| `0x10` | `LED_SET` | `[id][state]`, `id` = 0..3, `state`: 0 tắt, 1 bật, 2 đảo | `[mask]` |
| `0x11` | `LED_GET` | — | `[mask]` |
| `0x12` | `PWM_SET` | `[ch][duty_lo][duty_hi]`, `ch` = 0..1, `duty` = 0..1000 | `[ch][duty_lo][duty_hi]` |

`mask` là trạng thái 4 LED, bit *i* ứng với LED *i*. `duty` tính theo phần
nghìn; PWM 1 kHz với ARR = 999 cho đúng 1000 mức.

### 3.3 Cảm biến

| CMD | Tên | DATA của lệnh | DATA của phản hồi |
|---|---|---|---|
| `0x20` | `ADC_READ` | `[ch]`, 0..1 | `[ch][raw_lo][raw_hi][mv_lo][mv_hi]` |
| `0x21` | `DHT_READ` | — | `[status][temp_lo][temp_hi][hum_lo][hum_hi]` |

- `raw`: giá trị ADC 12 bit; `mv`: điện áp tính theo mV với Vref = 3300 mV.
- `temp`: nhiệt độ ×10, có dấu (275 = 27,5 °C). `hum`: độ ẩm ×10, không dấu.
- `status`: 0 là đọc được, khác 0 là mã lỗi ở mục 4.
- `ADC_READ` trả NACK `ERR_BUSY` khi đang streaming vì ADC đang chạy với DMA.

### 3.4 Streaming

| CMD | Tên | DATA của lệnh | DATA của phản hồi |
|---|---|---|---|
| `0x30` | `STREAM_START` | `[src][rate_lo][rate_hi][batch]` | — |
| `0x31` | `STREAM_STOP` | — | — |
| `0xF0` | `STREAM_DATA` | (board tự gửi) | `[idx: u32][n: u8][mẫu: u16 × n]` |

- `src`: 0 = ADC 1 kênh (PA0), 1 = ADC 2 kênh xen kẽ (PA0, PA1), 2 = sóng sin
  tạo bằng phần mềm.
- `rate`: tần số lấy mẫu, 1..20000 Hz. `batch`: số mẫu trong một khung, 1..60.
- `idx`: chỉ số của mẫu đầu tiên trong khung, tính từ lúc bắt đầu. PC phát
  hiện khung bị mất khi `idx` không liên tục.

### 3.5 Thống kê

| CMD | Tên | DATA của phản hồi |
|---|---|---|
| `0x40` | `STATS_GET` | 8 × `uint32`: `rx_bytes, rx_ok, err_cks, err_len, err_timeout, resync, tx_frames, tx_drop` |
| `0x41` | `STATS_RESET` | — |

## 4. Mã lỗi

| Mã | Tên | Điều kiện |
|---|---|---|
| `0x00` | `ERR_OK` | Không lỗi |
| `0x01` | `ERR_CKS` | Sai mã kiểm tra |
| `0x02` | `ERR_LEN` | `LEN` > 128 |
| `0x03` | `ERR_UNKNOWN_CMD` | Mã lệnh không tồn tại |
| `0x04` | `ERR_PARAM` | Tham số ngoài miền cho phép hoặc sai độ dài |
| `0x05` | `ERR_BUSY` | Tài nguyên đang được sử dụng |
| `0x06` | `ERR_TIMEOUT` | Quá 50 ms giữa hai byte của một khung |
| `0x07` | `ERR_NOT_SUPPORTED` | Cấu hình ngoại vi thất bại |
| `0x08` | `ERR_SENSOR` | Cảm biến không phản hồi hoặc sai checksum |
| `0x09` | `ERR_OVERRUN` | Hàng đợi TX đầy |

NACK cho khung sai mã kiểm tra mang `SEQ` và `CMD` đọc từ chính khung lỗi, nên
hai giá trị này chỉ mang tính tham khảo. Board gửi tối đa một NACK loại này mỗi
50 ms để không chiếm đường truyền khi nhiễu kéo dài.

## 5. Bộ tách khung

### 5.1 Máy trạng thái

| Trạng thái | Byte nhận | Trạng thái kế tiếp |
|---|---|---|
| `WAIT_SOF1` | `0xAA` | `WAIT_SOF2` |
| `WAIT_SOF1` | khác | `WAIT_SOF1` (bỏ qua byte) |
| `WAIT_SOF2` | `0x55` | `WAIT_LEN` |
| `WAIT_SOF2` | `0xAA` | `WAIT_SOF2` |
| `WAIT_SOF2` | khác | `WAIT_SOF1` |
| `WAIT_LEN` | ≤ 128 | `WAIT_SEQ` |
| `WAIT_LEN` | > 128 | lỗi `ERR_LEN`, đồng bộ lại |
| `WAIT_SEQ` | bất kỳ | `WAIT_CMD` |
| `WAIT_CMD` | bất kỳ | `WAIT_DATA` nếu `LEN` > 0, ngược lại `WAIT_CKS` |
| `WAIT_DATA` | bất kỳ | `WAIT_CKS` khi đủ `LEN` byte |
| `WAIT_CKS` | byte cuối, CRC đúng | khung hợp lệ, về `WAIT_SOF1` |
| `WAIT_CKS` | byte cuối, CRC sai | lỗi `ERR_CKS`, đồng bộ lại |

Ở mọi trạng thái khác `WAIT_SOF1`, nếu quá 50 ms không có byte mới thì khung
đang nhận bị huỷ (`ERR_TIMEOUT`) và máy trạng thái về `WAIT_SOF1`.

### 5.2 Đồng bộ lại

Khi một khung bị loại vì sai CRC hoặc sai `LEN`, các byte đã nhận sau `SOF2`
không bị bỏ đi mà được đưa lại vào đầu hàng đợi và quét lại từ `WAIT_SOF1`.
Lý do: nếu cặp `0xAA 0x55` vừa bắt được chỉ là nhiễu, điểm bắt đầu của khung
thật có thể nằm trong chính các byte đó.

```
Chuỗi vào:  … AA 55 | AA 55 03 07 05 aa bb cc C1 C2 | …
              SOF giả └────────── khung thật ─────────┘

1. Bắt SOF giả: LEN = 0xAA = 170 > 128  →  ERR_LEN
2. Quét lại các byte đã nhận            →  bắt được AA 55 03 07 …
3. Khung thật được tách ra bình thường
```

Mỗi lần đồng bộ lại, hai byte SOF của khung bị loại không được đưa lại vào
hàng đợi, nên số byte chờ xử lý giảm ít nhất 2 sau mỗi lần và quá trình luôn
kết thúc.

Giới hạn `LEN` ≤ 128 làm giảm số byte bị giữ lại khi bắt nhầm SOF.

## 6. Tầng vận chuyển trên board

**Nhận.** Chọn bằng `PROTO_RX_MODE`:

| Chế độ | Cách thực hiện |
|---|---|
| `PROTO_RX_IT` | `HAL_UART_Receive_IT` từng byte, ngắt ghi byte vào bộ đệm vòng |
| `PROTO_RX_DMA` (mặc định) | DMA circular 512 byte, vòng lặp chính đọc vị trí ghi từ `NDTR` |

Ở cả hai chế độ, bộ tách khung chạy trong vòng lặp chính, không chạy trong
ngắt. Khi UART báo lỗi (overrun, nhiễu, sai khung), `HAL_UART_ErrorCallback`
xoá cờ lỗi và khởi động lại việc nhận.

**Gửi.** Khung được đóng gói rồi ghi vào hàng đợi vòng 2 KB; DMA phát từng
đoạn liên tục của hàng đợi, ngắt `TxCplt` phát đoạn kế tiếp.

- Hàm gửi không chặn.
- Khung chỉ được xếp khi hàng đợi đủ chỗ cho cả khung; nếu không, khung bị bỏ
  và `tx_drop` tăng.

## 7. Streaming

```
  TIM2 (Update → TRGO)
      │
      ▼
   ADC1 ──DMA circular──▶ bộ đệm 2 × batch mẫu
                               │
               ngắt nửa bộ đệm / đầy bộ đệm → đặt cờ
                               ▼
              vòng lặp chính: batch mẫu → 1 khung STREAM_DATA
                               ▼
                   hàng đợi TX ──▶ UART TX (DMA)
```

Tần số lấy mẫu do TIM2 quyết định: `ARR = f_TIM2 / rate − 1`. TIM2 thuộc APB1;
khi hệ số chia APB1 khác 1 thì `f_TIM2 = 2 × PCLK1`.

Một khung `STREAM_DATA` dài `5 + (5 + 2 × batch) + 2` byte. Tải đường truyền
(UART 8-N-1, 10 bit cho mỗi byte):

```
byte/giây = (rate / batch) × (12 + 2 × batch)
tải (%)   = byte/giây × 10 / baud × 100
```

Giá trị tính theo công thức trên:

| rate | batch | khung/giây | byte/giây | Tải ở 115200 | Tải ở 921600 |
|---|---|---|---|---|---|
| 100 Hz | 1 | 100 | 1 400 | 12,2 % | 1,5 % |
| 100 Hz | 20 | 5 | 260 | 2,3 % | 0,3 % |
| 1 kHz | 20 | 50 | 2 600 | 22,6 % | 2,8 % |
| 5 kHz | 40 | 125 | 11 500 | 99,8 % | 12,5 % |

Nếu hàng đợi TX không đủ chỗ cho một khung `STREAM_DATA`, khung đó bị bỏ và bộ
đếm khung mất tăng; PC nhận biết qua `idx` không liên tục.

## 8. Kết quả kiểm thử trên PC

Tầng khung (`protocol.c`, `checksum.c`) không phụ thuộc HAL nên được kiểm thử
trên PC bằng `test/test_parser.c` (26 phép kiểm tra) và `pc/fuzz_test.py`.

Tỉ lệ phát hiện lỗi, 200 000 khung bị đảo ngẫu nhiên 1–4 bit:

| Mã kiểm tra | Số byte | Khung lỗi bị bỏ sót | Tỉ lệ phát hiện |
|---|---|---|---|
| SUM-8 | 1 | 4 128 | 97,936 % |
| CRC-8 | 1 | 457 | 99,772 % |
| CRC-16 | 2 | 1 | 99,9995 % |

CRC-16/CCITT phát hiện mọi lỗi 1, 2 và 3 bit với khung ngắn; lỗi 4 bit có thể
bị bỏ sót với xác suất khoảng 2⁻¹⁶.

Đồng bộ lại:

| Trường hợp | Kết quả |
|---|---|
| 20 000 khung, mỗi khung có 0–7 byte rác phía trước | nhận đủ 20 000 khung |
| 500 khung, mỗi khung có 64 byte rác phía trước | nhận đủ 500 khung |
| 40 khung, trước mỗi khung có `00 AA AA 55` | nhận đủ 40 khung |
| Khung hợp lệ nằm trong vùng DATA của một khung lỗi | tách được khung hợp lệ |
| Khung thiếu byte | huỷ sau 50 ms, khung kế tiếp nhận bình thường |

Khung do `protocol.c` và `v01proto.py` tạo ra cho cùng một lệnh giống nhau
từng byte.

## 9. Giới hạn

1. Không dùng byte stuffing: `0xAA 0x55` có thể xuất hiện trong `DATA`. Bộ
   tách khung dựa vào `LEN`, CRC và cơ chế đồng bộ lại để xử lý.
2. Không tự động truyền lại: tầng khung chỉ phát hiện lỗi; việc gửi lại do PC
   quyết định.
3. `SEQ` và `CMD` trong NACK của khung sai CRC không bảo đảm đúng.
4. `DHT_READ` chặn vòng lặp chính khoảng 20 ms do yêu cầu của xung khởi động
   DHT11. Trong thời gian đó việc đóng gói khung streaming bị trễ; ở tần số
   lấy mẫu cao (thời gian đầy nửa bộ đệm dưới 20 ms) có thể mất mẫu.
5. Một khung `STREAM_DATA` chứa tối đa 60 mẫu.
