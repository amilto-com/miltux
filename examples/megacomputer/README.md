# MiLTuX Mega-Computer — 7-node Docker cluster

This directory contains a complete example of seven MiLTuX nodes running on
Alpine Linux containers, automatically forming a self-organising distributed
system — the "mega-computer".

```
  oracle ──── sentinel ──── arbiter
     │  ╲         │         ╱  │
     │   ╲      herald    ╱    │
     │    ╲       │      ╱     │
  zenith   ╲    nexus  ╱    apex
     └────────────────────────┘
        (full peer mesh via gossip)
```

---

## Quick Start

```sh
# 1. Build and start all 7 nodes
cd examples/megacomputer
docker compose up -d

# 2. Watch the mesh form (nodes auto-connect to each other)
docker compose logs -f

# 3. Run the cross-node demonstration
sleep 5 && ./demo.sh

# 4. Get an interactive shell on any node
docker exec -it oracle sh
miltux -u alice -l0   # new session, auto-connects to the whole mesh
```

---

## Architecture

### Nodes

| Node      | Identity | Port |
|-----------|----------|------|
| `oracle`  | oracle   | 7070 |
| `sentinel`| sentinel | 7070 |
| `arbiter` | arbiter  | 7070 |
| `herald`  | herald   | 7070 |
| `nexus`   | nexus    | 7070 |
| `apex`    | apex     | 7070 |
| `zenith`  | zenith   | 7070 |

Each node:
1. Runs `miltux -u <identity> -l 7070 -d` (daemon mode, no REPL)
2. Reads `MILTUX_PEERS` and auto-connects to the six sibling nodes
3. Gossips its peer list so late-joiners discover the full mesh
4. Retries failed connections automatically (peers that aren't ready yet)

### Distributed namespace

Every node maintains its own **in-memory FS namespace**.  Nodes expose their
namespace to the mesh via the MiLTuX peer protocol (TCP, port 7070).

Cross-node access uses the `r*` commands from any interactive session:

```
rls   <node#> <path>           — list a remote directory
rcat  <node#> <path>           — read a remote file
rwrite <node#> <path> <text>   — write a remote file
rmkdir <node#> <path>          — create a remote directory
```

---

## Auto-Discovery (Gossip Protocol)

The self-organising mesh works through three complementary mechanisms:

### 1. Seed peers (`MILTUX_PEERS`)

Each node starts with a hard-coded list of sibling hostnames (set by
Docker Compose via the `MILTUX_PEERS` environment variable).  On startup,
the node attempts to connect to every seed.

### 2. Pending-peer retry queue

If a seed peer is not ready yet (e.g., still starting), the failed connection
is added to a **pending-retry queue**.  Every ~3 seconds, the node retries all
pending connections.  This makes the mesh eventually consistent regardless of
startup order.

### 3. Gossip (`PEERS` protocol message)

After connecting, every node periodically asks each peer for its peer list
(the `PEERS` request in the MiLTuX wire protocol).  Any unknown node in the
response is immediately connected to.  This propagates knowledge of the full
mesh even when nodes start with incomplete seed lists.

Together these three mechanisms guarantee that all nodes eventually connect
to all other nodes with **zero central coordination**.

---

## Ultramodern Concepts Implemented

| Concept | Implementation |
|---|---|
| **Zero-config self-organisation** | Gossip + retry forms the full mesh automatically |
| **Intent-based peer identity** | Each node declares its identity in the TCP handshake; no IP addresses in user-facing APIs |
| **Capability-preserving security** | Ring-level and ACL checks apply to *remote* FS operations — your identity and ring travel with your requests across the mesh |
| **Eventual consistency** | Failed connections are retried; peer lists propagate transitively so all nodes converge to a full mesh |
| **Uniform namespace interface** | The same FS primitives (read, write, mkdir, list) work identically on local and remote namespaces |
| **Minimal-surface protocol** | Line-based ASCII protocol — inspectable with `nc`, implementable in any language |
| **Disposable nodes** | Docker `restart: unless-stopped` — nodes can crash and rejoin without breaking the mesh |
| **Identity-as-hostname** | Docker service name = MiLTuX identity = DNS hostname; no separate config needed |

---

## Interactive Demo

```sh
# Open a shell on oracle and start an interactive session
docker exec -it oracle sh

# Inside oracle — start a session that auto-connects to ALL 7 daemon nodes
miltux -u alice -l0
```

```
MiLTuX — A Multics-inspired distributed system
Type 'help' for a list of commands.

miltux(alice:4)>> nodes
  #    Identity              Host                  Port
  -    --------              ----                  ----
  0    sentinel              sentinel              7070
  1    arbiter               arbiter               7070
  2    herald                herald                7070
  3    nexus                 nexus                 7070
  4    apex                  apex                  7070
  5    zenith                zenith                7070

miltux(alice:4)>> rwrite 0 >shared.txt Hello from alice on the mega-computer
miltux(alice:4)>> rcat 0 >shared.txt
Hello from alice on the mega-computer
miltux(alice:4)>> rls 0 >
  r---  shared.txt
  r-e-  system>
  r-e-  user_dir_dir>
```

---

## Environment Variables

| Variable | Default | Description |
|---|---|---|
| `MILTUX_IDENTITY` | `$(hostname)` | Node identity (must match Docker service name for DNS) |
| `MILTUX_PORT` | `7070` | TCP listen port |
| `MILTUX_PEERS` | — | Comma-separated `host[:port]` seed peers |
| `MILTUX_RING` | `4` | Initial ring level (0 = kernel, 7 = least privileged) |

---

## Adding More Nodes

To add an 8th node:

1. Add a service to `docker-compose.yml`:
   ```yaml
   newnode:
     <<: *node-defaults
     hostname: newnode
     environment:
       MILTUX_IDENTITY: newnode
       MILTUX_PORT: "7070"
       MILTUX_PEERS: "oracle:7070"   # just one seed is enough
   ```
2. `docker compose up -d newnode`

The new node connects to `oracle`, gossips peer lists, and within seconds is
connected to all other nodes — with no changes to the existing 7.

---

## Stopping

```sh
docker compose down          # stop and remove containers
docker compose down -v       # also remove volumes (none used here)
```
