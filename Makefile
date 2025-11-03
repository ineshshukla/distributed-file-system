CC=gcc
CFLAGS=-O2 -Wall -Wextra -Werror -pthread -std=c11

SRC_COMMON=src/common/net.c src/common/log.c src/common/protocol.c src/common/errors.c
SRC_SS=src/ss/file_scan.c src/ss/file_storage.c
INC_COMMON=-Isrc/common -Isrc/ss

all: nm ss client

nm: $(SRC_COMMON) src/nm/main.c
	$(CC) $(CFLAGS) $(INC_COMMON) -o bin_nm src/nm/main.c $(SRC_COMMON)

ss: $(SRC_COMMON) $(SRC_SS) src/ss/main.c
	$(CC) $(CFLAGS) $(INC_COMMON) -o bin_ss src/ss/main.c $(SRC_COMMON) $(SRC_SS)

client: $(SRC_COMMON) src/client/main.c
	$(CC) $(CFLAGS) $(INC_COMMON) -o bin_client src/client/main.c $(SRC_COMMON)

clean:
	rm -f bin_nm bin_ss bin_client

.PHONY: all clean

