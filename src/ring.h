/*
 * MiLTuX - Ring-based protection model
 *
 * Multics introduced rings of protection: concentric privilege levels
 * numbered 0 (most privileged / kernel) through 7 (least privileged).
 * A process running in ring N can only call gates into rings <= N unless
 * explicitly granted a ring bracket that permits outward calls.
 *
 * This implementation provides ring tracking and enforcement for the
 * MiLTuX userspace runtime.
 */

#ifndef RING_H
#define RING_H

#include "miltux.h"

/* -----------------------------------------------------------------------
 * Ring bracket: defines the range of rings in which a segment may be
 * called.  Matches the Multics concept of a ring bracket [r1, r2, r3].
 *   r1 - minimum ring that can read/execute the segment
 *   r2 - minimum ring that can write the segment
 *   r3 - minimum ring for gate calls into this segment
 * ----------------------------------------------------------------------- */
typedef struct {
    int r1; /* read/execute gate */
    int r2; /* write gate        */
    int r3; /* call gate         */
} ring_bracket_t;

/* -----------------------------------------------------------------------
 * Ring context: current execution state for a process/session
 * ----------------------------------------------------------------------- */
typedef struct {
    int          current_ring;   /* ring the process is running in  */
    ring_bracket_t bracket;      /* bracket for the active segment  */
    const char  *segment_name;   /* name of active segment (for log)*/
} ring_ctx_t;

/* Initialise a ring context at the given ring level */
miltux_err_t ring_ctx_init(ring_ctx_t *ctx, int ring);

/* Return the current ring level */
int ring_current(const ring_ctx_t *ctx);

/*
 * Attempt to enter a new ring level (transition).
 * Outward transitions (higher ring number) are always allowed.
 * Inward transitions (lower ring number, more privilege) succeed only
 * if the target ring is within the bracket's call-gate range.
 */
miltux_err_t ring_transition(ring_ctx_t *ctx, int target_ring);

/*
 * Check whether the given ring context has at least the privilege
 * required to perform an operation that demands `required_ring`.
 * Returns MILTUX_OK if allowed, MILTUX_ERR_PERM if not.
 */
miltux_err_t ring_check(const ring_ctx_t *ctx, int required_ring);

/* Human-readable ring name */
const char *ring_name(int ring);

#endif /* RING_H */
