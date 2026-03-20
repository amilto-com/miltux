# MiLTuX — TODO

This is the honest list of things that need doing before MiLTuX can be called
anything more than a proof-of-concept.  Contributions welcome.

---

## Core / correctness

- [ ] **Persistent storage** — the file system currently lives only in memory.
      Implement a simple on-disk format (flat file or directory mirror) so
      that data survives between sessions.
- [ ] **Real segment model** — Multics segments have a size, a max size, and
      are memory-mapped.  The current `fs_node_t.data` is a plain heap
      allocation; replace it with a proper segment abstraction.
- [ ] **Append mode** — the `ACL_PERM_APPEND` bit is tracked but `fs_write`
      always truncates.  Implement true append-only writes.
- [ ] **Hard and soft links** — Multics called these "links" too; add
      `ln` shell command and `FS_NODE_LINK` type.
- [ ] **File metadata** — add creation time, last-modified time, and owner
      fields to `fs_node_t`.
- [ ] **Larger files** — increase or remove the `MILTUX_FILE_MAX` (64 KiB)
      cap and back files with a growable buffer.
- [ ] **Relative ACL inheritance** — when a segment is created, inherit a
      sensible default ACL from the parent directory rather than always
      granting `*` read access.

---

## Ring model

- [ ] **Ring bracket configuration** — allow the user/admin to set per-segment
      ring brackets (`[r1, r2, r3]`); currently only the default bracket is
      used.
- [ ] **Gate calls** — implement proper call-gate crossing so code in an outer
      ring can invoke a specific entry point in an inner ring.
- [ ] **Per-process ring state** — multi-process support requires each process
      to carry its own `ring_ctx_t`; currently there is one per shell session.

---

## Shell

- [ ] **`mv` / `cp`** — move and copy segments.
- [ ] **`chmod`-style `setacl`** — a dedicated command to add/remove/modify
      ACL entries on existing segments.
- [ ] **Command history** — readline-style up-arrow history would make the
      shell much more pleasant to use.
- [ ] **Tab completion** — complete segment names from the current directory.
- [ ] **Scripting** — accept a script file argument (`miltux -f script.mx`)
      and execute it non-interactively.
- [ ] **Pipelines** — simple `cmd1 | cmd2` syntax.
- [ ] **Environment variables** — a small key-value store visible to all
      commands in a session.

---

## Security

- [ ] **Privilege escalation path** — define an explicit mechanism (akin to
      `su` / `sudo`) that allows a trusted identity to cross ring brackets
      safely.
- [ ] **ACL audit log** — every denied access should optionally be logged to a
      dedicated audit segment.
- [ ] **Segment integrity** — add a checksum or hash field to `fs_node_t` so
      tampering with on-disk data can be detected on load.
- [ ] **Identity authentication** — currently any string is accepted as an
      identity; add a simple password or key-based authentication layer.

---

## Portability & build

- [ ] **`meson` or `cmake` build** — offer an alternative to `make` for
      developers who prefer a meta-build system.
- [ ] **CI pipeline** — add a GitHub Actions workflow that builds and runs
      `make test` on Linux (GCC + Clang) and macOS.
- [ ] **`valgrind` / sanitizer run** — add a `make check` target that runs the
      test binary under AddressSanitizer and LeakSanitizer.
- [ ] **Cross-compilation** — test and document cross-compilation to at least
      one embedded POSIX target (e.g. musl-based).
- [ ] **`pkg-config` file** — generate a `miltux.pc` on `make install` so that
      other projects can link against the MiLTuX library.

---

## Documentation

- [x] Complete `README.md`
- [x] Create this `TODO.md`
- [x] Document networking / distributed mode in README
- [ ] Man page — `man miltux(1)` covering CLI flags and the shell command set.
- [ ] Module documentation — expand the header-file comments into a full API
      reference (consider Doxygen or a plain Markdown `docs/` directory).
- [ ] Tutorial — a walk-through that demonstrates rings, ACLs, and the
      file system together in a narrative way.
- [ ] Historical notes — a short essay comparing MiLTuX concepts to their
      Multics originals for readers who are not familiar with Multics.

---

## Testing

- [ ] Property-based / fuzz testing of the ACL and FS modules.
- [ ] Edge-case tests: path with only `>`, deeply nested paths, names at
      `MILTUX_NAME_MAX`, ACL at maximum capacity, file at `MILTUX_FILE_MAX`.
- [ ] Stress test: create, write, and remove a large number of segments and
      verify the tree remains consistent.
- [ ] Test that `make install` / `make uninstall` work correctly.

---

## Networking (distributed mega-computer)

- [x] TCP peer-to-peer mesh — nodes connect to each other over POSIX sockets
- [x] Simple line-based protocol (handshake, LS / CAT / MKDIR / WRITE / REMOVE)
- [x] `listen [port]` / `connect <host> [port]` / `nodes` shell commands
- [x] Remote FS commands: `rls`, `rcat`, `rmkdir`, `rwrite`
- [x] `-l [port]` CLI flag to start listening at launch
- [ ] **Mutable namespace** — propagate mkdir/write/remove from one node to all peers automatically (gossip or broadcast)
- [ ] **Global namespace mount** — `mount <node#>:<remote-path> <local-path>` makes a remote subtree appear in the local FS tree transparently
- [ ] **Node discovery / directory** — a well-known "name server" segment that lists all active nodes; new nodes register on connect
- [ ] **Encrypted transport** — wrap the TCP connection in TLS (using OS-provided APIs where available)
- [ ] **Remote ring enforcement** — the target node should honour the caller's claimed ring only when the caller can prove their identity (needs authentication first)
- [ ] **IPv6 support** — `getaddrinfo` already handles it; just set `AF_UNSPEC` instead of `AF_INET`

- [ ] Multi-user daemon mode — a background process that multiple shells
      connect to over a Unix-domain socket.
- [ ] Virtual terminal support — a proper Multics-style console with
      multiple user typewriters (`ut`).
- [ ] Multics-style `PL/I`-inspired command language extensions.
- [ ] A retro ASCII banner at startup.  Priorities.
