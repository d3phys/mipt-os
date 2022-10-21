#include <pwd.h>
#include <grp.h>
#include <fcntl.h>
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <getopt.h>
#include <libgen.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <linux/limits.h>

/**
 * I have no idea why to use bitfields here :)
 * But...
 */
struct {
        unsigned long_listing : 1;
        unsigned directory : 1;
        unsigned all : 1;
        unsigned recursive : 1;
        unsigned inode : 1;
        unsigned numeric_uid_gid : 1;
        unsigned name_dir: 1;
} opts = {0};

const char *
get_time(time_t time)
{
        static char date[0xff] = {0};
        const struct tm *tm = localtime(&time);

        const char *fmt = "%b %d %Y %H:%M";
        strftime(date, sizeof(date), fmt, localtime(&time));
        return date;
}

const char *
get_mode(mode_t mode)
{
        /* Simple but effective and clean... */
        static char smode[] = "drwxrwxrwx";

        char *bit = smode;
        *bit++ = S_ISDIR(mode) ? 'd' : '-';

        *bit++ = (S_IRUSR & mode) ? 'r' : '-';
        *bit++ = (S_IWUSR & mode) ? 'w' : '-';
        *bit++ = (S_IXUSR & mode) ? 'x' : '-';

        *bit++ = (S_IRGRP & mode) ? 'r' : '-';
        *bit++ = (S_IWGRP & mode) ? 'w' : '-';
        *bit++ = (S_IXGRP & mode) ? 'x' : '-';

        *bit++ = (S_IROTH & mode) ? 'r' : '-';
        *bit++ = (S_IWOTH & mode) ? 'w' : '-';
        *bit++ = (S_IXOTH & mode) ? 'x' : '-';

        return smode;
}

/**
 * According to perf stat 'getgrgid' and 'getpwuid'
 * runs incredibly slow.
 *
 * That is why we need to use cache for this information.
 * Original 'ls' and 'myls' comparation is shown below.
 * They have _practically_ the same performance :)
 *
 * Performance counter stats for 'ls /home -Rl' (5 runs):
 *
 *            397,40 msec task-clock:u              #    0,918 CPUs utilized            ( +-  0,70% )
 *                 0      context-switches:u        #    0,000 /sec
 *                 0      cpu-migrations:u          #    0,000 /sec
 *               419      page-faults:u             #    1,040 K/sec                    ( +-  0,18% )
 *       562 704 860      cycles:u                  #    1,397 GHz                      ( +-  1,32% )
 *     1 217 338 279      instructions:u            #    2,12  insn per cycle           ( +-  0,00% )
 *       235 287 339      branches:u                #  583,992 M/sec                    ( +-  0,00% )
 *         2 468 163      branch-misses:u           #    1,05% of all branches          ( +-  1,00% )
 *
 *           0,43269 +- 0,00216 seconds time elapsed  ( +-  0,50% )
 *
 *
 * Performance counter stats for './myls /home -Rl' (5 runs):
 *
 *            477,34 msec task-clock:u              #    0,948 CPUs utilized            ( +-  0,91% )
 *                 0      context-switches:u        #    0,000 /sec
 *                 0      cpu-migrations:u          #    0,000 /sec
 *               210      page-faults:u             #  443,166 /sec                     ( +-  0,38% )
 *       399 541 759      cycles:u                  #    0,843 GHz                      ( +-  1,68% )
 *       769 014 373      instructions:u            #    1,92  insn per cycle           ( +-  0,01% )
 *       165 339 951      branches:u                #  348,919 M/sec                    ( +-  0,01% )
 *           988 326      branch-misses:u           #    0,60% of all branches          ( +-  3,94% )
 *
 *           0,50354 +- 0,00421 seconds time elapsed  ( +-  0,84% )
 */
#define CACHED
#define hash_bits 8
struct entry {
    uint64_t key;
    void *value;
};

static struct entry groups [1 << hash_bits];
static struct entry passwds[1 << hash_bits];

void *get_cached(struct entry *cache, uint64_t key, void *(*miss)(uint64_t key))
{
    struct entry *ent = &cache[key & hash_bits];
    if (key != ent->key) {
        ent->value = miss(key);
        ent->key = key;
    }

    return ent->value;
}
#undef hash_bits

const char *
get_gid(gid_t gid)
{
#ifdef CACHED
        struct group *grp = (struct group *)get_cached(groups, gid, &getgrgid);
#else
        struct group *grp = getgrgid(gid);
#endif
        if (grp) return grp->gr_name;
        return NULL;
}

const char *
get_uid(uid_t uid)
{
#ifdef CACHED
        struct passwd *pwd = (struct passwd *)get_cached(passwds, uid, &getpwuid);
#else
        struct passwd *pwd = getpwuid(uid);
#endif
        if (pwd)
                return pwd->pw_name;
        return NULL;
}

void
info(const struct stat *st, const char *path)
{
        assert(st);
        assert(path);

        if (!opts.long_listing) {
                printf("%s ", path);
                return;
        }

        printf("%s ", get_mode(st->st_mode));
        printf("%s ", get_gid(st->st_gid));
        printf("%s ", get_uid(st->st_uid));
        printf("%9.ld ", st->st_size);
        printf("%s ", get_time(st->st_mtime));
        printf("%s\n", path);
}

int
ls(char *const path)
{
        assert(path);

        enum exit_status {
                OK      = 0,
                MINOR   = 1,
                TROUBLE = 2,
        };

        struct stat st;
        if (lstat(path, &st) == -1)
                return TROUBLE;

        /* We need to print newline between 'ls' calls */
        static int indent = 0;
        if (indent || (indent++))
                putchar('\n');

        if (!S_ISDIR(st.st_mode)) {
                info(&st, path);
                if (!opts.long_listing)
                        putchar('\n');

                return OK;
        }

        /* 'ls' prints directory name if -R specified */
        if (opts.name_dir | opts.recursive)
                printf("%s:\n", path);

        const struct dirent *ent = NULL;
        struct stat ent_st;

        DIR *dir = opendir(path);
        if (!dir) {
                perror("");
                return MINOR;
        }

        assert(dir);
        strncat(path, "/", PATH_MAX - 1);
        size_t path_len = strlen(path);

        errno = 0;
        while ((ent = readdir(dir))) {
                assert(ent);
                static int first = 0;
                if (errno) {
                        closedir(dir);
                        return MINOR;
                }

                if (!opts.all && (*ent->d_name == '.'))
                        continue;

                strncat(path, ent->d_name, PATH_MAX - 1);
                if (lstat(path, &ent_st) == -1) {
                        path[path_len] = '\0';
                        fprintf(stderr, "%s: %s\n", path, strerror(errno));
                        continue;
                }

                info(&ent_st, ent->d_name);
                path[path_len] = '\0';
        }

        if (!opts.long_listing)
                printf("\n");

        rewinddir(dir);

        /**
         * According to perf 'readdir' is not so efficient this way.
         * Better way is to read once and then recursively travel.
         *
         * perf report:
         *     15,36%  myls     libc.so.6             [.] readdir64
         *     10,25%  myls     [unknown]             [k] 0xffffffffb5600191
         *      8,77%  myls     libc.so.6             [.] _IO_file_xsputn
         *
         * But...
         */
        if (opts.recursive) {
                while ((ent = readdir(dir))) {

                        if (!strcmp(ent->d_name, "." ) ||
                            !strcmp(ent->d_name, ".."))
                                continue;

                        if (!opts.all && (*ent->d_name == '.'))
                                continue;

                        strncat(path, ent->d_name, PATH_MAX - 1);
                        if (lstat(path, &ent_st) == -1) {
                                path[path_len] = '\0';
                                fprintf(stderr, "%s: %s\n", path, strerror(errno));
                                continue;
                        }

                        if (S_ISDIR(ent_st.st_mode))
                                ls(path);

                        path[path_len] = '\0';
                }
        }

        closedir(dir);
        return OK;
}

int
main(int argc, char *argv[])
{
        /**
         * Redirect long options straight to the short options handlers
         * Note: the last element has to be filled with zeroes.
         */
        static struct option options[] = {
                {"directory",       no_argument, NULL, 'd'},
                {"all",             no_argument, NULL, 'a'},
                {"recursive",       no_argument, NULL, 'R'},
                {"inode",           no_argument, NULL, 'i'},
                {"numeric-uid-gid", no_argument, NULL, 'n'},
                { 0,                0,           0,     0 },
        };

        int opt = 0;
        /* Aldrin -- a toxic synthetic insecticide, now generally banned :D */
        while ((opt = getopt_long(argc, argv, "aldRin", options, NULL)) != -1) {

                switch(opt) {
                case 'l':
                        opts.long_listing = 1;
                        break;
                case 'd':
                        opts.directory = 1;
                        break;
                case 'a':
                        opts.all = 1;
                        break;
                case 'R':
                        opts.recursive = 1;
                        break;
                case 'i':
                        opts.inode = 1;
                        break;
                case 'n':
                        opts.numeric_uid_gid = 1;
                        break;
                default:
                        break;
                }
        }

        /**
         * Specific 'ls' behavior: print directory name
         * if more than one argument exists.
         */
        if ((argc - optind) > 1)
                opts.name_dir = 1;

        char path[PATH_MAX] = {0};
        for (int i = optind; i < argc; i++) {
                /* Make path null terminated */
                strncpy(path, argv[i], sizeof(path) - 1);
                ls(path);
        }

        return 0;
}

