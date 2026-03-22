/*
 * MiLTuX - Interactive command shell
 *
 * An interactive shell inspired by the Multics command language.
 * Commands are entered at the prompt and dispatched to handlers.
 *
 * Available commands:
 *   ls   [path]                   - list directory
 *   cd   <path>                   - change directory
 *   mkdir <path>                  - make directory
 *   rm   <path>                   - remove file or empty directory
 *   cat  <path>                   - show file contents
 *   write <path> <text...>        - write text to file
 *   acl  <path>                   - show ACL of a segment
 *   ring                          - show current ring
 *   su   <ring>                   - switch to a different ring
 *   whoami                        - show current identity
 *   pwd                           - print working directory
 *   rls     <node#> <path>              - list directory on remote node
 *   rcat    <node#> <path>              - read file from remote node
 *   rmkdir  <node#> <path>              - make directory on remote node
 *   rwrite  <node#> <path> <text...>    - write file on remote node
 *   proc                                - list sessions in >system>proc>
 *   bind   <source> <target>           - redirect target to source (Plan 9)
 *   unbind <target>                    - remove a bind
 *   binds                              - list all active binds
 *   help                                - show help
 *   exit / quit                         - leave MiLTuX
 */

#ifndef SHELL_H
#define SHELL_H

#include "miltux.h"
#include "ring.h"
#include "fs.h"
#include "net.h"

/* -----------------------------------------------------------------------
 * Shell session state
 * ----------------------------------------------------------------------- */
typedef struct {
    ring_ctx_t  ring_ctx;
    fs_t        fs;
    net_t       net;                      /* peer-to-peer networking state */
    char        identity[MILTUX_NAME_MAX + 1];
} shell_t;

/* Initialise a shell session for the given user at the given ring */
miltux_err_t shell_init(shell_t *sh, const char *identity, int ring);

/* Tear down a shell session */
void shell_destroy(shell_t *sh);

/* Run the interactive read-eval-print loop until EOF or "exit" */
void shell_run(shell_t *sh);

/*
 * Run in daemon mode: no interactive REPL.  Loops forever serving peer
 * requests and running the gossip protocol.  This is the mode used by
 * Docker/container deployments — nodes expose their FS to the mesh and
 * let remote peers read/write it via the protocol.
 * Call net_listen() and net_autoconnect() before this function.
 */
void shell_run_daemon(shell_t *sh);

/* Execute a single command line string (for testing / scripting) */
miltux_err_t shell_exec(shell_t *sh, const char *line);

#endif /* SHELL_H */
