# Frame size (`samples_per_channel`)

`oar_config_t.samples_per_channel` is the number of samples per channel in
every block that `oar_render()` renders and that
`oar_update_audio_element_data()` takes. `oar_create()` accepts 1 to
`def_oar_max_samples_per_channel` (2^24 = 16,777,216 samples, 349.5 s at
48 kHz) for every target layout, and returns NULL for 0 or a larger value.

## Limits

- **Upper limit, 2^24.** The renderers compute buffer sizes and offsets as
  32-bit channels × samples products: `uint32_t` in the OAR core, `int` in
  the object renderer, the loudspeaker renderers and the limiter. At 2^24
  samples per channel, on 64-bit platforms, the `uint32_t` products stay
  below 2^32 for up to 255 channels, and the `int` products, which cover at
  most 25 channels, stay below 2^31. On 32-bit platforms the byte counts of
  large buffers can exceed `size_t`; OAR's allocations then return NULL, and
  OBR's throw an exception that its C API turns into an error return.
- **Allocation failure.** OBR's memory grows with `samples_per_channel` (see
  below). If it cannot allocate its buffers for an added element,
  `oar_add_audio_element()` returns `ck_oar_error_nomem` and the group keeps
  its previous elements. If it cannot allocate them when the group's first
  element creates it, the call returns `ck_oar_error_inval`.
- **Binaural renderer.** OBR on its own accepts 1 to
  `ObrImpl::kMaxBufferSizePerChannel` (2^29) samples per channel. Its FFT
  size is twice the buffer size rounded up to a power of two, and pffft
  takes the FFT size as an `int`.
- **Lower limit, 1.** OBR's FFT has a minimum of 32 points, pffft's minimum
  size. At 16 samples per channel and below, the FFT stays at 32 points.

## Cost of small and large frames

### Binaural

OBR convolves each binaural filter set (one per combination of ambisonic
order and filter profile among a group's elements) with uniformly
partitioned FFT convolution. Each HRIR is split into partitions of
`samples_per_channel` samples. The HRIRs are 256 samples long for the
direct profile and 8192 samples for the ambient and reverberant profiles at
48 kHz (see
[binaural_rendering.md](../src/renderer/obr/obr_capi/obr/docs/binaural_rendering.md)).

- **CPU.** Each block multiplies every partition in the frequency domain,
  so below the HRIR length the number of partitions per block grows as
  `samples_per_channel` shrinks. With the ambient and reverberant profiles
  the measured cost per sample rises 1.8 to 2.3 times per halving from 256
  down to 32 samples. From 8 samples down to 1, where the FFT also stays at
  32 points, it rises 3.6 to 3.9 times per halving with every profile. The
  lowest cost per sample was at 256 samples for the direct profile and at
  4096 for ambient and reverberant; above that it rises slowly, to 2.0 to
  2.9 times the lowest at 1,048,576 samples.
- **Memory.** Above the HRIR length each HRIR is one partition, so memory
  grows linearly with `samples_per_channel`: about 1.8 MiB per 1000 samples
  for one third-order element. At 16 samples and below the partitions keep
  the 32-point FFT size, so memory grows again as `samples_per_channel`
  shrinks: 78 MiB at 1 sample with the ambient profile, against 5.7 MiB at
  32.

### Loudspeaker layouts

The loudspeaker renderers do not convolve. Their measured cost per sample
rises 1.9 to 2.0 times per halving from 32 samples down to 1, where it
reached 4.6% of one core, and was at most 0.11% of one core from 64 samples
up. Their memory grows linearly with `samples_per_channel`, by about 0.15 to
0.19 MiB per 1000 samples for one element.

## Recommended range

256 to 65,536 samples per channel. In that range, one binaural element
measured at most 2.7 times its lowest cost per sample and at most 121 MiB.
Sizes outside it are accepted, at the costs given below.

## Measurements

`tests/examples/benchmark_samples_per_channel.c` measures one `oar_t` at
48 kHz with one group holding one element, for each scenario and
`samples_per_channel`. It records the growth of the process's memory
footprint from before `oar_create()` to after the first rendered block, then
renders until at least 0.5 s of CPU time and two blocks have passed. The CPU
column is that time as a percentage of the rendered audio's duration: 100%
is one core busy in real time. The limiter is enabled, as by default. On
macOS and Linux each measurement runs in its own child process.

Scenarios:

- **Binaural 3OA *profile***: one third-order scene-based element
  (16 channels) with the given binaural filter profile.
- **Binaural object ambient**: one object at 30° azimuth and 10°
  elevation, with its position set before every block, and the ambient
  profile. OBR encodes objects to third order and renders them with the
  third-order filters.
- **7.1.4 3OA** and **7.1.4 object**: the same third-order element and
  object rendered to 7.1.4.

Results on an Apple M5 Pro, macOS 26.6.2, Apple clang 21.0.0, CMake Release
build, from one run. A second run differed by up to 11% in the CPU columns
and up to 1.3% in the memory figures of 1 MiB or more.

CPU time, % of one core in real time:

| samples_per_channel | ms at 48 kHz | Binaural 3OA direct | Binaural 3OA ambient | Binaural 3OA reverberant | Binaural object ambient | 7.1.4 3OA | 7.1.4 object |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 0.0208 | 213 | 5995 | 6039 | 6004 | 2.27 | 4.63 |
| 8 | 0.167 | 4.40 | 97.8 | 99.8 | 98.6 | 0.338 | 0.607 |
| 32 | 0.667 | 0.746 | 10.2 | 10.2 | 10.3 | 0.0841 | 0.172 |
| 64 | 1.33 | 0.473 | 4.36 | 4.45 | 4.48 | 0.0570 | 0.101 |
| 128 | 2.67 | 0.355 | 2.20 | 2.23 | 2.29 | 0.0448 | 0.0677 |
| 256 | 5.33 | 0.320 | 1.21 | 1.21 | 1.30 | 0.0378 | 0.0531 |
| 512 | 10.7 | 0.326 | 0.766 | 0.771 | 0.843 | 0.0440 | 0.0506 |
| 960 | 20.0 | 0.392 | 0.643 | 0.652 | 0.732 | 0.0405 | 0.0436 |
| 1024 | 21.3 | 0.365 | 0.579 | 0.576 | 0.666 | 0.0398 | 0.0433 |
| 4096 | 85.3 | 0.421 | 0.452 | 0.458 | 1.03 | 0.0501 | 0.0391 |
| 16384 | 341 | 0.650 | 0.649 | 0.654 | 1.20 | 0.0536 | 0.0397 |
| 65536 | 1365 | 0.745 | 0.744 | 0.755 | 1.45 | 0.0746 | 0.0400 |
| 262144 | 5461 | 0.826 | 0.826 | 0.821 | 1.35 | 0.0889 | 0.0415 |
| 1048576 | 21845 | 0.932 | 0.930 | 0.928 | 1.50 | 0.0800 | 0.0421 |

Memory footprint growth, MiB:

| samples_per_channel | ms at 48 kHz | Binaural 3OA direct | Binaural 3OA ambient | Binaural 3OA reverberant | Binaural object ambient | 7.1.4 3OA | 7.1.4 object |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 0.0208 | 3.72 | 77.6 | 77.6 | 77.6 | 0.0781 | 0.125 |
| 8 | 0.167 | 0.688 | 10.8 | 10.8 | 10.8 | 0.0625 | 0.125 |
| 32 | 0.667 | 0.484 | 5.70 | 5.70 | 5.69 | 0.0625 | 0.141 |
| 64 | 1.33 | 0.594 | 5.59 | 5.59 | 5.55 | 0.0625 | 0.125 |
| 128 | 2.67 | 0.641 | 5.58 | 5.58 | 5.55 | 0.0938 | 0.141 |
| 256 | 5.33 | 0.875 | 5.73 | 5.73 | 5.69 | 0.109 | 0.156 |
| 512 | 10.7 | 1.52 | 6.16 | 6.16 | 6.08 | 0.156 | 0.234 |
| 960 | 20.0 | 2.53 | 6.89 | 6.89 | 6.78 | 0.203 | 0.328 |
| 1024 | 21.3 | 2.62 | 6.91 | 6.91 | 6.78 | 0.203 | 0.328 |
| 4096 | 85.3 | 9.81 | 12.0 | 12.0 | 11.6 | 0.672 | 0.906 |
| 16384 | 341 | 31.7 | 31.7 | 31.7 | 30.6 | 2.56 | 3.25 |
| 65536 | 1365 | 121 | 120 | 120 | 113 | 10.1 | 12.7 |
| 262144 | 5461 | 473 | 473 | 473 | 444 | 40.1 | 50.2 |
| 1048576 | 21845 | 1876 | 1877 | 1877 | 1761 | 160 | 200 |

Memory figures below 1 MiB are a few 16 KiB pages and vary between runs.

## Running the benchmark

With CMake:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DOAR_BUILD_EXAMPLES=ON
cmake --build build --target benchmark_samples_per_channel
build/tests/examples/benchmark_samples_per_channel [max_samples_per_channel]
```

With Bazel:

```sh
bazel run -c opt //tests/examples:benchmark_samples_per_channel -- [max_samples_per_channel]
```

The default maximum is 1,048,576 samples, at which the binaural scenarios
take about 1.8 GiB. The tables go to stdout, progress to stderr. Memory is
measured on macOS (`phys_footprint`) and Linux (`RssAnon`); on other systems
the memory table shows "-".
