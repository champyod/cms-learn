# The api image. Two stages: one that resolves and builds, one that runs and holds only what the service
# reads — the binary and the three trees it renders from.
#
# The build stage pins the vcpkg checkout to the baseline the manifest names, so an image built next
# year resolves the same ports this commit resolved rather than whatever master happens to hold.

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
# One configuration per image; a debug build of the same 38 packages doubles the build.
RUN printf 'include(${CMAKE_CURRENT_LIST_DIR}/x64-linux.cmake)\nset(VCPKG_BUILD_TYPE release)\n' \
      > /vcpkg/triplets/x64-linux-release.cmake
RUN cmake --preset release -DVCPKG_TARGET_TRIPLET=x64-linux-release
RUN cmake --build --preset release --target cms_api --parallel

FROM debian:trixie-slim AS runtime

RUN apt-get update \
 && apt-get install -y --no-install-recommends libstdc++6 ca-certificates \
 && rm -rf /var/lib/apt/lists/*

COPY --from=build /src/build/release/api/cms_api /usr/local/bin/cms_api

# The service renders from files, so they travel with it. The template root and the static root are named by
# the operator's host file; the settings catalogue path is compiled into the binary (the catalogued keys this
# build honours). The paths below are the ones those two sources name, so the image is the tree they expect.
COPY --from=build /src/api/templates /src/api/templates
COPY --from=build /src/web/public /src/web/public
COPY --from=build /src/deploy/catalogue /src/deploy/catalogue

# The service reads its facts from a file the operator mounts; it never needs to write.
USER 10001:10001
ENTRYPOINT ["/usr/local/bin/cms_api"]
