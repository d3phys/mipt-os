#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <ctype.h>
#include <assert.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <poll.h>

void perror_s(const char *msg)
{
    assert(msg);
    int saved_errno = errno;
    perror(msg);
    errno = saved_errno;
}

struct command {
    const char *path;
    char *args[2];
};

/**
 * write pair: megacat --> cat
 * read  pair: cat --> megacat
 */
struct pipes_pair {
    int read[2];
    int write[2];
};

struct buffer {
    char data[0x1000];
    ssize_t size;
};

int main(int argc,
         char *argv[])
{
    if (argc != 3)
        return fprintf(stderr, "Invalid arguments number"), EXIT_FAILURE;

    int n_cats = atoi(argv[2]);
    if (n_cats == 0)
        return fprintf(stderr, "Invalid number of cats"), EXIT_FAILURE;

    fprintf(stderr, "Cats number: %d\n", n_cats);

    struct command cmd = {
        .path = argv[1],
        .args = { argv[1], NULL},
    };

    /* All cats fds and stdin/stdout */
    int n_fds = 2 * n_cats + 2;
    struct pollfd *fds = calloc(n_fds, sizeof(struct pollfd));
    if (fds == NULL)
        return perror_s("Calloc failed"), EXIT_FAILURE;

    struct pollfd *write_fds = fds;
    struct pollfd  *read_fds = fds + n_cats + 1;

    fprintf(stderr, "write : %p\n read: %p\n", write_fds, read_fds);

    /* Initialize poll fds */
    for (int i = 0; i != n_cats + 1; ++i) {
        write_fds[i].events = POLLOUT;
         read_fds[i].events = POLLIN;
    }

    size_t n_pipes = 2 * n_cats;
    struct pipes_pair *pairs = calloc(n_pipes, sizeof(struct pipes_pair *));
    if (pairs == NULL)
        return perror_s("Calloc failed"), EXIT_FAILURE;

    for (int i = 0; i != n_cats; ++i) {
        if (pipe(pairs[i].write) == -1 ||
            pipe(pairs[i].read)  == -1)
        {
            return perror_s("Can't create pipes pair"), EXIT_FAILURE;
        }

        pid_t pid = fork();
        if (pid == 0) {
            if (dup2(pairs[i].write[0], 0) == -1 ||
                dup2(pairs[i].read[1],  1) == -1)
            {
                return perror_s("Can't dup2"), EXIT_FAILURE;
            }

            /* Close all parent pipes */
            for (int j = 0; j != i ; j++) {
                close(pairs[j].read[0]),  close(pairs[j].read[1]);
                close(pairs[j].write[0]), close(pairs[j].write[1]);
            }

            execvp(cmd.path, cmd.args);
            perror_s("Can't run cat");
            return errno;
        }

        /* Close fds in parent */
        close(pairs[i].read[1]);
        close(pairs[i].write[0]);

        /* Set fds */
        write_fds[i].fd = pairs[i].write[1];
        read_fds[i + 1].fd = pairs[i].read[0];
    }

    read_fds[0].fd = 0;
    write_fds[n_cats].fd = 1;

    struct buffer *bufs = calloc(n_cats + 2, sizeof(struct buffer));
    if (bufs == NULL)
        return perror_s("Buffers calloc failed"), EXIT_FAILURE;

    for (;;) {
        int n = poll(fds, n_fds, 10);
        if (n == -1)
            return perror_s("poll() failed"), EXIT_FAILURE;

        for (int i = 0; i < n_fds; i++) {
            int buf_num = i % (n_cats + 1);
            if (fds[i].revents & POLLIN) {
                if (bufs[buf_num].size == 0) {
                    ssize_t n_read = read(fds[i].fd, bufs[buf_num].data, 0x1000);
                    if (n_read == -1)
                        return perror_s("read failed"), EXIT_FAILURE;

                    bufs[buf_num].size = n_read;
                }
            }
            else if (fds[i].revents & POLLOUT) {
                if (bufs[buf_num].size != 0) {
                    ssize_t n_write = write(fds[i].fd, bufs[buf_num].data, 0x1000);
                    if (n_write == -1)
                        return perror_s("write failed"), EXIT_FAILURE;

                    bufs[buf_num].size -= n_write;
                    memset(bufs[buf_num].data, 0, 0x1000);
                }
            }
        }
    }

    free(fds);
    return EXIT_SUCCESS;
}
