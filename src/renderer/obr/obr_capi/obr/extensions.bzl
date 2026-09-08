load("@bazel_tools//tools/build_defs/repo:git.bzl", "new_git_repository")

def _pffft_ext_impl(mctx):
    new_git_repository(
        name = "pffft",
        remote = "https://bitbucket.org/jpommier/pffft.git",
        commit = "0aec0327a6912e1a0ec5326eef737c2ce19bc836",
        # Repository-qualified on purpose: a bare `//external:pffft.BUILD`
        # resolves against the main repository rather than against the module
        # this file belongs to, so it breaks as soon as obr is consumed as a
        # dependency instead of built on its own.
        build_file = Label("@obr//external:pffft.BUILD"),
    )

pffft_ext = module_extension(implementation = _pffft_ext_impl)

def _com_google_audio_to_tactile_ext_impl(mctx):
    new_git_repository(
        name = "com_google_audio_to_tactile",
        remote = "https://github.com/google/audio-to-tactile.git",
        commit = "d3f449fdfd8cfe4a845d0ae244fce2a0bca34a15",
    )

com_google_audio_to_tactile_ext = module_extension(implementation = _com_google_audio_to_tactile_ext_impl)
