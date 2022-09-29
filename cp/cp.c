#include <fcntl.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <getopt.h>
#include <libgen.h>
#include <sys/mman.h>
#include <sys/stat.h>

int
submit()
{
        int res = getchar();

        int c = 0;
        while ((c = getchar()) != '\n' && c != EOF)
        {}

        return (res == 'y');
}

int
indicate_errno()
{
        int saved_errno = errno;
        perror("mycp: ");
        return saved_errno;
}

int
copy(const char *src_nm, const char *dst_nm, int dir_fd, int forced)
{
        assert(src_nm);
        assert(dst_nm);

        int src_fd = open(src_nm, O_RDONLY);
        if (src_fd == -1)
                return indicate_errno();

        if (forced && unlinkat(dir_fd, dst_nm, 0) == -1)
                return indicate_errno();

        struct stat src_st = {0};
        int error = fstat(src_fd, &src_st);
        if (error)
                return indicate_errno();

        int dst_fd = openat(dir_fd, dst_nm, O_TRUNC | O_CREAT | O_RDWR, src_st.st_mode);
        if (dst_fd == -1)
                return indicate_errno();

        error = ftruncate(dst_fd, (src_st.st_size));
        if (error == -1)
                return indicate_errno();

        void *src = mmap(NULL, src_st.st_size, PROT_READ, MAP_PRIVATE, src_fd, 0);
        if (src == MAP_FAILED)
                return indicate_errno();

        void *dst = mmap(NULL, src_st.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, dst_fd, 0);
        if (dst == MAP_FAILED) {
                munmap(src, src_st.st_size);
                return indicate_errno();
        }

        close(src_fd);
        close(dst_fd);

        memcpy(dst, src, (size_t)src_st.st_size);

        msync(dst, src_st.st_size, MS_SYNC);

        munmap(src, src_st.st_size);
        munmap(dst, src_st.st_size);

        return 0;
}

int
main(int argc, char *argv[])
{
        struct {
                int interactive;
                int force;
                int verbose;
        } opts = {0, 0, 0};

        /**
         * Redirect long options straight to the short options handlers
         * Note: the last element has to be filled with zeroes.
         */
        static struct option options[] = {
            {"verbose",     no_argument, NULL, 'v'},
            {"force",       no_argument, NULL, 'f'},
            {"interactive", no_argument, NULL, 'i'},
            { 0,            0,           0,     0 },
        };

        int opt = 0;
        while ((opt = getopt_long(argc, argv, "vfi", options, NULL)) != -1) {

                switch(opt) {
                case 'v':
                        opts.verbose = 1;
                        break;
                case 'i':
                        opts.interactive = 1;
                        break;
                case 'f':
                        opts.force = 1;
                        break;
                default:
                        assert(0 && "getopt_long failed what?");
                        break;
                }
        }

        /**
         * Eventually all nonoptions (file names in our case), are at the end.
         * Now we need at least source and destination file specified.
         */
        if (argc - optind < 2) {
                fprintf(stderr, "missing destination file operand after '%s'\n", argv[optind]);
                return 0xdead;
        }

        struct stat dst_st = {0};
        if (stat(argv[argc - 1], &dst_st) == 0 && S_ISDIR(dst_st.st_mode)) {

                const char *dir_name = argv[--argc];
                int dir = open(dir_name, O_RDONLY | O_DIRECTORY);
                if (dir == -1)
                        return indicate_errno();

                for (int i = optind; i < argc; i++) {

                        const char *dst = basename(argv[i]);
                        if (opts.interactive && fstatat(dir, dst, &dst_st, 0) == 0) {
                                fprintf(stderr, "overwrite %s/%s? ", dir_name, dst);
                                if (!submit())
                                        continue;
                        }

                        if (opts.verbose)
                                fprintf(stderr, "Moved %s --> %s/%s\n", argv[i], dir_name, dst);

                        copy(argv[i], basename(argv[i]), dir, opts.force);
                }

                return 0;
        }

        if (argc - optind != 2) {
                fprintf(stderr, "target '%s': Not a directory\n", argv[argc - 1]);
                return 0xdeadbeef;
        }

        const char *dst = argv[optind + 1];
        if (opts.interactive && stat(dst, &dst_st) == 0) {
                fprintf(stderr, "overwrite %s? ", dst);
                if (!submit())
                        return 0;
        }

        if (opts.verbose)
                fprintf(stderr, "Moved %s --> %s\n", argv[optind], dst);

        return copy(argv[optind], dst, AT_FDCWD, opts.force);
}


