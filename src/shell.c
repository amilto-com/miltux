/*
 * MiLTuX - Interactive command shell implementation
 */

#include "shell.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_ARGS 64
#define LINE_MAX_LEN 4096

/* -----------------------------------------------------------------------
 * Argument parsing
 * ----------------------------------------------------------------------- */

static int parse_args(char *line, char *argv[], int max_args)
{
    int argc = 0;
    char *p  = line;

    while (*p && argc < max_args - 1) {
        /* Skip whitespace */
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;

        argv[argc++] = p;

        /* Find end of token */
        while (*p && !isspace((unsigned char)*p)) p++;
        if (*p) { *p = '\0'; p++; }
    }
    argv[argc] = NULL;
    return argc;
}

/* -----------------------------------------------------------------------
 * Command handlers
 * ----------------------------------------------------------------------- */

static void cmd_help(void)
{
    printf(
        "MiLTuX commands:\n"
        "  ls   [path]              list directory contents\n"
        "  cd   <path>              change directory\n"
        "  mkdir <path>             create a directory\n"
        "  rm   <path>              remove a file or empty directory\n"
        "  cat  <path>              display file contents\n"
        "  write <path> <text...>   write text to a file\n"
        "  acl  <path>              show the ACL of a segment\n"
        "  ring                     show current ring level\n"
        "  su   <ring>              switch ring (0=kernel .. 7=user)\n"
        "  whoami                   show current identity\n"
        "  pwd                      print working directory\n"
        "  help                     show this help\n"
        "  exit | quit              leave MiLTuX\n"
    );
}

static void cmd_pwd(shell_t *sh)
{
    printf("%s\n", sh->fs.cwd_path[0] ? sh->fs.cwd_path : ">");
}

static void cmd_whoami(shell_t *sh)
{
    printf("%s  (%s)\n",
           sh->identity,
           ring_name(ring_current(&sh->ring_ctx)));
}

static void cmd_ring(shell_t *sh)
{
    printf("Current ring: %d  — %s\n",
           ring_current(&sh->ring_ctx),
           ring_name(ring_current(&sh->ring_ctx)));
}

static void cmd_su(shell_t *sh, int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "su: usage: su <ring>\n");
        return;
    }
    char *end;
    long  target = strtol(argv[1], &end, 10);
    if (*end != '\0' || target < MILTUX_RING_MIN || target > MILTUX_RING_MAX) {
        fprintf(stderr, "su: invalid ring number '%s' (must be %d–%d)\n",
                argv[1], MILTUX_RING_MIN, MILTUX_RING_MAX);
        return;
    }
    miltux_err_t err = ring_transition(&sh->ring_ctx, (int)target);
    if (err != MILTUX_OK)
        fprintf(stderr, "su: %s\n", miltux_strerror(err));
    else
        printf("Now running in %s\n", ring_name(ring_current(&sh->ring_ctx)));
}

static void cmd_ls(shell_t *sh, int argc, char *argv[])
{
    const char *path = (argc >= 2) ? argv[1] : ".";
    miltux_err_t err = fs_list(&sh->fs, path,
                                sh->identity,
                                ring_current(&sh->ring_ctx));
    if (err != MILTUX_OK)
        fprintf(stderr, "ls: %s: %s\n", path, miltux_strerror(err));
}

static void cmd_cd(shell_t *sh, int argc, char *argv[])
{
    if (argc < 2) {
        /* cd with no args goes to root, like Multics */
        miltux_err_t err = fs_chdir(&sh->fs, ">",
                                     sh->identity,
                                     ring_current(&sh->ring_ctx));
        if (err != MILTUX_OK)
            fprintf(stderr, "cd: %s\n", miltux_strerror(err));
        return;
    }
    miltux_err_t err = fs_chdir(&sh->fs, argv[1],
                                 sh->identity,
                                 ring_current(&sh->ring_ctx));
    if (err != MILTUX_OK)
        fprintf(stderr, "cd: %s: %s\n", argv[1], miltux_strerror(err));
}

static void cmd_mkdir(shell_t *sh, int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "mkdir: usage: mkdir <path>\n");
        return;
    }
    miltux_err_t err = fs_mkdir(&sh->fs, argv[1],
                                 sh->identity,
                                 ring_current(&sh->ring_ctx));
    if (err != MILTUX_OK)
        fprintf(stderr, "mkdir: %s: %s\n", argv[1], miltux_strerror(err));
}

static void cmd_rm(shell_t *sh, int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "rm: usage: rm <path>\n");
        return;
    }
    miltux_err_t err = fs_remove(&sh->fs, argv[1],
                                  sh->identity,
                                  ring_current(&sh->ring_ctx));
    if (err != MILTUX_OK)
        fprintf(stderr, "rm: %s: %s\n", argv[1], miltux_strerror(err));
}

static void cmd_cat(shell_t *sh, int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "cat: usage: cat <path>\n");
        return;
    }
    const char  *data;
    size_t       len;
    miltux_err_t err = fs_read(&sh->fs, argv[1], &data, &len,
                                sh->identity,
                                ring_current(&sh->ring_ctx));
    if (err != MILTUX_OK) {
        fprintf(stderr, "cat: %s: %s\n", argv[1], miltux_strerror(err));
        return;
    }
    fwrite(data, 1, len, stdout);
    if (len > 0 && data[len - 1] != '\n') putchar('\n');
}

static void cmd_write(shell_t *sh, int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "write: usage: write <path> <text...>\n");
        return;
    }

    /* Reconstruct text from remaining arguments */
    char   text[MILTUX_FILE_MAX];
    size_t pos = 0;
    for (int i = 2; i < argc; i++) {
        size_t alen = strlen(argv[i]);
        if (pos + alen + 2 > MILTUX_FILE_MAX) {
            fprintf(stderr, "write: text too long\n");
            return;
        }
        if (i > 2) text[pos++] = ' ';
        memcpy(text + pos, argv[i], alen);
        pos += alen;
    }
    text[pos++] = '\n';
    text[pos]   = '\0';

    miltux_err_t err = fs_write(&sh->fs, argv[1], text, pos,
                                 sh->identity,
                                 ring_current(&sh->ring_ctx));
    if (err != MILTUX_OK)
        fprintf(stderr, "write: %s: %s\n", argv[1], miltux_strerror(err));
}

static void cmd_acl(shell_t *sh, int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "acl: usage: acl <path>\n");
        return;
    }
    miltux_err_t err;
    fs_node_t   *node = fs_resolve(&sh->fs, argv[1], &err);
    if (!node) {
        fprintf(stderr, "acl: %s: %s\n", argv[1], miltux_strerror(err));
        return;
    }
    printf("ACL for %s:\n", argv[1]);
    acl_print(&node->acl);
}

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */

miltux_err_t shell_init(shell_t *sh, const char *identity, int ring)
{
    miltux_err_t err;

    if (!sh || !identity) return MILTUX_ERR_INVAL;

    err = ring_ctx_init(&sh->ring_ctx, ring);
    if (err != MILTUX_OK) return err;

    err = fs_init(&sh->fs);
    if (err != MILTUX_OK) return err;

    strncpy(sh->identity, identity, MILTUX_NAME_MAX);
    sh->identity[MILTUX_NAME_MAX] = '\0';

    /* Create a home directory for the user */
    char home[MILTUX_PATH_MAX];
    snprintf(home, sizeof(home), ">user_dir_dir>%s", identity);
    fs_mkdir(&sh->fs, home, identity, ring);

    /* cd into it */
    fs_chdir(&sh->fs, home, identity, ring);

    return MILTUX_OK;
}

void shell_destroy(shell_t *sh)
{
    if (!sh) return;
    fs_destroy(&sh->fs);
}

miltux_err_t shell_exec(shell_t *sh, const char *line)
{
    if (!sh || !line) return MILTUX_ERR_INVAL;

    /* Work on a mutable copy */
    char buf[LINE_MAX_LEN];
    strncpy(buf, line, LINE_MAX_LEN - 1);
    buf[LINE_MAX_LEN - 1] = '\0';

    /* Strip trailing newline */
    size_t n = strlen(buf);
    while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r')) buf[--n] = '\0';

    /* Skip empty lines and comments */
    char *p = buf;
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p == '\0' || *p == '#') return MILTUX_OK;

    char *argv[MAX_ARGS];
    int   argc = parse_args(p, argv, MAX_ARGS);
    if (argc == 0) return MILTUX_OK;

    const char *cmd = argv[0];

    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
        cmd_help();
    } else if (strcmp(cmd, "pwd") == 0) {
        cmd_pwd(sh);
    } else if (strcmp(cmd, "whoami") == 0) {
        cmd_whoami(sh);
    } else if (strcmp(cmd, "ring") == 0) {
        cmd_ring(sh);
    } else if (strcmp(cmd, "su") == 0) {
        cmd_su(sh, argc, argv);
    } else if (strcmp(cmd, "ls") == 0) {
        cmd_ls(sh, argc, argv);
    } else if (strcmp(cmd, "cd") == 0) {
        cmd_cd(sh, argc, argv);
    } else if (strcmp(cmd, "mkdir") == 0) {
        cmd_mkdir(sh, argc, argv);
    } else if (strcmp(cmd, "rm") == 0) {
        cmd_rm(sh, argc, argv);
    } else if (strcmp(cmd, "cat") == 0) {
        cmd_cat(sh, argc, argv);
    } else if (strcmp(cmd, "write") == 0) {
        cmd_write(sh, argc, argv);
    } else if (strcmp(cmd, "acl") == 0) {
        cmd_acl(sh, argc, argv);
    } else if (strcmp(cmd, "exit") == 0 || strcmp(cmd, "quit") == 0) {
        return MILTUX_ERR_RANGE; /* sentinel: caller checks for exit */
    } else {
        fprintf(stderr, "miltux: unknown command '%s' (try 'help')\n", cmd);
    }

    return MILTUX_OK;
}

void shell_run(shell_t *sh)
{
    char line[LINE_MAX_LEN];

    printf("MiLTuX — A Multics-inspired system\n");
    printf("Type 'help' for a list of commands.\n\n");

    while (1) {
        printf("miltux(%s:%d)%s> ",
               sh->identity,
               ring_current(&sh->ring_ctx),
               sh->fs.cwd_path[0] ? sh->fs.cwd_path : ">");
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) {
            printf("\n");
            break;
        }

        miltux_err_t err = shell_exec(sh, line);
        if (err == MILTUX_ERR_RANGE) /* exit sentinel */
            break;
    }

    printf("Farewell from MiLTuX.\n");
}
