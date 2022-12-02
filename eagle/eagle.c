#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <time.h>
#include <stdarg.h>

// Hit Ctrl-C to exit
//

typedef int semaphore_t;
semaphore_t bowl, need_food;

int shmid, semid;
size_t *food;

void
action(const char *fmt, ...)
{
    va_list arglist;
    va_start(arglist, fmt);
    vfprintf(stderr, fmt, arglist);
    va_end(arglist);
}

int
lock(semaphore_t sem)
{
    struct sembuf lock = {
        .sem_num = sem,
        .sem_op = -1,
        .sem_flg = 0,
    };

    return semop(semid, &lock, 1);
}

int
unlock(semaphore_t sem)
{
    struct sembuf unlock = {
        .sem_num = sem,
        .sem_op =  1,
        .sem_flg = 0,
    };

    return semop(semid, &unlock, 1);
}

int
mom()
{
    const size_t n_portions = 8;

    for (;;) {
        lock(need_food);
        action("Maть: прилетела\n");

        (*food) += n_portions;
        action("Maть: положила %lu порций еды (всего %ld)\n", n_portions, *food);

        // Эти строчки можно и местами поменять,
        // я думаю так прикольней...
        unlock(bowl);
        action("Maть: улетела когда захотела\n");
    }

    return 0;
}

int
eaglet(size_t num)
{
    for (;;) {
        lock(bowl);

        (*food)--;
        action("Птенец %lu: съел порцию (стало %lu)\n", num, *food);
        if (*food == 0) {
            action("Птенец %lu: позвал мать\n", num);
            unlock(need_food);
        } else {
            unlock(bowl);
        }

        action("Птенец %lu: лег спать\n", num);
        sleep(rand() % 4);
    }

    return 0;
}

void handler(int signo)
{
    shmdt(food);
    shmctl(shmid, IPC_RMID, NULL);
    semctl(semid, 0, IPC_RMID);

    const char msg[] = "Goodbye!!!\n";
    write(1, msg, sizeof(msg));

    exit(0);
}

int
main(int argc, char *argv[])
{
    srand(time(NULL));
    const size_t kchilds = 5;

    semid = semget(IPC_PRIVATE, 2, 0666);
    if (semid == -1)
        return perror(""), errno;

    bowl = 0;
    need_food = 1;

    shmid = shmget(IPC_PRIVATE, sizeof(size_t), 0666);
    if (shmid == -1)
        return perror(""), errno;

    food = shmat(shmid, NULL, 0666);
    *food = 14;

    unlock(bowl);

    pid_t pid = fork();
    if (pid == 0)
        return mom();

    for (size_t i = 0; i < kchilds; i++) {
        pid_t pid = fork();
        if (pid == 0)
            return eaglet(i);
    }

    signal(SIGINT, handler);

    int status = 0;
    for (size_t i = 0; i < kchilds; i++) {
        wait(&status);
        status = WEXITSTATUS(status);
        if (status == ENOENT)
            return status;
    }

    shmdt(food);
    shmctl(shmid, IPC_RMID, NULL);
    semctl(semid, 0, IPC_RMID);
    return errno;
}
