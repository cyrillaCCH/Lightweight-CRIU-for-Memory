#ifndef __CRIU_MTD_H__
#define __CRIU_MTD_H__

#include <stdbool.h>
#include <stdint.h>

int mtd_connect(void);
void mtd_disconnect(void);
int mtd_register_uffd(int pid, int uffd);
int mtd_is_dirty(int pid, uint64_t addr);
int mtd_clear(int pid);
int mtd_check_pid(int pid);
int mtd_mark_dirty_range(int pid, uint64_t addr, uint64_t len);

#endif
