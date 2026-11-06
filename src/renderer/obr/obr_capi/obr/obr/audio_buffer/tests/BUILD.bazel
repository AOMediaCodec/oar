load("@rules_cc//cc:cc_test.bzl", "cc_test")

cc_test(
    name = "audio_buffer_test",
    srcs = ["audio_buffer_test.cc"],
    deps = [
        "//obr/audio_buffer",
        "//obr/common",
        "//obr/common:test_util",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "channel_view_test",
    srcs = ["channel_view_test.cc"],
    deps = [
        "//obr/audio_buffer",
        "//obr/audio_buffer:channel_view",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "simd_utils_test",
    srcs = ["simd_utils_test.cc"],
    deps = [
        "//obr/audio_buffer",
        "//obr/audio_buffer:simd_utils",
        "//obr/common",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "aligned_allocator_test",
    srcs = ["aligned_allocator_test.cc"],
    deps = [
        "//obr/audio_buffer",
        "@com_google_googletest//:gtest_main",
    ],
)
