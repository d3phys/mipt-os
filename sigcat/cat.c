#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>

#define $ fprintf(stderr, "%s: %d\n", __PRETTY_FUNCTION__, __LINE__);

#define SIG0 SIGUSR1
#define SIG1 SIGUSR2

void perror_s(const char *msg)
{
    int saved_errno = errno;
    perror(msg);
    errno = saved_errno;
}

volatile sig_atomic_t bit;

void
handler(int signo)
{
    if (signo == SIG1)
        bit = 1;
    else
        bit = 0;
}

int
writer(pid_t pid,
       int fd)
{$
    sigset_t empty_set;
    sigemptyset(&empty_set);

    struct sigaction sa;
    sa.sa_handler = handler;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIG0, &sa, NULL) != 0)
        return perror_s("sigaction failed"), errno;
    if (sigaction(SIG1, &sa, NULL) != 0)
        return perror_s("sigaction failed"), errno;

    for (;;) {
        unsigned char byte = 0;
        for (size_t k = 0; k != 8; k++) {
            sigsuspend(&empty_set);
            byte |= ((unsigned char)bit << k);
            kill(pid, SIG0);
        }

        write(fd, &byte, 1);
    }

    return 0;
}

void
handler_ignore(int signo)
{
}

int
reader(pid_t pid,
       int fd)
{$
    sigset_t empty_set;
    sigemptyset(&empty_set);

    struct sigaction sa;
    sa.sa_handler = handler_ignore;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIG0, &sa, NULL) != 0)
        return perror_s("sigaction failed"), errno;
    if (sigaction(SIG1, &sa, NULL) != 0)
        return perror_s("sigaction failed"), errno;

    static char buf[0xff];
    size_t n_read = 0;
    do {
        n_read = read(fd, buf, sizeof(buf));
        if (n_read == -1)
            return perror_s("cat read failed"), errno;

        for (size_t i = 0; i != n_read; i++) {
            unsigned char byte = buf[i];
            for (size_t k = 0; k != 8; k++) {
                if (byte >> k & 1)
                    kill(pid, SIG1);
                else
                    kill(pid, SIG0);

                sigsuspend(&empty_set);
            }
        }
    }
    while (n_read != 0);

    return 0;
}

int
cat(int fd)
{$
    /* Block both signals in parent */
    sigset_t block_set, old_set;

    sigemptyset(&block_set);
    sigaddset(&block_set, SIGUSR1);
    sigaddset(&block_set, SIGUSR2);

    if (sigprocmask(SIG_BLOCK, &block_set, &old_set))
        return perror_s("sigprocmask"), errno;

    pid_t mypid = getpid();
    fprintf(stderr, "pid %d\n", mypid);
    pid_t pid = fork();
    if (pid == 0)
        return writer(mypid, 1);

    reader(pid, fd);
    kill(pid, SIGTERM);

    /* Restore old block mask */
    if (sigprocmask(SIG_BLOCK, &old_set, NULL))
        return perror_s("sigprocmask"), errno;

    return 0;
}

int
main(int argc,
     const char *argv[])
{$
    printf("Hello world\n");

    if (argc == 1) {
        cat(0);
    } else {
        for (int i = 1; i < argc; i++) {
            int fd = open(argv[i], O_RDONLY);
            if (fd == -1) {
                perror_s("cat");
                continue;
            }

            cat(fd);
            close(fd);
        }
    }

    return 0;
}
