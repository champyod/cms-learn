# 0019 — The administrative socket acts as the system account

Status: accepted 2026-11-09 · decision by the operator

## 1. The connection is the identity

The administrative channel is a Unix domain socket (`api/src/admin_socket.cpp`) that only the process
owning it — root — can reach: mode `0600`, and `SO_PEERCRED` checked on every accept. Everything
arriving on it therefore already carries the strongest identity the system has.

Until this record, an action's acting administrator was taken from the **request body**: `granted_by`,
`approved_by`, `author_id`, `actor_id` (`api/src/admin_protocol.cpp:145-215`). Any process able to
connect could therefore assert any administrator identity, and the audit trail recorded a name no one
had proved.

**This record removes that.** An action arriving over the socket is performed by the **system account**,
which the socket supplies itself. No field of the request names an actor, and a request that tries to
name one is refused rather than ignored, so a client cannot believe it changed anything.

## 2. Why the command line is not asked to sign in

The command line is how the system is brought up. On a fresh installation there is no administrator to
sign in as, and requiring one would make the first account impossible to create. Its authority is the
file permission on the socket, which is a stronger gate than a password: the process must already be
root.

The cost is stated rather than hidden: **the audit trail names the system, not the person.** On a
single-operator server that is the truth; on a server with several administrators, the command line
cannot say which of them typed a command, and the web panel is the surface that can.

## 3. Both surfaces, one action layer

| Surface | Who the caller is | How it is decided |
|---|---|---|
| the socket (`cmsctl`) | the system account | by the socket, from the connection |
| the web panel | the signed-in administrator | by the session and its capabilities |

Both call the same actions. The action layer never reads an actor from a request; it receives the
acting account from the surface, and the surface cannot be bypassed because it is the only thing that
can produce one.

## 4. What this closes, and what it does not

Closed: a caller claiming to be a specific administrator (`admin_protocol.cpp:145,155,173,194,215`), and
with it the unverifiable audit rows that action produced.

Not closed by this record, and to be fixed in their own steps:

- An approved elevation is never consumed: `consumed_at` is read at `api/src/elevation.cpp:57,216` and
  written nowhere, so a temporary permission keeps working until it expires.
- `POST /settings` authorises `settings_write` but writes any catalogued key through `apply_settings`
  (`api/src/main.cpp:141-149`), so the capability decides who may write, not what.
- The `tester` capability that `0017-tester-capability.md` §4 names does not exist in
  `core/include/cms/capability.hpp`.

## Consequences

- `admin_protocol.cpp` stops parsing an actor from four actions and takes it from the socket instead.
  That file is held by another run, so this lands with that run's step, not before it.
- The system account becomes the actor of every command-line action, so it must hold the capabilities
  those actions require — a decision that belongs with the capability list, not here.
- A future surface that is not root (for example a panel speaking the same protocol over a local
  socket) cannot reuse this identity and must carry a session instead.
