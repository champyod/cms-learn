# The migration step of a deploy. It carries pgschema and nothing else: the plan and the wrapper
# arrive as read-only mounts from the checkout the stack was rendered from, so the file that is
# applied is the file whose digests were reviewed, never a copy baked into an image.
#
# The binary is a static build published as a release asset, verified against the digest recorded in
# docs/dependencies.md. A replaced asset fails the build instead of shipping.

FROM debian:trixie-slim@sha256:f6b08b09a05725f9431502d3b5714f5c4576e82c1a84fa1debe0c24a7e0c3ec4 AS fetch

ARG PGSCHEMA_VERSION=1.13.4
ARG PGSCHEMA_SHA256=abce75a704ce411a8f9d1dcc786573d7e0e6dcc6b060a0e6b66a1550ed734bdb

RUN apt-get update \
 && apt-get install -y --no-install-recommends ca-certificates curl \
 && rm -rf /var/lib/apt/lists/*

RUN curl -fsSL -o /usr/local/bin/pgschema \
      "https://github.com/pgplex/pgschema/releases/download/v${PGSCHEMA_VERSION}/pgschema-${PGSCHEMA_VERSION}-linux-amd64" \
 && echo "${PGSCHEMA_SHA256}  /usr/local/bin/pgschema" | sha256sum -c - \
 && chmod 0555 /usr/local/bin/pgschema

FROM debian:trixie-slim@sha256:f6b08b09a05725f9431502d3b5714f5c4576e82c1a84fa1debe0c24a7e0c3ec4

# bash runs the wrapper; sha256sum is the check the wrapper performs before it connects.
COPY --from=fetch /usr/local/bin/pgschema /usr/local/bin/pgschema

WORKDIR /app

# Nothing in this container needs to write: it reads a mounting directory, talks to one database and
# exits. A migration tool with root in a database network is avoidable risk.
USER 10001:10001

ENTRYPOINT ["bash", "/app/tools/apply-plan.sh"]
