load("@rules_cc//cc:cc_test.bzl", "cc_test")

cc_test(
    name = "ambisonic_utils_test",
    srcs = ["ambisonic_utils_test.cc"],
    deps = [
        "//obr/common",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "misc_math_test",
    srcs = ["misc_math_test.cc"],
    deps = [
        "//obr/common",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "test_util_test",
    srcs = ["test_util_test.cc"],
    deps = [
        "//obr/audio_buffer",
        "//obr/common",
        "//obr/common:test_util",
        "@com_google_googletest//:gtest_main",
    ],
)
