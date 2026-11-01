# The sandbox the judge runs student code in, built where the judge runs it.
#
# isolate creates Linux namespaces and cgroups for each run, which is why the gate that proves containment has
# to run privileged and in a container rather than inside the test process.
FROM debian:trixie-slim

RUN apt-get update \
 && apt-get install --yes --no-install-recommends ca-certificates curl gnupg \
 && curl --fail --silent --show-error https://www.ucw.cz/isolate/debian/signing-key.asc \
      > /etc/apt/keyrings/isolate.asc \
 && printf 'Types: deb\nURIs: http://www.ucw.cz/isolate/debian/\nSuites: trixie-isolate\nComponents: main\nArchitectures: amd64\nSigned-By: /etc/apt/keyrings/isolate.asc\n' \
      > /etc/apt/sources.list.d/isolate.sources \
 && apt-get update \
 && apt-get install --yes --no-install-recommends isolate \
 && rm -rf /var/lib/apt/lists/*

ENTRYPOINT ["/usr/bin/isolate"]
