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

struct {
        int long_listing : 1;
        int directory : 1;
        int all : 1;
        int recursive : 1;
        int inode : 1;
        int numeric_uid_gid : 1;
        int dir_head: 1;
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

void info(const struct stat *st, const char *path)
{
        assert(st);
        assert(path);

        if (!opts.long_listing) {
                printf("%s ", path);
                return;
        }

        printf("%s %s\n", get_mode(st->st_mode), path);
}

int ls(const char *const path)
{
        assert(path);

        enum exit_status {
                OK      = 0,
                MINOR   = 1,
                TROUBLE = 2,
        };

        struct stat st;
        if (stat(path, &st) == -1)
                return TROUBLE;

        if (S_ISDIR(st.st_mode)) {
                DIR *dir = opendir(path);

                const struct dirent *ent = NULL;
                struct stat ent_st;

                if (opts.dir_head)
                        printf("%s:\n", path);

                if (opts.long_listing)
                        printf("total: xxx\n");

                errno = 0;
                while ((ent = readdir(dir))) {
                        if (errno)
                                return MINOR;

                        if (!opts.all && (*ent->d_name == '.'))
                                continue;

                        if (stat(ent->d_name, &ent_st) == -1)
                                return MINOR;

                        info(&ent_st, ent->d_name);
                }

                while ((ent = readdir(dir))) {
                        if (errno)
                                return MINOR;

                        if (!opts.all && (*ent->d_name == '.'))
                                continue;

                        if (stat(ent->d_name, &ent_st) == -1)
                                return MINOR;

                        info(&ent_st, ent->d_name);
                }

                closedir(dir);
        } else {
                info(&st, path);
        }

        printf("\n");
        if (opts.dir_head)
                printf("\n");

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
        while ((opt = getopt_long(argc, argv, "ldRina", options, NULL)) != -1) {

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
                        assert(0 && "getopt_long wtf: what a terrible failure?");
                        break;
                }
        }

        if ((argc - optind) > 1)
                opts.dir_head = 1;

        struct stat st = {0};
        for (int i = optind; i < argc; i++)
                ls(argv[i]);

        return 0;
}


