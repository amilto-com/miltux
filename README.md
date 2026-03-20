# MiLTuX

Remember Multics?  
Multics came in 1964. It was too big, with bells and whistles.  
Then Unix came in 1969 as a KISS, amputated version of Multics.  
Then Linux in 1991, which was just another version of Unix, but copyleft.  
Now you get it, huh?  
**MiLTuX is to Multics what Linux is to Unix.**

A free, open-source reimplementation of the core ideas that made Multics great —
rings of protection, ACLs on every object, and the hierarchical file system —
running on any POSIX operating system, written in portable C99.

---

## Does it work?

Yes — at least the fundamentals do.  
See [Building](#building) and try it yourself.

## Do you have time to work on it?

Not really. See [TODO](TODO.md).

## Do you think this is a good idea?

No. A good idea is to go play outside, not to spend days and nights
vibe-coding something that should have been built in 1972.

## Who are you, you who think you can do better than millions of coders?

I am William Gacquer, the Messiah, but you are not obliged to believe (it).
Sometimes I joke.

## Another word?

Yes. No warmonger, no militaro-industrial-complex disciple, no one holding a
gun is allowed to use MiLTuX.

---

## What MiLTuX implements

MiLTuX brings three of Multics' most important contributions to life in
userspace:

| Multics concept | MiLTuX module | Description |
|---|---|---|
| **Ring-based protection** | `src/ring.{h,c}` | Rings 0–7; 0 = kernel (most privileged), 7 = unprivileged user. Ring brackets control privilege transitions. |
| **Access Control Lists** | `src/acl.{h,c}` | Per-object ACLs with `r` (read), `w` (write), `e` (execute/search), `a` (append) permissions and per-entry ring limits. |
| **Hierarchical file system** | `src/fs.{h,c}` | In-memory segment tree using the Multics path separator `>` (e.g. `>user_dir_dir>alice>mail`). |
| **Command shell** | `src/shell.{h,c}` | Interactive REPL inspired by the Multics command language. |

---

## Building

MiLTuX requires only a C99-capable compiler and POSIX `make`.
No third-party libraries, no platform-specific flags.

```sh
make          # builds  build/miltux
make test     # runs all unit + integration tests
make clean    # removes the build/ directory
make install  # installs to /usr/local/bin/miltux  (override with PREFIX=...)
```

Tested with `gcc` and `clang` on Linux and macOS.
The `CC` and `PREFIX` variables are honoured from the environment:

```sh
CC=clang PREFIX=$HOME/.local make install
```

---

## Running

```
build/miltux [-u <identity>] [-r <ring>] [-l [port]]

  -u <identity>   run as the given identity  (default: your login name)
  -r <ring>       start at ring 0–7          (default: 4 = user ring)
  -l [port]       start listening for peers  (default port: 7070)
  -h              show this help
```

Example — start as user `alice` and immediately listen for peers:

```sh
build/miltux -u alice -l
```

Or on a custom port:

```sh
build/miltux -u alice -l 9090
```

---

## Shell commands

Once inside MiLTuX you are at an interactive prompt:

```
miltux(alice:4)>>
```

The prompt shows your identity and current ring.

| Command | Description |
|---|---|
| `ls [path]` | List the contents of a directory |
| `cd <path>` | Change the current directory |
| `mkdir <path>` | Create a new directory |
| `rm <path>` | Remove a file or empty directory |
| `cat <path>` | Display the contents of a file |
| `write <path> <text…>` | Write (or overwrite) a file with the given text |
| `acl <path>` | Show the ACL of a segment |
| `ring` | Show the current ring level |
| `su <ring>` | Attempt a ring transition (outward always allowed; inward requires bracket clearance) |
| `whoami` | Show your identity and ring |
| `pwd` | Print the working directory |
| `help` | Show command summary |
| `exit` / `quit` | Leave MiLTuX |

### Networking — forming the mega-computer

Every MiLTuX instance can connect to peers running on other POSIX machines.
Together they form a distributed system where your identity and ring level
travel with your operations.

| Command | Description |
|---|---|
| `listen [port]` | Start accepting incoming peer connections (default port 7070) |
| `connect <host> [port]` | Connect to a remote MiLTuX peer |
| `nodes` | List all connected peers |
| `rls <node#> <path>` | List a directory on a remote node |
| `rcat <node#> <path>` | Read a file from a remote node |
| `rmkdir <node#> <path>` | Create a directory on a remote node |
| `rwrite <node#> <path> <text…>` | Write a file on a remote node |

#### Connecting two nodes (quick start)

On machine A:
```
build/miltux -u alice -l 7070
```

On machine B:
```
build/miltux -u bob
miltux(bob:4)>> connect machineA.example.com 7070
Connected to alice@machineA:7070 (node #0).
miltux(bob:4)>> rls 0 >
  r-e-  system>
  r-e-  user_dir_dir>
miltux(bob:4)>> rwrite 0 >shared.txt Hello from machine B
miltux(bob:4)>> rcat 0 >shared.txt
Hello from machine B
```

The connection is authenticated by identity names (passed in the TCP
handshake); remote FS operations respect the ACLs on the target node,
checked against the caller's identity and ring level.

### Example session

```
miltux(alice:4)>> mkdir docs
miltux(alice:4)>> cd docs
miltux(alice:4)>docs> write readme.txt Hello from MiLTuX
miltux(alice:4)>docs> cat readme.txt
Hello from MiLTuX
miltux(alice:4)>docs> acl readme.txt
ACL for readme.txt:
  Identity              Perms   Ring limit
  --------              -----   ----------
  alice                 rwea    7
  *                     r---    7
miltux(alice:4)>docs> su 0
su: permission denied
miltux(alice:4)>docs> exit
Farewell from MiLTuX.
```

---

## Path conventions

MiLTuX uses `>` as the path separator, faithful to Multics:

| MiLTuX path | Unix equivalent |
|---|---|
| `>` | `/` |
| `>user_dir_dir>alice` | `/home/alice` |
| `>system` | `/usr` or `/lib` |

Both absolute (`>foo>bar`) and relative (`bar`) paths are accepted.
The special components `.` and `..` work as expected.

---

## Architecture

```
src/
  miltux.h / miltux.c   Core types, error codes, miltux_strerror()
  ring.h   / ring.c     Ring protection (ring_ctx_t, ring_bracket_t)
  acl.h    / acl.c      Access Control Lists (acl_t, acl_entry_t)
  fs.h     / fs.c       Hierarchical file system (fs_t, fs_node_t)
  net.h    / net.c      TCP peer mesh; simple line-based protocol (net_t, net_peer_t)
  shell.h  / shell.c    Interactive command shell (shell_t)
  main.c                Entry point, CLI argument parsing
tests/
  test_miltux.c         28 unit + integration tests (no external deps)
```

All modules are independent C compilation units with clear, minimal interfaces.
The test binary links everything except `main.c`.

---

## Contributing

Patches welcome.  Ground rules:

1. C99 only.  No compiler extensions, no platform-specific headers outside `main.c`.
2. Every new public function needs a declaration comment in its `.h` file.
3. New features must come with tests in `tests/test_miltux.c`.
4. `make test` must pass with zero warnings before opening a PR.
5. No warmongers. See above.