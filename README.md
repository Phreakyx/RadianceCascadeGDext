# Radiance Cascade GI (GDExtension plugin)

Compute-based Radiance Cascades global illumination, packaged as a standalone
GDExtension. It was extracted from the game's monolithic `PeerlessCPP` extension.

## Contents

```
addons/radiance_cascade/
  radiance_cascade.gdextension   # extension manifest (entry: radiance_cascade_library_init)
  plugin.cfg / plugin.gd         # EditorPlugin; registers the manager autoload
  radiance_cascade_manager.gd    # runtime glue: wires RCCompositorEffect -> CRadianceCascade
  RCCompositorEffect.gd          # CompositorEffect that calls CRadianceCascade::dispatch()
  shaders/                       # all rc_*.glsl / rc3d_*.glsl + rc_*.glslinc
  src/                           # CRadianceCascade.{h,cpp}, compat.h, register_types.{h,cpp}
  SConstruct                     # standalone build (uses godot-cpp submodule below)
  bin/                           # build output (referenced by the .gdextension)
  godot-cpp/                     # add as a git submodule (NOT committed here)
```

## Build (you must do this — it wasn't compiled here)

1. Add godot-cpp as a submodule inside this folder and check out the branch that
   matches your Godot version:
   ```
   git submodule add https://github.com/godotengine/godot-cpp addons/radiance_cascade/godot-cpp
   cd addons/radiance_cascade/godot-cpp && git checkout 4.x   # match your engine
   ```
   (Optionally drop a `.gdignore` in `godot-cpp/` so the editor doesn't scan it.)
2. From `addons/radiance_cascade/`, build:
   ```
   scons platform=windows target=template_debug      # and target=template_release
   ```
   Output lands in `bin/` with the names the `.gdextension` expects.

## First run in the editor

- The shaders were moved, so their old `.import` files were dropped. Reopen the
  project (or run `--import`) once so Godot re-imports them at the new path.
- Enable the plugin in **Project Settings → Plugins** (registers the
  `RadianceCascadeManager` autoload).
- The C++ loads shaders from `res://addons/radiance_cascade/shaders/...`.

## Usage

- Add a `CRadianceCascade` node to your scene (it joins the `radiance_cascade` group).
- Add an `RCCompositorEffect` to your `WorldEnvironment`'s compositor. It finds the
  RC node automatically (the manager also wires it explicitly).
- The RC node drives its own dynamic occluders + voxel-region recenter
  (`_physics_process`). Game-specific setup (sky color, ambient, hotkeys) stays in
  your own scene script — see `main.gd` for an example.

## Notes

- This extension registers only `CRadianceCascade`; the game's other native
  classes remain in `PeerlessCPP`.
- `compat.h` here is the game's include set minus the GeometricTools/boost headers
  that only the movement classes needed.
