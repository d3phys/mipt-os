#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>

typedef struct {
        int newline;
} setup_t;

setup_t
configure(int argc, char *argv[])
{
        setup_t setup = {0};

        int opt = 0;
        while ((opt = getopt(argc, argv, "n")) != -1) {
                switch (opt) {
                case 'n':
                        setup.newline = 1;
                        break;
                default:
                        return setup;
                }
        }

        return setup;
}

int
main(int argc, char *argv[])
{
        int error = 0;
        setup_t setup = configure(argc, argv);
        
        int arg = optind;
        while (arg < argc) {

                error = printf("%s", argv[arg]);
                if (error < 0)
                        return error;

                if (arg != argc - 1) {
                        error = printf(" ");
                        if (error < 0)
                                return error;
                }

                arg++;
        }

        if (!setup.newline)
                printf("\n");

        return 0;
}
