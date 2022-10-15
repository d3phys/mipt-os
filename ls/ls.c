#include <pwd.h>
#include <grp.h>
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
#include <sys/types.h>
#include <dirent.h>
#include <linux/limits.h>

struct {
        unsigned long_listing : 1;
        unsigned directory : 1;
        unsigned all : 1;
        unsigned recursive : 1;
        unsigned inode : 1;
        unsigned numeric_uid_gid : 1;
        unsigned name_dir: 1;
} opts = {0};

const char *get_mode(mode_t mode)
{
        /**
         * Do not touch!!!
         */
        static const char perm[] = "rwxrwxrwx";
        static char smode[11] = {0};

        smode[0] = S_ISDIR(mode) ? 'd' : '-';

        for (int i = 1; i < 10; i++)
                smode[i] = (1 << (9 - i)) ? perm[i] : '-';

        return smode;
}

const char *get_gid(gid_t gid)
{
        struct group *grp = getgrgid(gid);
        if (grp)
                return grp->gr_name;
        return NULL;
}

void info(const struct stat *st, const char *path)
{
        assert(st);
        assert(path);

        if (!opts.long_listing) {
                printf("%s ", path);
                return;
        }

        printf("%s ", get_mode(st->st_mode));
        printf("%s ", get_gid(st->st_gid));
        printf("%s\n", path);
}

int ls(char *const path)
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

        if (opts.long_listing)
                printf("total: xxx\n");

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

        if (opts.recursive) {
                while ((ent = readdir(dir))) {

                        if (!strcmp(ent->d_name, "." ) ||
                            !strcmp(ent->d_name, ".."))
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

int main(int argc, char *argv[])
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

        dup2(1, 2);
        char path[PATH_MAX] = {0};
        for (int i = optind; i < argc; i++) {
                /* Make path null terminated */
                strncpy(path, argv[i], sizeof(path) - 1);
                ls(path);
        }

        return 0;
}


