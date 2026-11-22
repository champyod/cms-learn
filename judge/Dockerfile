# The judge image: the supervisor, the checker runtime and the isolate sandbox in one image, built from
# the same checkout the deployment came from.
#
# The build stage pins the vcpkg checkout to the baseline the manifest names, the same way the api image
# does, so an image built next year resolves the ports this commit resolved.
#
# The runtime carries isolate (namespaces, cgroups and the box user come from the package, not from a
# hand-copied binary), a C++ compiler for the submissions, the supervisor and the token checker. It runs
# as root because isolate needs to create namespaces and switch to the box user; the container's
# capabilities are what bound it, not the user.
FROM gcc:16.2.0-trixie AS build

ARG VCPKG_BASELINE=ab9a05e70421507222d83861ac1a31be7e4272d4

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      cmake make git curl ca-certificates zip unzip tar pkg-config \
 && rm -rf /var/lib/apt/lists/*

RUN git clone --filter=blob:none https://github.com/microsoft/vcpkg /vcpkg \
 && git -C /vcpkg checkout "${VCPKG_BASELINE}" \
 && /vcpkg/bootstrap-vcpkg.sh -disableMetrics

# libpq's build asks for the parser generator by name; without it vcpkg stops at "Could not find bison".
# libpq asks for the parser generator, libsodium for autotools; both are named by the port, not by us.
RUN apt-get update \
 && apt-get install -y --no-install-recommends bison flex autoconf autoconf-archive automake libtool \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

ENV VCPKG_ROOT=/vcpkg
# The runner's image needs one configuration; a debug build of the same 38 packages doubles the build
# and is what a memory-tight runner cannot finish.
RUN printf 'set(VCPKG_TARGET_ARCHITECTURE x64)\nset(VCPKG_CRT_LINKAGE dynamic)\nset(VCPKG_LIBRARY_LINKAGE dynamic)\nset(VCPKG_BUILD_TYPE release)\n' \
      > /vcpkg/triplets/x64-linux-release.cmake
RUN cmake --preset release -DVCPKG_TARGET_TRIPLET=x64-linux-release \
 && cmake --build --preset release --target cms_judge_run cms_checker_token --parallel

FROM debian:trixie-slim AS runtime

RUN apt-get update \
 && apt-get install --yes --no-install-recommends ca-certificates curl gnupg \
 && curl --fail --silent --show-error https://www.ucw.cz/isolate/debian/signing-key.asc \
      > /etc/apt/keyrings/isolate.asc \
 && printf 'Types: deb\nURIs: http://www.ucw.cz/isolate/debian/\nSuites: trixie-isolate\nComponents: main\nArchitectures: amd64\nSigned-By: /etc/apt/keyrings/isolate.asc\n' \
      > /etc/apt/sources.list.d/isolate.sources \
 && apt-get update \
 && apt-get install --yes --no-install-recommends isolate g++ libstdc++6 \
 && rm -rf /var/lib/apt/lists/*

COPY --from=build /src/build/release/judge/cms_judge_run /usr/local/bin/cms_judge_run
COPY --from=build /src/build/release/judge/cms_checker_token /usr/local/bin/cms_checker_token

# One row per invocation: the runner claims a submission, runs it in the sandbox and writes the verdict.
# A failed run leaves the claim for the lease to return, so the loop is what keeps the worker alive
# rather than a retry around a wrong invariant.
ENTRYPOINT ["/bin/sh", "-c", "while :; do /usr/local/bin/cms_judge_run --isolate /usr/bin/isolate --checker /usr/local/bin/cms_checker_token --worker \"$(hostname)\" || sleep 2; done"]
