# Kiểm thử tầng khung trên PC.
#   make            biên dịch test_parser với kiểu mã kiểm tra mặc định
#   make compare    chạy lần lượt với SUM8, CRC8, CRC16

CC      ?= gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -O2 -I../firmware/Inc
SRC      = test_parser.c ../firmware/Src/protocol.c ../firmware/Src/checksum.c

all: test_parser

test_parser: $(SRC)
	$(CC) $(CFLAGS) -o $@ $(SRC)

# Chạy bộ kiểm thử với cả ba kiểu mã kiểm tra
compare:
	$(CC) $(CFLAGS) -DPROTO_CKS_MODE=0 -o t_sum8  $(SRC) && ./t_sum8  | tail -6
	$(CC) $(CFLAGS) -DPROTO_CKS_MODE=1 -o t_crc8  $(SRC) && ./t_crc8  | tail -6
	$(CC) $(CFLAGS) -DPROTO_CKS_MODE=2 -o t_crc16 $(SRC) && ./t_crc16 | tail -6

clean:
	rm -f test_parser t_sum8 t_crc8 t_crc16

.PHONY: all compare clean
