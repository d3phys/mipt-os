#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

int
cat(int src,         /* File descriptor from */
    int dst,         /* File descriptor to   */
    char *const buf, /* Buffer               */
    size_t bufsz)    /* Buffer size          */
{
        ssize_t n_read = 0;

        /**
         * Read zero indicates end of file.
         * But the number of bytes read mey be smaller
         * than the number of bytes requested.
         *
         * Read until read() returns zero.
         */
        do {
                n_read = read(src, buf, bufsz);
                if (n_read == -1) {
                        int saved_errno = errno;
                        perror("cat read failed");
                        return saved_errno;
                }

                /**
                 * write() may transfer fewer than n_read bytes.
                 * In the event of partial write, make another write
                 * to transfer the remaining bytes.
                 */
                for (ssize_t n_left = n_read; n_left;) {

                        ssize_t n_writt = write(dst, buf + n_read - n_left, n_left);
                        if (n_writt == -1) {
                                int saved_errno = errno;
                                perror("cat write failed");
                                return saved_errno;
                        }

                        n_left -= n_writt;
                }

        } while (n_read);

        return 0;
}

int
main(const int argc,
     const char *argv[])
{
        /**
         * Initialize cat buffer
         */
        const size_t bufsz = getpagesize();
        void *buf = valloc(bufsz);

        if (argc == 1) {
                int error = cat(0, 1, buf, bufsz);
                free(buf);
                return error;
        }

        for (int i = 1; i < argc; i++) {

                int fd = open(argv[i], O_RDONLY);
                if (fd == -1) {
                        perror("cat");
                        continue;
                }

                int error = cat(fd, 1, buf, bufsz);
                if (error) {
                        free(buf);
                        return error;
                }
        }

        free(buf);
        return EXIT_SUCCESS;
}
