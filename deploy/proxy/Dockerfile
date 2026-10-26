# Caddy with two modules the stock image does not ship:
#   - the CrowdSec bouncer (ask CrowdSec about every request before serving it)
#   - rate limiting (credential stuffing reaches the API far cheaper if it is shed at the edge)
#
# Pinned: a floating Caddy major would silently change TLS defaults between deploys.
FROM caddy:2.11.7-builder AS builder

RUN xcaddy build \
    --with github.com/hslatman/caddy-crowdsec-bouncer@v0.14.1 \
    --with github.com/mholt/caddy-ratelimit@v0.1.0

FROM caddy:2.11.7

COPY --from=builder /usr/bin/caddy /usr/bin/caddy

# Fail the image build if the modules did not actually make it into the binary: a bouncer that is
# absent at runtime fails open, which is the one failure mode this image exists to prevent.
RUN caddy list-modules | grep -q 'http.handlers.crowdsec' \
 && caddy list-modules | grep -q 'http.handlers.rate_limit' \
 && caddy version

HEALTHCHECK --interval=30s --timeout=5s --start-period=10s --retries=3 \
    CMD wget --spider -q http://127.0.0.1:2019/config/ || exit 1
