#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <pwd.h>
#include <grp.h>
#include <stdlib.h>
#include <sys/types.h>

void print_group(gid_t gid)
{
        struct group *grp = getgrgid(gid);
        printf("%d", gid);
        if (grp)
                printf("(%s)", grp->gr_name);

        printf(" ");
}

void print_user(uid_t uid)
{
        struct passwd *pwd = getpwuid(uid);
        printf("%d", uid);
        if (pwd)
                printf("(%s)", pwd->pw_name);

        printf(" ");
}

int
main(int argc, const char *argv[])
{
        int is_context = 1;

        errno = 0;
        switch (argc) {
        case 1:
                is_context = 1;
                break;
        case 2:
                is_context = 0;
                break;
        default:
                fprintf(stderr, "Incorrect input\n");
                return 0;
        }

        uid_t uid = 0;
        gid_t gid = 0;

        struct passwd *pwd = NULL;

        int n_groups = sysconf(_SC_NGROUPS_MAX) + 1;
        gid_t *groups = (gid_t *)calloc(n_groups, sizeof(gid_t));
        if (groups == NULL)
                return errno;

        if (is_context) {
                uid = getuid();
                gid = getgid();

                n_groups = getgroups(n_groups, groups);
                if (n_groups == -1)
                        return errno;

        } else {

                pwd = getpwnam(argv[1]);
                if (pwd == NULL) {
                        if (errno == 0)
                                fprintf(stderr, "myid: no such user\n");
                        else
                                perror("myid");

                        free(groups);
                        return errno;
                }

                uid = pwd->pw_uid;
                gid = pwd->pw_gid;

                int error = getgrouplist(pwd->pw_name, pwd->pw_gid, groups, &n_groups);
                if (error == -1) {
                        free(groups);
                        return errno;
                }
        }

        printf("uid=");
        print_user(uid);

        printf("gid=");
        print_group(gid);

        printf("groups=");
        for (int i = 0; i < n_groups; i++)
                print_group(groups[i]);

        free(groups);
        return 0;
}
