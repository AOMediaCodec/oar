# CLAUDE.md — fork notes

This repo is a fork of the AOM Open Audio Renderer.

- `origin` → `eclipsa-audio/oar` (this fork, read/write)
- `upstream` → `AOMediaCodec/oar` (fetch-only; push URL set to `no_push`)

The public `AOMediaCodec/oar` repo is a fresh cut (no shared history with the old `oar-private` staging repo) with the code moved from `liboar/` to the repository root. This fork's `main` was rebuilt on top of it in July 2026; the pre-rebuild history is archived on the `archive/oar-private-main` branch.

## Why the fork exists

Eclipsa's engine consumes OAR+OBR as a Bazel module. Upstream ships a CMake-only build, so this fork adds a `MODULE.bazel` plus the BUILD files needed for `bazel_dep`-style consumption. Fork-local changes are limited to what's needed for that integration.

## Fork-specific changes (on top of upstream)

- `MODULE.bazel`, `BUILD.bazel`, `extensions.bzl`, `third_party/pffft.BUILD` — Bazel module build.
- `oar_get_limiter_env()` — public API exposing per-frame limiter envelope (used by the engine for metering).
- Nested Bazel package markers inside the OBR subtree (`src/renderer/obr/obr_capi/obr/`) deleted so the top-level glob traverses.

### Bug fixes pending upstreaming

These are merged into our `main` and also pushed as branches on `AOMediaCodec/oar` (pending review/merge upstream). Once upstream merges them, a sync should reduce them to no-ops.

- `fix/arm-neon-matrix-render-stub` — `src/renderer/ear/arch/arm/matrix_render_arm.c`: adds `#include "matrix_render.h"` before the `#if defined(def_oar_arch_arm)` guard. Without it, `def_oar_arch_arm` (derived from `__ARM_NEON` in that header) is undefined in this translation unit and `multiply_channels_by_matrix_neon()` is preprocessed away to an empty stub — every matrix render (multichannel downmix M2M and HOA-to-loudspeaker H2M) is silent on ARM; only native-layout passthrough works. x86 is unaffected (scalar C `#else` path); OBR's HRIR binaural path doesn't use this renderer.
- `fix/obr-resampler-shhrir-dsp` — `src/renderer/obr/obr_capi/obr/obr/ambisonic_binaural_decoder/`, with tests:
  - `resampler.cc` — polyphase partition derived from `up_rate_` (downsampling gain/anti-aliasing errors) and `begin()`-relative state addressing for short input blocks.
  - `sh_hrir_creator.cc` — resampled HRIRs scaled by `wav_rate/target_rate` so binaural loudness is invariant to the output rate (fixed the louder-at-96k bug).
- `fix/lfe-filter-sample-rate` — LFE low-pass filter used a hardcoded 48 kHz sample rate; with test `tests/examples/test_hoa_lfe_rendering.c`.

## Syncing with upstream

```sh
git fetch upstream
git log --oneline HEAD..upstream/main          # what's new upstream
git log --oneline upstream/main..HEAD          # fork-only commits
```

When rebasing onto upstream, the fork-specific commits above are the ones to preserve. Anything else on `main` should match upstream.

## pffft

pffft isn't on BCR. It's pulled via the module extension in `extensions.bzl` with a local BUILD shim at `third_party/pffft.BUILD`. See the comment in `MODULE.bazel`.
