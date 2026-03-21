# MiLTuX — Beginner's Guide

> *MiLTuX is to Multics what Linux is to Unix.*

This guide explains every concept you need to understand and use MiLTuX, from
scratch. No prior knowledge of Multics, operating systems theory, or networking
is assumed.

---

## Table of Contents

1. [Why MiLTuX?](#1-why-miltux)
2. [A very short history of Multics](#2-a-very-short-history-of-multics)
3. [The ring protection model](#3-the-ring-protection-model)
4. [Access Control Lists (ACLs)](#4-access-control-lists-acls)
5. [The hierarchical file system](#5-the-hierarchical-file-system)
6. [Segments — Multics' name for "everything"](#6-segments--multics-name-for-everything)
7. [The interactive shell (REPL)](#7-the-interactive-shell-repl)
8. [The distributed peer mesh](#8-the-distributed-peer-mesh)
9. [Remote file operations](#9-remote-file-operations)
10. [The mega-computer: a 7-node cluster](#10-the-mega-computer-a-7-node-cluster)
11. [How the pieces fit together](#11-how-the-pieces-fit-together)
12. [Quick-start cheatsheet](#12-quick-start-cheatsheet)

---

## 1. Why MiLTuX?

When Multics was designed at MIT in the 1960s, its engineers invented several
ideas that we still use every day — including the hierarchical file system, Access
Control Lists, and ring-based protection. Unix, then Linux, borrowed the
hierarchical file system but discarded most of the rest in the name of
simplicity.

MiLTuX is a personal experiment: what if you took the *good* parts of Multics
and ran them on any modern POSIX system, in userspace, with no special
privileges? The result is a small program written in portable C99 (no external
libraries) that lets you explore these ideas hands-on.

---

## 2. A very short history of Multics

| Year | Event |
|------|-------|
| 1964 | Multics design begins at MIT (Project MAC) |
| 1969 | Ken Thompson strips Multics down to "Unix" — clean, small, no ACLs, no rings |
| 1991 | Linus Torvalds creates Linux — a free Unix clone |
| 2025 | MiLTuX revives the Multics ideas Linux dropped |

The key inventions Multics gave to computing:

- **The hierarchical file system** (directories inside directories — Unix copied this)
- **Ring-based protection** (privilege levels — Unix replaced this with uid=0)
- **Access Control Lists** (per-object permission lists — Unix only added basic rwx for owner/group/other)
- **Segments** (the uniform name for any stored object)

MiLTuX implements all four.

---

## 3. The ring protection model

### What are rings?

Think of the system as a set of concentric circles — like a target at a
shooting range. The centre is ring **0** (the most powerful, the kernel). The
outer rings are numbered 1, 2, 3… up to **7** (the least privileged, the
ordinary user).

```text
         outermost = least privileged
  ┌──────────────────────────────────┐
  │  ring 7  (normal users)          │
  │  ┌────────────────────────────┐  │
  │  │  ring 4  (default user)    │  │
  │  │  ┌──────────────────────┐  │  │
  │  │  │  ring 1  (system)    │  │  │
  │  │  │  ┌────────────────┐  │  │  │
  │  │  │  │  ring 0        │  │  │  │
  │  │  │  │  (kernel)      │  │  │  │
  │  │  │  └────────────────┘  │  │  │
  │  │  └──────────────────────┘  │  │
  │  └────────────────────────────┘  │
  └──────────────────────────────────┘
         innermost = most privileged
```

The rule is: **a lower ring number means more privilege**. A process in ring 2
can do things that a process in ring 6 cannot.

### MiLTuX ring constants

| Constant | Value | Meaning |
|----------|-------|---------|
| `MILTUX_RING_KERNEL` | 0 | Kernel — full control |
| `MILTUX_RING_SYSTEM` | 1 | System services |
| `MILTUX_RING_USER`   | 4 | Default user ring |
| `MILTUX_RING_MAX`    | 7 | Least privileged |

When you start MiLTuX without specifying a ring, you run at ring 4.

### Ring transitions

You can always move **outward** (to a higher ring number) — giving up privilege
is free. Moving **inward** (gaining privilege) requires a *ring bracket* that
explicitly allows it. By default, your bracket is set to your starting ring, so
`su 0` is denied unless the system has granted you a call gate into ring 0.

```text
miltux(alice:4)>> su 6   ← always allowed (going outward)
miltux(alice:6)>> su 4   ← allowed (back to bracket boundary)
miltux(alice:4)>> su 0   ← denied (ring 0 is inside your bracket)
su: permission denied
```

### Why this is better than Unix uid=0

In Unix, the superuser (`root`, uid=0) has *absolute* power. A single bug in
any privileged process can compromise the whole machine. In Multics, every ring
transition is explicit and auditable — you can't accidentally "fall into" a
higher privilege. MiLTuX models this same idea.

---

## 4. Access Control Lists (ACLs)

### What are ACLs?

Every object in MiLTuX (every file, every directory) has an **ACL** — a list of
rules that decides who can do what with that object. Unix only has three
permission sets (owner / group / other). Multics, and MiLTuX, have one entry
*per identity*, plus a wildcard.

### Permission bits

Each ACL entry grants zero or more of four permissions:

| Symbol | Name    | Meaning |
|--------|---------|---------|
| `r`    | read    | Read the contents of the file |
| `w`    | write   | Overwrite the file |
| `e`    | execute | For directories: traverse (enter) it; for files: run it |
| `a`    | append  | Add data to the end of a file |

### ACL entries

Each entry has three fields:

```text
Identity    Perms   Ring limit
--------    -----   ----------
alice       rwea    7           ← alice gets everything, from any ring
*           r---    7           ← everyone else: read only, from any ring
```

The **ring limit** is the *least* privileged ring that may use this entry. An
entry with ring limit 7 is available to everyone. An entry with ring limit 2
requires the caller to be in ring 2 or lower (more privileged).

### How access is checked

When you try to read a file, MiLTuX does this:

1. Look for an entry matching your identity exactly.
2. If none found, look for the wildcard entry (`*`).
3. Check that your current ring ≤ the entry's ring limit.
4. Check that the entry grants the required permission.

If all checks pass: access granted. Otherwise: `permission denied`.

### Seeing an ACL in action

```text
miltux(alice:4)>> write secret.txt top secret data
miltux(alice:4)>> acl secret.txt
ACL for secret.txt:
  Identity   Perms   Ring limit
  --------   -----   ----------
  alice      rwea    7
  *          r---    7

miltux(bob:4)>> cat alice>secret.txt   ← bob can read (wildcard r)
top secret data

miltux(bob:4)>> write alice>secret.txt bad   ← bob cannot write
write: permission denied
```

---

## 5. The hierarchical file system

### Multics invented this

The hierarchical file system — directories that contain files and other
directories, all the way down — was invented for Multics. Unix adopted it.
Windows adopted it. MiLTuX stays faithful to the Multics original, including
the original **path separator: `>`** (greater-than) instead of Unix's `/`.

### The path separator `>`

| MiLTuX path | Unix equivalent | Meaning |
|-------------|-----------------|---------|
| `>`         | `/`             | Root of the file system |
| `>user_dir_dir>alice` | `/home/alice` | Alice's home directory |
| `>user_dir_dir>alice>mail` | `/home/alice/mail` | Alice's mail folder |
| `>system`   | `/usr` or `/lib`| System files |

Both absolute paths (starting with `>`) and relative paths (from the current
directory) work. `.` means current directory and `..` means parent.

### The root structure

When MiLTuX starts, it creates two top-level directories automatically:

```text
>
├── system>          ← system files (like /usr)
└── user_dir_dir>    ← all users' home directories (like /home)
    └── alice>       ← created when alice logs in
```

The root directory's ACL allows **everyone** to read and traverse it
(`* r-e- ring_limit=7`). Individual user directories restrict access to their
owner.

### Working directory

When you start a session as `alice`, your working directory is automatically
set to `>user_dir_dir>alice`. The prompt shows your current location:

```text
miltux(alice:4)>>           ← you are at >user_dir_dir>alice
miltux(alice:4)>docs>       ← you have cd'd into the docs subdirectory
```

---

## 6. Segments — Multics' name for "everything"

In Multics, **everything** is a segment: files, directories, programs, devices.
There is no distinction between "file" and "directory" at the naming level — a
directory is just a segment that happens to contain other segments.

MiLTuX reflects this: each node in the file system tree is called a *segment*
in the source code (`fs_node_t`). Each segment has:

- A **name**
- A **type** (file or directory)
- An **ACL** (its own, independent access-control list)
- **Data** (for files: the content; for directories: the list of children)

This means you can set a different ACL on *every individual file*, not just on
directories. In Unix you can only control access at the file level with coarse
owner/group/other bits; in MiLTuX you list exactly which identities get which
permissions on each object.

---

## 7. The interactive shell (REPL)

### Starting MiLTuX

```sh
build/miltux -u alice -r 4 -l 7070
```

| Flag | Meaning |
|------|---------|
| `-u alice` | Run as the identity `alice` |
| `-r 4` | Start at ring 4 (the default) |
| `-l 7070` | Listen for incoming peer connections on port 7070 |
| `-d` | Daemon mode (no interactive prompt — for use in scripts/containers) |

The prompt tells you your identity and ring:

```text
miltux(alice:4)>>
```

### Local file system commands

| Command | Example | Description |
|---------|---------|-------------|
| `ls [path]` | `ls` or `ls >system` | List directory contents |
| `cd <path>` | `cd docs` or `cd >user_dir_dir>bob` | Change directory |
| `mkdir <path>` | `mkdir reports` | Create a new directory |
| `rm <path>` | `rm old.txt` | Remove a file or empty directory |
| `cat <path>` | `cat readme.txt` | Print file contents |
| `write <path> <text>` | `write hello.txt Hi there` | Write (or overwrite) a file |
| `acl <path>` | `acl hello.txt` | Show the ACL of a file or directory |
| `pwd` | `pwd` | Print working directory |

### Identity and ring commands

| Command | Description |
|---------|-------------|
| `whoami` | Show your current identity and ring |
| `ring` | Show your current ring number |
| `su <ring>` | Attempt to transition to a different ring |

### Networking commands

| Command | Description |
|---------|-------------|
| `listen [port]` | Start accepting incoming peer connections |
| `connect <host> [port]` | Connect to a remote MiLTuX node |
| `nodes` | List all currently connected peers (with their index number) |

### Leaving

```text
miltux(alice:4)>> exit
Farewell from MiLTuX.
```

---

## 8. The distributed peer mesh

One of MiLTuX's extensions beyond the original Multics is *distribution*: many
MiLTuX nodes running on different machines (or containers) can form a single
**mega-computer**, sharing a common namespace of identities and ring levels.

### How nodes find each other

There are two ways.

#### Option 1 — Manual connection

```sh
# On machine A — already running
build/miltux -u alice -l 7070

# On machine B
build/miltux -u bob
miltux(bob:4)>> connect machineA.example.com 7070
[net] connected to alice@machineA:7070
```

#### Option 2 — Automatic via `MILTUX_PEERS`

Set the environment variable before starting MiLTuX:

```sh
MILTUX_PEERS=node1:7070,node2:7070,node3:7070 \
  build/miltux -u alice -l 7070
```

MiLTuX connects to every listed host at startup. Failed connections are
retried automatically.

### Gossip — the self-organising mesh

Once two nodes are connected, they periodically exchange their peer lists
via a **gossip** protocol. Each node sends `PEERS` to its neighbours and
receives a list of *their* neighbours in return. If a node appears in the list
that you are not yet connected to, MiLTuX connects to it automatically.

The result is a **self-organising full mesh**: connect any two nodes and,
within a few seconds, all nodes in the cluster know about each other — without
any central coordinator.

```text
You connect A → B.
B tells A about C and D.
A connects to C and D.
C tells A about E.
A connects to E.
→ Full mesh: A–B–C–D–E all connected.
```

### The handshake

When two nodes connect, they exchange identities and listen ports:

```text
Server → Client:   MILTUX/1 oracle 7070
Client → Server:   HELLO alice 35555
Server → Client:   OK
```

After the handshake, the connection is live and can carry both gossip messages
and remote file-system operations.

### Peer index numbers

The `nodes` command lists your connected peers with an index:

```text
miltux(alice:4)>> nodes
  #    Identity   Host       Port
  0    nexus      nexus      7070
  1    apex       apex       7070
  2    oracle     oracle     7070
  3    sentinel   sentinel   7070
  4    arbiter    arbiter    7070
  5    herald     herald     7070
  6    zenith     zenith     7070
```

You use these index numbers (the `#` column) to address remote operations:
`rls 2 >` means "list the root on oracle" (peer #2 in the table above).

---

## 9. Remote file operations

Every local file-system command has a remote counterpart, prefixed with `r`.
Your identity and ring travel with the request; the remote node's ACLs are
enforced against your identity.

| Command | Example | Description |
|---------|---------|-------------|
| `rls <node#> <path>` | `rls 2 >` | List a remote directory |
| `rcat <node#> <path>` | `rcat 2 >system>log.txt` | Read a remote file |
| `rmkdir <node#> <path>` | `rmkdir 2 >shared` | Create a remote directory |
| `rwrite <node#> <path> <text>` | `rwrite 2 >note.txt Hello` | Write a remote file |
| `rremove <node#> <path>` | `rremove 2 >old.txt` | Delete a remote file |

### How remote ACL enforcement works

Suppose alice (ring 4) does `rwrite 2 >shared>report.txt data`:

1. Alice's session sends `WRITE 4 alice >shared>report.txt\n<data>` to oracle (node #2).
2. Oracle's daemon receives the request.
3. Oracle checks the ACL of `>shared>report.txt` against identity `alice` at ring 4.
4. If `alice` (or `*`) has `w` permission and ring limit ≥ 4: write succeeds.
5. Oracle replies `OK` and alice's shell prints nothing (success) or prints `rwrite: permission denied`.

The remote node does the full ACL check — alice cannot bypass oracle's security
by being local to another machine.

### A complete cross-node example

```text
# On machine A (oracle), bob has written a public report:
miltux(bob:4)>> write public_report.txt Quarterly results: all good.

# On machine B (alice's session, connected to oracle as node #0):
miltux(alice:4)>> rls 0 >user_dir_dir>bob
  rwea  public_report.txt

miltux(alice:4)>> rcat 0 >user_dir_dir>bob>public_report.txt
Quarterly results: all good.

miltux(alice:4)>> rwrite 0 >user_dir_dir>bob>public_report.txt Tampering
rwrite: permission denied   ← oracle's ACL denied alice write access to bob's file
```

---

## 10. The mega-computer: a 7-node cluster

The `examples/megacomputer/` directory contains a ready-to-run Docker Compose
cluster of 7 MiLTuX nodes that automatically form a full mesh.

### Starting the cluster

```sh
cd examples/megacomputer
docker compose up -d       # start all 7 containers
sleep 5                    # wait for the mesh to form
./demo.sh                  # run the cross-node demonstration
```

### The 7 nodes

| Container | Identity | Port |
|-----------|----------|------|
| `oracle`  | oracle   | 7070 |
| `sentinel`| sentinel | 7070 |
| `arbiter` | arbiter  | 7070 |
| `herald`  | herald   | 7070 |
| `nexus`   | nexus    | 7070 |
| `apex`    | apex     | 7070 |
| `zenith`  | zenith   | 7070 |

Each container runs `miltux -d` (daemon mode) and auto-connects to the others
via `MILTUX_PEERS`.

### Getting an interactive session

You can join the cluster as yourself from inside any container:

```sh
docker exec -it oracle sh

# Inside the container — connect to all 7 daemons as "alice" at ring 4:
MILTUX_PEERS=sentinel:7070,arbiter:7070,herald:7070,nexus:7070,apex:7070,zenith:7070,oracle:7070 \
  miltux -u alice -l0
```

(`-l0` means "listen on any free port", useful for ephemeral sessions.)

Once connected you can use `rls`, `rcat`, `rwrite` on all 7 nodes from a
single prompt:

```text
miltux(alice:4)>> nodes
  #    Identity   Host       Port
  0    nexus      nexus      7070
  ...
  6    zenith     zenith     7070

miltux(alice:4)>> rls 6 >
  r-e-  system>
  r-e-  user_dir_dir>

miltux(alice:4)>> rwrite 3 >user_dir_dir>herald>note.txt Hello from alice
miltux(alice:4)>> rcat 3 >user_dir_dir>herald>note.txt
Hello from alice
```

---

## 11. How the pieces fit together

Here is how the six source modules relate to each other and to the concepts
described above:

```text
┌─────────────────────────────────────────────────────────────┐
│                        shell.c                               │
│   Interactive REPL — reads commands, calls modules below     │
└────────────┬──────────────────────────┬─────────────────────┘
             │                          │
    ┌────────▼────────┐      ┌──────────▼──────────┐
    │     fs.c        │      │       net.c          │
    │  Hierarchical   │      │  TCP peer mesh       │
    │  file system    │      │  Gossip / discovery  │
    │  (segments)     │      │  Remote FS ops       │
    └────────┬────────┘      └──────────────────────┘
             │
    ┌────────▼────────┐
    │     acl.c       │
    │  Access Control │
    │  Lists          │
    │  r / w / e / a  │
    └────────┬────────┘
             │
    ┌────────▼────────┐
    │     ring.c      │
    │  Ring-based     │
    │  protection     │
    │  rings 0–7      │
    └─────────────────┘
```

Every FS operation goes through `fs.c`, which calls `acl.c` (can this identity
access this segment?), which calls `ring.c` (is the caller's ring privileged
enough for this ACL entry?).

Remote operations go through `net.c`, which serialises the identity and ring
and sends them to the remote node, where the same `fs.c` → `acl.c` → `ring.c`
chain runs.

---

## 12. Quick-start cheatsheet

```sh
# Build
make

# Run locally as alice
build/miltux -u alice -l 7070

# --- Inside the shell ---

# File system
ls                              # list current directory
ls >user_dir_dir                # list the home-directories root
mkdir docs                      # create a directory
cd docs                         # enter it
write hello.txt Hi there        # create a file
cat hello.txt                   # read it
acl hello.txt                   # inspect its ACL
rm hello.txt                    # delete it
cd ..                           # go up one level
pwd                             # where am I?

# Identity & rings
whoami                          # alice, ring 4
ring                            # 4
su 6                            # move to ring 6 (less privileged)
su 2                            # move to ring 2 — will fail unless bracketted

# Networking
connect other-machine.local 7070   # connect to a remote node
nodes                              # list connected nodes (#, identity, host, port)
rls 0 >                            # list root of node #0
rcat 0 >user_dir_dir>bob>readme    # read a remote file
rwrite 0 >shared.txt Hello         # write a remote file
rmkdir 0 >shared                   # create a remote directory

# Docker mega-computer
cd examples/megacomputer
docker compose up -d
sleep 5 && ./demo.sh               # automated cross-node demo
docker exec -it oracle sh          # shell in oracle container
  # then: MILTUX_PEERS=sentinel:7070,arbiter:7070,herald:7070,nexus:7070,apex:7070,zenith:7070 miltux -u alice -l0
```

---

## Glossary

| Term | Definition |
|------|------------|
| **Ring** | A privilege level from 0 (kernel, most powerful) to 7 (user, least powerful) |
| **Ring bracket** | The range of rings in which a segment may be called, controlling inward transitions |
| **ACL** | Access Control List — a list of `(identity, permissions, ring_limit)` entries on a segment |
| **Segment** | Multics' universal name for any stored object (file, directory, device…) |
| **Identity** | A name (like `alice`) used to identify a user or process |
| **Gossip** | The peer-discovery protocol: nodes exchange peer lists to build a full mesh |
| **Daemon mode** | Running MiLTuX without an interactive prompt, serving remote peers only (`-d`) |
| **`>`** | MiLTuX/Multics path separator, equivalent to Unix `/` |
| **`>user_dir_dir`** | The system directory holding all users' home directories (like `/home`) |
| **`>system`** | The system directory for OS-level files (like `/usr`) |
| **REPL** | Read-Eval-Print Loop — the interactive shell |
| **Peer** | Another MiLTuX instance connected over TCP |
| **Node index** | The number (0, 1, 2…) assigned to a connected peer, used in `rls`, `rcat`, etc. |
| **`MILTUX_PEERS`** | Environment variable listing peers to connect to automatically on startup |
