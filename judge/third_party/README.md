# Vendored sources

| File | Version | Source | Digest |
|---|---|---|---|
| `testlib.h` | 0.9.41 | <https://github.com/MikeMirzayanov/testlib/releases/tag/0.9.41>, asset `testlib.h` | `sha256:70f74c570f2b45d63086ae6e4c41bbb5c5ffd4428cba9914bbc0396d29be10d8` |

testlib is distributed under the MIT licence (Copyright (c) 2005-2023 Mike Mirzayanov). It is vendored
rather than fetched at build time because a contest judge must compile the same checker code on every
host: the file in this directory is the file the release published, and the digest above is how that is
checked rather than assumed.
