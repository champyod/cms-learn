# Settings

Every value an operator can change without rebuilding, without editing a file, and without a
migration is a setting. It lives in one table, is written through one service, and is reachable from
both the CLI and the admin panel because both call that service. Values that cannot honour those
three sentences are not settings, and the second section says where they live instead.

## What a setting is not

| Not a setting | Where it lives | Why not |
|---|---|---|
| table and column definitions | `deploy/schema/`, rendered into `deploy/plan/` | a change needs a migration, and a migration needs review |
| image tags and digests, container CPU and memory | `release.toml`, rendered into `deploy/docker-compose.yml` | the value describes a release, and two servers on one release must agree |
| the plan integrity digest | `deploy/plan/CHECKSUMS`, checked by `tools/apply-plan.sh` | a value that can switch the check off is not a control |
| the database connection and its credentials | a root-owned file read at startup | a compromised admin session must not be able to point the application at another database |
| database credential rotation | an audited operation, not a stored value | rotation rewrites that file and reloads; the value itself is never readable through the panel |
| the signing key for sessions | a root-owned file, rotated the same way | same reason |

The rule behind the table: **a setting may change behaviour; it may not change identity or
provenance.** Changing what the site is called is a setting. Changing which database it trusts, which
image it runs, or whether the migration is the reviewed one is not.

## The three tiers

`setting_tier` is already a closed vocabulary in `deploy/schema/settings.sql`.

| Tier | Readable by | Appears in | Example |
|---|---|---|---|
| `public` | anyone, including an unauthenticated page | rendered output, `/healthz` | `site.name` |
| `operator` | a principal holding `settings:read` | admin panel, CLI, snapshots | `judge.time_limit_ms` |
| `secret` | the process that needs it; never returned by a read | nothing rendered, nothing exported | `storage.secret_access_key` |

A `secret` write is audited with the value replaced by `<redacted>` and the row is hidden from
`cmslearn_judge` by row-level security, so an operator can see *that* a key changed and *when*, never
*what* it became. There is no read path that returns a secret; the only way to learn a current secret
is to hold the file it lives in or to rotate it.

## Kinds and structured values

`setting_kind` is `bool`, `int` or `text`, and the schema proves a value agrees with its kind. A
structured value — a navigation list, a colour pair — is written as `text` in a documented compact
grammar and validated by the registry before it reaches the table, because the table cannot check a
grammar it does not know. The grammar belongs in the registry entry, next to the key, so the panel and
the CLI reject the same inputs.

## Reload classes

| Class | Meaning | How the panel shows it |
|---|---|---|
| `hot` | takes effect on the next read; listeners learn through `NOTIFY cms_settings` and a 30 s version poll | applied immediately |
| `restart` | written now, read at process start | marked "applies on restart", with the service named |

A value that would need an image rebuild or a schema migration is not a setting at all; the panel does
not offer it, and the registry gate fails if someone declares one.

## Catalogue

The key shape is `domain.name`, at most eight segments, lowercase, as the schema's
`settings_key_shape` check requires. Every row below is one registry entry; the columns are the fields
that entry carries.

### Site and identity

| Key | Kind | Tier | Default | Reload |
|---|---|---|---|---|
| `site.name` | text | public | `CMS Learn` | hot |
| `site.tagline` | text | public | empty | hot |
| `site.base_url` | text | operator | empty | hot |
| `site.contact_email` | text | public | empty | hot |
| `site.locale_default` | text | public | `th` | hot |
| `site.locales` | text | public | `th,en` | hot |
| `site.timezone` | text | operator | `Asia/Bangkok` | restart |
| `site.maintenance_mode` | bool | operator | `false` | hot |
| `site.maintenance_message` | text | public | empty | hot |
| `site.announcement` | text | public | empty | hot |
| `site.announcement_level` | text | public | `info` | hot |
| `site.registration_open` | bool | operator | `false` | hot |
| `site.registration_domain_allowlist` | text | operator | empty | hot |
| `site.registration_requires_approval` | bool | operator | `true` | hot |

### Brand and navigation

| Key | Kind | Tier | Default | Reload |
|---|---|---|---|---|
| `brand.logo_light` | text | public | `/static/logo-light.svg` | hot |
| `brand.logo_dark` | text | public | `/static/logo-dark.svg` | hot |
| `brand.favicon` | text | public | `/static/favicon.svg` | hot |
| `brand.color_primary` | text | public | `#1d4ed8` | hot |
| `brand.color_accent` | text | public | `#0f766e` | hot |
| `brand.theme` | text | public | `auto` | hot |
| `brand.font_heading` | text | public | `sans` | hot |
| `brand.font_body` | text | public | `sans` | hot |
| `brand.footer_text` | text | public | empty | hot |
| `nav.items` | text | public | `tasks:/tasks,progress:/progress` | hot |
| `nav.hidden_items` | text | public | empty | hot |

`nav.items` is a comma-separated list of `label:path` pairs drawn from a closed set of routes, so the
panel cannot turn a navigation entry into a link to another site.

### UI and behaviour

| Key | Kind | Tier | Default | Reload |
|---|---|---|---|---|
| `ui.page_size` | int | operator | `25` | hot |
| `ui.editor_theme` | text | public | `light` | hot |
| `ui.editor_font_size` | int | operator | `14` | hot |
| `ui.show_leaderboard` | bool | public | `true` | hot |
| `ui.show_scores_during_contest` | bool | operator | `false` | hot |
| `ui.show_hints` | bool | public | `true` | hot |
| `ui.solutions_visible_after` | text | operator | `accept` | hot |
| `ui.timer_visible` | bool | public | `true` | hot |
| `ui.excerpt_length` | int | operator | `240` | hot |

### Content and assets

| Key | Kind | Tier | Default | Reload |
|---|---|---|---|---|
| `content.max_upload_bytes` | int | operator | `10485760` | hot |
| `content.allowed_image_types` | text | operator | `image/png,image/jpeg,image/webp,image/svg+xml` | hot |
| `content.image_max_dimension` | int | operator | `2048` | hot |
| `content.image_quality` | int | operator | `82` | hot |
| `content.slug_pattern` | text | operator | `^[a-z0-9]+(-[a-z0-9]+)*$` | hot |
| `content.markdown_enabled` | bool | operator | `true` | hot |
| `content.assets_path` | text | public | `/cdn` | hot |
| `content.assets_cache_seconds` | int | operator | `31536000` | hot |

`content.assets_path` is the prefix under which immutable public objects are reachable. The api owns
that path rather than the proxy, because the value must change from the panel without a reload; the
proxy caches the response by the `Cache-Control` the api sets. Keys are content addresses, so a
changed asset is a changed path and the long cache lifetime is safe.

### Storage

| Key | Kind | Tier | Default | Reload |
|---|---|---|---|---|
| `storage.endpoint` | text | operator | `http://objects:8333` | restart |
| `storage.region` | text | operator | `us-east-1` | restart |
| `storage.bucket_public` | text | operator | `cmslearn-public` | hot |
| `storage.bucket_private` | text | operator | `cmslearn-private` | hot |
| `storage.key_prefix` | text | operator | empty | hot |
| `storage.path_style` | bool | operator | `true` | restart |
| `storage.object_max_bytes` | int | operator | `268435456` | hot |
| `storage.presign_ttl_seconds` | int | operator | `0` | hot |
| `storage.access_key_id` | secret | secret | empty | restart |
| `storage.secret_access_key` | secret | secret | empty | restart |

`storage.presign_ttl_seconds` defaults to `0`, which means no presigned URL is ever minted. A
presigned URL is a bearer token: it works for anyone who holds it, for as long as it lives, outside
every capability check. Private objects are therefore streamed by the api, not redirected.

### Judge

| Key | Kind | Tier | Default | Reload |
|---|---|---|---|---|
| `judge.enabled` | bool | operator | `true` | hot |
| `judge.concurrency` | int | operator | `4` | restart |
| `judge.time_limit_ms` | int | operator | `2000` | hot |
| `judge.wall_limit_ms` | int | operator | `10000` | hot |
| `judge.memory_bytes` | int | operator | `268435456` | hot |
| `judge.stack_bytes` | int | operator | `67108864` | hot |
| `judge.disk_bytes` | int | operator | `67108864` | hot |
| `judge.process_limit` | int | operator | `32` | hot |
| `judge.compile_timeout_ms` | int | operator | `20000` | hot |
| `judge.output_limit_bytes` | int | operator | `1048576` | hot |
| `judge.queue_depth_limit` | int | operator | `256` | hot |
| `judge.max_attempts` | int | operator | `3` | hot |

The sandbox image digest is not here: it belongs to the release, and a judge that runs a different
image from the one the release names is a defect, not a preference.

### Sessions and access

| Key | Kind | Tier | Default | Reload |
|---|---|---|---|---|
| `identity.session_idle_minutes` | int | operator | `30` | hot |
| `identity.session_absolute_hours` | int | operator | `12` | hot |
| `identity.password_min_length` | int | operator | `12` | hot |
| `identity.lockout_threshold` | int | operator | `10` | hot |
| `identity.lockout_minutes` | int | operator | `15` | hot |
| `identity.elevation_minutes` | int | operator | `10` | hot |
| `identity.revoke_on_password_change` | bool | operator | `true` | hot |
| `identity.argon2_memory_kib` | int | operator | `65536` | restart |
| `identity.argon2_iterations` | int | operator | `3` | restart |

### HTTP

| Key | Kind | Tier | Default | Reload |
|---|---|---|---|---|
| `http.request_timeout_seconds` | int | operator | `15` | hot |
| `http.max_body_bytes` | int | operator | `1048576` | hot |
| `http.trust_proxy_hops` | int | operator | `1` | hot |
| `http.hsts_max_age` | int | operator | `31536000` | hot |
| `http.csp_report_only` | bool | operator | `false` | hot |
| `http.cors_origins` | text | operator | empty | hot |
| `http.workers` | int | operator | `4` | restart |

### Observability and audit

| Key | Kind | Tier | Default | Reload |
|---|---|---|---|---|
| `observability.log_level` | text | operator | `info` | hot |
| `observability.metrics_enabled` | bool | operator | `true` | hot |
| `observability.metrics_path` | text | operator | `/metrics` | restart |
| `observability.trace_sample_per_mille` | int | operator | `0` | hot |
| `observability.alert_queue_depth` | int | operator | `64` | hot |
| `observability.alert_latency_p95_ms` | int | operator | `500` | hot |
| `audit.retention_days` | int | operator | `365` | hot |
| `audit.record_client_ip` | bool | operator | `false` | hot |

### Backup

| Key | Kind | Tier | Default | Reload |
|---|---|---|---|---|
| `backup.enabled` | bool | operator | `true` | hot |
| `backup.schedule` | text | operator | `0 3 * * *` | hot |
| `backup.retention_daily` | int | operator | `7` | hot |
| `backup.retention_weekly` | int | operator | `4` | hot |
| `backup.retention_monthly` | int | operator | `3` | hot |
| `backup.age_recipient` | text | operator | empty | hot |
| `backup.offsite_url` | text | operator | empty | hot |
| `backup.offsite_access_key_id` | text | secret | empty | hot |
| `backup.offsite_secret_access_key` | text | secret | empty | hot |

### Feature switches

| Key | Kind | Tier | Default | Reload |
|---|---|---|---|---|
| `features.contests` | bool | operator | `true` | hot |
| `features.practice` | bool | operator | `true` | hot |
| `features.forum` | bool | operator | `false` | hot |
| `features.certificates` | bool | operator | `false` | hot |
| `features.search` | bool | operator | `true` | hot |
| `features.profile_editing` | bool | operator | `true` | hot |

A feature switch hides an entry point and refuses its routes. It does not delete data and it does not
skip an authorization check; turning `features.forum` off must not become a way to make a permission
disappear.

## How a value travels

1. **Declaration.** A registry entry names the key, kind, tier, default, reload class, capability and
   grammar. This is the only place a key is introduced; the panel form, the CLI completion list and
   the documentation table are generated from it.
2. **Validation.** A write is checked against the entry: kind, grammar, range, tier, and the caller's
   capability. An unknown key is refused rather than created, so a typo cannot become a setting.
3. **Write.** One key or many. A multi-key change goes through `settings_plans`, which expires in
   fifteen minutes, so a half-applied configuration cannot sit in the table waiting to surprise
   someone. The actor comes from `SET LOCAL cms.actor_id`; an unattributed write raises.
4. **Propagation.** The trigger bumps `settings_meta.version` and calls
   `pg_notify('cms_settings', version)`. Every process keeps a snapshot behind an atomic pointer,
   swaps it on the notification, and re-checks the version every 30 s in case a notification was
   missed.
5. **Audit.** The trigger writes `settings_audit` with the tier, kind and value; a `secret` value is
   replaced by `<redacted>` on both sides of the change.

The CLI and the panel are both thin clients of that service. Neither owns a second code path, which is
what makes "customisable from the panel" and "customisable from the CLI" the same claim rather than
two claims that can drift.

## What the gate proves

`check:settings` compares the registry against the live catalog and fails on any of these:

| Defect | Why it matters |
|---|---|
| a declared key with no row | the process reads a default the panel cannot see |
| a row with no declaration | an orphan value nobody validates or documents |
| kind, tier or default disagreeing | the panel renders a control that writes a value the schema rejects |
| a `secret` key reachable from a `public` read | the leak this tier exists to prevent |
| a `restart` key written but never read at start | a change that silently does nothing |

The falsification for that gate declares a key in the registry that the catalog does not hold, and
requires the gate to fail on it by name.
