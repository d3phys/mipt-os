#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>

int
main(int argc, const char *argv[])
{
        int *times = calloc(argc, sizeof(int));
        if (!times)
                return EXIT_FAILURE;

        for (int i = 1; i < argc; i++)
                times[i] = atoi(argv[i]);

        /* Sort */
        for (int i = 1; i < argc; i++) {

                pid_t pid = fork();
                if (pid == -1)
                        return errno;

                if (pid == 0) {
                        if (times[i] > 0)
                                usleep(times[i] * 100);

                        return fprintf(stderr, "%d ", times[i]);
                }
        }

        int status = 0;
        for (int i = 1; i < argc; i++)
                wait(&status);

        fprintf(stderr, "\n");
        free(times);
        return 0;
}
