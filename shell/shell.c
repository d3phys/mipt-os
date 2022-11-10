#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <ctype.h>
#include <assert.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/time.h>

void perror_s(const char *msg)
{
    assert(msg);
    int saved_errno = errno;
    perror(msg);
    errno = saved_errno;
}

struct command {
    const char *path;
    char **args;
};

struct stack {
    size_t size;
    size_t capacity;
    size_t elem_sz;
    void *elems;
};

/**
 * Push item on stack.
 * Returns:
 *      returns 0 in case of failure. Size of stack otherwise.
 *
 * Note! It also handles size overflow :)
 */
size_t stack_push(struct stack *stk, void *elem)
{
    assert(stk && stk->capacity);

    if (!stk->elems || stk->capacity <= stk->size) {
        size_t capacity = stk->capacity << 1;
        void *newbies = realloc(stk->elems, capacity * stk->elem_sz);
        if (!newbies)
            return fprintf(stderr, "stack_push alloc failed\n"), 0;

        stk->elems = newbies;
        stk->capacity = capacity;
    }

    memcpy((char *)stk->elems + stk->size * stk->elem_sz, elem, stk->elem_sz);
    return ++stk->size;
}

void *stack_extract(struct stack *stk)
{
    assert(stk);
    void *newbies = realloc(stk->elems, stk->size * stk->elem_sz);
    if (!newbies)
        return fprintf(stderr, "stack_extract alloc failed\n"), NULL;

    return (stk->elems = newbies);
}

struct command *split_input(char *buf, const size_t kinput_sz, size_t *n_cmds)
{
    assert(buf && n_cmds);

    struct stack tokens = {
        .size = 0,
        .capacity = 0x10,
        .elem_sz = sizeof(char *),
        .elems = NULL,
    };

    memset(buf, 0, kinput_sz);
    ssize_t n_read = read(0, buf, kinput_sz);
    if (n_read == -1)
        return perror("split"), free(tokens.elems), NULL;
    if (n_read == kinput_sz)
        return fprintf(stderr, "split: Buffer overflow\n"), free(tokens.elems), NULL;

    char *token = strtok(buf, "|");
    while (token) {
        stack_push(&tokens, &token);
        token = strtok(NULL, "|");
    }

    struct stack cmds = {
        .size = 0,
        .capacity = 0x10,
        .elem_sz = sizeof(struct command),
        .elems = NULL,
    };

    const char *delim = " \t\n";
    char **toks = stack_extract(&tokens);
    for (size_t i = 0; i != tokens.size; ++i) {

        struct stack args = {
            .size = 0,
            .capacity = 0x4,
            .elem_sz = sizeof(char *),
            .elems = NULL,
        };

        token = strtok(toks[i], delim);
        while (token) {
            stack_push(&args, &token);
            token = strtok(NULL, delim);
        }

        char *null = NULL;
        stack_push(&args, &null);

        char **arg = stack_extract(&args);
        struct command cmd = {
            .path = arg[0],
            .args = arg,
        };

        stack_push(&cmds, &cmd);
    }

    free(tokens.elems);
    *n_cmds = cmds.size;
    return stack_extract(&cmds);
}

void free_cmds(struct command *cmds, size_t n_cmds)
{
    for (size_t i = 0; i != n_cmds; ++i)
        free(cmds[i].args);
    free(cmds);
}

int main(int argc, char *argv[])
{
    const size_t kinput_sz = 0xff;
    char buf[kinput_sz];

    size_t n_cmds = 0;
    struct command *cmds = (struct command *)split_input(buf, kinput_sz, &n_cmds);
    if (!cmds)
        return fprintf(stderr, "split failed\n"), EXIT_FAILURE;

#ifdef DEBUG
    for (size_t i = 0; i < n_cmds; i++) {
        printf("path: %s\n", cmds[i].path);
        char **arg = cmds[i].args;
        printf("args: ");
        do
            printf("%s ", *arg);
        while (*arg++);
        printf("\n\n");
    }
#endif

    if (!n_cmds)
        return EXIT_SUCCESS;

    size_t n_pipes = n_cmds - 1;
    int (*pipes)[2] = malloc(n_pipes * 2 * sizeof(int));
    if (!pipes)
        return free_cmds(cmds, n_cmds), EXIT_FAILURE;

    /* Setup pipes */
    for (size_t i = 0; i != n_pipes; ++i) {
        if (pipe(pipes[i]) == -1)
            return free(pipes), free_cmds(cmds, n_cmds), errno;
    }

    for (size_t i = 0; i < n_cmds; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            /* Redirect stdin to the prev read */
            if (i != 0) {
                if (dup2(pipes[i - 1][0], 0) == -1)
                    return perror_s("write dup2 fail"), errno;
            }

            /* Redirect stdout to the next write */
            if (i < (n_cmds - 1)) {
                if (dup2(pipes[i][1], 1) == -1)
                    return perror_s("read dup2 fail"), errno;
            }

            /* Close other pipes */
            for (size_t j = 0; j != n_pipes; ++j)
                close(pipes[j][0]), close(pipes[j][1]);

            execvp(cmds[i].path, cmds[i].args);
            perror_s("in child");
            return ENOENT;
        }

    }

    /* Close other pipes in grandpa */
    for (size_t j = 0; j != n_pipes; ++j)
        close(pipes[j][0]), close(pipes[j][1]);

    int status = 0;
    for (size_t i = 0; i != n_cmds; ++i) {
      wait(&status);
      status = WEXITSTATUS(status);
      if (status == ENOENT)
          return perror_s("wait status"), status;
    }

    free_cmds(cmds, n_cmds);
    free(pipes);

    return EXIT_SUCCESS;
}
