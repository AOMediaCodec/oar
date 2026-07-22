load("@bazel_tools//tools/build_defs/repo:git.bzl", "new_git_repository")

def _pffft_ext_impl(mctx):
    new_git_repository(
        name = "pffft",
        remote = "https://bitbucket.org/jpommier/pffft.git",
        commit = "d7a4c0206a29423478776d6b23a37bbb308f21d5",
        build_file = Label("//external:pffft.BUILD"),
    )

pffft_ext = module_extension(implementation = _pffft_ext_impl)

def _com_google_audio_to_tactile_ext_impl(mctx):
    new_git_repository(
        name = "com_google_audio_to_tactile",
        remote = "https://github.com/google/audio-to-tactile.git",
        commit = "d3f449fdfd8cfe4a845d0ae244fce2a0bca34a15",
    )

com_google_audio_to_tactile_ext = module_extension(implementation = _com_google_audio_to_tactile_ext_impl)
