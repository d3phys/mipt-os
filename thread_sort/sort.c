#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>
#include <sys/stat.h>
#include <sys/mman.h>

#define HARD_DEBUG
#ifdef HARD_DEBUG
#define $ fprintf(stderr, "%s: %d\n", __PRETTY_FUNCTION__, __LINE__);
#else
#define $
#endif

void perror_s(const char *msg)
{
    int saved_errno = errno;
    perror(msg);
    errno = saved_errno;
}

int compare_ints(const void* a, const void* b)
{
    int arg1 = *(const int*)a;
    int arg2 = *(const int*)b;

    if (arg1 < arg2) return -1;
    if (arg1 > arg2) return 1;
    return 0;
}

int
sort(int *data,
     size_t count)
{
    qsort(data, count, sizeof(int), compare_ints);
    return 0;
}

struct pack_t {
    size_t size;
    int *data;
};

void *
load_thread(void *args_p)
{$
    struct pack_t *args = (struct pack_t *)args_p;
    sort(args->data, args->size);
    return NULL;
}

void
dump_data(int *data,
          size_t count)
{
    for (size_t j = 0; j < count; j++)
        fprintf(stderr, "%d ", data[j]);
    fprintf(stderr, "\n");
}

int
main(int argc,
     const char *argv[])
{$
    if (argc != 3)
        return fprintf(stderr, "Invalid arguments count\n"), EXIT_FAILURE;

    int count = atoi(argv[1]);
    if (count == 0)
        return fprintf(stderr, "Invalid elements count\n"), EXIT_FAILURE;

    int n_threads = atoi(argv[2]);
    if (n_threads == 0)
        return fprintf(stderr, "Invalid threads count\n"), EXIT_FAILURE;

    fprintf(stderr, "Threads count: %d\n", n_threads);
    fprintf(stderr, "Elements count: %d\n", count);

    int *data = (int *)calloc(count, sizeof(int));
    if (data == NULL)
        return fprintf(stderr, "Calloc failed\n"), EXIT_FAILURE;

    for (size_t i = 0; i < count; i++)
        data[i] = rand() % 20;

    size_t pack_sz = count / n_threads;
    size_t last_pack_sz = count % n_threads;
    if (last_pack_sz == 0) {
        pack_sz = count / n_threads;
    } else {
        pack_sz = count / n_threads + 1;
        last_pack_sz = count - pack_sz * (n_threads - 1);
    }

    fprintf(stderr, "Pack size: %lu\n", pack_sz);
    fprintf(stderr, "Last thread pack size: %lu\n", last_pack_sz);

    dump_data(data, count);

    pthread_t *tids = (pthread_t *)calloc(n_threads, sizeof(pthread_t));
    if (tids == NULL)
        return fprintf(stderr, "Calloc failed\n"), EXIT_FAILURE;

    struct pack_t *packs = (struct pack_t *)calloc(n_threads, sizeof(struct pack_t));
    if (packs == NULL)
        return fprintf(stderr, "Calloc failed\n"), EXIT_FAILURE;

    for (int i = 0; i < n_threads; i++) {
        packs[i].data = data + i * pack_sz;
        packs[i].size = pack_sz;

        if (i == n_threads - 1 && last_pack_sz != 0)
            packs[i].size = last_pack_sz;

        pthread_create(tids + i, NULL, load_thread, &packs[i]);
    }

    void *ret = NULL;
    for (int i = 0; i < n_threads; i++)
        pthread_join(tids[i], &ret);

    dump_data(data, count);

    for (int i = 0; i < n_threads; i++)
        fprintf(stderr, "pack size: %lu\n", packs[i].size);

    int *new_data = (int *)calloc(count, sizeof(int));
    if (new_data == NULL)
        fprintf(stderr, "Calloc failed\n");

    for (size_t i = 0; i < count; i++) {
        size_t min_idx = 0;
        for (size_t j = 0; j < n_threads; j++) {
            if (packs[j].size == 0)
                continue;

            if (packs[min_idx].size == 0 ||
                *packs[j].data < *packs[min_idx].data) {
                min_idx = j;
            }
        }

        new_data[i] = *packs[min_idx].data;
        packs[min_idx].data++;
        packs[min_idx].size--;
    }

    dump_data(data, count);
    dump_data(new_data, count);

    qsort(data, count, sizeof(int), compare_ints);
    dump_data(data, count);

    for (size_t i = 0; i < count; i++) {
        if (data[i] != new_data[i]) {
            fprintf(stderr, "FAIL\n");
            free(tids);
            free(packs);
            free(new_data);
            free(data);
            return EXIT_FAILURE;
        }
    }

    free(tids);
    free(packs);
    free(new_data);
    free(data);
    return 0;
}


