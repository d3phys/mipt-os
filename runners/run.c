#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/msg.h>
#include <sys/wait.h>
#include <sys/time.h>

static const long READY_ID = 21;

static int msqid;
#define msg(...) fprintf(stderr, __VA_ARGS__)

void
safe_perror()
{
    int saved_errno = errno;
    perror("");
    errno = saved_errno;
}

int
runner(long id)
{
    msg("runner %ld: here\n", id - 1);

    long msg = 1;
    if (msgsnd(msqid, &msg, 0, 0) == -1)
        return safe_perror(), errno;

    if (msgrcv(msqid, &msg, 0, id, 0) == -1)
        return safe_perror(), errno;

    msg("runner %ld: run\n", id - 1);

    msg = id + 1;
    if (msgsnd(msqid, &msg, 0, 0) == -1)
        return safe_perror(), errno;

    return 0;
}

int
judge(long n_runners)
{
    msg("judge: here\n");
    msg("judge: wait for runners\n");

    long msg;
    for (long i = 0; i < n_runners; i++)
        if (msgrcv(msqid, &msg, 0, 1, 0) == -1)
            return safe_perror(), errno;

    msg("judge: start\n");
    struct timeval start = {0};
    gettimeofday(&start, NULL);

    msg = 1;
    if (msgsnd(msqid, &msg, 0, 0) == -1)
        return safe_perror(), errno;

    if (msgrcv(msqid, &msg, 0, n_runners + 1, 0) == -1)
        return safe_perror(), errno;

    msg("judge: stop\n");
    struct timeval stop = {0};
    gettimeofday(&stop, NULL);

    struct timeval elapsed = {0};
    timersub(&stop, &start, &elapsed);
    msg("judge: time %ld usec\n", elapsed.tv_sec * 1000000 + elapsed.tv_usec);
    return 0;
}

int
main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "Must be 1 argument\n");
        return EXIT_FAILURE;
    }

    /* Create message queue */
    const int prot = S_IRUSR | S_IWUSR;
    msqid = msgget(IPC_PRIVATE, IPC_CREAT | prot);
    if (msqid == -1)
        return perror(""), EXIT_FAILURE;

    long n_runners = atoi(argv[1]);
    pid_t judge_pid = fork();
    if (judge_pid == 0)
        return judge(n_runners);

    for (long id = 1; id <= n_runners; ++id) {
        pid_t runner_pid = fork();
        if (runner_pid == 0)
            return runner(id);
    }

    int status = 0;
    waitpid(judge_pid, &status, 0);

    status = WEXITSTATUS(status);
    if (status == ENOENT)
            return status;

    if (msgctl(msqid, IPC_RMID, NULL) == -1)
        return perror(""), EXIT_FAILURE;

    return 0;
}
