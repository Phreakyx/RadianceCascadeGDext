# Radiance Cascade GI (GDExtension plugin)

Compute-based Radiance Cascades global illumination, packaged as a standalone
GDExtension. This was heavily inspired from the work of Alexander Sannikov
which you can read more about here:https://mini.gmshaders.com/p/radiance-cascades

So far this is an incomplete implementation using Godot's own CompositeEffect
pipeline but that is a limitation for example not having access to the albedo
G buffer and due to that the plugin is currently purely additive.

It uses a dynamic voxelizaiton pipeline that updates as a player moves in real time.
For more info check https://github.com/Phreakyx/RadianceCascadeGDext/blob/main/ARCHITECTURE.md

## Contents

```
  radiance_cascade.gdextension   # extension manifest (entry: radiance_cascade_library_init)
  plugin.cfg / plugin.gd         # EditorPlugin; registers the manager autoload
  radiance_cascade_manager.gd    # runtime glue: wires RCCompositorEffect -> CRadianceCascade
  RCCompositorEffect.gd          # CompositorEffect that calls CRadianceCascade::dispatch()
  shaders/                       # all rc_*.glsl / rc3d_*.glsl + rc_*.glslinc
  src/                           # CRadianceCascade.{h,cpp}, compat.h, register_types.{h,cpp}
  SConstruct                     # standalone build (uses godot-cpp submodule below)
  RadianceCascade.sln			 # Visual Studio solution
  bin/                           # build output (referenced by the .gdextension)
  godot-cpp/                     # submodule
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
