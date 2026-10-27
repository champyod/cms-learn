# Backup sidecar. Same Postgres major as the database image, so pg_dump can never be older than the
# server it dumps (a mismatched client is how a dump silently loses objects it does not understand).
FROM postgres:18.6

RUN apt-get update \
 && apt-get install -y --no-install-recommends age rclone ca-certificates \
 && rm -rf /var/lib/apt/lists/*

COPY run.sh restore-drill.sh health.sh /usr/local/bin/
RUN chmod 0555 /usr/local/bin/run.sh /usr/local/bin/restore-drill.sh /usr/local/bin/health.sh \
 && mkdir -p /backups \
 && chown postgres:postgres /backups

# Non-root: this container reads a database credential and writes files, nothing more.
USER postgres

# Healthy means "the scheduler is alive AND the newest archive is under 36h old" — not merely
# "the process exists", which is what a sidecar that stopped dumping would still report.
HEALTHCHECK --interval=5m --timeout=10s --start-period=60s --retries=2 \
    CMD ["/usr/local/bin/health.sh"]

ENTRYPOINT ["/usr/local/bin/run.sh"]
