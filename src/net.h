/*
 * MiLTuX - Peer-to-peer networking
 *
 * Allows multiple MiLTuX instances on different POSIX hosts to form a
 * distributed system: every node can serve remote FS operations and issue
 * operations against remote peers, so that all instances collectively form
 * a single shared namespace — the "mega computer".
 *
 * Architecture
 * ------------
 * Each instance can simultaneously be a server (accepting incoming peers)
 * and a client (connecting to remote peers).  The shell polls for new
 * connections and incoming requests after every command so the REPL stays
 * responsive without threads.
 *
 * Protocol (line-based ASCII over TCP)
 * -------------------------------------
 * Handshake (server → client first):
 *   S→C:  MILTUX/1 <server-identity>\n
 *   C→S:  HELLO <client-identity>\n
 *   S→C:  OK\n
 *
 * Requests (client → server, one per line except WRITE):
 *   LS     <ring> <identity> <path>\n
 *   CAT    <ring> <identity> <path>\n
 *   MKDIR  <ring> <identity> <path>\n
 *   REMOVE <ring> <identity> <path>\n
 *   WRITE  <ring> <identity> <path> <len>\n<len bytes of data>
 *   QUIT\n
 *
 * Responses (server → client):
 *   OK\n                      — success, no payload
 *   DATA <len>\n<data>        — success, payload follows
 *   ERR <message>\n           — failure
 */

#ifndef NET_H
#define NET_H

#include "miltux.h"
#include <stddef.h>

/* -----------------------------------------------------------------------
 * Constants
 * ----------------------------------------------------------------------- */
#define MILTUX_NET_PORT_DEFAULT 7070
#define MILTUX_PEERS_MAX        16

/* -----------------------------------------------------------------------
 * A single connected peer
 * ----------------------------------------------------------------------- */
typedef struct {
    int  fd;                              /* socket fd; -1 = unused slot */
    char host[256];                       /* remote host (dotted-decimal) */
    int  port;                            /* remote port                  */
    char identity[MILTUX_NAME_MAX + 1];  /* remote identity (from hello) */
} net_peer_t;

/* -----------------------------------------------------------------------
 * Net subsystem state (one per shell session)
 * ----------------------------------------------------------------------- */
typedef struct {
    int        listen_fd;                 /* -1 if not listening          */
    int        port;                      /* port we are listening on     */
    net_peer_t peers[MILTUX_PEERS_MAX];  /* currently connected peers    */
    int        peer_count;               /* number of live peers          */
    void      *_sh;                      /* back-pointer to owning shell_t (internal) */
} net_t;

/* Initialise net state (not yet listening or connected) */
void net_init(net_t *net);

/*
 * Start accepting incoming peer connections on the given TCP port.
 * Pass port = 0 to let the OS choose a free port (actual port stored in
 * net->port after the call).
 * Returns MILTUX_OK on success.
 */
miltux_err_t net_listen(net_t *net, int port, const char *identity);

/*
 * Non-blocking poll: accept any pending incoming connections and dispatch
 * one pending request per already-connected peer.  Call this after every
 * shell command to keep the network responsive.
 *
 * sh must point to the shell_t that owns this net_t; it is used to execute
 * server-side FS operations on behalf of remote callers.
 */
void net_poll(net_t *net, void *sh);   /* sh is shell_t * */

/*
 * Connect to a remote peer at host:port.  The handshake is performed
 * synchronously.
 * Returns the peer index (>= 0) on success, or a negative MILTUX_ERR_*
 * code on failure.
 */
int net_connect(net_t *net, const char *host, int port, const char *identity);

/* Close the connection to the peer at index idx */
void net_disconnect(net_t *net, int idx);

/* Free all resources (closes all sockets) */
void net_destroy(net_t *net);

/* Print the connected peer list to stdout */
void net_list_peers(const net_t *net);

/* -----------------------------------------------------------------------
 * Remote FS operations
 * Each function sends a request to the peer and prints the response to
 * stdout (mimicking the local FS commands).
 * Returns MILTUX_OK on success.
 * ----------------------------------------------------------------------- */
miltux_err_t net_remote_ls(net_peer_t *peer,
                            const char *path,
                            const char *identity, int ring);

miltux_err_t net_remote_cat(net_peer_t *peer,
                             const char *path,
                             const char *identity, int ring);

miltux_err_t net_remote_mkdir(net_peer_t *peer,
                               const char *path,
                               const char *identity, int ring);

miltux_err_t net_remote_write(net_peer_t *peer,
                               const char *path,
                               const char *data, size_t data_len,
                               const char *identity, int ring);

miltux_err_t net_remote_remove(net_peer_t *peer,
                                const char *path,
                                const char *identity, int ring);

#endif /* NET_H */
