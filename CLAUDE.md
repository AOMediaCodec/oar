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

These are merged into our `main` and kept as `fix/*` branches on this fork (not yet submitted upstream). Once upstream merges them, a sync should reduce them to no-ops.

- `fix/olr-azimuth-wrapping` — `src/renderer/olr/`: wraps out-of-range object azimuths and uses circular closest-speaker distance in OLR; with test `tests/examples/test_object_azimuth_wrapping.c`.
- `fix/register-example-tests-ctest` — registers the liboar example tests with ctest (`enable_testing()` at the root, `add_test()` entries, CI runs them via `ctest -L`).
- `fix/ear-dangling-layout-pointer` — `src/renderer/ear/ear.c`: clears a stack-local output-layout pointer before `_open` returns so it can't dangle.

Previously listed here and since merged upstream (now plain upstream history): the ARM NEON matrix-render include fix, the OBR resampler/`sh_hrir_creator` DSP fixes, and the LFE filter sample-rate fix.

## Syncing with upstream

```sh
git fetch upstream
git log --oneline HEAD..upstream/main          # what's new upstream
git log --oneline upstream/main..HEAD          # fork-only commits
```

When rebasing onto upstream, the fork-specific commits above are the ones to preserve. Anything else on `main` should match upstream.

## pffft

pffft isn't on BCR. It's pulled via the module extension in `extensions.bzl` with a local BUILD shim at `third_party/pffft.BUILD`. See the comment in `MODULE.bazel`.
