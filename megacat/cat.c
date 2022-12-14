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
#include <stdint.h>
#include <stdbool.h>

#ifdef HARD_DEBUG
#define debug(...) \
  do { fprintf(stderr, __VA_ARGS__); } while (0)
#else
#define debug(...) \
  do { } while (0)
#endif /* HARD_DEBUG */

uint64_t
check_sum(char *data, size_t size) {

    uint64_t checksum = 0;
    for (size_t i = 0; i != size; ++i)
        checksum += (unsigned char)data[i];

    return checksum;
}

void
perror_s(const char *msg)
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
    uint64_t checksum;
};

static void
close_pipes_pair(struct pipes_pair *pp)
{
    close(pp->read[0]),  close(pp->read[1]);
    close(pp->write[0]), close(pp->write[1]);
}

int main(int argc,
         char *argv[])
{
    if (argc != 3)
        return fprintf(stderr, "Invalid arguments number"), EXIT_FAILURE;

    int n_cats = atoi(argv[2]);
    if (n_cats < 0)
        return fprintf(stderr, "Invalid number of cats"), EXIT_FAILURE;

    debug("Cats number: %d\n", n_cats);

    struct command cmd = {
        .path = argv[1],
        .args = { argv[1], NULL},
    };

    /* All cats fds and stdin/stdout */
    int n_fds = 2 * n_cats + 2;
    struct pollfd *fds = calloc(n_fds, sizeof(struct pollfd));
    if (fds == NULL)
        return perror_s("Calloc failed"), EXIT_FAILURE;

    struct pipes_pair *pairs = calloc(n_cats, sizeof(struct pipes_pair));
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
            for (int j = 0; j <= i ; j++)
                close_pipes_pair(&pairs[j]);

            execvp(cmd.path, cmd.args);
            perror_s("Can't run cat");
            return errno;
        }

        /* Close fds in parent */
        close(pairs[i].read[1]);
        close(pairs[i].write[0]);

        /* Set fds */
        fds[2 * i + 1].fd = pairs[i].write[1];
        fds[2 * i + 2].fd = pairs[i].read[0];
    }

    /**
     * Set the first fd to stdin
     * Set the last  fd to stdout
     */
    fds[0].fd = 0;
    fds[n_fds - 1].fd = 1;

    for (int i = 0; i < n_fds; ++i) {
        debug("fds[%d]: %d\n", i, fds[i].fd);
    }

    struct buffer *bufs = calloc(n_cats + 1, sizeof(struct buffer));
    if (bufs == NULL)
        return perror_s("Buffers calloc failed"), EXIT_FAILURE;

    for (;;) {
        /**
         * Setup pollfds _before_ poll call
         *
         * TODO: There is another (and more clever) way
         * of ignoring file descriptor:
         * We can make it negative fd = ~fd.
         */
        for (int i = 0; i != n_fds;) {
            if (fds[i].fd == EOF)
                continue;

            struct buffer *buf = &bufs[i / 2];
            if (buf->size == 0) {
                fds[i++].events = POLLIN;
                fds[i++].events = 0;
            }
            else {
                fds[i++].events = 0;
                fds[i++].events = POLLOUT;
            }
        }

        int n_ready = poll(fds, n_fds, 500);
        if (n_ready == -1)
            return perror_s("poll() failed"), EXIT_FAILURE;

        for (int i = 0; i < n_fds; i++) {
            struct buffer *buf = &bufs[i / 2];

            if (fds[i].revents & POLLIN) {
                debug("fd [%d] pollin \n", i);
                ssize_t n_read = read(fds[i].fd, buf->data, 0x1000);
                if (n_read == -1)
                    return perror_s("read failed"), EXIT_FAILURE;

                debug("read to buffer[%d] %ld bytes\n", i/2, n_read);
                buf->size = n_read;
                buf->checksum += check_sum(buf->data, buf->size);
                debug("buffer[%d] checksum %lu\n", i/2, buf->checksum);
            }
            else if (fds[i].revents & POLLOUT) {
                debug("fd [%d] pollout\n", i);
                ssize_t n_write = write(fds[i].fd, buf->data, buf->size);
                if (n_write == -1)
                    return perror_s("write failed"), EXIT_FAILURE;
                if (fds[i].fd == 1)
                    debug("\n\n\n");

                debug("write from buffer[%d] %ld bytes\n", i/2, buf->size);

                /**
                 * TODO: Now we assume that write call will
                 * write all bytes from buffer
                 */
                buf->size = 0;
                memset(buf->data, 0, 0x1000);
            }
        }

        /**
         * We can not write or read in two cases:
         *
         * 1. We have finished cats chain run
         * 2. All data is stored inside cats internal buffers
         *
         * In the task megacat have to check cats endlessly.
         * That is why we assume that second case will not appear
         * in our pollfd time.
         */
        if (n_ready == 0) {
            for (int i = 0; i != n_cats; i++) {
                if (bufs[i].checksum != bufs[i + 1].checksum) {
                    fprintf(stderr, "Invalid checksum: %lu != %lu\n",
                        bufs[i].checksum,
                        bufs[i + 1].checksum
                    );

                    free(fds);
                    free(bufs);
                    free(pairs);
                    return EXIT_FAILURE;
                }
            }
        }

#ifdef HARD_DEBUG
        sleep(3);
#endif /* HARD_DEBUG */
    }

    free(fds);
    return EXIT_SUCCESS;
}
