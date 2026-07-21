# CMakeLists.txt for tests/examples

# Add the test for audio_element_type_t
add_executable(test_audio_element_types test_audio_element_types.c)

# Add the test for channel-based rendering
add_executable(test_channel_based_rendering test_channel_based_rendering.c)

# Add the test for scene-based rendering
add_executable(test_scene_based_rendering test_scene_based_rendering.c)

# Add the test for object-based rendering
add_executable(test_object_based_rendering test_object_based_rendering.c)

# Link the tests against the main oar library.
# The 'oar' target is defined in the parent CMakeLists.txt,
# so it's accessible here.
target_link_libraries(test_audio_element_types PRIVATE oar)
target_link_libraries(test_channel_based_rendering PRIVATE oar)
target_link_libraries(test_scene_based_rendering PRIVATE oar)
target_link_libraries(test_object_based_rendering PRIVATE oar)

# Include directories are inherited from the parent project,
# so oar.h and other headers should be found automatically.

# Enable C99 standard for the tests. Also link math library for sine wave.
set_property(TARGET test_audio_element_types PROPERTY C_STANDARD 99)
set_property(TARGET test_channel_based_rendering PROPERTY C_STANDARD 99)
set_property(TARGET test_scene_based_rendering PROPERTY C_STANDARD 99)
set_property(TARGET test_object_based_rendering PROPERTY C_STANDARD 99)
if(NOT MSVC)
  target_link_libraries(test_channel_based_rendering PRIVATE m)
  target_link_libraries(test_scene_based_rendering PRIVATE m)
  target_link_libraries(test_object_based_rendering PRIVATE m)
endif()
