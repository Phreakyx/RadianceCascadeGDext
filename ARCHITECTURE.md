# Radiance Cascade GI — Architecture & Critical Analysis

A developer-facing tour of how this plugin actually works, what every shader does,
where the bodies are buried, and what to fix next. Read the `README.md` first for
setup; this doc is for people who want to *change* the GI.

It's a real-time, no-bake global illumination solver built on Godot's
`RenderingDevice` compute API, inspired by Alexander Sannikov's Radiance Cascades.
It runs entirely at game runtime (never in the editor — see "Editor safety").

---

## 1. The overall view

Two data structures cooperate each frame:

1. **A voxel scene** — geometry rasterized into 3D textures that store, per voxel:
   albedo, normal, emission, and **injected radiance** (direct light bounced off the
   surface). A high-res camera-centred grid (`_voxel_tex`, 256³) plus coarser
   *clipmap* rings (each 2× the voxel size) give near detail + long range. This is
   what rays hit.

2. **Radiance Cascades** — a hierarchy of direction-resolved probes. Cascade 0 is
   dense/short-range/few-directions; each higher cascade is sparser, longer-range,
   more angular resolution. Probes cone-trace the voxel scene, then cascades are
   merged far→near so the cheap near field inherits the far field's angular detail.
   Probes are stored sparsely in a **world-hashed table** ("patches"), allocated only
   where the current view needs them.

The final per-pixel irradiance is gathered from cascade 0, denoised, upsampled, and
composited over the lit frame.

### Per-frame pipeline (the order in `CRadianceCascade::dispatch`)

```
(if dirty) bake voxels:  voxelize static → inject direct light → SDF → mips
dynamic voxelize → dynamic-occupancy temporal accumulate
patch_clear → patch_add → patch_indirect → patch_trace → (reduce) → patch_merge
patch_gather (half-res) → irradiance_atrous (denoise) → irradiance_upsample (→full)
composite (over the scene color)
```

---

## 2. Where to start reading

- **`RCCompositorEffect.gd`** — the entry point. A `CompositorEffect` whose
  `_render_callback` grabs the depth/normal/color buffers and calls
  `CRadianceCascade::dispatch(...)`. (Runs on the **render thread**.)
- **`src/CRadianceCascade.cpp`** — the orchestrator. `dispatch()` is the spine;
  `_init_pipelines()` allocates every GPU resource; `_build_static_sets()` wires the
  descriptor sets; the `_dispatch_*` helpers each drive one compute pass. Header is
  fully grouped + commented.
- **`shaders/`** — the compute kernels, documented one-by-one below.
- **`radiance_cascade_manager.gd`** — runtime glue: spawns the node, wires the effect.

---

## 3. Shader reference

Conventions: all are `#[compute]` GLSL. "set 0" = static/per-pass resources, "set 1"
= per-frame scene buffers, "set 2" = the trace backend. The shared cascade table is an
SSBO at `b7`; the camera UBO at `b5`.

### Shared includes

- **`rc_trace.glslinc`** — the **cone-trace backend** (`rc_trace(origin, dir,
  aperture_tan, t_start, t_end)`), included by `rc_patch_trace`. Marches the voxel
  scene front-to-back accumulating radiance and transmittance. Samples the **6-axis
  anisotropic mips** (picks the 3 faces opposing the ray, weighted by dir²) so
  occlusion is directional and walls don't self-leak. Composites emission with
  coverage weighting. Uses the **level-0 SDF** to jump empty space, and falls back to
  the coarse clipmap shells outside the level-0 window. Returns `rgb` radiance +
  `a` = residual transmittance (for sky).
- **`rc_light_eval_inc.glslinc`** — shared light math for the injectors. Turns an
  `RCLight` into `albedo·color·N·L·atten/π` + a march direction/reach. **Point/spot/
  area falloff is deliberately non-physical** (`RC_PT_ENERGY=24`, `RC_PT_RANGE=3`,
  `RC_PT_DECAY=1`) — pure tuning knobs.
- **`rc_patch_gather_inc.glslinc`** — the cascade-0 sample+integrate helper
  (`gather_c0_irradiance`), shared by the gather pass and the upsample's edge path.
  8-probe trilinear blend with a **backface-rejection weight** so a thin wall can't
  blend front-lit probes onto a back face.

### Voxelization & bake

- **`rc_voxelize_mesh.glsl`** — static mesh voxelizer, one thread per triangle. Exact
  13-axis triangle/box (SAT) test over the triangle's voxel AABB; writes
  emission/occupancy/albedo/face-normal. Toroidal addressing (`cell = world_voxel %
  res`). **Big triangles must be pre-subdivided CPU-side** (the chunk system does
  this) or a single thread stalls on a huge AABB.
- **`rc_voxelize_dynamic.glsl`** — same SAT test for moving **proxy** triangles
  (capsule/box stand-ins posed CPU-side each frame). Writes a single-channel binary
  occupancy grid; dynamics are **occluders only** (no dynamic albedo/emission).
- **`rc_slab_clear.glsl`** — clears a toroidal shell (slab) before re-voxelizing it on
  a recenter. *(Not read above; trivial.)*
- **`rc_dyn_occ_temporal.glsl`** — `acc = max(cur, acc·decay)`: temporally smears the
  binary dynamic occupancy so fast movers leave a short trail instead of flickering.

### Lighting injection (direct light → voxels)

- **`rc_voxel_inject.glsl`** — level-0 inject. Per occupied voxel: `emission + Σ
  albedo·light·N·L·atten/π · visibility`, where **visibility is a soft SDF march**
  toward each light. `blend_alpha < 1` cross-fades a relight over several frames.
  This is the "direct" term probes later gather as one-bounce indirect.
- **`rc_clip_inject.glsl`** — coarse-level inject. Same idea but visibility is a short
  **occupancy march** (no SDF), and far-field sun shadows are intentionally dropped
  (below coarse-voxel perceptibility; level 0 carries sharp shadows).

### Acceleration structures (SDF + mips)

- **`rc3d_voxel_sdf.glsl`** — half-res **jump-flood** distance field over level-0
  occupancy (`INIT` seed → `FLOOD` halving steps → `FINAL` distances). Built amortized
  ~2 passes/frame. Lets the trace skip empty space. Conservative (over-includes
  occupancy) so it never over-skips.
- **`rc3d_voxel_mip_aniso.glsl`** — the **6-axis anisotropic radiance mip** (GIProbe
  technique). Each level composites child slabs front-to-back per axis-sign, so a cone
  reading a wall's shadowed side gets the dark occluded face, not a leaking mean. This
  is the main anti-leak mechanism.
- **`rc3d_voxel_emission_mip.glsl`** — isotropic, occupancy-weighted emission mip
  (emission doesn't self-occlude, so no front-to-back gating). Lets an emissive cube
  broadcast its glow through coarse levels.

### Probe pipeline (the Radiance Cascades core)

- **`rc_patch_clear.glsl`** — resets the probe hash buckets + per-cascade live counters.
- **`rc_patch_add.glsl`** — allocates probes. Each screen pixel reconstructs its world
  point from depth and inserts the **8 cells** the gather will trilinear-read, for each
  cascade. **Deterministic slot-keyed hashing**: a cell's storage index *is* its hash
  slot, so add/trace/merge/gather all address the same slot every frame → stable
  radiance, no reshuffle. Read-before-CAS keeps the coarse-cascade contention storm
  parallel. Cascade 0 is seeded on the gather's exact half-res lattice; coarse
  cascades on a `_probe_seed_max_h` lattice (the quality/perf knob).
- **`rc_patch_indirect.glsl`** — turns live probe counts into indirect-dispatch args
  (trace = live×dirs, merge = whole region), so trace/merge only run over live probes
  without a CPU readback.
- **`rc_patch_trace.glsl`** — one thread per (probe, direction). Octahedral-decodes the
  direction and calls `rc_trace` over this cascade's `[t_start, t_end)` interval;
  writes `rgb`+transmittance into the probe's deterministic radiance region.
- **`rc_patch_reduce.glsl`** — angular pre-reduce. Only for folds where the direction
  count doubles (oct table `{4,4,8,8,8}`): averages a coarse probe's r² sub-directions
  into scratch so the merge becomes a plain 8-tap trilinear instead of 8×r² taps.
- **`rc_patch_merge.glsl`** — the RC merge. Folds cascade c+1 into c far→near:
  `merged = interval + interval_trans · trilinear(continuation)`. After it runs,
  cascade 0 holds the full radiance field per direction + cumulative transparency.
- **`rc_patch_gather.glsl`** — per **half-res** pixel: reconstruct world+normal,
  trilinear-blend the 8 surrounding c0 probes (with backface rejection), cosine-
  integrate the directions → irradiance. Sky enters as `sky_color` through each
  direction's residual transparency. NaN/Inf guarded.
- **`rc_patch_lookup.glsl`** — debug-only visualization of probes / radiance per
  cascade (driven by `set_debug_view`).

### Output

- **`rc_irradiance_atrous.glsl`** — edge-aware **à-trous** denoise of the half-res
  irradiance (B3-spline weights, doubling hole size), depth+normal edge stops. Removes
  c0 angular quantization + 0.25 m field structure. **Purely spatial — no temporal
  accumulation.**
- **`rc_irradiance_upsample.glsl`** — joint-bilateral upsample half→full using full-res
  depth+normal as the edge guide. At silhouettes/normal discontinuities (where the
  bilateral weight collapses) it does a **direct full-res c0 gather** to avoid haloing.
- **`rc_composite.glsl`** — blends GI over the scene color. **See the albedo limitation
  below** — it does *not* have a real albedo buffer, so it reconstructs a pseudo-albedo
  from the lit color (`approximate_albedo`, a luminance/chroma heuristic with a
  hardcoded `ESTIMATED_LIGHTING = 0.3`) and outputs
  `scene + irradiance · approximate_albedo(scene) · gi_intensity`. Debug modes 1/2
  show the probe/gather buffers instead.

---

## 4. Critical limitations

1. **No albedo G-buffer (the big one).** A `CompositorEffect` only exposes
   depth, normal-roughness, and the *already-lit* color — not the albedo. So composite
   can't do the physically-correct `scene + irradiance·albedo`. The current workaround
   guesses albedo from the lit pixel's luminance/chroma assuming an average incident
   level (`ESTIMATED_LIGHTING`). This mis-tints in bright/dark areas, can't separate
   light from material, and is the single biggest source of energy error. The clean
   fix needs engine access to the albedo buffer (or a custom albedo prepass).

2. **No temporal accumulation / reprojection.** This was the goal so denoising is spatial-only (à-trous).
   With only 16 c0 directions, fast camera/light motion shows shimmer and the GI lags
   the recenter. No motion vectors are consumed.

3. **Single bounce.** Probes gather one bounce of injected *direct* light. There is no
   radiance feedback into the voxels.
   Multi-bounce color bleed (cube→floor→wall) is possible future work.

4. **Dynamic objects are occluders only.** They block GI (binary, temporally smeared
   occupancy) but don't receive or emit it correctly; emissive/colored dynamics aren't
   injected.

5. **Light leaking is mitigated, not solved.** Anisotropic mips + SDF + backface probe
   weights cut it a lot, but thin geometry vs. voxel size can still leak; coarse
   clipmap levels are conservative-occluding to compensate (slightly over-dark).

6. **Non-physical positional lights.** Point/spot/area GI energy is hand-tuned
   (`RC_PT_*`), not inverse-square.

7. **Sky is a flat color.** `set_sky` takes one ambient color; there's no HDRI/real-sky
   sampling (the panorama-bake hook in the demo script is stubbed).

8. **Renderer/threading constraints.** Forward+ only (reads
   `forward_clustered/normal_roughness`); Vulkan RD. First-frame init does a
   *synchronous* scene scan + bake on the render thread (a visible hitch on load), and
   `dispatch` touches the scene tree during that first call.

9. **Diffuse irradiance only.** No glossy/specular GI, no reflections.

---

## 5. Where to improve next (rough priority)

- **Real albedo** → switch composite to true multiplicative GI. Everything else is a
  workaround for this.
- **Maybe Temporal accumulation + reprojection** → stability, fewer directions, cheaper.
- **Maybe Second / multi-bounce** radiance feedback into the voxel grid.
- **Dynamic emissive/colored injection** (not just occlusion).
- **Real sky / HDRI** sampling for the escaped-ray term.
- **Async first-frame bake** to remove the load hitch.
- **Specular/reflection** cascade.
- **Make it an engine addon instead of plugin** will need a fork of the engine and a PR but only when this is in a good state. Being in engine will give us albedo buffer though.
- **Maybe HW RT** Recently Godot merged Vulkan HW RT access directly from GDExt but its questionable if it will be genuinely useful and faster.
- Prune the vestigial isotropic grid-mip path.

---

## 6. Tuning knobs (exported on the `CRadianceCascade` node)

| Property | Effect |
|---|---|
| `gi_intensity` | Final GI strength multiplier. |
| `dist_mult` | Global probe spacing scale (Sannikov "dist mult"). |
| `step_mult` | Cascade interval/reach scale. |
| `scale_mult` | Per-cascade geometric ratio (2 = canonical). |
| `probe_seed_max_h` | Coarse-cascade seed lattice height (quality vs. cost). |
| `upsampler_sigma_z` / `upsampler_normal_pow` | Bilateral/à-trous depth & normal edge sensitivity. |
| `trace_max_steps` | Cone-trace step budget. |
| `voxel_resolution` | Level-0 grid resolution (triggers a reinit). |
| `voxel_update_skip_frames` | Frames between voxel-region recenters. |
| `debug_view` / `debug_cascade` / `debug_clip_level` | Visualizations. |
