# CMakeLists.txt for tests/examples

# --- Shared test infrastructure -------------------------------------------
# Build test_framework.c and test_helpers.c into a static library so that
# each test target only needs its own .c file + link oar_test_common.
add_library(oar_test_common STATIC test_framework.c test_helpers.c)
target_link_libraries(oar_test_common PUBLIC oar)
set_property(TARGET oar_test_common PROPERTY C_STANDARD 99)
if(NOT MSVC)
  target_link_libraries(oar_test_common PUBLIC m)
endif()

# --- Helper function -------------------------------------------------------
# Creates a test executable, links it, and registers it with CTest.
function(oar_add_test name)
  add_executable(${name} ${name}.c)
  target_link_libraries(${name} PRIVATE oar_test_common)
  set_property(TARGET ${name} PROPERTY C_STANDARD 99)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

# --- Test targets ---------------------------------------------------------

oar_add_test(test_audio_element_types)
oar_add_test(test_channel_based_rendering)
oar_add_test(test_scene_based_rendering)
oar_add_test(test_object_based_rendering)
oar_add_test(test_metadata_unit_processing)
oar_add_test(test_remove_audio_element)

# The HOA LFE test exercises the 120 Hz LFE low-pass, which is only compiled
# into the library when OAR_ENABLE_HOA_LFE is ON.
if(OAR_ENABLE_HOA_LFE)
  oar_add_test(test_hoa_lfe_rendering)
endif()
