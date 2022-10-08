#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <ctype.h>
#include <assert.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/time.h>

int main(int argc, char *argv[])
{
        memmove(argv, argv + 1, sizeof(char *) * argc);
        argv[argc] = NULL;

        /* Prepare pipe */
        int fds[2];
        if (pipe(fds) == -1)
                return errno;

        pid_t pid = fork();
        if (pid == 0) {
                /* Redirect the stdout to the write pipe */
                if (dup2(fds[1], 1) == -1)
                        return errno;

                close(fds[1]);
                close(fds[0]);

                execvp(*argv, argv);
                perror("");
                return ENOENT;
        }

        close(fds[1]);

        char buf[0xff] = {0};

        ssize_t n_read = 0;

        size_t lines = 0;
        size_t words = 0;
        size_t bytes = 0;

        enum states {
                WORD,
                NOWORD,
        };

        int state = NOWORD;
        do {
                n_read = read(fds[0], buf, sizeof(buf));
                for (size_t i = 0; i < n_read; i++) {

                        if (buf[i] == '\n')
                                lines++;

                        switch (state) {
                        case WORD:
                                if (isspace(buf[i]))
                                        state = NOWORD;
                                break;
                        case NOWORD:
                                if (isspace(buf[i]))
                                        continue;

                                state = WORD;
                                words++;
                                break;
                        default:
                                assert(0);
                        }
                }

                bytes += n_read;
        } while (n_read);

        int status = 0;
        wait(&status);

        status = WEXITSTATUS(status);
        if (status == ENOENT)
                return status;

        close(fds[0]);

        fprintf(stderr, "\t%ld\t%ld\t%ld\n", lines, words, bytes);
        return 0;
}


