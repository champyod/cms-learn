# Caddy with two modules the stock image does not ship:
#   - the CrowdSec bouncer (ask CrowdSec about every request before serving it)
#   - rate limiting (credential stuffing reaches the API far cheaper if it is shed at the edge)
#
# Pinned: a floating Caddy major would silently change TLS defaults between deploys.
FROM caddy:2.11.7-builder@sha256:c4cd20eaa0b69112fd5d4f721ffc9671687e1d5d0b155c8620cf7546dbef94c0 AS builder

RUN xcaddy build \
    --with github.com/hslatman/caddy-crowdsec-bouncer@v0.14.1 \
    --with github.com/mholt/caddy-ratelimit@v0.1.0

FROM caddy:2.11.7@sha256:36c24ad91a7ff7bc4c87fe22bfa8df3c4b18da0773d73fc12ee2c52b98c0bb7e

COPY --from=builder /usr/bin/caddy /usr/bin/caddy

# Fail the image build if the modules did not actually make it into the binary: a bouncer that is
# absent at runtime fails open, which is the one failure mode this image exists to prevent.
RUN caddy list-modules | grep -q 'http.handlers.crowdsec' \
 && caddy list-modules | grep -q 'http.handlers.rate_limit' \
 && caddy version

# The stock image has no user and runs as root. A process that terminates TLS for the whole site has
# no business doing it as uid 0: it only needs its own config, certs and log directory.
# Ports below 1024 are unprivileged-mapped in compose instead (80→8080, 443→8443), precisely so this
# USER line can exist.
RUN mkdir -p /data /config /var/log/caddy \
 && chown -R 10001:10001 /data /config /var/log/caddy

USER 10001:10001

HEALTHCHECK --interval=30s --timeout=5s --start-period=10s --retries=3 \
    CMD wget --spider -q http://127.0.0.1:2019/config/ || exit 1
