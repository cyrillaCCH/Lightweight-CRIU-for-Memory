#include <iostream>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <thread>
#include <mutex>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/userfaultfd.h>
#include <poll.h>
#include <string.h>
#include "mtd_proto.h"

#define MAX_EVENTS 64

struct ProcessInfo {
    int uffd;
    std::unordered_set<uint64_t> dirty_pages;
};

std::unordered_map<uint32_t, ProcessInfo> tracked_processes;
std::mutex processes_mutex;
int epoll_fd;

int recv_fd(int sock) {
    struct msghdr msg = {0};
    char buf[CMSG_SPACE(sizeof(int))];
    char dummy;
    struct iovec io = {
        .iov_base = &dummy,
        .iov_len = 1
    };

    msg.msg_iov = &io;
    msg.msg_iovlen = 1;
    msg.msg_control = buf;
    msg.msg_controllen = sizeof(buf);

    ssize_t n = recvmsg(sock, &msg, 0);
    if (n <= 0) {
        if (n < 0) perror("recvmsg in recv_fd");
        return -1;
    }

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    if (!cmsg || cmsg->cmsg_type != SCM_RIGHTS) {
        std::cerr << "recv_fd: No SCM_RIGHTS control message received" << std::endl;
        return -1;
    }

    return *((int*)CMSG_DATA(cmsg));
}

long page_size;

void handle_uffd_event(int uffd, uint32_t pid) {
    struct uffd_msg msg;
    ssize_t nread = read(uffd, &msg, sizeof(msg));
    if (nread != sizeof(msg)) return;

    if (msg.event == UFFD_EVENT_PAGEFAULT) {
        if (msg.arg.pagefault.flags & UFFD_PAGEFAULT_FLAG_WP) {
            uint64_t addr = msg.arg.pagefault.address & ~(page_size - 1);
            {
                std::lock_guard<std::mutex> lock(processes_mutex);
                tracked_processes[pid].dirty_pages.insert(addr);
            }

            // Un-write-protect the page
            struct uffdio_writeprotect wp;
            wp.range.start = addr;
            wp.range.len = page_size;
            wp.mode = 0;
            if (ioctl(uffd, UFFDIO_WRITEPROTECT, &wp) == -1) {
                perror("ioctl(UFFDIO_WRITEPROTECT)");
            }
        }
    }
}

void mtd_daemon() {
    int server_sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_sock == -1) {
        perror("socket");
        return;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, MTD_SOCK_PATH, sizeof(addr.sun_path) - 1);
    unlink(MTD_SOCK_PATH);

    if (bind(server_sock, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        perror("bind");
        return;
    }

    if (listen(server_sock, 5) == -1) {
        perror("listen");
        return;
    }

    epoll_fd = epoll_create1(0);
    struct epoll_event ev, events[MAX_EVENTS];
    ev.events = EPOLLIN;
    ev.data.fd = server_sock;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_sock, &ev);

    while (true) {
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        for (int n = 0; n < nfds; ++n) {
            if (events[n].data.fd == server_sock) {
                int client_sock = accept(server_sock, NULL, NULL);
                ev.events = EPOLLIN;
                ev.data.fd = client_sock;
                epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_sock, &ev);
            } else {
                int fd = events[n].data.fd;
                // Check if it's a client socket or a uffd
                bool is_uffd = false;
                uint32_t uffd_pid = 0;
                {
                    std::lock_guard<std::mutex> lock(processes_mutex);
                    for (auto const& [pid, info] : tracked_processes) {
                        if (info.uffd == fd) {
                            is_uffd = true;
                            uffd_pid = pid;
                            break;
                        }
                    }
                }

                if (is_uffd) {
                    handle_uffd_event(fd, uffd_pid);
                } else {
                    // Client request
                    struct mtd_msg m;
                    ssize_t nread = recv(fd, &m, sizeof(m), MSG_WAITALL);
                    if (nread <= 0) {
                        if (nread < 0) perror("recv client request");
                        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
                        close(fd);
                        continue;
                    }

                    struct mtd_resp resp;
                    resp.status = MTD_STATUS_OK;

                    std::cout << "MTD: cmd=" << m.cmd << " pid=" << m.pid << " addr=" << std::hex << m.addr << std::dec << std::endl;

                    if (m.cmd == MTD_CMD_REGISTER) {
                        int uffd = recv_fd(fd);
                        if (uffd != -1) {
                            std::lock_guard<std::mutex> lock(processes_mutex);
                            if (tracked_processes.count(m.pid)) {
                                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, tracked_processes[m.pid].uffd, NULL);
                                close(tracked_processes[m.pid].uffd);
                            }
                            tracked_processes[m.pid].uffd = uffd;
                            tracked_processes[m.pid].dirty_pages.clear();
                            ev.events = EPOLLIN;
                            ev.data.fd = uffd;
                            if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, uffd, &ev) == -1) {
                                perror("epoll_ctl ADD uffd");
                                resp.status = MTD_STATUS_ERROR;
                            } else {
                                std::cout << "Successfully registered uffd " << uffd << " for pid " << m.pid << std::endl;
                            }
                        } else {
                            std::cerr << "Failed to receive uffd FD for pid " << m.pid << std::endl;
                            resp.status = MTD_STATUS_ERROR;
                        }
                    } else if (m.cmd == MTD_CMD_IS_DIRTY) {
                        std::lock_guard<std::mutex> lock(processes_mutex);
                        if (tracked_processes.count(m.pid)) {
                            if (tracked_processes[m.pid].dirty_pages.count(m.addr & ~(page_size - 1))) {
                                resp.status = MTD_STATUS_DIRTY;
                            } else {
                                resp.status = MTD_STATUS_CLEAN;
                            }
                        } else {
                            resp.status = MTD_STATUS_NOT_FOUND;
                        }
                    } else if (m.cmd == MTD_CMD_CLEAR) {
                        std::lock_guard<std::mutex> lock(processes_mutex);
                        if (tracked_processes.count(m.pid)) {
                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, tracked_processes[m.pid].uffd, NULL);
                            close(tracked_processes[m.pid].uffd);
                            tracked_processes.erase(m.pid);
                        }
                    } else if (m.cmd == MTD_CMD_CHECK) {
                        std::lock_guard<std::mutex> lock(processes_mutex);
                        if (!tracked_processes.count(m.pid)) {
                            resp.status = MTD_STATUS_NOT_FOUND;
                        }
                    } else if (m.cmd == MTD_CMD_MARK_DIRTY_RANGE) {
                        std::lock_guard<std::mutex> lock(processes_mutex);
                        if (tracked_processes.count(m.pid)) {
                            uint64_t start_addr = m.addr & ~(page_size - 1);
                            uint64_t end_addr = (m.addr + m.len + page_size - 1) & ~(page_size - 1);
                            for (uint64_t a = start_addr; a < end_addr; a += page_size) {
                                tracked_processes[m.pid].dirty_pages.insert(a);
                            }
                            resp.status = MTD_STATUS_OK;
                        } else {
                            resp.status = MTD_STATUS_NOT_FOUND;
                        }
                    }

                    send(fd, &resp, sizeof(resp), 0);
                }
            }
        }
    }
}

int main() {
    page_size = sysconf(_SC_PAGESIZE);
    mtd_daemon();
    return 0;
}
