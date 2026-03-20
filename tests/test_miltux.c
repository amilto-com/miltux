/*
 * MiLTuX - Test suite
 *
 * A minimal self-contained test runner.  Each test function returns 0 on
 * pass and non-zero on failure.  The runner tallies results and exits with
 * a non-zero status if any test failed, so CI can detect failures.
 */

#include "miltux.h"
#include "ring.h"
#include "acl.h"
#include "fs.h"
#include "shell.h"
#include "net.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>       /* fork, dup2, STDOUT_FILENO, _exit */
#include <fcntl.h>        /* open, O_WRONLY */
#include <time.h>         /* nanosleep */
#include <signal.h>       /* kill, SIGKILL */
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>     /* waitpid */

/* -----------------------------------------------------------------------
 * Minimal test framework
 * ----------------------------------------------------------------------- */

static int tests_run    = 0;
static int tests_failed = 0;

#define ASSERT(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "  FAIL  %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            return 1; \
        } \
    } while (0)

#define RUN_TEST(fn) \
    do { \
        tests_run++; \
        printf("  TEST  %s ... ", #fn); \
        int _r = fn(); \
        if (_r == 0) { printf("OK\n"); } \
        else { tests_failed++; } \
    } while (0)

/* -----------------------------------------------------------------------
 * Ring tests
 * ----------------------------------------------------------------------- */

static int test_ring_init(void)
{
    ring_ctx_t ctx;
    ASSERT(ring_ctx_init(&ctx, MILTUX_RING_USER) == MILTUX_OK);
    ASSERT(ring_current(&ctx) == MILTUX_RING_USER);
    return 0;
}

static int test_ring_init_invalid(void)
{
    ring_ctx_t ctx;
    ASSERT(ring_ctx_init(&ctx, -1) == MILTUX_ERR_RANGE);
    ASSERT(ring_ctx_init(&ctx, 8)  == MILTUX_ERR_RANGE);
    return 0;
}

static int test_ring_outward_transition(void)
{
    ring_ctx_t ctx;
    ring_ctx_init(&ctx, MILTUX_RING_USER);   /* ring 4 */
    ASSERT(ring_transition(&ctx, 7) == MILTUX_OK);
    ASSERT(ring_current(&ctx) == 7);
    return 0;
}

static int test_ring_inward_transition_denied(void)
{
    ring_ctx_t ctx;
    ring_ctx_init(&ctx, MILTUX_RING_USER);   /* ring 4, bracket r3=4 */
    /* Attempting to go to ring 0 should be denied */
    ASSERT(ring_transition(&ctx, 0) == MILTUX_ERR_PERM);
    ASSERT(ring_current(&ctx) == MILTUX_RING_USER); /* unchanged */
    return 0;
}

static int test_ring_check(void)
{
    ring_ctx_t ctx;
    ring_ctx_init(&ctx, 2);
    /* Ring 2 can access objects requiring ring 2 or higher (less privileged) */
    ASSERT(ring_check(&ctx, 2) == MILTUX_OK);
    ASSERT(ring_check(&ctx, 5) == MILTUX_OK);
    /* Ring 2 cannot access objects requiring ring 0 or 1 */
    ASSERT(ring_check(&ctx, 0) == MILTUX_ERR_PERM);
    ASSERT(ring_check(&ctx, 1) == MILTUX_ERR_PERM);
    return 0;
}

static int test_ring_name(void)
{
    ASSERT(strstr(ring_name(0), "kernel") != NULL);
    ASSERT(strstr(ring_name(4), "user")   != NULL);
    return 0;
}

/* -----------------------------------------------------------------------
 * ACL tests
 * ----------------------------------------------------------------------- */

static int test_acl_basic(void)
{
    acl_t acl;
    acl_init(&acl);
    ASSERT(acl.count == 0);

    ASSERT(acl_set(&acl, "alice", ACL_PERM_READ | ACL_PERM_WRITE, 7) == MILTUX_OK);
    ASSERT(acl.count == 1);

    ASSERT(acl_check(&acl, "alice", 7, ACL_PERM_READ)  == MILTUX_OK);
    ASSERT(acl_check(&acl, "alice", 7, ACL_PERM_WRITE) == MILTUX_OK);
    ASSERT(acl_check(&acl, "alice", 7, ACL_PERM_EXEC)  == MILTUX_ERR_PERM);
    return 0;
}

static int test_acl_wildcard(void)
{
    acl_t acl;
    acl_init(&acl);
    acl_set(&acl, "*", ACL_PERM_READ, MILTUX_RING_MAX);

    /* Anyone should be able to read */
    ASSERT(acl_check(&acl, "bob", 7, ACL_PERM_READ)  == MILTUX_OK);
    ASSERT(acl_check(&acl, "bob", 7, ACL_PERM_WRITE) == MILTUX_ERR_PERM);
    return 0;
}

static int test_acl_ring_limit(void)
{
    acl_t acl;
    acl_init(&acl);
    /* Only users in ring 2 or lower (more privileged) may read */
    acl_set(&acl, "alice", ACL_PERM_READ, 2);

    ASSERT(acl_check(&acl, "alice", 2, ACL_PERM_READ) == MILTUX_OK);
    /* Ring 3 is less privileged than ring 2 → denied */
    ASSERT(acl_check(&acl, "alice", 3, ACL_PERM_READ) == MILTUX_ERR_PERM);
    return 0;
}

static int test_acl_remove(void)
{
    acl_t acl;
    acl_init(&acl);
    acl_set(&acl, "alice", ACL_PERM_ALL, MILTUX_RING_MAX);
    ASSERT(acl.count == 1);

    ASSERT(acl_remove(&acl, "alice") == MILTUX_OK);
    ASSERT(acl.count == 0);

    ASSERT(acl_remove(&acl, "alice") == MILTUX_ERR_NOENT);
    return 0;
}

static int test_acl_update_existing(void)
{
    acl_t acl;
    acl_init(&acl);
    acl_set(&acl, "bob", ACL_PERM_READ, MILTUX_RING_MAX);
    ASSERT(acl.count == 1);

    /* Updating bob should not increase count */
    acl_set(&acl, "bob", ACL_PERM_ALL, MILTUX_RING_MAX);
    ASSERT(acl.count == 1);
    ASSERT(acl_check(&acl, "bob", 7, ACL_PERM_WRITE) == MILTUX_OK);
    return 0;
}

static int test_acl_perm_str(void)
{
    char s[5];
    acl_perm_str(ACL_PERM_ALL, s);
    ASSERT(strcmp(s, "rwea") == 0);

    acl_perm_str(ACL_PERM_READ, s);
    ASSERT(s[0] == 'r' && s[1] == '-' && s[2] == '-' && s[3] == '-');
    return 0;
}

/* -----------------------------------------------------------------------
 * File system tests
 * ----------------------------------------------------------------------- */

static int test_fs_init(void)
{
    fs_t fs;
    ASSERT(fs_init(&fs) == MILTUX_OK);
    ASSERT(fs.root != NULL);
    ASSERT(fs.cwd  != NULL);
    fs_destroy(&fs);
    return 0;
}

static int test_fs_mkdir(void)
{
    fs_t fs;
    fs_init(&fs);

    ASSERT(fs_mkdir(&fs, ">testdir", "alice", MILTUX_RING_MAX) == MILTUX_OK);

    miltux_err_t err;
    fs_node_t *node = fs_resolve(&fs, ">testdir", &err);
    ASSERT(node != NULL);
    ASSERT(err  == MILTUX_OK);

    /* Creating it again should fail */
    ASSERT(fs_mkdir(&fs, ">testdir", "alice", MILTUX_RING_MAX) == MILTUX_ERR_EXIST);

    fs_destroy(&fs);
    return 0;
}

static int test_fs_write_read(void)
{
    fs_t         fs;
    const char  *data;
    size_t       len;

    fs_init(&fs);
    ASSERT(fs_write(&fs, ">hello.txt", "hello, world\n", 13, "alice",
                    MILTUX_RING_MAX) == MILTUX_OK);

    ASSERT(fs_read(&fs, ">hello.txt", &data, &len, "alice",
                   MILTUX_RING_MAX) == MILTUX_OK);
    ASSERT(len == 13);
    ASSERT(memcmp(data, "hello, world\n", 13) == 0);

    fs_destroy(&fs);
    return 0;
}

static int test_fs_remove(void)
{
    fs_t fs;
    fs_init(&fs);

    fs_write(&fs, ">tmp.txt", "data", 4, "alice", MILTUX_RING_MAX);

    miltux_err_t err;
    ASSERT(fs_resolve(&fs, ">tmp.txt", &err) != NULL);

    ASSERT(fs_remove(&fs, ">tmp.txt", "alice", MILTUX_RING_MAX) == MILTUX_OK);
    ASSERT(fs_resolve(&fs, ">tmp.txt", &err) == NULL);
    ASSERT(err == MILTUX_ERR_NOENT);

    fs_destroy(&fs);
    return 0;
}

static int test_fs_chdir(void)
{
    fs_t fs;
    fs_init(&fs);

    fs_mkdir(&fs, ">sub", "alice", MILTUX_RING_MAX);
    ASSERT(fs_chdir(&fs, ">sub", "alice", MILTUX_RING_MAX) == MILTUX_OK);
    ASSERT(strcmp(fs.cwd_path, ">sub") == 0);

    /* Relative path back to root */
    ASSERT(fs_chdir(&fs, ">", "alice", MILTUX_RING_MAX) == MILTUX_OK);
    ASSERT(strcmp(fs.cwd_path, ">") == 0);

    fs_destroy(&fs);
    return 0;
}

static int test_fs_perm_denied(void)
{
    fs_t fs;
    fs_init(&fs);

    /* alice creates a file with no wildcard write permission */
    fs_write(&fs, ">secret.txt", "secret", 6, "alice", MILTUX_RING_MAX);

    /* Find the node and remove the wildcard write */
    miltux_err_t err;
    fs_node_t *node = fs_resolve(&fs, ">secret.txt", &err);
    ASSERT(node != NULL);
    acl_remove(&node->acl, "*");
    acl_set(&node->acl, "*", ACL_PERM_NONE, MILTUX_RING_MAX);

    /* bob should not be able to read */
    const char *data; size_t len;
    ASSERT(fs_read(&fs, ">secret.txt", &data, &len, "bob",
                   MILTUX_RING_MAX) == MILTUX_ERR_PERM);

    fs_destroy(&fs);
    return 0;
}

static int test_fs_remove_notempty(void)
{
    fs_t fs;
    fs_init(&fs);

    fs_mkdir(&fs, ">parent", "alice", MILTUX_RING_MAX);
    fs_mkdir(&fs, ">parent>child", "alice", MILTUX_RING_MAX);

    ASSERT(fs_remove(&fs, ">parent", "alice", MILTUX_RING_MAX)
           == MILTUX_ERR_NOTEMPTY);

    fs_destroy(&fs);
    return 0;
}

/* -----------------------------------------------------------------------
 * Shell / integration tests
 * ----------------------------------------------------------------------- */

static int test_shell_init(void)
{
    shell_t sh;
    ASSERT(shell_init(&sh, "alice", MILTUX_RING_USER) == MILTUX_OK);
    ASSERT(ring_current(&sh.ring_ctx) == MILTUX_RING_USER);
    shell_destroy(&sh);
    return 0;
}

static int test_shell_exec_mkdir_cd_ls(void)
{
    shell_t sh;
    shell_init(&sh, "alice", MILTUX_RING_USER);

    ASSERT(shell_exec(&sh, "mkdir >docs") == MILTUX_OK);

    miltux_err_t err;
    ASSERT(fs_resolve(&sh.fs, ">docs", &err) != NULL);

    ASSERT(shell_exec(&sh, "cd >docs") == MILTUX_OK);
    ASSERT(strcmp(sh.fs.cwd_path, ">docs") == 0);

    ASSERT(shell_exec(&sh, "ls") == MILTUX_OK);

    shell_destroy(&sh);
    return 0;
}

static int test_shell_exec_write_cat(void)
{
    shell_t sh;
    shell_init(&sh, "alice", MILTUX_RING_USER);

    ASSERT(shell_exec(&sh, "write >note.txt hello world") == MILTUX_OK);

    /* Capture stdout by checking the node directly */
    miltux_err_t err;
    fs_node_t *node = fs_resolve(&sh.fs, ">note.txt", &err);
    ASSERT(node != NULL);
    ASSERT(node->data != NULL);
    ASSERT(strstr(node->data, "hello world") != NULL);

    shell_destroy(&sh);
    return 0;
}

static int test_shell_exec_unknown_cmd(void)
{
    shell_t sh;
    shell_init(&sh, "alice", MILTUX_RING_USER);
    /* Unknown command should return MILTUX_OK (just prints error) */
    ASSERT(shell_exec(&sh, "frobnicate") == MILTUX_OK);
    shell_destroy(&sh);
    return 0;
}

static int test_shell_exec_empty(void)
{
    shell_t sh;
    shell_init(&sh, "alice", MILTUX_RING_USER);
    ASSERT(shell_exec(&sh, "")    == MILTUX_OK);
    ASSERT(shell_exec(&sh, "   ") == MILTUX_OK);
    ASSERT(shell_exec(&sh, "# comment") == MILTUX_OK);
    shell_destroy(&sh);
    return 0;
}

/* -----------------------------------------------------------------------
 * Networking tests
 * ----------------------------------------------------------------------- */

/* Timing constants for the fork-based network test */
#define NET_TEST_CONNECT_DELAY_NS  (50L * 1000000L)  /* 50 ms: wait for server */
#define NET_TEST_POLL_INTERVAL_NS  (20L * 1000000L)  /* 20 ms per poll cycle  */
#define NET_TEST_MAX_POLLS         50                 /* 50 × 20 ms = 1 s max  */

static int test_net_init(void)
{
    net_t net;
    net_init(&net);
    ASSERT(net.listen_fd  == -1);
    ASSERT(net.peer_count ==  0);
    net_destroy(&net);
    return 0;
}

static int test_net_listen_on_random_port(void)
{
    net_t net;
    net_init(&net);

    /* port=0 lets the OS pick a free port */
    ASSERT(net_listen(&net, 0, "test") == MILTUX_OK);
    ASSERT(net.listen_fd >= 0);
    ASSERT(net.port > 0);       /* OS should have assigned a real port */

    net_destroy(&net);
    ASSERT(net.listen_fd == -1);
    return 0;
}

/*
 * Fork a server and a client; client connects, does a remote LS, verifies
 * the response.  Uses fork() + wait() to simulate two nodes in one test run.
 */
static int test_net_connect_and_remote_ls(void)
{
    net_t server;
    net_init(&server);

    /* Bind to a random port */
    ASSERT(net_listen(&server, 0, "server") == MILTUX_OK);
    int port = server.port;

    /* Flush stdio before fork so the child starts with a clean buffer */
    fflush(NULL);

    pid_t pid = fork();
    ASSERT(pid >= 0);

    if (pid == 0) {
        /* -------- child: act as the client -------- */

        /* Redirect stdout and stderr to /dev/null so child output does not
         * interleave with the parent test runner's output. */
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }

        /* Give the server a moment to enter accept() */
        struct timespec ts = {0, NET_TEST_CONNECT_DELAY_NS};
        nanosleep(&ts, NULL);

        net_t client;
        net_init(&client);

        int idx = net_connect(&client, "127.0.0.1", port, "client");
        if (idx < 0) _exit(1);

        /* Remote ls of root */
        miltux_err_t err = net_remote_ls(&client.peers[idx], ">",
                                         "client", MILTUX_RING_USER);
        net_destroy(&client);
        _exit(err == MILTUX_OK ? 0 : 2);
    }

    /* -------- parent: act as the server -------- */
    shell_t sh;
    shell_init(&sh, "server", MILTUX_RING_USER);
    server._sh = &sh;

    /* Poll until the child finishes (with a generous timeout) */
    int i;
    for (i = 0; i < NET_TEST_MAX_POLLS; i++) {
        net_poll(&server, &sh);

        struct timespec ts = {0, NET_TEST_POLL_INTERVAL_NS};
        nanosleep(&ts, NULL);

        int    status = 0;
        pid_t  w      = waitpid(pid, &status, WNOHANG);
        if (w == pid) {
            net_destroy(&server);
            shell_destroy(&sh);
            return (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? 0 : 1;
        }
    }

    /* Timed out: clean up the child */
    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);
    net_destroy(&server);
    shell_destroy(&sh);
    return 1;
}

static int test_fs_list_buf(void)
{
    fs_t fs;
    fs_init(&fs);
    fs_mkdir(&fs, ">alpha", "alice", MILTUX_RING_MAX);
    fs_write(&fs, ">beta.txt", "data", 4, "alice", MILTUX_RING_MAX);

    char buf[4096];
    int  n = fs_list_buf(&fs, ">", "alice", MILTUX_RING_MAX,
                         buf, sizeof(buf));
    ASSERT(n > 0);
    ASSERT(strstr(buf, "alpha") != NULL);
    ASSERT(strstr(buf, "beta.txt") != NULL);

    /* Should fail for a file node */
    ASSERT(fs_list_buf(&fs, ">beta.txt", "alice", MILTUX_RING_MAX,
                       buf, sizeof(buf)) < 0);

    fs_destroy(&fs);
    return 0;
}

/* -----------------------------------------------------------------------
 * Main test runner
 * ----------------------------------------------------------------------- */

int main(void)
{
    printf("=== MiLTuX test suite ===\n\n");

    printf("Ring protection:\n");
    RUN_TEST(test_ring_init);
    RUN_TEST(test_ring_init_invalid);
    RUN_TEST(test_ring_outward_transition);
    RUN_TEST(test_ring_inward_transition_denied);
    RUN_TEST(test_ring_check);
    RUN_TEST(test_ring_name);

    printf("\nAccess Control Lists:\n");
    RUN_TEST(test_acl_basic);
    RUN_TEST(test_acl_wildcard);
    RUN_TEST(test_acl_ring_limit);
    RUN_TEST(test_acl_remove);
    RUN_TEST(test_acl_update_existing);
    RUN_TEST(test_acl_perm_str);

    printf("\nFile system:\n");
    RUN_TEST(test_fs_init);
    RUN_TEST(test_fs_mkdir);
    RUN_TEST(test_fs_write_read);
    RUN_TEST(test_fs_remove);
    RUN_TEST(test_fs_chdir);
    RUN_TEST(test_fs_perm_denied);
    RUN_TEST(test_fs_remove_notempty);

    printf("\nShell / integration:\n");
    RUN_TEST(test_shell_init);
    RUN_TEST(test_shell_exec_mkdir_cd_ls);
    RUN_TEST(test_shell_exec_write_cat);
    RUN_TEST(test_shell_exec_unknown_cmd);
    RUN_TEST(test_shell_exec_empty);

    printf("\nNetworking:\n");
    RUN_TEST(test_net_init);
    RUN_TEST(test_net_listen_on_random_port);
    RUN_TEST(test_fs_list_buf);
    RUN_TEST(test_net_connect_and_remote_ls);

    printf("\n=== Results: %d/%d passed ===\n",
           tests_run - tests_failed, tests_run);

    return tests_failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
