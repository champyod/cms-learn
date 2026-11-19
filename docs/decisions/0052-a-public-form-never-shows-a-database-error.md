# 0052 — A public form never shows a database error

Status: accepted 2026-11-19 · taken under the operator's autonomous window, listed for his review

## 1. The finding this record answers

`api/src/accounts.cpp:208-210` returns the raw database exception as the message an anonymous visitor reads
when a signup conflicts:

```cpp
catch (const std::exception& error) {
  return std::unexpected(Error::make(ErrorCode::conflict, error.what()));
}
```

PostgreSQL's duplicate-key text names the constraint — `learner_accounts_email_key` — and the value it
collided on. So a public form publishes the shape of the schema, and it confirms whether a given address is
already registered: an enumeration channel the sign-in path closes deliberately (`auth.cpp:30`, "the one
refusal an unknown address and a wrong password share").

## 2. The decision

| Reader | What a failure may say |
|---|---|
| the operator, a log, an audit row | everything: the full error, every invalid key, the constraint that refused it |
| a public form | one sentence naming **what the person can change**, and nothing about the schema |

The mapping is explicit, case by case: "that email is already registered" and "that handle is taken" are two
sentences, never one. A database error that is not a known conflict becomes "we could not register that
account", with the real error in the audit row.

Never, in any response body: a database message, a constraint name, a SQL fragment, a stack.

## 3. Why

The repository's "fail loudly, name every invalid key in one message" rule exists so an operator can fix a
misconfiguration. A public form has a different reader, and the loud version tells that reader nothing
actionable while telling an attacker plenty.

One generic sentence for every conflict was rejected too: it makes a fixable mistake unfixable, because the
person cannot tell whether to change the address or the handle.

## 4. Consequences

- `sign_up` maps unique violations to the two named sentences; any other failure is generic plus an audit row.
- The rule is about **public readers**, not about one line: the signup, sign-in, submission and settings forms
  all obey it, and any future form does from its first commit.
- A test asserts the raw text does not appear in a response, and one case per sentence asserts the sentence.
