/*
 * MiLTuX - Virtual hierarchical file system implementation
 */

#include "fs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -----------------------------------------------------------------------
 * Internal helpers
 * ----------------------------------------------------------------------- */

static fs_node_t *node_new(const char *name, fs_node_type_t type,
                            fs_node_t *parent)
{
    fs_node_t *n = calloc(1, sizeof(fs_node_t));
    if (!n) return NULL;

    strncpy(n->name, name, MILTUX_NAME_MAX);
    n->name[MILTUX_NAME_MAX] = '\0';
    n->type        = type;
    n->parent      = parent;
    n->child_count = 0;
    n->data        = NULL;
    n->data_len    = 0;
    acl_init(&n->acl);
    return n;
}

static void node_free(fs_node_t *n)
{
    int i;
    if (!n) return;
    for (i = 0; i < n->child_count; i++)
        node_free(n->children[i]);
    free(n->data);
    free(n);
}

static fs_node_t *dir_find_child(const fs_node_t *dir, const char *name)
{
    int i;
    for (i = 0; i < dir->child_count; i++)
        if (strcmp(dir->children[i]->name, name) == 0)
            return dir->children[i];
    return NULL;
}

static miltux_err_t dir_add_child(fs_node_t *dir, fs_node_t *child)
{
    if (dir->child_count >= MILTUX_CHILDREN_MAX)
        return MILTUX_ERR_NOMEM;
    dir->children[dir->child_count++] = child;
    return MILTUX_OK;
}

static miltux_err_t dir_remove_child(fs_node_t *dir, fs_node_t *child)
{
    int i;
    for (i = 0; i < dir->child_count; i++) {
        if (dir->children[i] == child) {
            int remaining = dir->child_count - i - 1;
            if (remaining > 0)
                memmove(&dir->children[i], &dir->children[i + 1],
                        (size_t)remaining * sizeof(fs_node_t *));
            dir->child_count--;
            return MILTUX_OK;
        }
    }
    return MILTUX_ERR_NOENT;
}

/* -----------------------------------------------------------------------
 * Path resolution
 *
 * Multics uses ">" as the path separator.  An absolute path starts with
 * ">".  We support both ">" and "/" for convenience.
 * ----------------------------------------------------------------------- */

/* Follow a bind chain with cycle protection.
 * Returns the ultimate target node (may be the same node if unbound). */
static fs_node_t *follow_bind(fs_node_t *n)
{
    int depth = 0;
    while (n && n->bound_to && depth++ < MILTUX_BIND_DEPTH_MAX)
        n = n->bound_to;
    return n;
}

/* Split path into the first component and the rest.
 * e.g. "foo>bar>baz" -> component="foo", rest="bar>baz"
 *      ">foo>bar"    -> component="" (root), rest="foo>bar"
 */
static void path_split(const char *path, char *component, size_t comp_size,
                        const char **rest)
{
    const char *sep = strchr(path, '>');
    if (!sep) sep = strchr(path, '/');

    if (!sep) {
        /* Last component */
        strncpy(component, path, comp_size - 1);
        component[comp_size - 1] = '\0';
        *rest = NULL;
    } else {
        size_t len = (size_t)(sep - path);
        if (len >= comp_size) len = comp_size - 1;
        strncpy(component, path, len);
        component[len] = '\0';
        *rest = sep + 1;
    }
}

fs_node_t *fs_resolve(fs_t *fs, const char *path, miltux_err_t *err)
{
    fs_node_t  *cur;
    const char *p;
    char        comp[MILTUX_NAME_MAX + 1];
    const char *rest;

    if (!fs || !path) {
        if (err) *err = MILTUX_ERR_INVAL;
        return NULL;
    }

    /* Absolute path: starts with ">" or "/" */
    if (path[0] == '>' || path[0] == '/') {
        cur = fs->root;
        p   = path + 1;
        if (*p == '\0') return cur; /* root itself */
    } else {
        cur = fs->cwd;
        p   = path;
    }

    while (p && *p != '\0') {
        path_split(p, comp, sizeof(comp), &rest);

        if (comp[0] == '\0') {
            /* double separator, skip */
            p = rest;
            continue;
        }
        if (strcmp(comp, ".") == 0) {
            p = rest;
            continue;
        }
        if (strcmp(comp, "..") == 0) {
            if (cur->parent)
                cur = cur->parent;
            p = rest;
            continue;
        }

        if (cur->type != FS_NODE_DIR) {
            if (err) *err = MILTUX_ERR_NOTDIR;
            return NULL;
        }

        fs_node_t *child = dir_find_child(cur, comp);
        if (!child) {
            if (err) *err = MILTUX_ERR_NOENT;
            return NULL;
        }
        /* Transparently follow any bind mounted on this node */
        cur = follow_bind(child);
        p   = rest;
    }

    if (err) *err = MILTUX_OK;
    return cur;
}

miltux_err_t fs_node_path(const fs_node_t *node, char *buf, size_t len)
{
    /* Walk up to root collecting names, then reverse */
    const fs_node_t *stack[256];
    int              depth = 0;
    const fs_node_t *n     = node;

    if (!node || !buf || len == 0)
        return MILTUX_ERR_INVAL;

    while (n && n->parent) {
        if (depth >= 256) return MILTUX_ERR_RANGE;
        stack[depth++] = n;
        n = n->parent;
    }

    /* Root */
    buf[0] = '>';
    buf[1] = '\0';
    size_t pos = 1;

    for (int i = depth - 1; i >= 0; i--) {
        size_t nlen = strlen(stack[i]->name);
        if (pos + nlen + 2 > len) return MILTUX_ERR_RANGE;
        if (pos > 1) { buf[pos++] = '>'; }
        memcpy(buf + pos, stack[i]->name, nlen);
        pos += nlen;
        buf[pos] = '\0';
    }

    return MILTUX_OK;
}

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */

miltux_err_t fs_init(fs_t *fs)
{
    if (!fs) return MILTUX_ERR_INVAL;

    fs->root = node_new(">", FS_NODE_DIR, NULL);
    if (!fs->root) return MILTUX_ERR_NOMEM;

    /* Root ACL: everyone has full access */
    acl_set(&fs->root->acl, "*", ACL_PERM_ALL, MILTUX_RING_MAX);

    fs->cwd = fs->root;
    fs->cwd_path[0] = '>';
    fs->cwd_path[1] = '\0';

    /* Create some initial directories like Multics had */
    fs_mkdir(fs, ">system", "system", MILTUX_RING_SYSTEM);
    fs_mkdir(fs, ">user_dir_dir", "system", MILTUX_RING_SYSTEM);

    /* >system>proc — Plan 9-style session introspection directory.
     * Each active session registers a subdirectory here containing
     * "status" and "peers" segments, readable by all users.
     * Remote peers can inspect them via rls/rcat. */
    fs_mkdir(fs, ">system>proc", "system", MILTUX_RING_SYSTEM);

    return MILTUX_OK;
}

void fs_destroy(fs_t *fs)
{
    if (!fs) return;
    node_free(fs->root);
    fs->root = NULL;
    fs->cwd  = NULL;
}

/* Resolve the parent directory and the last component of a path */
static fs_node_t *resolve_parent(fs_t *fs, const char *path,
                                  char *basename, size_t base_size,
                                  miltux_err_t *err)
{
    /* Find the last ">" or "/" separator */
    const char *last_sep = strrchr(path, '>');
    const char *last_sl  = strrchr(path, '/');
    const char *sep;

    if (!last_sep && !last_sl) {
        /* No separator: parent is cwd */
        strncpy(basename, path, base_size - 1);
        basename[base_size - 1] = '\0';
        if (err) *err = MILTUX_OK;
        return fs->cwd;
    }

    sep = (last_sep && (!last_sl || last_sep > last_sl)) ? last_sep : last_sl;

    strncpy(basename, sep + 1, base_size - 1);
    basename[base_size - 1] = '\0';

    if (sep == path || (sep == path + 1 && (path[0] == '>' || path[0] == '/'))) {
        /* Parent is root */
        if (err) *err = MILTUX_OK;
        return fs->root;
    }

    /* Resolve the parent portion */
    char parent_path[MILTUX_PATH_MAX];
    size_t plen = (size_t)(sep - path);
    if (plen >= MILTUX_PATH_MAX) plen = MILTUX_PATH_MAX - 1;
    strncpy(parent_path, path, plen);
    parent_path[plen] = '\0';

    return fs_resolve(fs, parent_path, err);
}

miltux_err_t fs_mkdir(fs_t *fs, const char *path,
                       const char *owner, int ring)
{
    char         basename[MILTUX_NAME_MAX + 1];
    miltux_err_t err;
    fs_node_t   *parent;
    fs_node_t   *newdir;

    if (!fs || !path || !owner) return MILTUX_ERR_INVAL;

    parent = resolve_parent(fs, path, basename, sizeof(basename), &err);
    if (!parent) return err;
    if (parent->type != FS_NODE_DIR) return MILTUX_ERR_NOTDIR;
    if (basename[0] == '\0') return MILTUX_ERR_INVAL;

    /* ACL check on parent: need write+exec */
    err = acl_check(&parent->acl, owner, ring,
                    ACL_PERM_WRITE | ACL_PERM_EXEC);
    if (err != MILTUX_OK) return err;

    if (dir_find_child(parent, basename)) return MILTUX_ERR_EXIST;

    newdir = node_new(basename, FS_NODE_DIR, parent);
    if (!newdir) return MILTUX_ERR_NOMEM;

    /* New directory: owner gets full access; others get read+exec */
    acl_set(&newdir->acl, owner, ACL_PERM_ALL, MILTUX_RING_MAX);
    acl_set(&newdir->acl, "*",   ACL_PERM_READ | ACL_PERM_EXEC,
            MILTUX_RING_MAX);

    err = dir_add_child(parent, newdir);
    if (err != MILTUX_OK) {
        node_free(newdir);
        return err;
    }
    return MILTUX_OK;
}

miltux_err_t fs_write(fs_t *fs, const char *path, const char *data,
                       size_t data_len, const char *owner, int ring)
{
    char         basename[MILTUX_NAME_MAX + 1];
    miltux_err_t err;
    fs_node_t   *parent;
    fs_node_t   *node;

    if (!fs || !path || !owner) return MILTUX_ERR_INVAL;
    if (data_len > MILTUX_FILE_MAX) return MILTUX_ERR_RANGE;

    parent = resolve_parent(fs, path, basename, sizeof(basename), &err);
    if (!parent) return err;
    if (parent->type != FS_NODE_DIR) return MILTUX_ERR_NOTDIR;
    if (basename[0] == '\0') return MILTUX_ERR_INVAL;

    node = dir_find_child(parent, basename);
    if (node) {
        /* File exists: check write permission */
        if (node->type == FS_NODE_DIR) return MILTUX_ERR_ISDIR;
        err = acl_check(&node->acl, owner, ring, ACL_PERM_WRITE);
        if (err != MILTUX_OK) return err;
    } else {
        /* Check parent write access */
        err = acl_check(&parent->acl, owner, ring,
                        ACL_PERM_WRITE | ACL_PERM_EXEC);
        if (err != MILTUX_OK) return err;

        node = node_new(basename, FS_NODE_FILE, parent);
        if (!node) return MILTUX_ERR_NOMEM;

        /* Owner gets full access; others get read only */
        acl_set(&node->acl, owner, ACL_PERM_ALL, MILTUX_RING_MAX);
        acl_set(&node->acl, "*",   ACL_PERM_READ, MILTUX_RING_MAX);

        err = dir_add_child(parent, node);
        if (err != MILTUX_OK) { node_free(node); return err; }
    }

    /* Write data */
    free(node->data);
    if (data_len > 0) {
        node->data = malloc(data_len + 1);
        if (!node->data) return MILTUX_ERR_NOMEM;
        memcpy(node->data, data, data_len);
        node->data[data_len] = '\0';
        node->data_len = data_len;
    } else {
        node->data     = NULL;
        node->data_len = 0;
    }

    return MILTUX_OK;
}

miltux_err_t fs_read(fs_t *fs, const char *path,
                      const char **data_out, size_t *len_out,
                      const char *accessor, int ring)
{
    miltux_err_t err;
    fs_node_t   *node;

    if (!fs || !path || !accessor || !data_out || !len_out)
        return MILTUX_ERR_INVAL;

    node = fs_resolve(fs, path, &err);
    if (!node) return err;
    if (node->type == FS_NODE_DIR) return MILTUX_ERR_ISDIR;

    err = acl_check(&node->acl, accessor, ring, ACL_PERM_READ);
    if (err != MILTUX_OK) return err;

    *data_out = node->data ? node->data : "";
    *len_out  = node->data_len;
    return MILTUX_OK;
}

miltux_err_t fs_remove(fs_t *fs, const char *path,
                        const char *accessor, int ring)
{
    miltux_err_t err;
    fs_node_t   *node;

    if (!fs || !path || !accessor) return MILTUX_ERR_INVAL;

    node = fs_resolve(fs, path, &err);
    if (!node) return err;
    if (!node->parent) return MILTUX_ERR_PERM; /* cannot remove root */

    if (node->type == FS_NODE_DIR && node->child_count > 0)
        return MILTUX_ERR_NOTEMPTY;

    /* Need write on parent */
    err = acl_check(&node->parent->acl, accessor, ring, ACL_PERM_WRITE);
    if (err != MILTUX_OK) return err;

    dir_remove_child(node->parent, node);
    /* Update cwd if we removed it or an ancestor */
    if (fs->cwd == node || fs->cwd_path[0] == '\0')
        fs->cwd = fs->root;
    node_free(node);
    return MILTUX_OK;
}

miltux_err_t fs_chdir(fs_t *fs, const char *path,
                       const char *accessor, int ring)
{
    miltux_err_t err;
    fs_node_t   *node;

    if (!fs || !path || !accessor) return MILTUX_ERR_INVAL;

    node = fs_resolve(fs, path, &err);
    if (!node) return err;
    if (node->type != FS_NODE_DIR) return MILTUX_ERR_NOTDIR;

    err = acl_check(&node->acl, accessor, ring, ACL_PERM_EXEC);
    if (err != MILTUX_OK) return err;

    fs->cwd = node;
    fs_node_path(node, fs->cwd_path, MILTUX_PATH_MAX);
    return MILTUX_OK;
}

miltux_err_t fs_list(fs_t *fs, const char *path,
                      const char *accessor, int ring)
{
    miltux_err_t err;
    fs_node_t   *node;
    char         pstr[5];
    int          i;

    if (!fs || !path || !accessor) return MILTUX_ERR_INVAL;

    node = fs_resolve(fs, path, &err);
    if (!node) return err;
    if (node->type != FS_NODE_DIR) return MILTUX_ERR_NOTDIR;

    err = acl_check(&node->acl, accessor, ring,
                    ACL_PERM_READ | ACL_PERM_EXEC);
    if (err != MILTUX_OK) return err;

    for (i = 0; i < node->child_count; i++) {
        fs_node_t  *child = node->children[i];
        const char *suffix;
        /* Show "@" for bound nodes (like Unix shows "l" for symlinks) */
        if (child->bound_to)
            suffix = (child->type == FS_NODE_DIR) ? ">@" : "@";
        else
            suffix = (child->type == FS_NODE_DIR) ? ">" : "";
        /* Show effective permissions for this accessor */
        int eff = ACL_PERM_NONE;
        if (acl_check(&child->acl, accessor, ring, ACL_PERM_READ)  == MILTUX_OK)
            eff |= ACL_PERM_READ;
        if (acl_check(&child->acl, accessor, ring, ACL_PERM_WRITE) == MILTUX_OK)
            eff |= ACL_PERM_WRITE;
        if (acl_check(&child->acl, accessor, ring, ACL_PERM_EXEC)  == MILTUX_OK)
            eff |= ACL_PERM_EXEC;
        if (acl_check(&child->acl, accessor, ring, ACL_PERM_APPEND)== MILTUX_OK)
            eff |= ACL_PERM_APPEND;

        acl_perm_str(eff, pstr);
        printf("  %s  %s%s\n", pstr, child->name, suffix);
    }
    if (node->child_count == 0)
        printf("  (empty)\n");

    return MILTUX_OK;
}

int fs_list_buf(fs_t *fs, const char *path,
                const char *accessor, int ring,
                char *buf, size_t bufsz)
{
    miltux_err_t err;
    fs_node_t   *node;
    char         pstr[5];
    int          i;
    size_t       pos = 0;

    if (!fs || !path || !accessor || !buf || bufsz == 0) return -1;

    node = fs_resolve(fs, path, &err);
    if (!node) return -1;
    if (node->type != FS_NODE_DIR) return -1;

    err = acl_check(&node->acl, accessor, ring,
                    ACL_PERM_READ | ACL_PERM_EXEC);
    if (err != MILTUX_OK) return -1;

    for (i = 0; i < node->child_count; i++) {
        fs_node_t  *child = node->children[i];
        const char *suffix;
        if (child->bound_to)
            suffix = (child->type == FS_NODE_DIR) ? ">@" : "@";
        else
            suffix = (child->type == FS_NODE_DIR) ? ">" : "";
        int eff = ACL_PERM_NONE;
        if (acl_check(&child->acl, accessor, ring, ACL_PERM_READ)  == MILTUX_OK)
            eff |= ACL_PERM_READ;
        if (acl_check(&child->acl, accessor, ring, ACL_PERM_WRITE) == MILTUX_OK)
            eff |= ACL_PERM_WRITE;
        if (acl_check(&child->acl, accessor, ring, ACL_PERM_EXEC)  == MILTUX_OK)
            eff |= ACL_PERM_EXEC;
        if (acl_check(&child->acl, accessor, ring, ACL_PERM_APPEND)== MILTUX_OK)
            eff |= ACL_PERM_APPEND;

        acl_perm_str(eff, pstr);
        int written = snprintf(buf + pos, bufsz - pos, "  %s  %s%s\n",
                               pstr, child->name, suffix);
        if (written < 0 || (size_t)written >= bufsz - pos) break;
        pos += (size_t)written;
    }

    if (node->child_count == 0) {
        const char *empty = "  (empty)\n";
        size_t      elen  = strlen(empty);
        if (pos + elen < bufsz) {
            memcpy(buf + pos, empty, elen);
            pos += elen;
        }
    }

    buf[pos] = '\0';
    return (int)pos;
}

/* -----------------------------------------------------------------------
 * Bind — Plan 9-style per-session namespace
 * ----------------------------------------------------------------------- */

/*
 * fs_bind(source, target): transparently redirect target → source.
 *
 * After binding, any path resolution that reaches target will silently
 * continue from source instead.  Both nodes must be of the same type
 * (both dirs or both files).  The caller needs exec on source and write
 * on target.  Chains of up to MILTUX_BIND_DEPTH_MAX are followed;
 * deeper chains are silently truncated (cycle protection).
 */
miltux_err_t fs_bind(fs_t *fs, const char *source, const char *target,
                     const char *accessor, int ring)
{
    miltux_err_t  err;
    fs_node_t    *src_node;
    fs_node_t    *tgt_parent;
    fs_node_t    *tgt_node;
    char          tgt_base[MILTUX_NAME_MAX + 1];

    if (!fs || !source || !target || !accessor) return MILTUX_ERR_INVAL;
    if (strcmp(source, target) == 0)            return MILTUX_ERR_INVAL;

    src_node = fs_resolve(fs, source, &err);
    if (!src_node) return err;

    /* Resolve the target without following its own bind (so we can set it).
     * We navigate to the parent and find the child directly. */
    tgt_parent = resolve_parent(fs, target, tgt_base, sizeof(tgt_base), &err);
    if (!tgt_parent) return err;
    tgt_node = dir_find_child(tgt_parent, tgt_base);
    if (!tgt_node) return MILTUX_ERR_NOENT;

    /* Types must match */
    if (src_node->type != tgt_node->type) return MILTUX_ERR_INVAL;

    /* Permissions: exec on source (to enter/read), write on target (to rebind) */
    err = acl_check(&src_node->acl, accessor, ring, ACL_PERM_EXEC);
    if (err != MILTUX_OK) return err;
    err = acl_check(&tgt_node->acl, accessor, ring, ACL_PERM_WRITE);
    if (err != MILTUX_OK) return err;

    tgt_node->bound_to = src_node;
    return MILTUX_OK;
}

/*
 * fs_unbind(target): remove the bind from target.
 *
 * Resolves target WITHOUT following its bind (so you always unbind the
 * node you name, not the node it points to).
 */
miltux_err_t fs_unbind(fs_t *fs, const char *target,
                        const char *accessor, int ring)
{
    miltux_err_t  err;
    fs_node_t    *tgt_parent;
    fs_node_t    *tgt_node;
    char          tgt_base[MILTUX_NAME_MAX + 1];

    if (!fs || !target || !accessor) return MILTUX_ERR_INVAL;

    tgt_parent = resolve_parent(fs, target, tgt_base, sizeof(tgt_base), &err);
    if (!tgt_parent) return err;
    tgt_node = dir_find_child(tgt_parent, tgt_base);
    if (!tgt_node)        return MILTUX_ERR_NOENT;
    if (!tgt_node->bound_to) return MILTUX_ERR_INVAL; /* not bound */

    err = acl_check(&tgt_node->acl, accessor, ring, ACL_PERM_WRITE);
    if (err != MILTUX_OK) return err;

    tgt_node->bound_to = NULL;
    return MILTUX_OK;
}

/* Recursive helper for fs_list_binds */
static void list_binds_rec(const fs_node_t *node, int depth)
{
    char src[MILTUX_PATH_MAX];
    char tgt[MILTUX_PATH_MAX];
    int  i;

    if (!node || depth > 32) return;
    if (node->bound_to) {
        fs_node_path(node,           tgt, sizeof(tgt));
        fs_node_path(node->bound_to, src, sizeof(src));
        printf("  %s  →  %s\n", tgt, src);
    }
    if (node->type == FS_NODE_DIR) {
        for (i = 0; i < node->child_count; i++)
            list_binds_rec(node->children[i], depth + 1);
    }
}

/* Print all active binds in this session's namespace. */
void fs_list_binds(fs_t *fs)
{
    if (!fs || !fs->root) return;
    list_binds_rec(fs->root, 0);
}
