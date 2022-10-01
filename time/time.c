#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/time.h>

int
main(int argc, char *argv[])
{
        memmove(argv, argv + 1, sizeof(char *) * argc);

        struct timeval start = {0};
        gettimeofday(&start, NULL);

        pid_t pid = fork();
        if (pid == 0) {
                execvp(*argv, argv);
                perror("");
                return ENOENT;
        }

        int status = 0;
        wait(&status);

        status = WEXITSTATUS(status);
        if (status == ENOENT)
                return status;

        struct timeval end = {0};
        gettimeofday(&end, NULL);

        struct timeval elapsed = {0};
        timersub(&end, &start, &elapsed);

        fprintf(stderr, "Elapsed time: %ld usec\n", elapsed.tv_sec * 1000000 + elapsed.tv_usec);

        return 0;
}
