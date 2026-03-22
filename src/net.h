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
 *   S→C:  MILTUX/1 <server-identity> <server-listen-port>\n
 *   C→S:  HELLO <client-identity> <client-listen-port>\n
 *   S→C:  OK\n
 *
 * Requests (client → server, one per line except WRITE):
 *   LS     <ring> <identity> <path>\n
 *   CAT    <ring> <identity> <path>\n
 *   MKDIR  <ring> <identity> <path>\n
 *   REMOVE <ring> <identity> <path>\n
 *   WRITE  <ring> <identity> <path> <len>\n<len bytes of data>
 *   PEERS\n              — request list of known peers for gossip
 *   QUIT\n
 *
 * Responses (server → client):
 *   OK\n                      — success, no payload
 *   DATA <len>\n<data>        — success, payload follows
 *   ERR <message>\n           — failure
 *
 * Gossip / auto-discovery
 * -----------------------
 * Every MILTUX_GOSSIP_TICKS poll calls, each node sends PEERS to each
 * connected peer and tries to connect to any newly discovered nodes.
 * Combined with the MILTUX_PEERS environment variable (comma-separated
 * "host[:port]" entries), this forms a self-organising full mesh with
 * zero manual configuration.
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
#define MILTUX_PENDING_MAX      16
#define MILTUX_GOSSIP_TICKS     60   /* gossip every ~3 s (@ 20 polls/s) */
#define MILTUX_SOCK_TIMEOUT_S   5    /* per-socket send/recv timeout      */

/* -----------------------------------------------------------------------
 * A single connected peer
 * ----------------------------------------------------------------------- */
typedef struct {
    int  fd;                              /* socket fd; -1 = unused slot   */
    char host[256];                       /* host used to reach this peer   */
    int  port;                            /* remote LISTEN port             */
    char identity[MILTUX_NAME_MAX + 1];  /* remote identity (from hello)   */
    int  server_accepted;                 /* 1 = they connected to us (client session);
                                            0 = we connected to them (daemon peer).
                                            Client sessions are excluded from gossip
                                            to avoid mixing PEERS messages with FS
                                            operation responses on the same socket. */
} net_peer_t;

/* -----------------------------------------------------------------------
 * A pending peer: failed connect, will be retried automatically
 * ----------------------------------------------------------------------- */
typedef struct {
    char host[256];
    int  port;
} net_pending_t;

/* -----------------------------------------------------------------------
 * An exported subtree: a named path that remote peers can mount
 *
 * When a session calls `export <path> <name>`, the path is registered
 * here.  Remote peers call `rmount <node#> <name> <local-target>` which
 * sends an EXPORT request, receives the real path, then applies a local
 * bind: local-target → remote node + remote path (via rmount protocol).
 * ----------------------------------------------------------------------- */
typedef struct {
    char name[MILTUX_NAME_MAX + 1];   /* short export name, e.g. "home"  */
    char path[MILTUX_PATH_MAX];        /* absolute FS path being exported  */
    char owner[MILTUX_NAME_MAX + 1];  /* identity that created the export */
    int  ring;                         /* minimum ring required to mount   */
} net_export_t;

/* -----------------------------------------------------------------------
 * Net subsystem state (one per shell session)
 * ----------------------------------------------------------------------- */
typedef struct {
    int           listen_fd;                  /* -1 if not listening           */
    int           port;                       /* port we are listening on      */
    net_peer_t    peers[MILTUX_PEERS_MAX];   /* currently connected peers     */
    int           peer_count;                /* number of live peers           */
    net_pending_t pending[MILTUX_PENDING_MAX];/* peers to retry on next cycle  */
    int           pending_count;             /* number of pending retries      */
    int           poll_tick;                 /* monotonic counter; drives gossip*/
    void         *_sh;                       /* back-pointer to owning shell_t  */
    net_export_t  exports[MILTUX_EXPORTS_MAX]; /* subtrees exported by this session */
    int           export_count;              /* number of active exports         */
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
 * Non-blocking poll: accept any pending incoming connections, dispatch
 * one pending request per already-connected peer, retry pending peers,
 * and (every MILTUX_GOSSIP_TICKS) run the gossip round.
 * Call this after every shell command to keep the network responsive.
 *
 * sh must point to the shell_t that owns this net_t.
 */
void net_poll(net_t *net, void *sh);   /* sh is shell_t * */

/*
 * Connect to a remote peer at host:port.  The handshake is performed
 * synchronously.
 * Returns the peer index (>= 0) on success, or a negative MILTUX_ERR_*
 * code on failure (MILTUX_ERR_EXIST if already connected or self-connect).
 */
int net_connect(net_t *net, const char *host, int port, const char *identity);

/*
 * Add host:port to the pending-retry queue.
 * Entries in the queue are retried automatically during net_poll.
 */
void net_add_pending(net_t *net, const char *host, int port);

/*
 * Parse the MILTUX_PEERS environment variable and attempt to connect to
 * each listed peer.  Format: comma-separated "host" or "host:port" entries.
 * Failed connections are added to the pending-retry queue automatically.
 */
void net_autoconnect(net_t *net, const char *identity);

/*
 * Gossip round: ask every connected peer for its peer list and attempt
 * to connect to any nodes not yet in our peer table.
 */
void net_gossip(net_t *net, const char *identity);

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

/* -----------------------------------------------------------------------
 * Export / mount (Plan 9 exportfs)
 *
 * Server side:
 *   net_export()         — register a named export for this session
 *   net_unexport()       — remove a named export
 *   net_list_exports()   — print the export table to stdout
 *
 * Client side:
 *   net_remote_mount()   — query peer's export table (EXPORTS request),
 *                          find <name>, then apply a local bind so that
 *                          <local_target> transparently resolves to the
 *                          remote subtree via the existing rls/rcat path.
 *                          Returns MILTUX_OK and prints a confirmation.
 * ----------------------------------------------------------------------- */
miltux_err_t net_export(net_t *net,
                         const char *path, const char *name,
                         const char *owner, int ring);

miltux_err_t net_unexport(net_t *net, const char *name,
                            const char *owner, int ring);

void net_list_exports(const net_t *net);

/*
 * Ask peer for its export table (EXPORTS), find <name>, then bind
 * <local_target> → peer's exported path in this session's namespace.
 * fs must be the caller's fs_t so the bind can be applied locally.
 */
miltux_err_t net_remote_mount(net_peer_t *peer,
                               const char *export_name,
                               const char *local_target,
                               const char *identity, int ring,
                               void *fs /* fs_t * */);

#endif /* NET_H */
