#!/usr/bin/env python
# Standalone build for the Radiance Cascade GDExtension plugin.
#
# Expects godot-cpp as a submodule inside this folder:
#   addons/radiance_cascade/godot-cpp
# (add it with:  git submodule add https://github.com/godotengine/godot-cpp
#                addons/radiance_cascade/godot-cpp  — then check out the branch
#                matching your Godot version.)
#
# Build from this directory:   scons platform=windows target=template_debug
# Output goes to ./bin/ and is referenced by radiance_cascade.gdextension.

import os

env = SConscript("godot-cpp/SConstruct")

# Only the plugin's own sources; no boost / GeometricTools (the game build needs
# those, this one does not).
env.Append(CPPPATH=["src"])

if env.get("is_msvc", False):
    env.Append(CXXFLAGS=["/EHsc"])     # exceptions (parity with the game build)
    env.Append(CCFLAGS=["/MT"])        # static CRT, matches godot-cpp's default
else:
    env.Append(CXXFLAGS=["-fexceptions"])

sources = Glob("src/*.cpp")

if env["platform"] == "macos":
    library = env.SharedLibrary(
        "bin/radiance_cascade.{}.{}.framework/radiance_cascade.{}.{}".format(
            env["platform"], env["target"], env["platform"], env["target"]
        ),
        source=sources,
    )
else:
    library = env.SharedLibrary(
        "bin/radiance_cascade{}{}".format(env["suffix"], env["SHLIBSUFFIX"]),
        source=sources,
    )

Default(library)
