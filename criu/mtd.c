#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>

#include "mtd.h"
#include "../../mtd/mtd_proto.h"
#include "common/scm.h"
#include "criu-log.h"

static int mtd_sk = -1;

int mtd_connect(void) {
    struct sockaddr_un addr;

    if (mtd_sk != -1) return 0;

    mtd_sk = socket(AF_UNIX, SOCK_STREAM, 0);
    if (mtd_sk == -1) {
        pr_perror("MTD: Failed to create socket");
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, MTD_SOCK_PATH, sizeof(addr.sun_path) - 1);

    if (connect(mtd_sk, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        pr_perror("MTD: Failed to connect to daemon at %s", MTD_SOCK_PATH);
        close(mtd_sk);
        mtd_sk = -1;
        return -1;
    }

    return 0;
}

void mtd_disconnect(void) {
    if (mtd_sk != -1) {
        close(mtd_sk);
        mtd_sk = -1;
    }
}

int mtd_register_uffd(int pid, int uffd) {
    struct mtd_msg msg = {
        .cmd = MTD_CMD_REGISTER,
        .pid = pid,
        .addr = 0
    };
    struct mtd_resp resp;

    if (mtd_sk == -1 && mtd_connect() < 0) return -1;

    if (send(mtd_sk, &msg, sizeof(msg), 0) == -1) {
        pr_perror("MTD: Failed to send REGISTER command");
        return -1;
    }

    if (send_fd(mtd_sk, NULL, 0, uffd) == -1) {
        pr_perror("MTD: Failed to send uffd FD");
        return -1;
    }

    if (recv(mtd_sk, &resp, sizeof(resp), MSG_WAITALL) <= 0) {
        pr_perror("MTD: Failed to receive response for REGISTER");
        return -1;
    }

    return resp.status == MTD_STATUS_OK ? 0 : -1;
}

int mtd_is_dirty(int pid, uint64_t addr) {
    struct mtd_msg msg = {
        .cmd = MTD_CMD_IS_DIRTY,
        .pid = pid,
        .addr = addr
    };
    struct mtd_resp resp;

    if (mtd_sk == -1 && mtd_connect() < 0) return -1;

    if (send(mtd_sk, &msg, sizeof(msg), 0) == -1) {
        pr_perror("MTD: Failed to send IS_DIRTY command");
        return -1;
    }

    if (recv(mtd_sk, &resp, sizeof(resp), MSG_WAITALL) <= 0) {
        pr_perror("MTD: Failed to receive response for IS_DIRTY");
        return -1;
    }

    return resp.status;
}

int mtd_clear(int pid) {
    struct mtd_msg msg = {
        .cmd = MTD_CMD_CLEAR,
        .pid = pid,
        .addr = 0
    };
    struct mtd_resp resp;

    if (mtd_sk == -1 && mtd_connect() < 0) return -1;

    if (send(mtd_sk, &msg, sizeof(msg), 0) == -1) {
        pr_perror("MTD: Failed to send CLEAR command");
        return -1;
    }

    if (recv(mtd_sk, &resp, sizeof(resp), MSG_WAITALL) <= 0) {
        pr_perror("MTD: Failed to receive response for CLEAR");
        return -1;
    }

    return resp.status == MTD_STATUS_OK ? 0 : -1;
}

int mtd_check_pid(int pid) {
    struct mtd_msg msg = {
        .cmd = MTD_CMD_CHECK,
        .pid = pid,
        .addr = 0,
        .len = 0
    };
    struct mtd_resp resp;

    if (mtd_sk == -1 && mtd_connect() < 0) return -1;

    if (send(mtd_sk, &msg, sizeof(msg), 0) == -1) {
        pr_perror("MTD: Failed to send CHECK command");
        return -1;
    }

    if (recv(mtd_sk, &resp, sizeof(resp), MSG_WAITALL) <= 0) {
        pr_perror("MTD: Failed to receive response for CHECK");
        return -1;
    }

    return resp.status == MTD_STATUS_OK ? 0 : -1;
}

int mtd_mark_dirty_range(int pid, uint64_t addr, uint64_t len) {
    struct mtd_msg msg = {
        .cmd = MTD_CMD_MARK_DIRTY_RANGE,
        .pid = pid,
        .addr = addr,
        .len = len
    };
    struct mtd_resp resp;

    if (mtd_sk == -1 && mtd_connect() < 0) return -1;

    if (send(mtd_sk, &msg, sizeof(msg), 0) == -1) {
        pr_perror("MTD: Failed to send MARK_DIRTY_RANGE command");
        return -1;
    }

    if (recv(mtd_sk, &resp, sizeof(resp), MSG_WAITALL) <= 0) {
        pr_perror("MTD: Failed to receive response for MARK_DIRTY_RANGE");
        return -1;
    }

    return resp.status == MTD_STATUS_OK ? 0 : -1;
}
