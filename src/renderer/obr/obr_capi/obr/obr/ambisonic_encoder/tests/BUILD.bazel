load("@rules_cc//cc:cc_test.bzl", "cc_test")

# keep-sorted start block=yes prefix_order=cc_test newline_separated=yes
# Benchmark with
#   `bazel run -c opt :ambisonic_encoder_benchmark -- --benchmark_filter=.`
cc_test(
    name = "ambisonic_encoder_benchmark",
    srcs = ["ambisonic_encoder_benchmark.cc"],
    deps = [
        "//obr/ambisonic_encoder",
        "//obr/audio_buffer",
        "//obr/common",
        "@com_google_benchmark//:benchmark_main",
    ],
)

cc_test(
    name = "ambisonic_encoder_test",
    srcs = ["ambisonic_encoder_test.cc"],
    deps = [
        "//obr/ambisonic_encoder",
        "//obr/audio_buffer",
        "//obr/common",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "associated_legendre_polynomials_generator_test",
    srcs = [
        "associated_legendre_polynomials_generator_test.cc",
    ],
    deps = [
        "//obr/ambisonic_encoder:associated_legendre_polynomials_generator",
        "//obr/common",
        "@com_google_googletest//:gtest_main",
    ],
)

# keep-sorted end
