/*
 * MiLTuX - A Multics-inspired system for POSIX environments
 *
 * MiLTuX is to Multics what Linux is to Unix: a free, open-source
 * reimplementation of Multics concepts running on any POSIX OS.
 *
 * Key Multics innovations implemented here:
 *   - Ring-based protection (rings 0-7, 0 = most privileged)
 *   - Access Control Lists (ACLs) on every object
 *   - Hierarchical file system (invented by Multics)
 *   - Segment-based naming
 */

#ifndef MILTUX_H
#define MILTUX_H

#include <stddef.h>
#include <stdint.h>

/* -----------------------------------------------------------------------
 * Ring protection constants
 * Ring 0 = kernel (most privileged), Ring 7 = least privileged user
 * MiLTuX follows Multics convention: lower ring number = more privilege
 * ----------------------------------------------------------------------- */
#define MILTUX_RING_MIN     0
#define MILTUX_RING_MAX     7
#define MILTUX_RING_KERNEL  0
#define MILTUX_RING_SYSTEM  1
#define MILTUX_RING_USER    4

/* -----------------------------------------------------------------------
 * Limits
 * ----------------------------------------------------------------------- */
#define MILTUX_NAME_MAX     255
#define MILTUX_PATH_MAX     4096
#define MILTUX_ACL_MAX      32      /* max ACL entries per object   */
#define MILTUX_CHILDREN_MAX 256     /* max children in a directory  */
#define MILTUX_FILE_MAX     65536   /* max bytes per file           */
#define MILTUX_USERS_MAX    64      /* max users in the system      */

/* -----------------------------------------------------------------------
 * Error codes
 * ----------------------------------------------------------------------- */
typedef enum {
    MILTUX_OK        =  0,
    MILTUX_ERR_PERM  = -1,   /* permission denied (ring violation or ACL) */
    MILTUX_ERR_NOENT = -2,   /* no such file or directory                 */
    MILTUX_ERR_EXIST = -3,   /* file already exists                       */
    MILTUX_ERR_INVAL = -4,   /* invalid argument                          */
    MILTUX_ERR_NOMEM = -5,   /* out of memory                             */
    MILTUX_ERR_NOTDIR= -6,   /* not a directory                           */
    MILTUX_ERR_ISDIR = -7,   /* is a directory                            */
    MILTUX_ERR_NOTEMPTY = -8,/* directory not empty                       */
    MILTUX_ERR_RANGE = -9,   /* value out of range                        */
} miltux_err_t;

const char *miltux_strerror(miltux_err_t err);

#endif /* MILTUX_H */
