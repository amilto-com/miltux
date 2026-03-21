/*
 * MiLTuX - Entry point
 *
 * Usage: miltux [options]
 *   -u <identity>   run as the given identity (default: current POSIX user)
 *   -r <ring>       start at ring <ring> (default: 4 = user ring)
 *   -l [port]       listen for peer connections on port (default: 7070)
 *   -d              daemon mode: serve peers without an interactive REPL
 *   -h              show usage
 *
 * Environment variables (read at startup):
 *   MILTUX_PEERS    comma-separated "host" or "host:port" peers to connect to
 */

#include "miltux.h"
#include "shell.h"
#include "net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>  /* getlogin_r / getuid (POSIX) */
#include <pwd.h>     /* getpwuid (POSIX) */

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s [-u <identity>] [-r <ring>] [-l [port]] [-d]\n"
            "  -u <identity>  run as identity (default: login name)\n"
            "  -r <ring>      start at ring 0-%d (default: %d)\n"
            "  -l [port]      listen for peers on port (default: %d)\n"
            "  -d             daemon mode (no interactive REPL)\n"
            "  -h             show this help\n"
            "\n"
            "  MILTUX_PEERS=host[:port],host[:port],...\n"
            "               auto-connect to these peers on startup\n",
            prog, MILTUX_RING_MAX, MILTUX_RING_USER,
            MILTUX_NET_PORT_DEFAULT);
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
    int  ring         = MILTUX_RING_USER;
    int  listen_port  = -1;
    int  daemon_mode  = 0;
    int  opt;

    get_default_identity(identity, sizeof(identity));

    while ((opt = getopt(argc, argv, "u:r:l::dh")) != -1) {
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
        case 'l':
            if (optarg) {
                char *end;
                long  p = strtol(optarg, &end, 10);
                if (*end != '\0' || p < 0 || p > 65535) {
                    fprintf(stderr, "miltux: invalid port '%s'\n", optarg);
                    usage(argv[0]);
                    return EXIT_FAILURE;
                }
                listen_port = (int)p;
            } else {
                listen_port = MILTUX_NET_PORT_DEFAULT;
            }
            break;
        case 'd':
            daemon_mode = 1;
            break;
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

    /* Start listening if -l was given */
    if (listen_port >= 0) {
        err = net_listen(&sh.net, listen_port, identity);
        if (err != MILTUX_OK) {
            fprintf(stderr, "miltux: cannot listen on port %d: %s\n",
                    listen_port, miltux_strerror(err));
            /* Non-fatal: continue without listening */
        } else {
            fprintf(stderr, "miltux: listening for peers on port %d\n",
                    sh.net.port);
        }
    }

    /* Auto-connect to peers listed in MILTUX_PEERS env var */
    net_autoconnect(&sh.net, identity);

    if (daemon_mode) {
        shell_run_daemon(&sh);
    } else {
        shell_run(&sh);
    }

    shell_destroy(&sh);
    return EXIT_SUCCESS;
}
