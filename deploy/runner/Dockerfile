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

# The gate asks each tool what it is; the image is the answer, so a green run inside it means what it says.
RUN clang-tidy --version && g++ --version
