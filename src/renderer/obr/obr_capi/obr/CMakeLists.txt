cmake_minimum_required(VERSION 3.28)
project(obr_cmake)

set(CMAKE_CXX_STANDARD 20)

if (WIN32)
    add_definitions(-D_USE_MATH_DEFINES)
    add_definitions(-DNOMINMAX)
    # Disable type conversion warnings.
    add_definitions(/wd4244)
    add_definitions(/wd4305)
    add_definitions(/wd4722)
endif ()

include(FetchContent)

# Fetch GoogleTest (must come before Abseil since Abseil uses it).
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.17.0
)
FetchContent_MakeAvailable(googletest)

# Fetch Abseil library (without tests to avoid build issues).
set(ABSL_BUILD_TESTING OFF CACHE BOOL "Disable abseil tests")
FetchContent_Declare(
        absl
        GIT_REPOSITORY https://github.com/abseil/abseil-cpp.git
        GIT_TAG 20250512.1
)
FetchContent_MakeAvailable(absl)

# Manually add abseil status_matchers (testonly library not built when ABSL_BUILD_TESTING is OFF).
add_library(absl_status_matchers STATIC
    ${absl_SOURCE_DIR}/absl/status/internal/status_matchers.cc
)
target_include_directories(absl_status_matchers PUBLIC ${absl_SOURCE_DIR})
target_link_libraries(absl_status_matchers PUBLIC
    absl::base
    absl::status
    absl::statusor
    absl::strings
    GTest::gmock
    GTest::gtest
)
add_library(absl::status_matchers ALIAS absl_status_matchers)

# Fetch Eigen library.
FetchContent_Declare(
        eigen
        GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git
        GIT_TAG 3147391d946bb4b6c68edd901f2add6ac1f31f8c
)
FetchContent_MakeAvailable(eigen)

# Fetch PFFFT.
FetchContent_Declare(
        pffft
        GIT_REPOSITORY https://bitbucket.org/jpommier/pffft.git
        GIT_TAG 180c2d62717c0150d8aebd165fce19ee4e5f86f9
)
FetchContent_MakeAvailable(pffft)

# Manually add PFFFT library.
add_library(pffft STATIC
        ${pffft_SOURCE_DIR}/pffft.c
)

# Manually list all source files.
set(SourceFiles
        obr/ambisonic_binaural_decoder/ambisonic_binaural_decoder.cc
        obr/ambisonic_binaural_decoder/ambisonic_binaural_decoder.h
        obr/ambisonic_binaural_decoder/dsp_utils.cc
        obr/ambisonic_binaural_decoder/dsp_utils.h
        obr/ambisonic_binaural_decoder/fft_manager.cc
        obr/ambisonic_binaural_decoder/fft_manager.h
        obr/ambisonic_binaural_decoder/partitioned_fft_filter.cc
        obr/ambisonic_binaural_decoder/partitioned_fft_filter.h
        obr/ambisonic_binaural_decoder/planar_interleaved_conversion.cc
        obr/ambisonic_binaural_decoder/planar_interleaved_conversion.h
        obr/ambisonic_binaural_decoder/resampler.cc
        obr/ambisonic_binaural_decoder/resampler.h
        obr/ambisonic_binaural_decoder/sample_type_conversion.cc
        obr/ambisonic_binaural_decoder/sample_type_conversion.h
        obr/ambisonic_binaural_decoder/sh_hrir_creator.cc
        obr/ambisonic_binaural_decoder/sh_hrir_creator.h
        obr/ambisonic_binaural_decoder/wav.cc
        obr/ambisonic_binaural_decoder/wav.h
        obr/ambisonic_binaural_decoder/wav_reader.cc
        obr/ambisonic_binaural_decoder/wav_reader.h
        obr/ambisonic_binaural_decoder/binaural_filters/binaural_filters_1_oa_direct_l.cc
        obr/ambisonic_binaural_decoder/binaural_filters/binaural_filters_1_oa_direct_l.h
        obr/ambisonic_binaural_decoder/binaural_filters/binaural_filters_1_oa_direct_r.cc
        obr/ambisonic_binaural_decoder/binaural_filters/binaural_filters_1_oa_direct_r.h
        obr/ambisonic_binaural_decoder/binaural_filters/binaural_filters_1_oa_ambient_l.cc
        obr/ambisonic_binaural_decoder/binaural_filters/binaural_filters_1_oa_ambient_l.h
        obr/ambisonic_binaural_decoder/binaural_filters/binaural_filters_1_oa_ambient_r.cc
        obr/ambisonic_binaural_decoder/binaural_filters/binaural_filters_1_oa_ambient_r.h
        obr/ambisonic_binaural_decoder/binaural_filters/binaural_filters_1_oa_reverberant_l.cc
        obr/ambisonic_binaural_decoder/binaural_filters/binaural_filters_1_oa_reverberant_l.h
        obr/ambisonic_binaural_decoder/binaural_filters/binaural_filters_1_oa_reverberant_r.cc
        obr/ambisonic_binaural_decoder/binaural_filters/binaural_filters_1_oa_reverberant_r.h
        obr/ambisonic_binaural_decoder/binaural_filters/binaural_filters_2_oa_direct_l.cc
        obr/ambisonic_binaural_decoder/binaural_filters/binaural_filters_2_oa_direct_l.h
        obr/ambisonic_binaural_decoder/binaural_filters/binaural_filters_2_oa_direct_r.cc
        obr/ambisonic_binaural_decoder/binaural_filters/binaural_filters_2_oa_direct_r.h
        obr/ambisonic_binaural_decoder/binaural_filters/binaural_filters_2_oa_ambient_l.cc
        obr/ambisonic_binaural_decoder/binaural_filters/binaural_filters_2_oa_ambient_l.h
        obr/ambisonic_binaural_decoder/binaural_filters/binaural_filters_2_oa_ambient_r.cc
        obr/ambisonic_binaural_decoder/binaural_filters/binaural_filters_2_oa_ambient_r.h
        obr/ambisonic_binaural_decoder/binaural_filters/binaural_filters_2_oa_reverberant_l.cc
        obr/ambisonic_binaural_decoder/binaural_filters/binaural_filters_2_oa_reverberant_l.h
        obr/ambisonic_binaural_decoder/binaural_filters/binaural_filters_2_oa_reverberant_r.cc
        obr/ambisonic_binaural_decoder/binaural_filters/binaural_filters_2_oa_reverberant_r.h
        obr/ambisonic_binaural_decoder/binaural_filters/binaural_filters_3_oa_direct_l.cc
        obr/ambisonic_binaural_decoder/binaural_filters/binaural_filters_3_oa_direct_l.h
        obr/ambisonic_binaural_decoder/binaural_filters/binaural_filters_3_oa_direct_r.cc
        obr/ambisonic_binaural_decoder/binaural_filters/binaural_filters_3_oa_direct_r.h
        obr/ambisonic_binaural_decoder/binaural_filters/binaural_filters_3_oa_ambient_l.cc
        obr/ambisonic_binaural_decoder/binaural_filters/binaural_filters_3_oa_ambient_l.h
        obr/ambisonic_binaural_decoder/binaural_filters/binaural_filters_3_oa_ambient_r.cc
        obr/ambisonic_binaural_decoder/binaural_filters/binaural_filters_3_oa_ambient_r.h
        obr/ambisonic_binaural_decoder/binaural_filters/binaural_filters_3_oa_reverberant_l.cc
        obr/ambisonic_binaural_decoder/binaural_filters/binaural_filters_3_oa_reverberant_l.h
        obr/ambisonic_binaural_decoder/binaural_filters/binaural_filters_3_oa_reverberant_r.cc
        obr/ambisonic_binaural_decoder/binaural_filters/binaural_filters_3_oa_reverberant_r.h
        obr/ambisonic_binaural_decoder/binaural_filters/binaural_filters_4_oa_direct_l.cc
        obr/ambisonic_binaural_decoder/binaural_filters/binaural_filters_4_oa_direct_l.h
        obr/ambisonic_binaural_decoder/binaural_filters/binaural_filters_4_oa_direct_r.cc
        obr/ambisonic_binaural_decoder/binaural_filters/binaural_filters_4_oa_direct_r.h
        obr/ambisonic_binaural_decoder/binaural_filters/binaural_filters_4_oa_ambient_l.cc
        obr/ambisonic_binaural_decoder/binaural_filters/binaural_filters_4_oa_ambient_l.h
        obr/ambisonic_binaural_decoder/binaural_filters/binaural_filters_4_oa_ambient_r.cc
        obr/ambisonic_binaural_decoder/binaural_filters/binaural_filters_4_oa_ambient_r.h
        obr/ambisonic_binaural_decoder/binaural_filters/binaural_filters_4_oa_reverberant_l.cc
        obr/ambisonic_binaural_decoder/binaural_filters/binaural_filters_4_oa_reverberant_l.h
        obr/ambisonic_binaural_decoder/binaural_filters/binaural_filters_4_oa_reverberant_r.cc
        obr/ambisonic_binaural_decoder/binaural_filters/binaural_filters_4_oa_reverberant_r.h
        obr/ambisonic_binaural_decoder/binaural_filters/binaural_filters_wrapper.cc
        obr/ambisonic_binaural_decoder/binaural_filters/binaural_filters_wrapper.h
        obr/ambisonic_encoder/ambisonic_encoder.cc
        obr/ambisonic_encoder/ambisonic_encoder.h
        obr/ambisonic_encoder/associated_legendre_polynomials_generator.cc
        obr/ambisonic_encoder/associated_legendre_polynomials_generator.h
        obr/ambisonic_rotator/ambisonic_rotator.cc
        obr/ambisonic_rotator/ambisonic_rotator.h
        obr/audio_buffer/audio_buffer.cc
        obr/audio_buffer/audio_buffer.h
        obr/audio_buffer/aligned_allocator.h
        obr/audio_buffer/channel_view.cc
        obr/audio_buffer/channel_view.h
        obr/audio_buffer/simd_macros.h
        obr/audio_buffer/simd_utils.cc
        obr/audio_buffer/simd_utils.h
        obr/common/ambisonic_utils.h
        obr/common/constants.h
        obr/common/status_macros.h
        obr/common/misc_math.h
        obr/peak_limiter/peak_limiter.cc
        obr/peak_limiter/peak_limiter.h
        obr/renderer/audio_element_config.cc
        obr/renderer/audio_element_config.h
        obr/renderer/audio_element_table_formatter.cc
        obr/renderer/audio_element_table_formatter.h
        obr/renderer/audio_element_type.h
        obr/renderer/input_channel_config.h
        obr/renderer/processing_group.cc
        obr/renderer/processing_group.h
        obr/renderer/loudspeaker_layouts.h
        obr/renderer/obr_impl.cc
        obr/renderer/obr_impl.h
)

# Define obr library target.
add_library(obr STATIC)

# Add the collected source files to the library target.
target_sources(obr PRIVATE ${SourceFiles})

# Set include directories.
target_include_directories(obr PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${eigen_SOURCE_DIR}
        ${absl_SOURCE_DIR}
        ${pffft_SOURCE_DIR}
)

# Link libraries.
target_link_libraries(obr PRIVATE
        pffft
        eigen
        absl::base
        absl::die_if_null
        absl::flat_hash_map
        absl::log
        absl::log_internal_message
        absl::log_internal_check_op
        absl::status
        absl::statusor
        absl::strings
)

# Enable testing.
enable_testing()
include(CTest)

# Test utility library.
add_library(test_util STATIC
    obr/common/test_util.cc
    obr/common/test_util.h
)
target_include_directories(test_util PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_util PUBLIC obr GTest::gtest)

# Renderer tests.
add_executable(audio_element_config_test obr/renderer/tests/audio_element_config_test.cc)
target_link_libraries(audio_element_config_test PRIVATE obr GTest::gtest_main)
add_test(NAME audio_element_config_test COMMAND audio_element_config_test)

add_executable(audio_element_type_test obr/renderer/tests/audio_element_type_test.cc)
target_link_libraries(audio_element_type_test PRIVATE obr GTest::gtest_main absl::status_matchers)
add_test(NAME audio_element_type_test COMMAND audio_element_type_test)

add_executable(obr_impl_test obr/renderer/tests/obr_impl_test.cc)
target_link_libraries(obr_impl_test PRIVATE test_util GTest::gtest_main absl::status_matchers)
add_test(NAME obr_impl_test COMMAND obr_impl_test)

add_executable(processing_group_test obr/renderer/tests/processing_group_test.cc)
target_link_libraries(processing_group_test PRIVATE test_util GTest::gtest_main absl::status_matchers)
add_test(NAME processing_group_test COMMAND processing_group_test)

add_executable(loudspeaker_layouts_test obr/renderer/tests/loudspeaker_layouts_test.cc)
target_link_libraries(loudspeaker_layouts_test PRIVATE obr GTest::gtest_main)
add_test(NAME loudspeaker_layouts_test COMMAND loudspeaker_layouts_test)

# Audio buffer tests.
add_executable(aligned_allocator_test obr/audio_buffer/tests/aligned_allocator_test.cc)
target_link_libraries(aligned_allocator_test PRIVATE obr GTest::gtest_main)
add_test(NAME aligned_allocator_test COMMAND aligned_allocator_test)

add_executable(audio_buffer_test obr/audio_buffer/tests/audio_buffer_test.cc)
target_link_libraries(audio_buffer_test PRIVATE test_util GTest::gtest_main)
add_test(NAME audio_buffer_test COMMAND audio_buffer_test)

add_executable(channel_view_test obr/audio_buffer/tests/channel_view_test.cc)
target_link_libraries(channel_view_test PRIVATE obr GTest::gtest_main)
add_test(NAME channel_view_test COMMAND channel_view_test)

add_executable(simd_utils_test obr/audio_buffer/tests/simd_utils_test.cc)
target_link_libraries(simd_utils_test PRIVATE obr GTest::gtest_main)
add_test(NAME simd_utils_test COMMAND simd_utils_test)

# Ambisonic binaural decoder tests.
add_executable(ambisonic_binaural_decoder_test obr/ambisonic_binaural_decoder/tests/ambisonic_binaural_decoder_test.cc)
target_link_libraries(ambisonic_binaural_decoder_test PRIVATE test_util GTest::gtest_main)
add_test(NAME ambisonic_binaural_decoder_test COMMAND ambisonic_binaural_decoder_test)

add_executable(dsp_utils_test obr/ambisonic_binaural_decoder/tests/dsp_utils_test.cc)
target_link_libraries(dsp_utils_test PRIVATE obr GTest::gtest_main)
add_test(NAME dsp_utils_test COMMAND dsp_utils_test)

add_executable(fft_manager_test obr/ambisonic_binaural_decoder/tests/fft_manager_test.cc)
target_link_libraries(fft_manager_test PRIVATE obr GTest::gtest_main)
add_test(NAME fft_manager_test COMMAND fft_manager_test)

add_executable(partitioned_fft_filter_test obr/ambisonic_binaural_decoder/tests/partitioned_fft_filter_test.cc)
target_link_libraries(partitioned_fft_filter_test PRIVATE test_util GTest::gtest_main)
add_test(NAME partitioned_fft_filter_test COMMAND partitioned_fft_filter_test)

add_executable(planar_interleaved_conversion_test obr/ambisonic_binaural_decoder/tests/planar_interleaved_conversion_test.cc)
target_link_libraries(planar_interleaved_conversion_test PRIVATE obr GTest::gtest_main)
add_test(NAME planar_interleaved_conversion_test COMMAND planar_interleaved_conversion_test)

add_executable(resampler_test obr/ambisonic_binaural_decoder/tests/resampler_test.cc)
target_link_libraries(resampler_test PRIVATE test_util GTest::gtest_main)
add_test(NAME resampler_test COMMAND resampler_test)

add_executable(sample_type_conversion_test obr/ambisonic_binaural_decoder/tests/sample_type_conversion_test.cc)
target_link_libraries(sample_type_conversion_test PRIVATE obr GTest::gtest_main)
add_test(NAME sample_type_conversion_test COMMAND sample_type_conversion_test)

# Ambisonic encoder tests.
add_executable(ambisonic_encoder_test obr/ambisonic_encoder/tests/ambisonic_encoder_test.cc)
target_link_libraries(ambisonic_encoder_test PRIVATE test_util GTest::gtest_main)
add_test(NAME ambisonic_encoder_test COMMAND ambisonic_encoder_test)

add_executable(associated_legendre_polynomials_generator_test obr/ambisonic_encoder/tests/associated_legendre_polynomials_generator_test.cc)
target_link_libraries(associated_legendre_polynomials_generator_test PRIVATE obr GTest::gtest_main)
add_test(NAME associated_legendre_polynomials_generator_test COMMAND associated_legendre_polynomials_generator_test)

# Ambisonic rotator tests.
# Spherical angle library (used by rotator tests).
add_library(spherical_angle STATIC
    obr/ambisonic_rotator/tests/spherical_angle.cc
    obr/ambisonic_rotator/tests/spherical_angle.h
)
target_include_directories(spherical_angle PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(spherical_angle PUBLIC obr)

add_executable(ambisonic_rotator_test obr/ambisonic_rotator/tests/ambisonic_rotator_test.cc)
target_link_libraries(ambisonic_rotator_test PRIVATE spherical_angle test_util GTest::gtest_main)
add_test(NAME ambisonic_rotator_test COMMAND ambisonic_rotator_test)

add_executable(spherical_angle_test obr/ambisonic_rotator/tests/spherical_angle_test.cc)
target_link_libraries(spherical_angle_test PRIVATE spherical_angle GTest::gtest_main)
add_test(NAME spherical_angle_test COMMAND spherical_angle_test)

# Common tests.
add_executable(ambisonic_utils_test obr/common/tests/ambisonic_utils_test.cc)
target_link_libraries(ambisonic_utils_test PRIVATE obr GTest::gtest_main)
add_test(NAME ambisonic_utils_test COMMAND ambisonic_utils_test)

add_executable(misc_math_test obr/common/tests/misc_math_test.cc)
target_link_libraries(misc_math_test PRIVATE obr GTest::gtest_main)
add_test(NAME misc_math_test COMMAND misc_math_test)

add_executable(test_util_test obr/common/tests/test_util_test.cc)
target_link_libraries(test_util_test PRIVATE test_util GTest::gtest_main)
add_test(NAME test_util_test COMMAND test_util_test)

# Label all OBR tests for easy filtering with ctest -L obr.
set_tests_properties(
    audio_element_config_test
    audio_element_type_test
    obr_impl_test
    processing_group_test
    loudspeaker_layouts_test
    aligned_allocator_test
    audio_buffer_test
    channel_view_test
    simd_utils_test
    ambisonic_binaural_decoder_test
    dsp_utils_test
    fft_manager_test
    partitioned_fft_filter_test
    planar_interleaved_conversion_test
    resampler_test
    sample_type_conversion_test
    ambisonic_encoder_test
    associated_legendre_polynomials_generator_test
    ambisonic_rotator_test
    spherical_angle_test
    ambisonic_utils_test
    misc_math_test
    test_util_test
    PROPERTIES LABELS "obr"
)

# TODO(b/407470208): Define obr_cli executable (requires protobuf and audio_to_tactile).
