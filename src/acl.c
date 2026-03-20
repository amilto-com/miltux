/*
 * MiLTuX - Access Control List implementation
 */

#include "acl.h"
#include <stdio.h>
#include <string.h>

void acl_init(acl_t *acl)
{
    if (!acl) return;
    memset(acl, 0, sizeof(*acl));
    acl->count = 0;
}

miltux_err_t acl_set(acl_t *acl, const char *identity, int perms, int ring_limit)
{
    int i;

    if (!acl || !identity)
        return MILTUX_ERR_INVAL;
    if (ring_limit < MILTUX_RING_MIN || ring_limit > MILTUX_RING_MAX)
        return MILTUX_ERR_RANGE;

    /* Update existing entry if identity already present */
    for (i = 0; i < acl->count; i++) {
        if (strcmp(acl->entries[i].identity, identity) == 0) {
            acl->entries[i].perms      = perms;
            acl->entries[i].ring_limit = ring_limit;
            return MILTUX_OK;
        }
    }

    /* Add new entry */
    if (acl->count >= MILTUX_ACL_MAX)
        return MILTUX_ERR_NOMEM;

    strncpy(acl->entries[acl->count].identity, identity, MILTUX_NAME_MAX);
    acl->entries[acl->count].identity[MILTUX_NAME_MAX] = '\0';
    acl->entries[acl->count].perms      = perms;
    acl->entries[acl->count].ring_limit = ring_limit;
    acl->count++;
    return MILTUX_OK;
}

miltux_err_t acl_remove(acl_t *acl, const char *identity)
{
    int i;

    if (!acl || !identity)
        return MILTUX_ERR_INVAL;

    for (i = 0; i < acl->count; i++) {
        if (strcmp(acl->entries[i].identity, identity) == 0) {
            /* Shift remaining entries down */
            int remaining = acl->count - i - 1;
            if (remaining > 0)
                memmove(&acl->entries[i], &acl->entries[i + 1],
                        (size_t)remaining * sizeof(acl_entry_t));
            acl->count--;
            return MILTUX_OK;
        }
    }
    return MILTUX_ERR_NOENT;
}

miltux_err_t acl_check(const acl_t *acl, const char *identity, int ring,
                        int required_perms)
{
    int i;
    int wildcard_perms      = ACL_PERM_NONE;
    int wildcard_ring_limit = MILTUX_RING_MAX;

    if (!acl || !identity)
        return MILTUX_ERR_INVAL;

    /* First pass: exact identity match */
    for (i = 0; i < acl->count; i++) {
        if (strcmp(acl->entries[i].identity, identity) == 0) {
            if (ring > acl->entries[i].ring_limit)
                return MILTUX_ERR_PERM;  /* insufficient privilege */
            if ((acl->entries[i].perms & required_perms) == required_perms)
                return MILTUX_OK;
            return MILTUX_ERR_PERM;
        }
        if (strcmp(acl->entries[i].identity, "*") == 0) {
            wildcard_perms      = acl->entries[i].perms;
            wildcard_ring_limit = acl->entries[i].ring_limit;
        }
    }

    /* Fall back to wildcard */
    if (wildcard_perms != ACL_PERM_NONE || acl->count == 0) {
        if (ring > wildcard_ring_limit)
            return MILTUX_ERR_PERM;
        if ((wildcard_perms & required_perms) == required_perms)
            return MILTUX_OK;
    }

    return MILTUX_ERR_PERM;
}

void acl_perm_str(int perms, char out[5])
{
    out[0] = (perms & ACL_PERM_READ)   ? 'r' : '-';
    out[1] = (perms & ACL_PERM_WRITE)  ? 'w' : '-';
    out[2] = (perms & ACL_PERM_EXEC)   ? 'e' : '-';
    out[3] = (perms & ACL_PERM_APPEND) ? 'a' : '-';
    out[4] = '\0';
}

void acl_print(const acl_t *acl)
{
    char pstr[5];
    int  i;

    if (!acl || acl->count == 0) {
        printf("  (empty ACL)\n");
        return;
    }
    printf("  %-20s  %-6s  %s\n", "Identity", "Perms", "Ring limit");
    printf("  %-20s  %-6s  %s\n", "--------", "-----", "----------");
    for (i = 0; i < acl->count; i++) {
        acl_perm_str(acl->entries[i].perms, pstr);
        printf("  %-20s  %-6s  %d\n",
               acl->entries[i].identity,
               pstr,
               acl->entries[i].ring_limit);
    }
}
