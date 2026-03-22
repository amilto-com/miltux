/*
 * MiLTuX - Virtual hierarchical file system
 *
 * Multics invented the hierarchical file system.  MiLTuX implements an
 * in-memory version that persists for the lifetime of the process.
 *
 * Each node in the tree is a "segment" in Multics terminology.  Directories
 * are segments that contain other segments.  Every segment has its own ACL.
 *
 * Path separator is ">", faithful to the Multics convention (e.g.
 * >user_dir_dir>bob>mail instead of /home/bob/mail).
 */

#ifndef FS_H
#define FS_H

#include "miltux.h"
#include "acl.h"

/* -----------------------------------------------------------------------
 * Node types
 * ----------------------------------------------------------------------- */
typedef enum {
    FS_NODE_FILE = 0,
    FS_NODE_DIR  = 1,
} fs_node_type_t;

/* -----------------------------------------------------------------------
 * A file-system node (segment)
 * ----------------------------------------------------------------------- */
typedef struct fs_node {
    char            name[MILTUX_NAME_MAX + 1];
    fs_node_type_t  type;
    acl_t           acl;

    /* File data */
    char           *data;
    size_t          data_len;

    /* Directory children */
    struct fs_node *children[MILTUX_CHILDREN_MAX];
    int             child_count;

    /* Parent pointer (NULL for root) */
    struct fs_node *parent;

    /* Bind point (Plan 9 "bind"): if non-NULL, any path resolution that
     * reaches this node is transparently redirected to bound_to.
     * This gives each session its own private namespace view without
     * affecting the global tree.  ls shows bound nodes with a "@" suffix. */
    struct fs_node *bound_to;
} fs_node_t;

/* -----------------------------------------------------------------------
 * File-system handle (one per MiLTuX session)
 * ----------------------------------------------------------------------- */
typedef struct {
    fs_node_t  *root;
    fs_node_t  *cwd;        /* current working directory */
    char        cwd_path[MILTUX_PATH_MAX];
} fs_t;

/*
 * Initialise the file system.  Creates the root directory ">".
 * The root ACL grants full access to everyone (wildcard "*").
 */
miltux_err_t fs_init(fs_t *fs);

/* Free all resources held by the file system */
void fs_destroy(fs_t *fs);

/*
 * Resolve a path (absolute or relative) to a node.
 * Absolute paths begin with ">".  Relative paths are resolved from cwd.
 * Returns NULL and sets *err if resolution fails.
 */
fs_node_t *fs_resolve(fs_t *fs, const char *path, miltux_err_t *err);

/* Return the full path of a node (written into buf, max len bytes) */
miltux_err_t fs_node_path(const fs_node_t *node, char *buf, size_t len);

/* Create a directory.  Parent must exist and be a directory. */
miltux_err_t fs_mkdir(fs_t *fs, const char *path,
                       const char *owner, int ring);

/* Create or truncate a file and write data to it. */
miltux_err_t fs_write(fs_t *fs, const char *path, const char *data,
                       size_t data_len, const char *owner, int ring);

/* Read file contents (returns pointer into node's data; do not free). */
miltux_err_t fs_read(fs_t *fs, const char *path,
                      const char **data_out, size_t *len_out,
                      const char *accessor, int ring);

/* Remove a file or empty directory */
miltux_err_t fs_remove(fs_t *fs, const char *path,
                        const char *accessor, int ring);

/* Change the current working directory */
miltux_err_t fs_chdir(fs_t *fs, const char *path,
                       const char *accessor, int ring);

/* List directory contents to stdout */
miltux_err_t fs_list(fs_t *fs, const char *path,
                      const char *accessor, int ring);

/*
 * Serialize directory contents into buf (at most bufsz bytes).
 * Returns the number of bytes written (>= 0) on success,
 * or -1 on error (permission denied, not a directory, etc.).
 * Used by the network layer to send ls output over a socket.
 */
int fs_list_buf(fs_t *fs, const char *path,
                const char *accessor, int ring,
                char *buf, size_t bufsz);

/* -----------------------------------------------------------------------
 * Bind (Plan 9-style per-session namespace)
 *
 * bind source target — transparently redirect "target" to "source".
 *   Both paths must exist and have the same type (dir/file).
 *   Accessing target subsequently resolves to source's subtree.
 *   ls shows bound nodes with a "@" suffix (like Unix symlinks).
 *
 * unbind target      — remove the binding from target.
 *
 * fs_list_binds      — print all active binds in this session's namespace.
 * ----------------------------------------------------------------------- */
miltux_err_t fs_bind(fs_t *fs, const char *source, const char *target,
                     const char *accessor, int ring);

miltux_err_t fs_unbind(fs_t *fs, const char *target,
                        const char *accessor, int ring);

void fs_list_binds(fs_t *fs);

#endif /* FS_H */
