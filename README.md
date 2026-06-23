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
  RadianceCascade.sln			 # Visual Studio solution
  bin/                           # build output (referenced by the .gdextension)
  godot-cpp/                     # add as a git submodule (NOT committed here)
```

## Build (you must do this — it wasn't compiled here)

   Open the project in Visual Studio and build Release and Debug or:
   ```
   scons platform=windows target=template_debug
   scons platform=windows target=template_release
   ```
   Output lands in `bin/` with the names the `.gdextension` expects.

## First run in the editor

- Enable the plugin in **Project Settings → Plugins** (registers the
  `RadianceCascadeManager` autoload).
- The C++ loads shaders from `res://addons/radiance_cascade/shaders/...`.

## Usage

- Add a `CRadianceCascade` node to your scene.
- Add an `RCCompositorEffect` to your `WorldEnvironment`'s compositor. It finds the
  RC node automatically (the manager also wires it explicitly).
- The RC node drives its own dynamic occluders + voxel-region recenter
  (`_physics_process`)

## Notes

- I used an example project https://github.com/mikatomik/Godot-4-Overgrown-Subway-Demo
  from mikatomik simply for showcasing the plugin working and being set up.
