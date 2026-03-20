/*
 * MiLTuX - Access Control Lists
 *
 * Multics introduced ACLs to computing.  Every segment (file, directory,
 * or other object) carries an ACL that lists which users / groups may
 * access it and in which mode.
 *
 * MiLTuX follows the Multics convention:
 *   r  - read permission
 *   w  - write permission
 *   e  - execute (for directories: search / traverse) permission
 *   a  - append permission
 *
 * The special identity "*" matches any user ("*.*" in full Multics notation).
 */

#ifndef ACL_H
#define ACL_H

#include "miltux.h"

/* -----------------------------------------------------------------------
 * Permission bits (can be OR-ed together)
 * ----------------------------------------------------------------------- */
#define ACL_PERM_READ    (1 << 0)   /* r */
#define ACL_PERM_WRITE   (1 << 1)   /* w */
#define ACL_PERM_EXEC    (1 << 2)   /* e (execute / search) */
#define ACL_PERM_APPEND  (1 << 3)   /* a */

#define ACL_PERM_NONE    0
#define ACL_PERM_ALL     (ACL_PERM_READ | ACL_PERM_WRITE | ACL_PERM_EXEC | ACL_PERM_APPEND)

/* -----------------------------------------------------------------------
 * A single ACL entry
 * ----------------------------------------------------------------------- */
typedef struct {
    char identity[MILTUX_NAME_MAX + 1]; /* user name, group, or "*" */
    int  perms;                          /* OR of ACL_PERM_* bits    */
    int  ring_limit;                     /* min ring required         */
} acl_entry_t;

/* -----------------------------------------------------------------------
 * An ACL: a list of entries attached to an object
 * ----------------------------------------------------------------------- */
typedef struct {
    acl_entry_t entries[MILTUX_ACL_MAX];
    int         count;
} acl_t;

/* Initialise an empty ACL */
void acl_init(acl_t *acl);

/* Add or update an entry.  Returns MILTUX_ERR_NOMEM if the ACL is full. */
miltux_err_t acl_set(acl_t *acl, const char *identity, int perms, int ring_limit);

/* Remove an entry by identity.  Returns MILTUX_ERR_NOENT if not found. */
miltux_err_t acl_remove(acl_t *acl, const char *identity);

/*
 * Check whether `identity` running at `ring` has at least `required_perms`.
 * Returns MILTUX_OK if access is granted, MILTUX_ERR_PERM otherwise.
 * Matching order: exact identity first, then wildcard "*".
 */
miltux_err_t acl_check(const acl_t *acl, const char *identity, int ring,
                        int required_perms);

/* Print ACL contents to stdout (for the `acl` shell command) */
void acl_print(const acl_t *acl);

/* Return a short permission string like "rwe-" */
void acl_perm_str(int perms, char out[5]);

#endif /* ACL_H */
