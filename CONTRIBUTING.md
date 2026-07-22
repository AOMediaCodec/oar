# How to Contribute

We welcome community contributions to OAR. Thank you for your time! By
contributing to the project, you agree to the license, patent and copyright
terms in the AOM License and Patent License and to the release of your
contribution under these terms. See [LICENSE](LICENSE) and [PATENTS](PATENTS)
for details.

## Contributor License Agreement

In order to contribute, you must execute the appropriate
[Contributor License Agreement](https://aomedia.org/about/legal/#contributor-license-agreements)
to ensure that AOMedia has the right to distribute your changes.

## Coding style

We are using the style defined by the
[Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html) as
applied to C, enforced with clang-format using the
[.clang-format](.clang-format) configuration file in the root of the
repository.

The bundled Open Binaural Renderer (obr) library at
[src/renderer/obr/obr_capi/obr](src/renderer/obr/obr_capi/obr) carries its own
[.clang-format](src/renderer/obr/obr_capi/obr/.clang-format) configuration.
Since clang-format applies the configuration file nearest to each source file,
the command below formats files in both trees correctly.

```sh
# Apply clang-format to modified .c, .h files
clang-format -i --style=file \
  $(git diff --name-only --diff-filter=ACMR '*.c' '*.h')
```

## Contribution process

- Validate that your changes do not break the build, locally and through the
  GitHub Actions CI:

  ```sh
  cmake -S . -B build -DOAR_BUILD_EXAMPLES=ON
  cmake --build build
  ```

- Run the example programs as smoke tests and ensure they pass:

  ```sh
  ./build/tests/examples/test_audio_element_types
  ./build/tests/examples/test_channel_based_rendering
  ./build/tests/examples/test_scene_based_rendering
  ./build/tests/examples/test_object_based_rendering
  ```

- Run the obr unit tests and ensure they pass:

  ```sh
  ctest --test-dir build/src/renderer/obr/obr_capi/obr -L obr --output-on-failure
  ```

- Submit a pull request for review by the maintainers.

## Pull request process

- Authors should use a valid email account when committing.
- Make clear and concise commits (one commit per feature or issue).
- Authors are responsible for breaking down the pull request into sensible
  commits with proper commit messages.
- Avoid using force push when addressing comments and review items.
- Maintainers shall use 'rebase and merge' to make sure all commits can apply
  cleanly onto the main branch.
- Maintainers shall only use 'squash and merge' with the permission of the
  authors.
