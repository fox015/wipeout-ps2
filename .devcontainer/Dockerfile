# Pinned by digest to one exact ps2dev build (multi-arch, native arm64), so the
# toolchain only changes when we bump it on purpose: ps2dev/ps2dev:latest as of
# 3 Nov 2026, gcc 15.2.0, ps2sdk, gsKit. It is the same pin as the shared PS2
# toolchain used by the user's other PS2 projects (see CLAUDE.md); bump both
# together.
FROM ps2dev/ps2dev@sha256:769c4dd608ee3958ff651eb08a10a51568662630efb06d1c9540cbc70232d02d

# The image ships the compiler and its gmp/mpfr/mpc libraries, but not make.
RUN apk add --no-cache make
