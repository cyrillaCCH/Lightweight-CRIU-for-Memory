#ifndef MTD_PROTO_H
#define MTD_PROTO_H

#include <stdint.h>

#define MTD_SOCK_PATH "/tmp/mtd.sock"

enum mtd_cmd {
    MTD_CMD_REGISTER,
    MTD_CMD_IS_DIRTY,
    MTD_CMD_CLEAR,
    MTD_CMD_CHECK,
    MTD_CMD_MARK_DIRTY_RANGE,
};

struct mtd_msg {
    uint32_t cmd;
    uint32_t pid;
    uint64_t addr;
    uint64_t len;
};

struct mtd_resp {
    int32_t status;
};

#define MTD_STATUS_OK 0
#define MTD_STATUS_DIRTY 1
#define MTD_STATUS_CLEAN 2
#define MTD_STATUS_ERROR -1
#define MTD_STATUS_NOT_FOUND -2

#endif
