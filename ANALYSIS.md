# Radiance Cascade GI — Implementation Critique & Engine Roadmap

A critical assessment of *this* implementation: how faithfully it realizes sparse
3D Radiance Cascades, where it cuts corners (and whether those cuts are sound), the
engineering issues, and the path to moving it into Godot's engine/renderer code.

Companion to `ARCHITECTURE.md` (which is the neutral "what each piece does" tour).

---

## 1. Verdict — how close is this to sparse RC in 3D?

It is a **genuine sparse-3D Radiance Cascades skeleton**, not an RC-flavored hack.
The hierarchy, interval partition, trilinear + angular merge, deterministic sparse
probe store, and cone-traced scene representation are all present and wired
correctly.

But it is a **pragmatic, diffuse-only approximation** of the canonical formulation,
not a from-the-paper implementation. Roughly **~75% of the structure**, with the two
scaling laws simplified and the output collapsed to a single diffuse irradiance term.

It keeps RC's headline win — it is **deterministic and noise-free** (no Monte Carlo,
no temporal jitter). It gives up canonical angular/interval rigor and any directional
(specular) use of the field.

---

## 2. Fidelity, element by element

### 2.1 Cascade scaling — partially faithful
`_build_cascade_table()` produces:

- **Spacing** `0.25 · 2^c` = {0.25, 0.5, 1, 2, 4} m. Spatial spacing ×2 per cascade is
  canonical. ✓
- **Directions** `oct = {4,4,8,8,8}` → dirs {16, 16, 64, 64, 64}. This is **not** the
  clean ×4-per-cascade angular growth the penumbra condition wants. c2–c4 all sit at
  64 directions while their intervals keep doubling, so the **far cascades are
  angularly under-resolved relative to their reach**. The merge even special-cases
  `r = oct_n/oct_c ∈ {1,2}` because the table isn't a uniform ratio.
- **Intervals** `t_end = t + 1·2^c` → ranges [0,1], [1,3], [3,7], [7,15], [15,31] m.
  Interval length ×2 per cascade. Canonical 3D ties interval length to angular
  resolution (≈×4), so this is a shorter, cheaper schedule reaching ~31 m with 5
  cascades.

**Net:** the penumbra condition is *approximated, not maintained*. Because the final
output is a cosine integral (diffuse, very low frequency), we won't see the error in
practice — but this schedule would not hold up for sharp/specular RC.

### 2.2 The merge — faithful
Trilinear spatial fold of cascade c+1 into c, with
`merged = interval + interval_trans · continuation` and cumulative transmittance, is
the correct 3D RC merge. The angular pre-reduce pass is a sound optimization. This is
the strongest, most canonical part of the implementation.

### 2.3 Sparse allocation — faithful and well executed
Screen-driven, world-hashed, **deterministic slot-keyed** probes (storage index = hash
slot) is exactly the practical answer to the impossibility of a dense 3D probe volume,
and it is what makes this "sparse RC." The read-before-CAS contention handling on
coarse cascades is genuinely good engineering, and the determinism keeps the field
stable frame-to-frame.

### 2.4 The tracer — legitimate, but it's VCT (not exact)
Cone-tracing an anisotropic voxel mip with SDF empty-space skipping is a reasonable RC
backend. RC is tracer-agnostic, so this is
a fair choice — but it inherits voxel cone tracing's compromises: light leaking,
resolution limits, conservative/coarse occlusion. It is not ground truth.

### 2.5 The biggest theoretical critique — directionality is paid for, then discarded
The cascades store and merge 16–64 **directional** radiance samples per probe, and then
`rc_patch_gather` **cosine-integrates them to a single scalar irradiance**. For pure
diffuse, that directional machinery is largely wasted cost. Two honest ways out:

- store far less per probe (e.g. SH-9 / irradiance) for much cheaper diffuse, **or**
- actually **use the directions** for specular / directional occlusion to justify
  carrying them.

As it stands it pays for the expensive half of RC and reaps only the cheap half.

### 2.6 No parallax / ray-origin fix
Each cascade traces from its probe centre with a plain trilinear merge — none of the
bilinear / ray-alignment correction canonical RC uses to hide inter-cascade parallax.
Invisible for diffuse; would matter for anything sharper.

---

## 3. Implementation issues (engineering, not theory)

- **Magic-number driven.** `RC_PT_ENERGY/RANGE/DECAY`, `ESTIMATED_LIGHTING = 0.3`, the
  cascade table, `COARSE_OCC_CONSERVATISM = 8`, `SDF_MAX_SKIP_VOXELS`, etc. are
  hand-tuned to one demo scene, not derived. Fragile across content.
- **The albedo irony.** It voxelizes albedo into `_voxel_albedo`, yet the composite
  cannot use real per-pixel albedo and falls back to `approximate_albedo(scene)`
  (a luminance/chroma guess). The data exists, but at the wrong resolution and stage.
- **Not energy-conserving.** Non-physical positional lights + guessed albedo + a single
  bounce make it "plausible," not radiometric.
- **First-frame synchronous bake + render-thread scene access** in `dispatch()`'s init
  path → a load-time hitch and a threading smell.
- **Fixed probe caps** with silent cell drops when a hash chain fills (`MAX_LINEAR`).

---

## 4. The architectural ceiling — why "into the engine" is right

The `CompositorEffect` API is the hard ceiling, and it's worth being precise about why.
It only exposes **depth, normal-roughness, and the already-lit color**. Two consequences
cannot be fixed from a post-effect:

1. **No albedo** — and crucially, **Godot's Forward+ is a forward renderer with no
   albedo G-buffer to grab even from engine code.** So "move into the engine" does *not*
   hand us an albedo buffer; stock Godot doesn't keep one.
2. We composite *after* lighting, which is the wrong stage of the pipeline.

Therefore the correct engine integration is **not** "post-process with a real
G-buffer." It is to deliver RC the way Godot's own **SDFGI / VoxelGI** are delivered:
**as an indirect-light source sampled inside the forward object shader**, where the
fragment's real albedo and roughness are in hand. The object shader does
`color += rc_irradiance · albedo` (and could use the cascade directions for specular)
*at shading time*. That one change dissolves the albedo limitation **and** makes RC
coexist correctly with the engine's direct lighting, shadows, and tonemapping.

---

## 5. Next steps (ordered)

1. **Build as a C++ module, not a GDExtension.** The code is already module-ready
   (the `IS_MODULE_BUILD` branch in `compat.h`, the dual-path `register_types`). As a
   module we can include `RendererSceneRenderRD` / render-buffer internals and stop
   fighting the CompositorEffect accessors. This is the enabling step for everything
   below.
2. **Integrate as indirect light in `scene_forward_clustered`**, mirroring how SDFGI is
   sampled: expose the cascade-0 irradiance field (or a screen-space irradiance target)
   to the forward shader and multiply by the material albedo there. Fixes albedo
   correctly; delete `approximate_albedo`.
3. **Reuse Godot's existing SDF clipmap.** SDFGI already maintains a cascaded scene SDF
   in-engine. Tracing RC against that would delete this plugin's entire voxelize / slab
   / SDF apparatus and likely improve quality.
4. **Add temporal reprojection** using the engine's motion vectors → stability and the ability to cut directions.
5. **Make the scaling lawful, then exploit it.** Derive the cascade table from the
   penumbra condition (parameterized), then either downsize to an SH/irradiance store
   for cheap diffuse **or** keep directions and add a specular tap so the directional
   cost pays off.
6. **Multi-bounce** by feeding gathered irradiance back into voxel radiance (the removed
   `rc_voxel_bounce` kernel was the start of this).

---

## 6. One-paragraph summary

This is a legitimate, noise-free, sparse 3D Radiance Cascades diffuse-GI solver with
solid systems engineering (deterministic probe hashing, anisotropic voxel mips, SDF
skipping, async toroidal streaming), held back by a hand-tuned scaling schedule, a
VCT-grade tracer, and — most of all — the `CompositorEffect` sandbox that denies it the
albedo buffer and the correct shading-time integration point. The highest-leverage move
is not more shader tuning; it is relocating into engine/module code and consuming RC as
an indirect-light source in the forward pass, after which correct albedo, temporal
reuse, SDF sharing, and specular all become reachable.
