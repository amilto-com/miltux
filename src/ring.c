/*
 * MiLTuX - Ring-based protection model implementation
 */

#include "ring.h"
#include <stdio.h>
#include <string.h>

miltux_err_t ring_ctx_init(ring_ctx_t *ctx, int ring)
{
    if (!ctx)
        return MILTUX_ERR_INVAL;
    if (ring < MILTUX_RING_MIN || ring > MILTUX_RING_MAX)
        return MILTUX_ERR_RANGE;

    ctx->current_ring  = ring;
    ctx->segment_name  = "miltux";
    /* Default bracket: readable at the current ring, call gate at same ring */
    ctx->bracket.r1    = ring;
    ctx->bracket.r2    = ring;
    ctx->bracket.r3    = ring;
    return MILTUX_OK;
}

int ring_current(const ring_ctx_t *ctx)
{
    if (!ctx)
        return MILTUX_RING_MAX;
    return ctx->current_ring;
}

miltux_err_t ring_transition(ring_ctx_t *ctx, int target_ring)
{
    if (!ctx)
        return MILTUX_ERR_INVAL;
    if (target_ring < MILTUX_RING_MIN || target_ring > MILTUX_RING_MAX)
        return MILTUX_ERR_RANGE;

    /* Outward transition (less privilege) is always permitted */
    if (target_ring >= ctx->current_ring) {
        ctx->current_ring = target_ring;
        return MILTUX_OK;
    }

    /* Inward transition: only allowed if target_ring >= bracket.r3 */
    if (target_ring >= ctx->bracket.r3) {
        ctx->current_ring = target_ring;
        return MILTUX_OK;
    }

    return MILTUX_ERR_PERM;
}

miltux_err_t ring_check(const ring_ctx_t *ctx, int required_ring)
{
    if (!ctx)
        return MILTUX_ERR_INVAL;
    if (required_ring < MILTUX_RING_MIN || required_ring > MILTUX_RING_MAX)
        return MILTUX_ERR_RANGE;
    /*
     * A process in ring N can access objects that require ring R
     * if N <= R  (lower ring number = more privilege).
     */
    if (ctx->current_ring <= required_ring)
        return MILTUX_OK;
    return MILTUX_ERR_PERM;
}

const char *ring_name(int ring)
{
    switch (ring) {
    case 0: return "ring-0 (kernel)";
    case 1: return "ring-1 (supervisor)";
    case 2: return "ring-2 (extended-supervisor)";
    case 3: return "ring-3 (privileged-user)";
    case 4: return "ring-4 (user)";
    case 5: return "ring-5 (user-extended)";
    case 6: return "ring-6 (user-library)";
    case 7: return "ring-7 (user-application)";
    default: return "ring-unknown";
    }
}
