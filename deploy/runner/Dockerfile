# The runner image: every tool the C++ gate runs, each at one recorded version (ADR 0043 §2, §6).
#
# The base carries the pinned GCC 16.2.0 and is named by digest, so a rebuild cannot silently move the compiler. clang-tidy
# comes from LLVM's own repository rather than from Debian, because Debian 13 ships 19.0 and the gate's findings were
# triaged against 23: the same tree passes on 23 and reports fourteen pre-existing findings on 19, which is a difference in
# the tool's opinion, not in the code. §6 made the pin cover every tool the gate runs; this file is where that happens.
FROM gcc@sha256:07280eb2e744f1b7a2d8d4a3894c089ea07266c109758afc388a634bd098e4eb

# The clang-tidy version 0043 §6 records, as the exact package LLVM published, so an apt upgrade cannot move it either.
ARG CLANG_TIDY_PACKAGE=1:23.1.6~++20261116014037+5b10d2d74929-1~exp1~20261116134151.96

RUN apt-get update \
    && apt-get install -y --no-install-recommends wget gnupg ca-certificates \
    && wget -qO- https://apt.llvm.org/llvm-snapshot.gpg.key \
        | gpg --dearmor -o /usr/share/keyrings/llvm.gpg \
    && echo 'deb [signed-by=/usr/share/keyrings/llvm.gpg] http://apt.llvm.org/trixie/ llvm-toolchain-trixie-23 main' \
        > /etc/apt/sources.list.d/llvm.list \
    && apt-get update \
    && apt-get install -y --no-install-recommends \
        "clang-tidy-23=${CLANG_TIDY_PACKAGE}" \
        "clang-23=${CLANG_TIDY_PACKAGE}" \
    && ln -sf /usr/bin/clang-tidy-23 /usr/local/bin/clang-tidy \
    && ln -sf /usr/bin/clang-23 /usr/local/bin/clang++ \
    && rm -rf /var/lib/apt/lists/*

# cmake, ninja and the vcpkg ports' own build tools, the same list the CI lanes install.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        cmake ninja-build make git curl zip unzip tar pkg-config \
        bison flex autoconf autoconf-archive automake libtool python3 perl \
    && rm -rf /var/lib/apt/lists/*

# bun runs the gate scripts themselves (tools/cpp/gate.ts and every other gate). Pinned by version and by the release
# digest, so a rebuilt image cannot pick up a different runtime than the one the gate was green on.
ARG BUN_VERSION=1.3.14
ARG BUN_SHA256=951ee2aee855f08595aeec6225226a298d3fea83a3dcd6465c09cbccdf7e848f
RUN wget -qO /tmp/bun.zip "https://github.com/oven-sh/bun/releases/download/bun-v${BUN_VERSION}/bun-linux-x64.zip" \
    && echo "${BUN_SHA256}  /tmp/bun.zip" | sha256sum -c - \
    && unzip -q /tmp/bun.zip -d /opt \
    && ln -sf /opt/bun-linux-x64/bun /usr/local/bin/bun \
    && rm -f /tmp/bun.zip \
    && bun --version

# The gate asks each tool what it is; the image is the answer, so a green run inside it means what it says.
RUN clang-tidy --version && g++ --version && bun --version

# The gate executes the checkout's own code; it must not be able to write outside the tree it was handed.
# UID and GID 1000 are the first user on a typical Linux host, so the checkout and the vcpkg cache mounted
# from it stay writable without a chown; other hosts pass --user and mount a home they own (README).
RUN groupadd --gid 1000 runner \
    && useradd --uid 1000 --gid 1000 --create-home --home-dir /home/runner --shell /bin/bash runner \
    && mkdir -p /src/build /home/runner/.cache/ccache /home/runner/.cache/tmp \
        /home/runner/.cache/bun /home/runner/.vcpkg \
    && chown -R runner:runner /home/runner /src

# Every path the gate legitimately writes is named here rather than inherited from whoever runs the image.
ENV HOME=/home/runner \
    XDG_CACHE_HOME=/home/runner/.cache \
    CCACHE_DIR=/home/runner/.cache/ccache \
    BUN_INSTALL_CACHE_DIR=/home/runner/.cache/bun \
    TMPDIR=/home/runner/.cache/tmp \
    VCPKG_ROOT=/home/runner/.vcpkg

USER runner
RUN id -un && test "$(id -u)" -ne 0
