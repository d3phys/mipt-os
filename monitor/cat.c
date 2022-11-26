#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>
#include <sys/stat.h>
#include <sys/mman.h>

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

struct buffer {
    char *data;
    ssize_t size;
};

struct monitor {
    size_t n_bufs;
    size_t bufsz;
    struct buffer *bufs;
    char *data;

    size_t head;
    size_t tail;
    size_t size;

    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_filled;
};

int
mon_ctor(struct monitor *mon,
         size_t n_bufs)
{$
    assert(mon && n_bufs);

    pthread_mutex_init(&mon->mutex, NULL);
    pthread_mutex_lock(&mon->mutex);

    pthread_cond_init(&mon->not_empty, NULL);
    pthread_cond_init(&mon->not_filled, NULL);

    const size_t page_size = 0x1000;

    struct buffer *bufs = (struct buffer *)calloc(n_bufs, sizeof(struct buffer));
    if (!bufs)
        return perror_s("Monitor buffers allocation failed"), errno;

    char *data = mmap(NULL, n_bufs * page_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (data == MAP_FAILED)
        return free(bufs), perror_s("Monitor data allocation failed"), errno;

    for (size_t i = 0; i != n_bufs; ++i)
        bufs[i].data = data + i * page_size;

    mon->n_bufs = n_bufs;
    mon->bufsz = page_size;
    mon->bufs = bufs;
    mon->data = data;
    mon->tail = 0;
    mon->head = 0;
    mon->size = 0;

    pthread_mutex_unlock(&mon->mutex);
    return 0;
}

void
mon_dtor(struct monitor *mon)
{$
    pthread_mutex_lock(&mon->mutex);

    free(mon->bufs);
    mon->bufs = NULL;

    munmap(mon->data, mon->n_bufs * mon->bufsz);
    mon->data = NULL;

    mon->n_bufs = 0;
    mon->tail = 0;
    mon->head = 0;
    mon->size = 0;

    pthread_cond_destroy(&mon->not_empty);
    pthread_cond_destroy(&mon->not_filled);
    pthread_mutex_unlock(&mon->mutex);
    pthread_mutex_destroy(&mon->mutex);
}

struct buffer *
mon_get_filled(struct monitor *mon)
{$
    pthread_mutex_lock(&mon->mutex);

    if (mon->size == 0)
        pthread_cond_wait(&mon->not_empty, &mon->mutex);

    struct buffer *buf = mon->bufs + mon->tail;

    pthread_mutex_unlock(&mon->mutex);
    return buf;
}

void
mon_put_filled(struct monitor *mon)
{$
    pthread_mutex_lock(&mon->mutex);

    mon->head = (mon->head + 1) % mon->n_bufs;
    mon->size++;

    if (mon->size == 1)
        pthread_cond_signal(&mon->not_empty);

    pthread_mutex_unlock(&mon->mutex);
}

struct buffer *
mon_get_empty(struct monitor *mon)
{$
    pthread_mutex_lock(&mon->mutex);

    if (mon->size == mon->n_bufs)
        pthread_cond_wait(&mon->not_filled, &mon->mutex);

    struct buffer *buf = mon->bufs + mon->head;

    pthread_mutex_unlock(&mon->mutex);
    return buf;
}

void
mon_put_empty(struct monitor *mon)
{$
    pthread_mutex_lock(&mon->mutex);

    mon->tail = (mon->tail + 1) % mon->n_bufs;
    mon->size--;

    if (mon->size == mon->n_bufs - 1)
        pthread_cond_signal(&mon->not_filled);

    pthread_mutex_unlock(&mon->mutex);
}

int
reader(struct monitor *mon,
       int fd)
{$
    size_t n_read = 0;
    do {
        struct buffer *buf = mon_get_empty(mon);

        n_read = read(fd, buf->data, mon->bufsz);
        if (n_read == -1)
            return perror_s("cat read failed"), errno;

        buf->size = n_read;

        mon_put_filled(mon);
    }
    while (n_read != 0);

    return 0;
}

int
writer(struct monitor *mon,
       int fd)
{$
    struct buffer *buf;
    do {
        buf = mon_get_filled(mon);

        /**
         * write() may transfer fewer than buf->size bytes.
         * In the event of partial write, make another write
         * to transfer the remaining bytes.
         */
        for (ssize_t n_remain = buf->size; n_remain > 0;) {
            ssize_t n_written = write(fd, buf->data + buf->size - n_remain, n_remain);
            if (n_written == -1)
                return perror_s("cat write failed"), errno;

            n_remain -= n_written;
        }

        mon_put_empty(mon);
    }
    while (buf->size);

    return 0;
}

struct thread_args {
    int fd;
    struct monitor *mon;
};

void *
load_writer(void *args_p)
{$
    struct thread_args *args = (struct thread_args *)args_p;
    writer(args->mon, args->fd);
    return NULL;
}

void *
load_reader(void *args_p)
{$
    struct thread_args *args = (struct thread_args *)args_p;
    reader(args->mon, args->fd);
    return NULL;
}

int
cat(struct monitor *mon, int fd)
{
    struct thread_args reader_args = {
        .fd = fd,
        .mon = mon,
    };

    struct thread_args writer_args = {
        .fd = 1,
        .mon = mon,
    };

    pthread_t tid_reader, tid_writer;
    if(pthread_create(&tid_reader, NULL, load_reader, &reader_args))
        fprintf(stderr, "READER CREATE FAILED\n");
    if (pthread_create(&tid_writer, NULL, load_writer, &writer_args))
        fprintf(stderr, "WRITER CREATE FAILED\n");

    void *ret = NULL;
    pthread_join(tid_reader, &ret);
    pthread_join(tid_writer, &ret);

    return 0;
}

int
main(int argc,
     const char *argv[])
{$
    struct monitor mon;
    mon_ctor(&mon, 0x10);

    if (argc == 1) {
        cat(&mon, 0);
    } else {
        for (int i = 1; i < argc; i++) {
            int fd = open(argv[i], O_RDONLY);
            if (fd == -1) {
                perror_s("cat");
                continue;
            }

            cat(&mon, fd);
            close(fd);
        }
    }

    mon_dtor(&mon);
    return 0;
}


