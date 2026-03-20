/*
 * MiLTuX - Entry point
 *
 * Usage: miltux [options]
 *   -u <identity>   run as the given identity (default: current POSIX user)
 *   -r <ring>       start at ring <ring> (default: 4 = user ring)
 *   -h              show usage
 */

#include "miltux.h"
#include "shell.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>  /* getlogin_r / getuid (POSIX) */
#include <pwd.h>     /* getpwuid (POSIX) */

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s [-u <identity>] [-r <ring>]\n"
            "  -u <identity>  run as identity (default: login name)\n"
            "  -r <ring>      start at ring 0-%d (default: %d)\n"
            "  -h             show this help\n",
            prog, MILTUX_RING_MAX, MILTUX_RING_USER);
}

static void get_default_identity(char *buf, size_t len)
{
    /* Try getlogin first (POSIX) */
    if (getlogin_r(buf, len) == 0 && buf[0] != '\0')
        return;

    /* Fall back to passwd entry */
    struct passwd *pw = getpwuid(getuid());
    if (pw && pw->pw_name) {
        strncpy(buf, pw->pw_name, len - 1);
        buf[len - 1] = '\0';
        return;
    }

    /* Last resort */
    strncpy(buf, "user", len - 1);
    buf[len - 1] = '\0';
}

int main(int argc, char *argv[])
{
    char identity[MILTUX_NAME_MAX + 1];
    int  ring = MILTUX_RING_USER;
    int  opt;

    get_default_identity(identity, sizeof(identity));

    while ((opt = getopt(argc, argv, "u:r:h")) != -1) {
        switch (opt) {
        case 'u':
            strncpy(identity, optarg, MILTUX_NAME_MAX);
            identity[MILTUX_NAME_MAX] = '\0';
            break;
        case 'r': {
            char *end;
            long  r = strtol(optarg, &end, 10);
            if (*end != '\0' || r < MILTUX_RING_MIN || r > MILTUX_RING_MAX) {
                fprintf(stderr, "miltux: invalid ring '%s'\n", optarg);
                usage(argv[0]);
                return EXIT_FAILURE;
            }
            ring = (int)r;
            break;
        }
        case 'h':
            usage(argv[0]);
            return EXIT_SUCCESS;
        default:
            usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    shell_t sh;
    miltux_err_t err = shell_init(&sh, identity, ring);
    if (err != MILTUX_OK) {
        fprintf(stderr, "miltux: failed to initialise: %s\n",
                miltux_strerror(err));
        return EXIT_FAILURE;
    }

    shell_run(&sh);
    shell_destroy(&sh);
    return EXIT_SUCCESS;
}
