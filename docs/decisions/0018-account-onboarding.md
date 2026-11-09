# 0018 — How an account is created, and which switches apply

Status: accepted 2026-11-09 · decision by the operator

## 1. Two ordering paths, two separate pages

An account arrives one of two ways, and the two are separate pages with separate buttons, not one form
with a dropdown:

| Path | Who acts | Where |
|---|---|---|
| self-registration | the learner | the public sign-up page |
| administrator creates the account | an administrator, holding the capability to do so | the admin surface, with a Create learner page and a Create administrator page |

The two pages share components (handle field, password-setup choice) but are distinct pages, because
each shows only the fields its kind needs: a learner has a level and an e-mail rule, an administrator
has roles.

## 2. The three switches, and exactly which path each governs

| Switch | Setting | Default | Governs |
|---|---|---|---|
| self-registration open | `site.registration_open` | **closed** | whether the public sign-up page exists at all |
| approval required | `site.registration_requires_approval` | **on** | only a self-registered learner, who waits in `pending` |
| an address is required | `site.registration_requires_email` | **on** | whether a new account must carry an e-mail |
| the server can send mail | `mail.enabled` | **off** | whether a verification or setup link can be e-mailed |

**Approval never applies to an administrator-created account.** An administrator vouches for an account
by creating it, either by typing the details or by issuing a one-time setup link; asking them to then
approve it would be asking the same person twice. This is the point the diagram in
`account-flows.html` makes and the reason the approval step appears on one lane only.

`site.registration_requires_email` and `mail.enabled` stay two settings rather than one: a broken mail
server must never silently make an address optional, and an optional address must never silently stop
mail being sent (`fe7f39d`).

## 3. How the password is set

Both pages offer the same two ways, as the operator instructed:

1. **a one-time setup link** — the learner chooses the password. When `mail.enabled` is off, the link
   cannot be e-mailed, so the button is **disabled and carries a tooltip saying e-mail is turned off**,
   and the administrator hands the link over by hand. It is never silently unavailable.
2. **a password set directly** — typed by the administrator or generated, and the learner changes it
   after signing in.

## 4. What the database stores for a password

Never the password. `crypto_pwhash_str` (libsodium, Argon2id) produces the stored hash, and
`crypto_pwhash_str_verify` compares in constant time at sign-in (`api/src/auth.cpp`). The cost is read
from `identity.argon2_memory_kib` and `identity.argon2_iterations`, so an operator can raise it without
a rebuild.

## Consequences

- The admin surface gains two create pages; the learner page gains no creation path at all — a learner
  cannot create an account for someone else.
- A one-time setup link is a new token purpose beside the existing verification and reset purposes.
- `site.registration_requires_email` already exists (`fe7f39d`); this record states what it does not
  govern, which is the approval step.
- Not decided here: how long a setup link stays valid, and whether generating a password shows it once
  or keeps it visible until the administrator leaves the page.
