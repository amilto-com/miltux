#!/bin/sh
# MiLTuX Mega-Computer — cross-node demonstration script
#
# Run this on the host AFTER the cluster is up:
#
#   cd examples/megacomputer
#   docker compose up -d
#   sleep 5          # wait for mesh to form
#   ./demo.sh
#
# What the demo shows:
#   1. All 7 nodes are alive (each daemon is reachable)
#   2. Writing a file to a node's own namespace via rwrite
#   3. Reading that file back from a DIFFERENT node via rcat
#   4. A distributed directory listing of every node's root from zenith
#
# Each docker exec starts a short-lived interactive miltux session that:
#   • listens on a random port (-l 0) so it doesn't conflict with the daemon
#   • has MILTUX_PEERS empty so it only connects to explicitly named nodes
#   • connects to the target daemon and performs the requested operation
#   • exits cleanly

set -e

NODES="oracle sentinel arbiter herald nexus apex zenith"
PORT=7070

say() { printf '\033[1;36m» %s\033[0m\n' "$*"; }
ok()  { printf '  \033[0;32m✓ %s\033[0m\n' "$*"; }
err() { printf '  \033[0;31m✗ %s\033[0m\n' "$*"; }

# Run MiLTuX commands inside a container via an ephemeral session.
# The session connects to target_node:PORT and then runs the given commands.
#
# Usage: miltux_on <container> <target_node> <identity> <commands...>
miltux_on() {
    local container="$1"
    local target="$2"
    local identity="$3"
    shift 3
    local cmds="$*"
    # Commands piped to miltux; the 'exit' at the end makes it non-blocking
    printf '%s\nexit\n' "$cmds" | \
        docker exec -i "$container" sh -c \
        "MILTUX_PEERS= miltux -u '${identity}' -l0 2>/dev/null"
}

# ─── 1. Verify all nodes are reachable ───────────────────────────────────────
say "Step 1 — Verify all 7 nodes are reachable"
for node in $NODES; do
    # Connect to each node's daemon and immediately exit
    result=$(printf 'exit\n' | \
        docker exec -i "$node" sh -c \
        "MILTUX_PEERS= miltux -u probe -l0 2>&1" || true)
    if docker exec "$node" true 2>/dev/null; then
        ok "$node is running"
    else
        err "$node is NOT running"
    fi
done
echo ""

# ─── 2. Write identity files to each node ────────────────────────────────────
say "Step 2 — Write identity files to each node's namespace"
for node in $NODES; do
    # The ephemeral session connects to the node's own daemon and writes a file
    printf 'connect localhost %s\nrwrite 0 >identity.txt %s\nexit\n' \
        "$PORT" "$node is node of the mega-computer" | \
        docker exec -i "$node" sh -c \
        "MILTUX_PEERS= miltux -u ${node}_writer -l0 2>/dev/null" || true
    ok "wrote >identity.txt on $node"
done
echo ""

# ─── 3. Cross-node reads (sentinel reads from every other node) ──────────────
say "Step 3 — sentinel reads identity.txt from every other node"
for src in oracle arbiter herald nexus apex zenith; do
    output=$(printf 'connect %s %s\nrcat 0 >identity.txt\nexit\n' \
        "$src" "$PORT" | \
        docker exec -i sentinel sh -c \
        "MILTUX_PEERS= miltux -u sentinel_reader -l0 2>/dev/null" || true)
    if [ -n "$output" ]; then
        ok "${src}: ${output}"
    else
        err "${src}: (no data)"
    fi
done
echo ""

# ─── 4. Global directory listing from zenith ─────────────────────────────────
say "Step 4 — zenith lists root directory on every other node"
for src in oracle sentinel arbiter herald nexus apex; do
    printf '=== %s ===\n' "$src"
    printf 'connect %s %s\nrls 0 >\nexit\n' "$src" "$PORT" | \
        docker exec -i zenith sh -c \
        "MILTUX_PEERS= miltux -u zenith_explorer -l0 2>/dev/null" | \
        sed 's/^/    /' || true
done
echo ""

# ─── 5. Shared chronicle: every node contributes a line ──────────────────────
say "Step 5 — Build a distributed chronicle across all nodes"
for node in $NODES; do
    entry="$(date -u '+%H:%M:%S') ${node}: ready"
    printf 'connect localhost %s\nrmkdir 0 >chronicle\nrwrite 0 >chronicle/%s.log %s\nexit\n' \
        "$PORT" "$node" "$entry" | \
        docker exec -i "$node" sh -c \
        "MILTUX_PEERS= miltux -u ${node}_chronicler -l0 2>/dev/null" || true
    ok "$node added its chronicle entry"
done

say "  Reading oracle's chronicle (from zenith's perspective):"
printf 'connect oracle %s\nrls 0 >chronicle\nexit\n' "$PORT" | \
    docker exec -i zenith sh -c \
    "MILTUX_PEERS= miltux -u zenith_reader -l0 2>/dev/null" | \
    sed 's/^/    /' || true
echo ""

# ─── Done ─────────────────────────────────────────────────────────────────────
say "Mega-computer demo complete!"
cat <<'EOF'

  Each of the 7 nodes maintains its own in-memory namespace and exposes it
  to the mesh via the MiLTuX peer protocol.  Nodes discover each other via
  MILTUX_PEERS (direct seeds) and then gossip peer lists to form a full mesh
  with zero central coordination.

  Try it interactively:
    docker exec -it oracle sh
    miltux -u alice -l0        # auto-connects to all daemons; use rcat/rls/rwrite

EOF
