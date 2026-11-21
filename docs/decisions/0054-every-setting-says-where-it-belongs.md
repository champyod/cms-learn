# 0054 — Every setting says where it belongs, who may change it, what it costs to apply, and how it is stored

Status: accepted 2026-11-19 · decision by the operator

## 1. The problem this record answers

The settings page shows **109** controls. Seventeen of them are read by code. Among the ninety-two: a task's
time limit, a session's idle minutes, a lockout threshold, an HTTP CSP flag, and a mail password stored as
plain text. The page has one permission level (anyone who sees it can change it) and says nothing about which
saving restarts a service.

An operator who sets a control and sees nothing happen learns to distrust the whole page.

## 2. The decision

Every setting carries four facts, and the surface is built from them.

| Axis | Values |
|---|---|
| **scope** | `system` · `task` · `deployment` |
| **permission** | `view` · `edit` · `secret-write` (needs an elevation) |
| **reload** | `hot` · `service restart` · `container restart` |
| **storage** | `plain` · `sealed` |

| Rule | Consequence |
|---|---|
| a value that belongs to a task is not on this page | `judge.time_limit_ms` is edited where the task is |
| a deployment value is not editable from a page at all | it lives in the deployment's own files |
| only a `system` setting the build reads is shown | the page stops promising what nothing honours |
| a secret is write-only, behind an elevation, and sealed before it touches a row | until sealing exists, a secret is not offered at all |
| every row shows its reload class **before** it is saved | nobody discovers a restart afterwards |
| rows are grouped by what they restart | one action per restart |
| every change is audited with who and when | an operator can answer "who changed this" |

## 3. Why

A settings page is a promise: if the control is there, it works, and if it needs a restart, the page says so
first. Showing a hundred controls to look complete is the opposite of that promise, and the project's own rule
for visible content already forbids it — *never render a placeholder a reader would take for real material*.

## 4. Consequences

- The catalogue gains `scope` and `reload` per entry, and `docs/settings.md` becomes the document that decides
  them rather than a wish list.
- Two capabilities join the existing ones: settings read and settings write; a secret additionally needs the
  elevation the admin panel already has.
- **Sealing becomes a prerequisite.** `CMSLEARN_SEAL_KEY` and libsodium sealing are declared and not
  implemented, so no secret may be offered on the page until they are.
- Task-scoped settings move to the task editor, which does not exist for all of them yet — those entries leave
  the page now and return there later.
- Deployment-scoped entries leave the page entirely.

## 5. What was rejected

- **Showing all 109 unchanged.** It is a page that lies, and it teaches every operator to distrust the next
  control.
- **One permission level with a reload badge.** An operator without the right role could change a lockout, and
  a restart would still be discovered after the fact.
- **Removing only the non-system entries.** It fixes the classification and leaves the secrets in plain text.

## 6. Amendment, 2026-11-21 — the page offers what works, and says what a change costs

The catalogue is not the platform. `deploy/catalogue/settings.json` declares **109** keys; the tree reads **17** of them
(the grep that decides it excludes the catalogue itself, the tests and the docs). The other 92 are declarations of intent,
and offering one on `/settings` promised an effect the platform does not have — an operator changes `site.tagline` and
nothing anywhere reads it.

So the page offers only the 17, from the one list in `api/include/cms/api/settings_catalogue.hpp`, and each row states the
four facts §1 requires plus the cost of the change in the operator's words:

| Fact | Where it comes from |
|---|---|
| scope | the key's prefix — `site`, `identity`, `mail` — the same groups the tabs already use |
| who may change it | the capability the route already enforces, `settings:write` (a secret also needs `settings:secret_write`) |
| how it takes effect | the catalogue's `reload`: `hot` → "takes effect immediately"; `restart` → "takes effect after the api service restarts; the running service keeps the old value until then" |
| whether it is sealed | the catalogue's `secret` tier; the page renders `sealed` and never the value |

The unread 92 are recorded here rather than shown with a warning, because a warning still offers them. A key that gains a
reader is added to that list in the same change that reads it; a key whose reader is deleted is removed from it, and
`api/tests/settings_route_test.sh` holds the page to the list in both directions — `site.tagline` must not be offered,
`site.name` must be, and every offered row must carry all of its facts.
