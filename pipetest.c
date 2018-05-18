// Test that pipe works atomically 

#include "types.h"
#include "stat.h"
#include "user.h"
#define WRITE_SZ 128
#define FORK_AMOUNT 20

void do_fork(int fd, char c) {
    char buffer[WRITE_SZ];
    for (int i = 0; i < WRITE_SZ; i++) {
        buffer[i] = c;
    }
    int sz = write(fd, buffer, WRITE_SZ);
    if (sz == -1) {
        printf(1, "test failed\n");
    }
    exit();
}

void read_all(int fd) {
    for (int i = 0; i < FORK_AMOUNT; i++) {
        char buffer[WRITE_SZ + 1];
        int cur = 0;
        while (cur != WRITE_SZ) {
            int toadd = read(fd, &buffer[cur], WRITE_SZ-cur);
            if (toadd <= 0) {
                printf(1, "test failed\n");
            }
            cur += toadd;
        }
        buffer[WRITE_SZ] = '\0';
        for (int i = 1; i < WRITE_SZ; i++) {
            if (buffer[i] != buffer[0]) {
                printf(1, "test failed");
            }
        }
    }
    exit();
}

int main(void) {
    int fd[2];
    pipe(fd);
    for (int i = 0; i < FORK_AMOUNT; i++) {
        int pid = fork();
        if (pid == 0) {
            close(fd[0]);
            do_fork(fd[1], 'a' + i);
        }
        if (pid < 0) {
            printf(1, "test failed\n");
        }
    }
    int pid = fork();
    if (pid == 0) {
        read_all(fd[0]);
    }
    close(fd[0]);
    close(fd[1]);
    for (int i = 0; i < FORK_AMOUNT + 1; i++) {
        wait();
    }
    exit();
}
