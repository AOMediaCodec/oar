"""Module extension for non-bzlmod dependencies of OAR."""

load("@bazel_tools//tools/build_defs/repo:git.bzl", "new_git_repository")

def _non_module_deps_impl(_ctx):
    new_git_repository(
        name = "pffft",
        build_file = "@oar//third_party:pffft.BUILD",
        commit = "d7a4c0206a29423478776d6b23a37bbb308f21d5",
        remote = "https://bitbucket.org/jpommier/pffft.git",
    )

non_module_deps = module_extension(implementation = _non_module_deps_impl)
