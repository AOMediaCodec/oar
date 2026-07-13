load("@rules_cc//cc:cc_test.bzl", "cc_test")

cc_test(
    name = "ambisonic_binaural_decoder_test",
    srcs = [
        "ambisonic_binaural_decoder_test.cc",
    ],
    deps = [
        "//obr/ambisonic_binaural_decoder",
        "//obr/ambisonic_binaural_decoder:fft_manager",
        "//obr/audio_buffer",
        "//obr/common",
        "@abseil-cpp//absl/log:absl_check",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "dsp_utils_test",
    srcs = ["dsp_utils_test.cc"],
    deps = [
        "//obr/ambisonic_binaural_decoder:dsp_utils",
        "//obr/audio_buffer",
        "//obr/common",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "fft_manager_test",
    srcs = ["fft_manager_test.cc"],
    deps = [
        "//obr/ambisonic_binaural_decoder:fft_manager",
        "//obr/audio_buffer",
        "//obr/common",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "partitioned_fft_filter_test",
    srcs = ["partitioned_fft_filter_test.cc"],
    deps = [
        "//obr/ambisonic_binaural_decoder:fft_manager",
        "//obr/ambisonic_binaural_decoder:partitioned_fft_filter",
        "//obr/audio_buffer",
        "//obr/common",
        "//obr/common:test_util",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "planar_interleaved_conversion_test",
    srcs = ["planar_interleaved_conversion_test.cc"],
    deps = [
        "//obr/ambisonic_binaural_decoder:planar_interleaved_conversion",
        "//obr/audio_buffer",
        "@abseil-cpp//absl/log:absl_check",
        "@abseil-cpp//absl/log:absl_log",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "resampler_test",
    srcs = ["resampler_test.cc"],
    deps = [
        "//obr/ambisonic_binaural_decoder:resampler",
        "//obr/audio_buffer",
        "//obr/common",
        "//obr/common:test_util",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "sample_type_conversion_test",
    srcs = ["sample_type_conversion_test.cc"],
    deps = [
        "//obr/ambisonic_binaural_decoder:sample_type_conversion",
        "@com_google_googletest//:gtest_main",
    ],
)
