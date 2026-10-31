# The api image. Two stages: one that resolves and builds, one that runs and holds nothing else.
#
# The build stage pins the vcpkg checkout to the baseline the manifest names, so an image built next
# year resolves the same ports this commit resolved rather than whatever master happens to hold.

FROM debian:trixie-slim AS build

ARG VCPKG_BASELINE=ab9a05e70421507222d83861ac1a31be7e4272d4

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      build-essential cmake ninja-build git curl ca-certificates zip unzip tar pkg-config \
 && rm -rf /var/lib/apt/lists/*

RUN git clone --filter=blob:none https://github.com/microsoft/vcpkg /vcpkg \
 && git -C /vcpkg checkout "${VCPKG_BASELINE}" \
 && /vcpkg/bootstrap-vcpkg.sh -disableMetrics

WORKDIR /src
COPY . .

ENV VCPKG_ROOT=/vcpkg
RUN cmake --preset release \
 && cmake --build --preset release --target cms_api --parallel

FROM debian:trixie-slim AS runtime

RUN apt-get update \
 && apt-get install -y --no-install-recommends libstdc++6 ca-certificates \
 && rm -rf /var/lib/apt/lists/*

COPY --from=build /src/build/release/api/cms_api /usr/local/bin/cms_api

# The service reads its facts from a file the operator mounts; it never needs to write.
USER 10001:10001
ENTRYPOINT ["/usr/local/bin/cms_api"]
