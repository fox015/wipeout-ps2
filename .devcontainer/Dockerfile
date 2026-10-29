# Pinned to an exact ps2dev release (multi-arch, native arm64) so the toolchain
# only changes when we bump it on purpose. gcc 15.2.0, ps2sdk, gsKit.
FROM ps2dev/ps2dev:v2.0.0@sha256:8e6dc4564598ecce94d078d3111b8c4803f88faa94a6f183ac60240a0ae010a1

# The image ships the compiler and its gmp/mpfr/mpc libraries, but not make.
RUN apk add --no-cache make
