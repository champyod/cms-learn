# Backup sidecar. Same Postgres major as the database image, so pg_dump can never be older than the
# server it dumps (a mismatched client is how a dump silently loses objects it does not understand).
FROM postgres:18.6

RUN apt-get update \
 && apt-get install -y --no-install-recommends age rclone ca-certificates \
 && rm -rf /var/lib/apt/lists/*

COPY run.sh restore-drill.sh /usr/local/bin/
RUN chmod 0555 /usr/local/bin/run.sh /usr/local/bin/restore-drill.sh \
 && mkdir -p /backups \
 && chown postgres:postgres /backups

# Non-root: this container reads a database credential and writes files, nothing more.
USER postgres

ENTRYPOINT ["/usr/local/bin/run.sh"]
