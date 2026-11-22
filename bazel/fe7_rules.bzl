"""Bazel rules and compiler flag macros for fe7."""

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

FE7_COMMON_COPTS = [
    "-std=c++20",
    "-fno-exceptions",
    "-fno-rtti",
    "-Wall",
    "-Wextra",
    "-Werror",
    "-Wsign-conversion",
    "-Wconversion",
    "-Wimplicit-fallthrough",
    "-Wshadow",
    "-Wno-unused-parameter",
]

FE7_GAME_COPTS = FE7_COMMON_COPTS + [
    "-fno-strict-aliasing",
]

FE7_RUNTIME_COPTS = FE7_COMMON_COPTS

def fe7_game_cc_library(name, srcs = [], hdrs = [], deps = [], visibility = None, **kwargs):
    """Defines a C++20 library in game/ with strict game copts."""
    cc_library(
        name = name,
        srcs = srcs,
        hdrs = hdrs,
        deps = deps,
        copts = FE7_GAME_COPTS,
        visibility = visibility,
        **kwargs
    )

def fe7_runtime_cc_library(name, srcs = [], hdrs = [], deps = [], copts = [], visibility = None, **kwargs):
    """Defines a C++20 library in runtime/."""
    cc_library(
        name = name,
        srcs = srcs,
        hdrs = hdrs,
        deps = deps,
        copts = FE7_RUNTIME_COPTS + copts,
        visibility = visibility,
        **kwargs
    )

def fe7_cc_test(name, srcs = [], deps = [], copts = [], **kwargs):
    """Defines a colocated GoogleTest C++20 unit test."""
    cc_test(
        name = name,
        srcs = srcs,
        deps = deps + ["@googletest//:gtest_main"],
        copts = FE7_RUNTIME_COPTS + copts,
        **kwargs
    )
