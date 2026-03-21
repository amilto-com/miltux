#!/bin/sh
# MiLTuX Mega-Computer — node entrypoint
#
# Environment variables (set in docker-compose.yml):
#   MILTUX_IDENTITY  — node name used as identity AND as the DNS hostname
#                      that other nodes use to connect (required)
#   MILTUX_PORT      — TCP listen port (default: 7070)
#   MILTUX_PEERS     — comma-separated host[:port] of sibling nodes to join
#   MILTUX_RING      — initial ring level (default: 4 = user ring)
#
# The node:
#   1. Listens for peer connections on MILTUX_PORT
#   2. Auto-connects to all MILTUX_PEERS
#   3. Runs in daemon mode (no REPL), serving peer requests forever
#   4. Periodically gossips its peer list to help late-joiners discover nodes

set -e

IDENTITY="${MILTUX_IDENTITY:-$(hostname)}"
PORT="${MILTUX_PORT:-7070}"
RING="${MILTUX_RING:-4}"

echo "[miltux] node '${IDENTITY}' starting — port ${PORT}, ring ${RING}"
if [ -n "${MILTUX_PEERS}" ]; then
    echo "[miltux] seed peers: ${MILTUX_PEERS}"
fi

exec miltux \
    -u "${IDENTITY}" \
    -r "${RING}"     \
    -l"${PORT}"      \
    -d
