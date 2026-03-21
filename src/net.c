/*
 * MiLTuX - Peer-to-peer networking implementation
 */

#include "net.h"
#include "shell.h"   /* shell_t, for server-side request dispatch */
#include "fs.h"
#include "ring.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

/* -----------------------------------------------------------------------
 * Low-level I/O helpers
 * ----------------------------------------------------------------------- */

/* Set per-socket send/recv timeout */
static void set_sock_timeout(int fd, int seconds)
{
    struct timeval tv;
    tv.tv_sec  = seconds;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

/* Write exactly len bytes; return 1 on success, 0 on error */
static int send_all(int fd, const char *buf, size_t len)
{
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = write(fd, buf + sent, len - sent);
        if (n <= 0) return 0;
        sent += (size_t)n;
    }
    return 1;
}

/* Read exactly len bytes; return 1 on success, 0 on error/EOF */
static int recv_bytes(int fd, char *buf, size_t len)
{
    size_t got = 0;
    while (got < len) {
        ssize_t n = read(fd, buf + got, len - got);
        if (n <= 0) return 0;
        got += (size_t)n;
    }
    return 1;
}

/*
 * Read up to bufsz-1 bytes until '\n' or EOF.
 * Strips the trailing '\n' (and optional '\r').
 * Returns 1 on success, 0 on EOF/error.
 */
static int recv_line(int fd, char *buf, size_t bufsz)
{
    size_t pos = 0;
    while (pos < bufsz - 1) {
        char c;
        ssize_t n = read(fd, &c, 1);
        if (n <= 0) return 0;
        if (c == '\n') break;
        if (c != '\r') buf[pos++] = c;
    }
    buf[pos] = '\0';
    return 1;
}

/* Send a formatted line (appends '\n') */
static int send_line(int fd, const char *line)
{
    if (!send_all(fd, line, strlen(line))) return 0;
    return send_all(fd, "\n", 1);
}

/* Send an error response */
static void send_err(int fd, miltux_err_t err)
{
    char msg[256];
    snprintf(msg, sizeof(msg), "ERR %s", miltux_strerror(err));
    send_line(fd, msg);
}

/* Send a DATA block (header + raw bytes) */
static int send_data(int fd, const char *data, size_t len)
{
    char hdr[64];
    snprintf(hdr, sizeof(hdr), "DATA %zu", len);
    if (!send_line(fd, hdr)) return 0;
    if (len > 0 && !send_all(fd, data, len)) return 0;
    return 1;
}

/* -----------------------------------------------------------------------
 * Handshake
 *
 * Format includes the listen port so gossip can share reconnectable
 * addresses without knowing each peer's hostname separately.
 * ----------------------------------------------------------------------- */

/* Server side: greet client, learn client identity + listen port */
static int do_server_handshake(int fd, const char *local_identity,
                                int local_listen_port,
                                char *peer_identity, size_t pid_size,
                                int *peer_listen_port)
{
    char line[512];
    char fmt[32];

    snprintf(line, sizeof(line), "MILTUX/1 %s %d",
             local_identity, local_listen_port);
    if (!send_line(fd, line)) return 0;

    if (!recv_line(fd, line, sizeof(line))) return 0;
    if (strncmp(line, "HELLO ", 6) != 0) return 0;

    /* Parse: HELLO <identity> <port> */
    snprintf(fmt, sizeof(fmt), "%%%zus %%d", pid_size - 1);
    int pport = 0;
    sscanf(line + 6, fmt, peer_identity, &pport);
    peer_identity[pid_size - 1] = '\0';
    *peer_listen_port = pport;

    return send_line(fd, "OK");
}

/* Client side: receive greeting, send HELLO */
static int do_client_handshake(int fd, const char *local_identity,
                                int local_listen_port,
                                char *peer_identity, size_t pid_size,
                                int *peer_listen_port)
{
    char line[512];
    char fmt[32];

    if (!recv_line(fd, line, sizeof(line))) return 0;
    if (strncmp(line, "MILTUX/1 ", 9) != 0) return 0;

    /* Parse: MILTUX/1 <identity> <port> */
    snprintf(fmt, sizeof(fmt), "%%%zus %%d", pid_size - 1);
    int pport = 0;
    sscanf(line + 9, fmt, peer_identity, &pport);
    peer_identity[pid_size - 1] = '\0';
    *peer_listen_port = pport;

    snprintf(line, sizeof(line), "HELLO %s %d",
             local_identity, local_listen_port);
    if (!send_line(fd, line)) return 0;

    if (!recv_line(fd, line, sizeof(line))) return 0;
    return (strncmp(line, "OK", 2) == 0);
}

/* -----------------------------------------------------------------------
 * Server-side request dispatch
 * ----------------------------------------------------------------------- */

/*
 * Parse "LS <ring> <identity> <path>" and similar.
 * Returns the number of fields consumed, or -1 on parse error.
 */
static int parse_request(const char *line, char *op, size_t op_sz,
                          int *ring, char *identity, size_t id_sz,
                          char *path, size_t path_sz)
{
    char ring_s[16];
    char fmt[64];

    snprintf(fmt, sizeof(fmt), "%%%zus %%15s %%%zus %%%zus",
             op_sz - 1, id_sz - 1, path_sz - 1);

    int n = sscanf(line, fmt, op, ring_s, identity, path);
    if (n < 1) return -1;
    op[op_sz - 1]       = '\0';
    identity[id_sz - 1] = '\0';
    path[path_sz - 1]   = '\0';

    if (n >= 2) *ring = (int)strtol(ring_s, NULL, 10);
    return n;
}

/* Called when select() says a peer fd is readable */
static void handle_peer_request(net_t *net, int idx)
{
    net_peer_t *peer = &net->peers[idx];
    shell_t    *sh   = (shell_t *)net->_sh;

    char line[MILTUX_PATH_MAX + 256];
    if (!recv_line(peer->fd, line, sizeof(line))) {
        net_disconnect(net, idx);
        return;
    }

    char op[64];
    char identity[MILTUX_NAME_MAX + 1];
    char path[MILTUX_PATH_MAX];
    int  ring = MILTUX_RING_USER;

    int nf = parse_request(line, op, sizeof(op),
                            &ring, identity, sizeof(identity),
                            path, sizeof(path));

    if (strcmp(op, "QUIT") == 0) {
        net_disconnect(net, idx);
        return;
    }

    /* PEERS: gossip — return our known peers so the caller can join them */
    if (strcmp(op, "PEERS") == 0) {
        char buf[4096];
        size_t pos = 0;
        int j;
        for (j = 0; j < net->peer_count; j++) {
            if (net->peers[j].fd >= 0) {
                int w = snprintf(buf + pos, sizeof(buf) - pos,
                                 "%s %s %d\n",
                                 net->peers[j].identity,
                                 net->peers[j].host,
                                 net->peers[j].port);
                if (w < 0 || (size_t)w >= sizeof(buf) - pos) break;
                pos += (size_t)w;
            }
        }
        send_data(peer->fd, buf, pos);
        return;
    }

    if (strcmp(op, "LS") == 0 && nf >= 4) {
        char buf[65536];
        int  n = fs_list_buf(&sh->fs, path, identity, ring,
                             buf, sizeof(buf));
        if (n < 0) {
            send_err(peer->fd, MILTUX_ERR_PERM);
        } else {
            send_data(peer->fd, buf, (size_t)n);
        }

    } else if (strcmp(op, "CAT") == 0 && nf >= 4) {
        const char  *data;
        size_t       data_len;
        miltux_err_t err = fs_read(&sh->fs, path, &data, &data_len,
                                   identity, ring);
        if (err != MILTUX_OK) {
            send_err(peer->fd, err);
        } else {
            send_data(peer->fd, data, data_len);
        }

    } else if (strcmp(op, "MKDIR") == 0 && nf >= 4) {
        miltux_err_t err = fs_mkdir(&sh->fs, path, identity, ring);
        if (err != MILTUX_OK)
            send_err(peer->fd, err);
        else
            send_line(peer->fd, "OK");

    } else if (strcmp(op, "REMOVE") == 0 && nf >= 4) {
        miltux_err_t err = fs_remove(&sh->fs, path, identity, ring);
        if (err != MILTUX_OK)
            send_err(peer->fd, err);
        else
            send_line(peer->fd, "OK");

    } else if (strcmp(op, "WRITE") == 0) {
        /*
         * WRITE <ring> <identity> <path> <len>\n<data>
         */
        char   len_s[32];
        char   path2[MILTUX_PATH_MAX];
        char   id2[MILTUX_NAME_MAX + 1];
        char   ring_s[16];
        char   fmt[64];
        snprintf(fmt, sizeof(fmt), "WRITE %%15s %%%zus %%%zus %%31s",
                 sizeof(id2) - 1, sizeof(path2) - 1);
        int    n2 = sscanf(line, fmt, ring_s, id2, path2, len_s);
        if (n2 < 4) { send_err(peer->fd, MILTUX_ERR_INVAL); return; }

        int    wr   = (int)strtol(ring_s, NULL, 10);
        size_t wlen = (size_t)strtoul(len_s, NULL, 10);

        if (wlen > MILTUX_FILE_MAX) {
            send_err(peer->fd, MILTUX_ERR_RANGE);
            return;
        }

        char *wbuf = malloc(wlen + 1);
        if (!wbuf) { send_err(peer->fd, MILTUX_ERR_NOMEM); return; }

        if (wlen > 0 && !recv_bytes(peer->fd, wbuf, wlen)) {
            free(wbuf);
            net_disconnect(net, idx);
            return;
        }
        wbuf[wlen] = '\0';

        miltux_err_t err = fs_write(&sh->fs, path2, wbuf, wlen, id2, wr);
        free(wbuf);
        if (err != MILTUX_OK)
            send_err(peer->fd, err);
        else
            send_line(peer->fd, "OK");

    } else {
        send_err(peer->fd, MILTUX_ERR_INVAL);
    }
}

/* -----------------------------------------------------------------------
 * Peer array management
 * ----------------------------------------------------------------------- */

static void compact_peers(net_t *net)
{
    int i = 0;
    while (i < net->peer_count) {
        if (net->peers[i].fd < 0) {
            int remaining = net->peer_count - i - 1;
            if (remaining > 0)
                memmove(&net->peers[i], &net->peers[i + 1],
                        (size_t)remaining * sizeof(net_peer_t));
            net->peer_count--;
        } else {
            i++;
        }
    }
}

/* Return 1 if we already have a live connection to this identity */
static int already_connected(const net_t *net, const char *identity)
{
    int i;
    for (i = 0; i < net->peer_count; i++)
        if (net->peers[i].fd >= 0 &&
            strcmp(net->peers[i].identity, identity) == 0)
            return 1;
    return 0;
}

/* Return 1 if host:port is already in the pending retry queue */
static int already_pending(const net_t *net, const char *host, int port)
{
    int i;
    for (i = 0; i < net->pending_count; i++)
        if (net->pending[i].port == port &&
            strcmp(net->pending[i].host, host) == 0)
            return 1;
    return 0;
}

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */

void net_init(net_t *net)
{
    int i;
    if (!net) return;

    /*
     * Ignore SIGPIPE so that writing to a closed socket returns EPIPE
     * instead of killing the process.
     */
    signal(SIGPIPE, SIG_IGN);

    net->listen_fd     = -1;
    net->port          = 0;
    net->peer_count    = 0;
    net->pending_count = 0;
    net->poll_tick     = 0;
    net->_sh           = NULL;
    for (i = 0; i < MILTUX_PEERS_MAX; i++)
        net->peers[i].fd = -1;
}

miltux_err_t net_listen(net_t *net, int port, const char *identity)
{
    int                fd;
    int                opt = 1;
    struct sockaddr_in addr;
    socklen_t          addrlen = sizeof(addr);

    (void)identity;

    if (!net) return MILTUX_ERR_INVAL;
    if (net->listen_fd >= 0) return MILTUX_ERR_EXIST;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return MILTUX_ERR_NOMEM;

    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons((unsigned short)port);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return MILTUX_ERR_PERM;
    }

    if (getsockname(fd, (struct sockaddr *)&addr, &addrlen) == 0)
        net->port = ntohs(addr.sin_port);
    else
        net->port = port;

    if (listen(fd, 8) < 0) {
        close(fd);
        return MILTUX_ERR_PERM;
    }

    net->listen_fd = fd;
    return MILTUX_OK;
}

void net_add_pending(net_t *net, const char *host, int port)
{
    if (!net || !host || net->pending_count >= MILTUX_PENDING_MAX) return;
    if (already_pending(net, host, port)) return;

    strncpy(net->pending[net->pending_count].host, host,
            sizeof(net->pending[0].host) - 1);
    net->pending[net->pending_count].host[sizeof(net->pending[0].host) - 1] = '\0';
    net->pending[net->pending_count].port = port;
    net->pending_count++;
}

void net_autoconnect(net_t *net, const char *identity)
{
    const char *env;
    char        buf[4096];
    char       *saveptr;
    char       *token;

    if (!net || !identity) return;

    env = getenv("MILTUX_PEERS");
    if (!env || !*env) return;

    strncpy(buf, env, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    token = strtok_r(buf, ",", &saveptr);
    while (token) {
        /* strip leading/trailing spaces */
        while (*token == ' ') token++;
        size_t tlen = strlen(token);
        while (tlen > 0 && token[tlen - 1] == ' ') token[--tlen] = '\0';

        char host[256];
        int  port = MILTUX_NET_PORT_DEFAULT;

        char *colon = strrchr(token, ':');
        if (colon) {
            size_t hlen = (size_t)(colon - token);
            if (hlen >= sizeof(host)) hlen = sizeof(host) - 1;
            strncpy(host, token, hlen);
            host[hlen] = '\0';
            port = (int)strtol(colon + 1, NULL, 10);
        } else {
            strncpy(host, token, sizeof(host) - 1);
            host[sizeof(host) - 1] = '\0';
        }

        if (host[0] != '\0' && port > 0) {
            int idx = net_connect(net, host, port, identity);
            if (idx < 0 && idx != MILTUX_ERR_EXIST)
                net_add_pending(net, host, port);
        }

        token = strtok_r(NULL, ",", &saveptr);
    }
}

void net_gossip(net_t *net, const char *identity)
{
    int   i;
    char  resp[64];
    char *data;

    if (!net || !identity) return;

    for (i = 0; i < net->peer_count; i++) {
        if (net->peers[i].fd < 0) continue;

        /*
         * Skip peers that connected TO us (server_accepted == 1).
         * Those are interactive client sessions (e.g. `miltux -l0`), not
         * daemon peers.  Gossiping with them causes a race condition: the
         * daemon sends "PEERS" on the shared socket while the client is
         * waiting for a FS-operation response (LS/CAT/…), so the client
         * reads "PEERS" instead of "DATA …" and gets MILTUX_ERR_PERM.
         * Client sessions still discover peers via their own gossip calls.
         */
        if (net->peers[i].server_accepted) continue;

        /* Request peer list */
        if (!send_line(net->peers[i].fd, "PEERS")) continue;
        if (!recv_line(net->peers[i].fd, resp, sizeof(resp))) continue;
        if (strncmp(resp, "DATA ", 5) != 0) continue;

        size_t len = (size_t)strtoul(resp + 5, NULL, 10);
        if (len == 0 || len > 16384) continue;

        data = malloc(len + 1);
        if (!data) continue;

        if (!recv_bytes(net->peers[i].fd, data, len)) { free(data); continue; }
        data[len] = '\0';

        /* Parse: each line is "<identity> <host> <port>" */
        char *saveptr;
        char *line = strtok_r(data, "\n", &saveptr);
        while (line) {
            char pident[MILTUX_NAME_MAX + 1];
            char phost[256];
            int  pport = MILTUX_NET_PORT_DEFAULT;

            pident[0] = '\0';
            phost[0]  = '\0';

            char fmt[64];
            snprintf(fmt, sizeof(fmt), "%%%zus %%%zus %%d",
                     sizeof(pident) - 1, sizeof(phost) - 1);
            int n = sscanf(line, fmt, pident, phost, &pport);

            if (n >= 1 && pident[0] != '\0') {
                /* Use identity as host if no host field provided */
                if (n < 2 || phost[0] == '\0')
                    snprintf(phost, sizeof(phost), "%s", pident);

                /* Don't connect to self */
                if (strcmp(pident, identity) == 0) {
                    line = strtok_r(NULL, "\n", &saveptr);
                    continue;
                }
                /* Don't double-connect */
                if (!already_connected(net, pident) &&
                    !already_pending(net, phost, pport) &&
                    pport > 0) {
                    int idx = net_connect(net, phost, pport, identity);
                    if (idx < 0 && idx != MILTUX_ERR_EXIST)
                        net_add_pending(net, phost, pport);
                }
            }
            line = strtok_r(NULL, "\n", &saveptr);
        }
        free(data);
    }
}

void net_poll(net_t *net, void *sh)
{
    fd_set         rfds;
    struct timeval tv;
    int            maxfd = -1;
    int            i;

    if (!net) return;
    if (sh) net->_sh = sh;

    compact_peers(net);

    net->poll_tick++;

    /* Retry pending peers every MILTUX_GOSSIP_TICKS */
    if (net->poll_tick % MILTUX_GOSSIP_TICKS == 1) {
        shell_t *s = (shell_t *)net->_sh;
        const char *id = s ? s->identity : "miltux";

        /* Copy pending list so retries can modify it */
        net_pending_t tmp[MILTUX_PENDING_MAX];
        int           tmp_count = net->pending_count;
        memcpy(tmp, net->pending, (size_t)tmp_count * sizeof(net_pending_t));
        net->pending_count = 0;

        for (i = 0; i < tmp_count; i++) {
            int idx = net_connect(net, tmp[i].host, tmp[i].port, id);
            if (idx < 0 && idx != MILTUX_ERR_EXIST)
                net_add_pending(net, tmp[i].host, tmp[i].port);
        }

        /* Gossip with connected peers */
        net_gossip(net, id);
    }

    FD_ZERO(&rfds);

    if (net->listen_fd >= 0) {
        FD_SET(net->listen_fd, &rfds);
        if (net->listen_fd > maxfd) maxfd = net->listen_fd;
    }

    for (i = 0; i < net->peer_count; i++) {
        if (net->peers[i].fd >= 0) {
            FD_SET(net->peers[i].fd, &rfds);
            if (net->peers[i].fd > maxfd) maxfd = net->peers[i].fd;
        }
    }

    if (maxfd < 0) return;

    tv.tv_sec  = 0;
    tv.tv_usec = 0;   /* fully non-blocking */

    int nready = select(maxfd + 1, &rfds, NULL, NULL, &tv);
    if (nready <= 0) return;

    /* Accept new connections */
    if (net->listen_fd >= 0 && FD_ISSET(net->listen_fd, &rfds)) {
        struct sockaddr_in caddr;
        socklen_t          clen = sizeof(caddr);
        int cfd = accept(net->listen_fd, (struct sockaddr *)&caddr, &clen);

        if (cfd >= 0) {
            if (net->peer_count >= MILTUX_PEERS_MAX) {
                close(cfd);
            } else {
                shell_t    *s = (shell_t *)net->_sh;
                net_peer_t *p = &net->peers[net->peer_count];
                p->fd = cfd;
                inet_ntop(AF_INET, &caddr.sin_addr, p->host, sizeof(p->host));

                set_sock_timeout(cfd, MILTUX_SOCK_TIMEOUT_S);

                const char *local_id = s ? s->identity : "miltux";
                int local_port = net->port;
                int peer_port  = 0;

                if (do_server_handshake(cfd, local_id, local_port,
                                        p->identity, sizeof(p->identity),
                                        &peer_port)) {
                    /* Use identity as canonical host for gossip */
                    strncpy(p->host, p->identity, sizeof(p->host) - 1);
                    p->host[sizeof(p->host) - 1] = '\0';
                    p->port = peer_port;
                    p->server_accepted = 1; /* they connected to us */

                    /* Reject duplicates and self-connections */
                    if (already_connected(net, p->identity) ||
                        strcmp(p->identity, local_id) == 0) {
                        close(cfd);
                        p->fd = -1;
                    } else {
                        printf("[net] peer joined: %s (port %d)\n",
                               p->identity, p->port);
                        fflush(stdout);
                        net->peer_count++;
                    }
                } else {
                    close(cfd);
                    p->fd = -1;
                }
            }
        }
    }

    /* Handle one pending request per peer */
    for (i = 0; i < net->peer_count; i++) {
        if (net->peers[i].fd >= 0 && FD_ISSET(net->peers[i].fd, &rfds)) {
            handle_peer_request(net, i);
        }
    }
}

int net_connect(net_t *net, const char *host, int port, const char *identity)
{
    struct addrinfo  hints;
    struct addrinfo *res = NULL;
    char             port_s[16];
    int              fd;
    net_peer_t      *p;

    if (!net || !host || !identity) return MILTUX_ERR_INVAL;
    if (net->peer_count >= MILTUX_PEERS_MAX) return MILTUX_ERR_NOMEM;

    snprintf(port_s, sizeof(port_s), "%d", port);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, port_s, &hints, &res) != 0 || !res)
        return MILTUX_ERR_NOENT;

    fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) { freeaddrinfo(res); return MILTUX_ERR_NOMEM; }

    set_sock_timeout(fd, MILTUX_SOCK_TIMEOUT_S);

    if (connect(fd, res->ai_addr, res->ai_addrlen) < 0) {
        close(fd);
        freeaddrinfo(res);
        return MILTUX_ERR_NOENT;
    }
    freeaddrinfo(res);

    p = &net->peers[net->peer_count];
    p->fd   = fd;
    p->port = port;
    strncpy(p->host, host, sizeof(p->host) - 1);
    p->host[sizeof(p->host) - 1] = '\0';

    int peer_port = 0;
    if (!do_client_handshake(fd, identity, net->port,
                             p->identity, sizeof(p->identity),
                             &peer_port)) {
        close(fd);
        p->fd = -1;
        return MILTUX_ERR_PERM;
    }

    /* Use listen port from handshake if available */
    if (peer_port > 0) p->port = peer_port;
    p->server_accepted = 0; /* we connected to them (daemon peer) */

    /* Reject self-connections and duplicates */
    if (strcmp(p->identity, identity) == 0 ||
        already_connected(net, p->identity)) {
        close(fd);
        p->fd = -1;
        return MILTUX_ERR_EXIST;
    }

    printf("[net] connected to %s@%s:%d\n", p->identity, host, p->port);
    fflush(stdout);
    return net->peer_count++;
}

void net_disconnect(net_t *net, int idx)
{
    if (!net || idx < 0 || idx >= net->peer_count) return;
    if (net->peers[idx].fd >= 0) {
        send_line(net->peers[idx].fd, "QUIT");
        close(net->peers[idx].fd);
        net->peers[idx].fd = -1;
    }
}

void net_destroy(net_t *net)
{
    int i;
    if (!net) return;
    for (i = 0; i < net->peer_count; i++)
        net_disconnect(net, i);
    net->peer_count    = 0;
    net->pending_count = 0;
    if (net->listen_fd >= 0) {
        close(net->listen_fd);
        net->listen_fd = -1;
    }
}

void net_list_peers(const net_t *net)
{
    int i;
    if (!net || net->peer_count == 0) {
        printf("  (no connected peers)\n");
        return;
    }
    printf("  %-3s  %-20s  %-20s  %s\n", "#", "Identity", "Host", "Port");
    printf("  %-3s  %-20s  %-20s  %s\n", "-", "--------", "----", "----");
    for (i = 0; i < net->peer_count; i++) {
        if (net->peers[i].fd >= 0)
            printf("  %-3d  %-20s  %-20s  %d\n",
                   i,
                   net->peers[i].identity,
                   net->peers[i].host,
                   net->peers[i].port);
    }
    if (net->pending_count > 0) {
        printf("  Pending retries: %d\n", net->pending_count);
    }
}

/* -----------------------------------------------------------------------
 * Remote FS operations (client side)
 * ----------------------------------------------------------------------- */

static miltux_err_t remote_simple(net_peer_t *peer, const char *req)
{
    char resp[512];
    if (!send_line(peer->fd, req)) return MILTUX_ERR_PERM;
    if (!recv_line(peer->fd, resp, sizeof(resp))) return MILTUX_ERR_PERM;
    if (strncmp(resp, "OK", 2) == 0) return MILTUX_OK;
    if (strncmp(resp, "ERR ", 4) == 0)
        fprintf(stderr, "remote: %s\n", resp + 4);
    return MILTUX_ERR_PERM;
}

static miltux_err_t remote_data_print(net_peer_t *peer, const char *req)
{
    char   resp[64];
    char  *buf    = NULL;
    size_t buflen = 0;

    if (!send_line(peer->fd, req)) return MILTUX_ERR_PERM;
    if (!recv_line(peer->fd, resp, sizeof(resp))) return MILTUX_ERR_PERM;

    if (strncmp(resp, "DATA ", 5) == 0) {
        size_t len = (size_t)strtoul(resp + 5, NULL, 10);
        buf = malloc(len + 1);
        if (!buf) return MILTUX_ERR_NOMEM;
        if (len > 0 && !recv_bytes(peer->fd, buf, len)) {
            free(buf);
            return MILTUX_ERR_PERM;
        }
        buf[len] = '\0';
        buflen   = len;
        fwrite(buf, 1, buflen, stdout);
        if (buflen > 0 && buf[buflen - 1] != '\n') putchar('\n');
        free(buf);
        return MILTUX_OK;
    }

    if (strncmp(resp, "ERR ", 4) == 0)
        fprintf(stderr, "remote: %s\n", resp + 4);
    return MILTUX_ERR_PERM;
}

miltux_err_t net_remote_ls(net_peer_t *peer, const char *path,
                            const char *identity, int ring)
{
    char req[MILTUX_PATH_MAX + 128];
    snprintf(req, sizeof(req), "LS %d %s %s", ring, identity, path);
    return remote_data_print(peer, req);
}

miltux_err_t net_remote_cat(net_peer_t *peer, const char *path,
                             const char *identity, int ring)
{
    char req[MILTUX_PATH_MAX + 128];
    snprintf(req, sizeof(req), "CAT %d %s %s", ring, identity, path);
    return remote_data_print(peer, req);
}

miltux_err_t net_remote_mkdir(net_peer_t *peer, const char *path,
                               const char *identity, int ring)
{
    char req[MILTUX_PATH_MAX + 128];
    snprintf(req, sizeof(req), "MKDIR %d %s %s", ring, identity, path);
    return remote_simple(peer, req);
}

miltux_err_t net_remote_remove(net_peer_t *peer, const char *path,
                                const char *identity, int ring)
{
    char req[MILTUX_PATH_MAX + 128];
    snprintf(req, sizeof(req), "REMOVE %d %s %s", ring, identity, path);
    return remote_simple(peer, req);
}

miltux_err_t net_remote_write(net_peer_t *peer,
                               const char *path,
                               const char *data, size_t data_len,
                               const char *identity, int ring)
{
    char hdr[MILTUX_PATH_MAX + 128];
    snprintf(hdr, sizeof(hdr), "WRITE %d %s %s %zu",
             ring, identity, path, data_len);
    if (!send_line(peer->fd, hdr)) return MILTUX_ERR_PERM;
    if (data_len > 0 && !send_all(peer->fd, data, data_len)) return MILTUX_ERR_PERM;

    char resp[256];
    if (!recv_line(peer->fd, resp, sizeof(resp))) return MILTUX_ERR_PERM;
    if (strncmp(resp, "OK", 2) == 0) return MILTUX_OK;
    if (strncmp(resp, "ERR ", 4) == 0)
        fprintf(stderr, "remote: %s\n", resp + 4);
    return MILTUX_ERR_PERM;
}
