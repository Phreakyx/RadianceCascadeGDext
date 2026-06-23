#pragma once
#include "compat.h"

// =============================================================================
//  CRadianceCascade — real-time global illumination for Godot (RenderingDevice)
//
//  A compute-only GI solver: it combines a voxel scene representation with
//  Radiance Cascades (Alexander Sannikov's technique) to produce one bounce of
//  diffuse indirect light (plus sky + emissive) every frame, no bake step.
//
//  Driven as a CompositorEffect backend: each frame the host calls set_camera_*
//  then dispatch(depth, normal, color, size). Output is an irradiance texture
//  (get_irradiance_texture) the host composites as
//  `scene + irradiance * albedo * gi_intensity`.
//
//  THE TWO HALVES
//  1) VOXEL SCENE — geometry is rasterized into a 3D grid storing, per voxel:
//     albedo, normal, emission and *injected radiance* (direct light bounced off
//     the surface). A camera-centred level-0 grid gives near detail; coarser
//     clipmap levels (1..N, each 2x the voxel size) extend range. Anisotropic
//     mips + a signed-distance field make the grid cheap to cone-trace. This is
//     what probes "see" when they shoot rays.
//
//  2) RADIANCE CASCADES — a hierarchy of probe sets ("cascades"). Cascade 0 has
//     dense probes with few directions and short rays; each higher cascade is
//     sparser with more angular resolution and longer ray intervals. Probes
//     cone-trace the voxel grid, then cascades are merged far->near so the cheap
//     near field inherits the far field's angular detail. Probes are stored
//     sparsely in a world-hashed table ("patches"), allocated only where the
//     current view needs them.
//
//  PER-FRAME PIPELINE (see dispatch()):
//     bake voxels if dirty -> voxelize dynamic occluders -> clear/allocate
//     probes (patch_add) -> trace probes vs voxels -> merge cascades ->
//     gather c0 into a half-res irradiance buffer -> a-trous denoise ->
//     bilateral upsample to full-res -> composite.
//
//  STREAMING — static geometry lives in 16 m chunks (lazily subdivided on
//  worker threads, prefetched ahead of the camera). As the camera moves the
//  grids scroll toroidally: only the newly exposed shell ("slab") is
//  re-voxelized, off the render thread.
//
//  GLOSSARY  probe/patch = a directional radiance sample at a world point.
//            cascade     = one level of the probe hierarchy.
//            inject      = write direct lighting into voxels.
//            slab        = the shell of voxels exposed by a recenter.
// =============================================================================

namespace godot
{
    // ── GPU layout structs ───────────────────────────────────────────────────
    // One record per compute pass; each mirrors the push_constant block (or an
    // SSBO/UBO struct) in the matching .glsl. Layouts are std430/std140-tight and
    // 16-byte aligned, so field order and the _pN padding must stay in lockstep
    // with the shaders.

    // rc_composite.glsl — final blend of GI over the lit scene color.
    struct alignas(16) RCCompositePushConstants
    {
        uint32_t screen_width;
        uint32_t screen_height;
        float    gi_intensity;
        uint32_t debug_mode;
    };

    // rc_patch_trace set 2 — level-0 grid placement (world origin/size) + step cap.
    struct alignas(16) RCTraceParams { float vox_origin[3]; float voxel_size; float vox_extent[3]; uint32_t max_steps; };
    // rc_voxel_debug.glsl — raymarch a grid level to the screen for visualization.
    struct alignas(16) RCVoxelDebugPC { uint32_t sw, sh, res, max_steps; float vox_origin[3]; float voxel_size; float vox_extent[3]; float occ_threshold; };

    // rc3d_voxel_emission_mip — downsample one emission mip level.
    struct alignas(16) RCVoxelMipPC { uint32_t dst_res, _p2, _p0, _p1; };

    // rc_patch_clear.glsl — reset probe buckets + per-cascade alloc counters.
    struct alignas(16) RCPatchClearPC { uint32_t total_buckets, num_cascades, _b, _c; };
    // rc_patch_add.glsl — allocate probes for a screen-pixel grid over cascades [begin,end).
    struct RCPatchAddPC
    {
        uint32_t screen_width, screen_height, cascade_begin, cascade_end;
        float    z_near, z_far, _p1, _p2;
    };
    // rc_patch_indirect.glsl — turn live probe counts into indirect-dispatch args.
    struct alignas(16) RCPatchIndirectPC { uint32_t num_cascades, local_size, _b, _c; };
    // rc_patch_trace.glsl — which cascade this dispatch traces.
    struct alignas(16) RCPatchTracePC { uint32_t cascade, local_trans, frame, amortize_n; };

    // rc_patch_lookup.glsl — debug readout of probes / radiance for one cascade.
    struct alignas(16) RCPatchLookupPC
    {
        uint32_t screen_width, screen_height, debug_kind, cascade;
        float    z_near, z_far, _p0, _p1;
        float    sky_color[3]; float _p2;   // kind 1 (gather preview) sky fallback; matches the gather
    };

    // rc_patch_gather.glsl — sample cascade-0 probes into the half-res irradiance buffer.
    struct alignas(16) RCPatchGatherPC
    {
        uint32_t screen_width, screen_height, _p0, _p1;
        float    z_near, z_far, _p2, _p3;
        float    sky_color[3]; float _p4;   // sky fallback where no probe is hit
    };

    // rc_patch_merge.glsl — merge cascade c+1 down into cascade c.
    struct alignas(16) RCPatchMergePC { uint32_t cascade, _p0, _p1, _p2; };

    // rc3d_voxel_mip_aniso.glsl — build 6-axis directional radiance mips.
    struct alignas(16) RCMipAnisoPC { uint32_t dst_res; uint32_t src_is_aniso; uint32_t _p1, _p2; };

    // rc_irradiance_upsample.glsl — bilateral half->full upsample (depth/normal aware).
    struct alignas(16) RCUpsamplePC
    {
        uint32_t full_w, full_h, half_w, half_h;
        float    z_near, z_far, sigma_z, normal_pow;
        float sky_color[3];
        float _pad;
    };

    // rc_voxelize_dynamic.glsl — rasterize dynamic proxy tris into the occupancy grid.
    struct alignas(16) RCDynVoxelizePC { float vox_origin[3]; uint32_t res; float vox_extent[3]; uint32_t tri_count; };

    // rc_voxelize_mesh.glsl — rasterize static tris into a grid slab.
    struct alignas(16) RCVoxelizePC
    {                       // matches modified rc_voxelize_mesh.glsl
        float vox_extent[3]; uint32_t res;
        int32_t slab_lo[3];  uint32_t tri_count;
        int32_t slab_dim[3]; uint32_t _p;
    };
    // rc_slab_clear.glsl — clear a toroidal shell before re-voxelizing it.
    struct alignas(16) RCSlabClearPC { int32_t slab_lo[3]; uint32_t res; int32_t slab_dim[3]; uint32_t _p; };

    // rc_voxel_inject.glsl — inject direct light into a level-0 grid slab.
    // blend_alpha < 1 cross-fades a relight over several frames instead of popping.
    struct alignas(16) RCInjectSlabPC
    {
        float    vox_origin[3]; uint32_t light_count;
        float    voxel_size;    float blend_alpha; uint32_t _pf, _pg;   // was uint32_t _pe,_pf,_pg
        int32_t  slab_lo[3];    uint32_t res;
        int32_t  slab_dim[3];   uint32_t _p2;
        int32_t  phase[3];      uint32_t _p3;
    };

    // rc3d_voxel_sdf.glsl — jump-flood distance field (mode = init/flood/finalize, step = jump size).
    struct alignas(16) RCSdfPC { uint32_t mode; int32_t step; uint32_t res; uint32_t _p0; int32_t phase[3]; uint32_t _p1; };

    struct LevelGeom { float voxel_size; float extent; Vector3 origin; };   // CPU: one grid level's placement
    struct SrcTri { Vector3 a, b, c; Color e, al; };                        // CPU: source triangle (pos + emission/albedo)

    // rc_clip_inject.glsl — inject sun (+ lights) into a coarse clipmap level.
    struct alignas(16) RCClipInjectSlabPC
    {
        float    sun_dir[3];   float    blend_alpha;
        float    sun_color[3]; float    voxel_size;
        int32_t  slab_lo[3];   uint32_t res;
        int32_t  slab_dim[3];  uint32_t light_count;
        int32_t  phase[3];     uint32_t _p3;
    };

    // One light, SSBO record read by the inject passes (_light_buf).
    struct alignas(16) RCLightGPU
    {                                     // 4×vec4 = 64 B, std430
        float position[3];  float inv_range;     // omni/spot world pos; 1/range (0 = directional)
        float direction[3]; float type;          // light→scene dir; 0 dir, 1 omni, 2 spot
        float color[3];     float spot_cos_in;   // linear color × energy; spot inner-cone cos
        float spot_cos_out; float _p0, _p1, _p2;  // spot outer-cone cos
    };

    // Camera UBO — both directions of the transform so probe passes can go
    // screen->world (inverse) and world->screen (forward) for reprojection.
    struct RCCameraData
    {
        float inv_proj[16];
        float inv_view[16];
        float fwd_proj[16];  // NEW
        float fwd_view[16];  // NEW
        float jitter[2];
        float _pad[2];
    };

    // rc_irradiance_atrous.glsl — edge-aware a-trous denoise (step = hole size, doubles per pass).
    struct alignas(16) RCAtrousPC
    {
        uint32_t half_w, half_h; int32_t step; uint32_t _p0;
        float    z_near, z_far, sigma_z, normal_pow;          // 32 B
    };

    // rc_dyn_occ_temporal.glsl — temporally accumulate the dynamic occupancy grid.
    struct alignas(16) RCDynOccTemporalPC { uint32_t res; float decay; uint32_t _p0, _p1; };

    // One cascade's geometry + buffer offsets. Uploaded once to _cascade_buf and
    // shared by every patch shader (bound at b7); the GLSL mirror must match.
    struct CascadeDesc
    {                      // matches the GLSL struct EXACTLY (48 B, std430-tight)
        float    spacing, t_start, t_end, aperture;   // probe spacing; ray interval [t_start,t_end); cone aperture
        uint32_t dirs, oct_res, bucket_off, bucket_cap;   // dirs = oct_res^2; hash region [bucket_off, +bucket_cap)
        uint32_t probe_off, probe_cap, rad_off, _p0;      // parallel slot arrays; radiance region base = rad_off
    };

    // Node used as the compute backend of a CompositorEffect. Lives in group
    // "radiance_cascade"; the host effect forwards depth/normal/color + camera here.
    class CRadianceCascade : public Node
    {
        GDCLASS(CRadianceCascade, Node)

    public:
        // ── Constants ────────────────────────────────────────────────────────
        static const uint32_t RC_CASCADES = 5;   // tune; 4–6 reasonable
        static const int MAX_DYN_TRIS = 8192;
        static const uint32_t MAX_STATIC_TRIS = 1u << 22;   // fixed _tri_buffer cap (window is far below)
        static const uint32_t FLOATS_PER_TRI = 20;         // 5×vec4: a,b,c,emission,albedo
        static constexpr float CHUNK_SIZE = 16.0f;        // world metres per chunk edge
        static const int  MAX_SUB0_CHUNKS = 16384;          // cached-subdivision budget (eviction trigger)
        static const int MAX_CLIP = 5;                     // voxel levels: index 0 = fine grid, 1..4 = coarse rings
        static const int MAX_LIGHTS = 256;
        static const int CLIP_ANISO_M = 4;                 // level-0 aniso depth → snap = 16 vox = 4 m

        // ── Nested types ─────────────────────────────────────────────────────
        // A static-geometry chunk and the state of its cached subdivision (sub0).
        // raw tris are subdivided once into sub0; workers flip the state machine.
        enum class Sub0State : uint8_t { Unbuilt = 0, Building = 1, Built = 2 };
        struct Chunk
        {
            std::vector<SrcTri> raw;        // INVARIANT: immutable once generated (see Notes)
            std::vector<float>  sub0;       // written once by the worker that claims Building
            std::atomic<uint8_t> sub0_state{ (uint8_t) Sub0State::Unbuilt };
        };

        // Which intermediate buffer dispatch() visualises (set_debug_view).
        enum DebugView { DEBUG_OFF = 0, DEBUG_PATCHES = 1, DEBUG_PATCHES_RADIANCE = 2, DEBUG_PROBE_TRACE = 3, DEBUG_GATHER = 4, DEBUG_VOXEL = 5 };

        // ── Godot lifecycle ──────────────────────────────────────────────────
        void _ready()   override;
        void _process(double delta) override;
        void _physics_process(double delta) override;   // main-thread driver: occluders + voxel-region recenter
        void _notification(int p_what);

        // ── Main entry point + outputs ───────────────────────────────────────
        // Called once per frame by the host effect with the compositor's depth,
        // normal-roughness and (lit) color buffers; runs the whole GI pipeline.
        void dispatch(RID p_depth, RID p_normalRoughness, RID p_color, Vector2i p_size);
        RID  get_debug_texture() const { return _debug_tex; }
        RID  get_irradiance_texture() const { return _irradiance_tex; }   // the public GI output

        // ── Camera ───────────────────────────────────────────────────────────
        void set_camera_params(float z_near, float z_far) { _z_near = z_near; _z_far = z_far; }
        void set_camera_matrices(const Projection& p_proj, const Transform3D& p_view)
        {
            _pending_proj = p_proj;
            _pending_view = p_view;
            _camera_dirty = true;   // UBO is rebuilt next dispatch()
        }

        // ── Lighting (sun / sky) ─────────────────────────────────────────────
        void set_sun(Vector3 dir, Color color, float energy)
        {
            _sun_dir = dir.normalized();
            Color l = color.srgb_to_linear();
            _sun_color = Vector3(l.r, l.g, l.b) * energy;
            for (int L = 0; L < MAX_CLIP; ++L) _level_dirty[L] = true;
            _voxel_dirty = true;       // re-inject (cheap; no re-voxelize needed)
        }
        void set_sky(Color color, float energy)
        {
            Color l = color.srgb_to_linear();
            _sky_color = Vector3(l.r, l.g, l.b) * energy;   // ambient where a ray escapes the scene
        }

        // ── Cascade tuning (exported properties) ─────────────────────────────
        // dist/step/scale knobs reshape the cascade table; consumed at the top of
        // the next dispatch() (never mid-list, so the table stays self-consistent).
        void set_dist_mult(float v)
        {
            _dist_mult = MAX(v, 0.05f);   // guard against 0 spacing
            _cascade_dirty = true;        // rebuilt at top of dispatch() — never mid-list
        }
        float get_dist_mult() const { return _dist_mult; }

        void set_step_mult(float v)
        {
            _step_mult = MAX(v, 0.05f);
            _cascade_dirty = true;
        }
        float get_step_mult() const { return _step_mult; }

        void set_scale_mult(float v)
        {
            _cascade_scale = MAX(v, 1.05f);   // <=1 would never grow / would shrink → degenerate
            _cascade_dirty = true;            // rebuilt at top of dispatch(), same as dist/step
        }
        float get_scale_mult() const { return _cascade_scale; }

        void set_interval_overlap(float v)
        {
            _interval_overlap = CLAMP(v, 0.0f, 0.5f);   // >0.5 would double-count more than half the band
            _cascade_dirty = true;                      // rebuilt at top of dispatch(), same as dist/step
        }
        float get_interval_overlap() const { return _interval_overlap; }

        void set_local_transmittance(bool v) { _local_transmittance = v; }   // per-frame PC; no table rebuild
        bool get_local_transmittance() const { return _local_transmittance; }

        // Temporal direction amortization: spread a full directional refresh over N frames. N=1 = off
        // (trace every direction every frame). Higher = cheaper trace, more temporal latency. Per-frame
        // trace PC; no table rebuild. Slot-keyed probes + persisted radiance make this safe (see rc_patch_add).
        void set_trace_amortization(int v) { _trace_amortization = (uint32_t) CLAMP(v, 1, 64); }
        int  get_trace_amortization() const { return (int) _trace_amortization; }

        void set_probe_seed_max_h(int v) { _probe_seed_max_h = MAX(v, 64); }   // coarse-cascade seed lattice height
        int  get_probe_seed_max_h() const { return _probe_seed_max_h; }

        void set_gi_intensity(float new_gi_intensity) { _gi_intensity = new_gi_intensity; }
        float get_gi_intensity() { return _gi_intensity; }

        // ── Upsampler tuning (exported properties) ───────────────────────────
        // Bilateral weights for the half->full upsample and the a-trous denoise:
        // sigma_z = depth sensitivity, normal_pow = normal sensitivity.
        void set_upsampler_sigma_z(float v)
        {
            sigma_z = MAX(v, 0.05f);
        }
        float get_upsampler_sigma_z() const { return sigma_z; }

        void set_upsampler_normal_pow(float v)
        {
            normal_pow = MAX(v, 0.05f);
        }
        float get_upsampler_normal_pow() const { return normal_pow; }

        // ── Trace tuning ─────────────────────────────────────────────────────
        void set_trace_max_steps(int v) { _trace_max_steps = v; }   // cone-trace step budget per ray
        int get_trace_max_steps() { return _trace_max_steps; }

        // ── Voxel grid config ────────────────────────────────────────────────
        float get_voxel_size() const { return _vox_extent.x / float(_vox_res); }
        void set_voxel_resolution(int r) { int n = CLAMP(r, 16, 512); if (n != _vox_res) { _vox_res = n; _needs_reinit = true; } }
        int  get_voxel_resolution() const { return _vox_res; }
        // Recenter the grids on the player each frame. Level 0 snaps to a 2^M
        // multiple (toroidal slab update); coarse levels snap to a voxel. Movement
        // past _recenter_margin_frac of the extent kicks an async recenter job.
        void set_voxel_region(Vector3 player_pos)
        {
            if (!_initialized) return;
            _prefetch_sub0_ahead(player_pos);
            // LEVEL 0 — toroidal, snap to 2^M, slab update
            {
                LevelGeom g = _level_geom(0);
                float vs = g.voxel_size, half = g.extent * 0.5f;
                Vector3 center = _vox_origin + Vector3(half, half, half);   // benign cross-thread read; see Hazards
                if ((player_pos - center).length() > g.extent * _recenter_margin_frac)
                {
                    if (_slab_job[0].active) return;        // one job at a time — supersede policy (see Hazards)

                    float snap = vs * float(1 << CLIP_ANISO_M);
                    Vector3 c = player_pos - Vector3(half, half, half);
                    Vector3 snapped(Math::floor(c.x / snap) * snap,
                        Math::floor(c.y / snap) * snap,
                        Math::floor(c.z / snap) * snap);
                    if (snapped != _vox_origin)
                        _enqueue_recenter(0, _vox_origin, snapped);   // builds SlabJob, kicks worker; NO origin advance, NO _rd
                }
            }
            // COARSE — toroidal cells but full-rebuild on dirty (snap to 1 voxel; no aniso/SDF constraint)
            for (int L = 1; L < _clip_levels; ++L)
            {
                LevelGeom g = _level_geom(L);
                float vs = g.voxel_size, half = g.extent * 0.5f;
                Vector3 center = _clip_origin[L] + Vector3(half, half, half);
                if ((player_pos - center).length() > g.extent * _recenter_margin_frac)
                {
                    if (_slab_job[L].active) continue;                     // one job per level in flight
                    Vector3 c = player_pos - Vector3(half, half, half);
                    Vector3 snapped(Math::floor(c.x / vs) * vs, Math::floor(c.y / vs) * vs, Math::floor(c.z / vs) * vs);
                    if (snapped != _clip_origin[L])
                        _enqueue_recenter(L, _clip_origin[L], snapped);    // async; origin advances in the commit
                }
            }
        }

        // ── Bake / relight triggers ──────────────────────────────────────────
        void bake_now() { for (int L = 0; L < MAX_CLIP; ++L) _level_dirty[L] = true; _voxel_dirty = true; }          // re-voxelize + re-inject all levels
        void request_revoxelize() { for (int L = 0; L < MAX_CLIP; ++L) _level_dirty[L] = true; _voxel_dirty = true; }
        void  request_relight() { _relight_frames = _relight_frames_max; }   // re-inject light only (faded), no re-voxelize

        // ── Static geometry API ──────────────────────────────────────────────
        void clear_static_geometry() { _chunks.clear(); }
        void add_static_surface(PackedVector3Array verts, PackedInt32Array idx,
            Transform3D xform, Color emission, Color albedo);
        void bake_static_geometry();           // packs accumulated tris → _tri_buffer, marks dirty
        void scan_static_geometry(Node* root);       // clear + add whole scene (existing entry point)

        // ── Dynamic geometry API ─────────────────────────────────────────────
        // Low level: clear, add proxy surfaces, then upload. update_dynamic_occluders()
        // below drives these automatically from the cached occluder list each frame.
        void clear_dynamic();
        void add_dynamic_surface(PackedVector3Array verts, PackedInt32Array indices, Transform3D xform);
        void update_dynamic();
        // Scan the scene for dynamic-body collision shapes whose visual is GI-disabled
        // and cache them as occluders. Called once from _physics_process; expose so the
        // game can re-scan after spawning/despawning physics bodies.
        void scan_dynamic_occluders(Node* root);
        void set_voxel_update_skip_frames(int v) { _voxel_update_skip_frames = MAX(v, 0); }
        int  get_voxel_update_skip_frames() const { return _voxel_update_skip_frames; }

        // ── Debug controls (exported) ────────────────────────────────────────
        void set_debug_view(int v) { _debug_view = v; }
        int get_debug_view() { return _debug_view; }
        void set_debug_cascade(uint32_t v) { _debug_cascade = v; }
        uint32_t  get_debug_cascade() { return _debug_cascade; }
        void set_debug_clip_level(int L) { _debug_clip_level = L; }
        int get_debug_clip_level() { return _debug_clip_level; }

    protected:
        static void _bind_methods();

    private:
        ~CRadianceCascade();

        // ── Private nested types ─────────────────────────────────────────────
        // An async recenter unit: the worker fills each Slab's tris off-thread,
        // then the render thread uploads + voxelizes them and advances the origin.
        struct SlabJob
        {
            struct Slab { Vector3i lo, hi, dim; std::vector<float> tris; PackedByteArray tris_bytes; uint32_t tri_count = 0; };
            int       level = 0;             // 0 = L0 (_vox_origin), 1..N = coarse ring (_clip_origin[L])
            bool      active = false;
            Vector3   new_origin;
            Vector3i  new_phase;
            bool      full_rebuild = false;  // teleport: rebuild the whole level instead of a shell
            std::vector<Slab> slabs;
            int64_t   task = -1;             // WorkerThreadPool task id (-1 = none)
        };

        // ── Init / teardown ──────────────────────────────────────────────────
        void _init_pipelines(Vector2i screen_size);   // create all textures/buffers/shaders for a given screen size
        void _build_static_sets();           // called once at end of _init_pipelines
        void _rebuild_per_frame_sets(RID depth, RID normal_rough, RID color);  // called once per frame in dispatch()
        void _free_rids();                   // cleanup on reinit (resize) and on delete

        // ── Camera / cascade table ───────────────────────────────────────────
        void _update_camera_ubo(const Projection& proj, const Transform3D& view);
        void _build_cascade_table();         // fill _cascades + sizes from the tuning knobs; upload _cascade_buf

        // ── Per-pipeline dispatch ────────────────────────────────────────────
        // Each wraps one compute pass; the per-frame order lives in dispatch().
        void _dispatch_composite();          // scene + irradiance*albedo*intensity -> output
        void _dispatch_patch_clear();        // reset probe buckets + counters
        void _dispatch_patch_add();          // allocate probes from screen pixels (depth-reprojected)
        void _dispatch_patch_trace();        // each probe cone-traces the voxel grid
        void _dispatch_patch_merge();        // merge cascades far->near
        void _dispatch_patch_gather();       // c0 probes -> half-res irradiance
        void _dispatch_patch_lookup(uint32_t debug_kind);   // debug visualization only
        void _dispatch_voxel_mips();         // isotropic + anisotropic radiance mips
        void _dispatch_emission_mips();      // emission mips (for emissive bounce)
        void _dispatch_voxel_debug();        // raymarch a grid level to _debug_tex
        void _dispatch_dynamic_voxelize();   // rasterize dynamic proxies into _dyn_occ
        void _dispatch_dyn_occ_temporal();   // accumulate _dyn_occ over time
        void _dispatch_irradiance_atrous(RID depth_tex, RID normal_tex);     // half-res denoise
        void _dispatch_irradiance_upsample(RID depth_tex, RID normal_tex);   // half -> full bilateral
        void _dispatch_slab_clear(Vector3i lo, Vector3i dim, RID set = RID());           // clear a grid shell
        void _dispatch_voxelize(Vector3i lo, Vector3i dim, const Vector3& extent, RID set);   // rasterize _tri_buffer into a shell
        void _dispatch_slab_inject(Vector3i lo, Vector3i dim, float blend_alpha);        // inject light into a L0 shell
        void _dispatch_clip_slab_inject(int L, Vector3i lo, Vector3i dim, float blend_alpha = 1.0f);   // inject into a coarse shell
        void _dispatch_relight(float alpha);        // re-inject L0 with fade alpha (light changed, geometry didn't)
        void _dispatch_clip_relight(float alpha);   // same for coarse levels (sun rotation)

        // ── Voxel baking / clipmap ───────────────────────────────────────────
        void _bake_voxels();                 // consume dirty flags: (re)voxelize + inject + mips + SDF as needed
        void _full_revoxelize();             // level-0 full rebuild (first bake / geometry change / teleport)
        void _bake_coarse_level(int L);      // coarse level full rebuild
        void _update_trace_params();         // refresh _trace_params_ubo + _clip_params_ubo from current origins
        void _rebuild_level_tris(int L);     // clip chunk tris to level L's window → _tri_buffer
        // Geometry of grid level L: voxel size, world extent, snapped origin.
        LevelGeom _level_geom(int L) const
        {
            float vs = (_vox_extent.x / float(_vox_res)) * float(1 << L);   // each level doubles the voxel size
            float ext = float(_vox_res) * vs;
            Vector3 o = (L == 0) ? _vox_origin : _clip_origin[L];
            return { vs, ext, o };
        }
        // Seed the coarse-level origins once, centred on the camera.
        void _init_clip_origins(const Vector3& cam)
        {
            float base = _vox_extent.x / float(_vox_res);
            for (int L = 1; L < MAX_CLIP; ++L)
            {
                float vs = base * float(1 << L);
                float ext = float(_vox_res) * vs;
                _clip_origin[L] = ((cam - Vector3(ext, ext, ext) * 0.5f) / vs).floor() * vs;
            }
        }

        // ── SDF flood ────────────────────────────────────────────────────────
        // Jump-flood distance field over the level-0 occupancy; lets the cone
        // trace skip empty space. Built amortized across frames after a bake.
        void _build_sdf();                   // full synchronous flood (teleport / first bake)
        void _sdf_amortize_begin();          // arm the amortized rebuild
        void _sdf_amortize_step();           // advance ~2 flood passes per frame

        // ── Async slab recenter ──────────────────────────────────────────────
        void _enqueue_recenter(int L, const Vector3& o_old, const Vector3& o_new);  // build the SlabJob, kick the worker
        void _slab_job_worker(int L);             // worker entry — pure CPU, NO _rd
        void _poll_slab_job();               // render-thread commit — ALL _rd lives here

        // ── Chunk system (static geometry storage + subdivision) ─────────────
        // Static tris are bucketed into CHUNK_SIZE cells; each chunk's tris are
        // subdivided (capped edge length) once into a "sub0" cache on workers,
        // prefetched ahead of camera motion and evicted when over budget.
        void _scan_into_chunks(Node* root);  // walk a subtree, add MeshInstance3D surfaces to chunks
        void _emit_tri(std::vector<float>& out, const Vector3& a, const Vector3& b, const Vector3& c,
            const Color& e, const Color& albedo, float max_edge,
            const Vector3& gmin, const Vector3& gmax);   // recursively split a tri to <= max_edge, clipped to [gmin,gmax]
        void _emit_chunk_into(const Chunk& ch, int cx, int cy, int cz, std::vector<float>& out); // shared subdivision
        void _evict_sub0(const Vector3& l0_origin, float l0_extent);   // drop far sub0 caches when over budget
        void _kick_sub0_build(int64_t key);                 // claim Unbuilt→Building, enqueue one chunk's build
        void _build_sub0_task(int64_t key);                 // worker: subdivide raw → sub0, publish Built
        void _prefetch_sub0_ahead(const Vector3& player_pos);  // per-frame lookahead kick
        Color _material_albedo_linear(const Ref<BaseMaterial3D>& sm);   // 1px-averaged albedo (cached per texture)
        // Pack/unpack a chunk coordinate into one int64 map key (3×21-bit, sign-offset).
        static int64_t _chunk_key(int cx, int cy, int cz)
        {        // pack 3×21-bit (offset for sign)
            auto u = [] (int v) { return (int64_t) (uint32_t) (v + (1 << 20)) & 0x1FFFFF; };
            return u(cx) | (u(cy) << 21) | (u(cz) << 42);
        }
        static int _chunk_coord(float w) { return (int) Math::floor(w / CHUNK_SIZE); }   // world -> chunk index
        static void _decode_chunk_key(int64_t k, int& cx, int& cy, int& cz);

        // ── Lights ───────────────────────────────────────────────────────────
        // Scan Light3D nodes into _light_buf; the sun is the first directional.
        // Static lights bake once; dynamic lights are polled for movement.
        bool _light_to_gpu(Light3D* light, RCLightGPU& g);   // false = skip (bake-disabled / unsupported)
        void _scan_lights_into(Node* node, std::vector<RCLightGPU>& out, std::vector<uint8_t>& dyn);
        void _ensure_fallback_light();       // if no lights, push the default sun so the scene isn't black
        void _upload_light_buffer();
        void _poll_dynamic_lights();         // detect added/removed/moved lights -> relight
        void _upload_lights(Node* root);     // full (re)scan + upload
        void _sync_sun_from_lights();        // copy first directional into _sun_dir/_sun_color

        // ── Per-frame drivers (main thread) ──────────────────────────────────
        void update_dynamic_occluders();    // feed cached occluders' shadow meshes into the dynamic API
        Camera3D* _find_camera();           // the viewport's active Camera3D
        Node3D* _find_player();             // camera's CharacterBody3D parent, else the camera itself

        // ── Debug ────────────────────────────────────────────────────────────
        void _debug_print_probe_counts();   // blocking readback of _patch_alloc, gated

        // ════════════════════════════ MEMBER DATA ════════════════════════════

        // ── Core / RenderingDevice ──
        RenderingDevice* _rd = nullptr;      // Godot's local RD; ALL GPU work goes through it
        Vector2i         _screen_size;
        Vector2i         _half_size;         // gather/denoise/upsample run at half-res
        bool             _initialized = false;
        bool             _needs_reinit = false;   // set on resolution/resolution-config change
        bool             _gpu_profile = false;   // flip true for a capture session
        bool             _camera_is_set = false;

        // ── Camera ──
        RID         _camera_ubo;             // RCCameraData (inverse + forward view/proj)
        Camera3D*   _current_active_camera = nullptr;;
        Projection  _pending_proj;
        Transform3D _pending_view;
        bool        _camera_dirty = false;
        float       _z_near = 0.05f;
        float       _z_far = 4000.0f;

        // ── Cascade table ──
        CascadeDesc _cascades[RC_CASCADES];
        uint32_t    _total_buckets = 0, _total_probes = 0, _total_rad = 0;   // summed buffer sizes across cascades
        RID         _cascade_buf;                 // SSBO holding the table, bound at b7 (and b2 for indirect)
        float       _dist_mult = 1.0f;            // Sannikov "Dist mult": global probe-spacing scale
        float       _step_mult = 1.0f;            // Sannikov "Step mult": global cascade interval/reach scale
        float       _cascade_scale = 2.0f;        // per-cascade geometric ratio for spacing AND interval.
        // 2.0 = canonical (current). <2 = gentler ramp (Sannikov).
        float       _interval_overlap = 0.10f;    // fraction each cascade's near cone overruns its seam,
        // so the origin-true near cone (not the offset coarse probes) owns occlusion across the interval
        // cut. Cheap partial bilinear-fix; stacks with the cone footprint. 0 = exact tiling (old behavior).
        bool        _local_transmittance = true;  // true = interval-local trace (correct merge chain; the
        // pre-roll's T[0,t_start] attenuation is supplied once by finer cascades). false = legacy
        // from-origin pre-roll, which double-darkens far cascades in enclosed scenes. Per-frame trace PC.
        bool        _cascade_dirty = false;       // set by the mult setters, consumed at top of dispatch()
        int         _probe_seed_max_h = 1080;
        uint32_t    _trace_amortization = 1u;      // N: full directional refresh spread over N frames (1 = off).
        // Higher = cheaper trace, more temporal latency. Per-frame trace PC; safe because probes are slot-keyed
        // and probe_radiance persists (rc_patch_add tags each slot's owner so changed slots refresh in full).
        uint32_t    _frame_index = 0u;             // ++ once per trace dispatch; drives the amortization rotation

        // ── Patch build ──
        // Sparse probe store. patch_add hashes screen-reprojected world points
        // into _patch_buckets per cascade; the parallel _patch_* SSBOs hold each
        // slot's key / world pos / directional radiance. Shaders share the cascade
        // table at b7; _patch_indirect_buf feeds indirect dispatch of trace/merge.
        RID _patch_clear_shader, _patch_clear_pipeline, _patch_clear_set0;
        RID _patch_add_shader, _patch_add_pipeline, _patch_add_set0;
        RID _patch_lookup_shader, _patch_lookup_pipeline, _patch_lookup_set0;
        RID _patch_buckets, _patch_alloc, _patch_keys, _patch_world, _patch_live;   // alloc = per-cascade live counters; live = compact live-slot list
        RID _patch_indirect_shader, _patch_indirect_pipeline, _patch_indirect_set0;
        RID _patch_trace_shader, _patch_trace_pipeline, _patch_trace_set0;
        RID _patch_add_set1, _patch_lookup_set1;                       // depth b0 + normal b1, shared by add/lookup
        RID _probe_radiance, _patch_indirect_buf, _voxel_linear_sampler;
        RID _probe_rad_tag;   // per-slot owner hash (uint/slot), persisted across frames — NOT cleared — for temporal amortization
        RID _patch_gather_shader, _patch_gather_pipeline, _patch_gather_set0;
        RID _patch_merge_shader, _patch_merge_pipeline, _patch_merge_set0;
        RID _patch_reduce_shader, _patch_reduce_pipeline, _patch_reduce_set0;   // angular pre-reduce before a merge with ratio>1
        RID _reduced_radiance;     // angular pre-reduce scratch (uvec2 per (slot,dir)), reused per fold

        // ── Voxel grid (level 0) ──
        // Camera-centred radiance grid: the trace target. Toroidal addressing —
        // _vox_origin scrolls and _vox_phase = origin voxel mod res, so a recenter
        // only rewrites the exposed shell. albedo/normal/emission feed the inject.
        RID _voxel_tex, _voxel_sampler;
        RID _voxel_albedo;   // rgba8, same res as _voxel_tex
        RID _voxel_normal;   // rgba8
        RID _voxel_emission; // rgba16f, res³, full mips — isotropic emission (rgb), .a occ
        int      _vox_res = 256;                       // 256 for quality
        Vector3  _vox_origin = Vector3(-32, -2, -32);
        Vector3  _vox_extent = Vector3(64, 64, 64);
        Vector3i _vox_phase;                           // origin_voxel % res (SDF + inject)
        bool     _voxel_dirty = true;
        float    _voxel_tri_cap = 2.0f;   // max triangle edge after the one-time cap, world metres.
        // Bounds the GPU per-thread AABB loop; used for ALL levels.
        float    _recenter_margin_frac = 0.125f;       // recenter when camera > frac*extent from centre

        // ── Voxel mips (anisotropic + emission) ──
        // Cone tracing samples the 6-axis anisotropic mips (direction-dependent)
        // to limit light leaking; emission mips carry emissive bounce up the chain.
        // (The old isotropic mean-mip pass — rc3d_voxel_mip — was removed; the aniso mip
        //  replaced it and the trace only ever reads grid mip 0 + the aniso/emission chains.)
        int _vox_mip_levels = 1;
        RID _voxel_aniso[6];                      // rgba16f, res/2³, mips 0..N-1  (= grid mips 1..N)
        std::vector<RID> _aniso_views[6];         // per-level views, one array per direction
        RID _mip_aniso_shader, _mip_aniso_pipeline;
        int _aniso_levels = 0;                    // = _vox_mip_levels - 1
        std::vector<RID> _emis_mip_views;    // per-level views for the emission mip pass
        RID _emis_mip_shader, _emis_mip_pipeline;

        // ── Voxelize (static tris → grid) ──
        // _tri_buffer is the GPU scratch the voxelize pass reads; CPU packs the
        // current level's window of chunk tris into _tri_accum then uploads it.
        RID _voxelize_shader, _voxelize_pipeline, _voxelize_set0, _tri_buffer;
        std::vector<float> _tri_accum;
        uint32_t _tri_count = 0;
        RID _slab_clear_shader, _slab_clear_pipeline, _slab_clear_set;

        // ── Trace backend / voxel debug ──
        // Descriptor set 2 binds everything the trace reads: grid, aniso mips,
        // SDF, dynamic occupancy and the coarse clip levels (see _build_static_sets).
        RID _trace_params_ubo, _trace_voxel_set2;    // descriptor set 2 = trace backend
        int _trace_max_steps = 64;
        RID _voxel_debug_shader, _voxel_debug_pipeline, _voxel_debug_set0;
        RID _voxel_debug_clip_set[MAX_CLIP];         // debug set bound to each coarse grid

        // ── Inject (direct light → voxels) ──
        // Writes sun + lights into voxel radiance, shadowed by the SDF; this is
        // the "direct" term the probes then gather as one-bounce indirect.
        RID _inject_pipeline, _inject_shader;
        RID _inject_set0;

        // ── SDF march ──
        RID _sdf_tex;                          // r16f, res³ — distance to occluder (voxels)
        RID _sdf_seed_a, _sdf_seed_b;          // rgba16f ping-pong for the flood
        RID _sdf_shader, _sdf_pipeline;
        RID _sdf_set_writeA, _sdf_set_writeB;  // ping-pong sets (seed_out = a / b)
        int  _sdf_pass = -1;                   // -1 idle; else amortized flood step
        bool _sdf_seed_in_a = true;            // ping-pong tracker across frames

        // ── Voxel clipmap (coarse levels) ──
        // Concentric rings around level 0; each level doubles the voxel size to
        // extend trace range cheaply. Level 0 lives in _voxel_tex; [1..4] here.
        bool _clip_origins_inited = false;
        bool _level_dirty[MAX_CLIP] = { true, true, true, true, true };
        int  _clip_levels = 5;                       // 1 = validation anchor; 5 = production
        RID  _clip_grid[MAX_CLIP];                   // [0] unused (level 0 is _voxel_tex); [1..4] coarse
        RID  _clip_voxelize_set[MAX_CLIP];           // per-coarse-level voxelize dest set
        RID  _clip_inject_set[MAX_CLIP];             // per-coarse-level inject dest set
        Vector3 _clip_origin[MAX_CLIP];              // per-level snapped origin
        RID  _clip_params_ubo;                       // ClipParams (binding 14)
        RID  _dummy_clip_tex;                        // 1³ black, fills unused bindings 10..13
        RID _clip_albedo, _clip_normal, _clip_emission;   // shared coarse-voxelize scratch attrs
        RID _clip_inject_shader, _clip_inject_pipeline;
        RID  _clip_slab_clear_set[MAX_CLIP];   // per-coarse-level shell clear (clip grid + coarse scratch)

        // ── Dynamic occupancy ──
        // Moving objects rasterized each frame into _dyn_occ, then temporally
        // accumulated into _dyn_occ_acc and added as occluders during the trace.
        RID _dyn_occ;                 // r8 dynamic occupancy, SAME res/origin/voxel_size/extent as static
        RID _dyn_tri_buffer;
        RID _dyn_voxelize_shader, _dyn_voxelize_pipeline, _dyn_voxelize_set0;
        std::vector<float> _dyn_tri_accum;     // 12 floats/tri: v0.xyz_,v1.xyz_,v2.xyz_
        uint32_t _dyn_tri_count = 0;
        RID   _dyn_occ_acc;
        RID   _dyn_occ_temporal_shader, _dyn_occ_temporal_pipeline, _dyn_occ_temporal_set0;
        float _dyn_occ_decay = 0.95f;   // per-frame retention: higher = smoother + longer trail
        std::vector<uint64_t> _dyn_occluder_ids; // cached dynamic-body CollisionShape3D ids (occluders)
        bool _dyn_occluders_scanned = false;    // one-time scan guard (first _physics_process)
        int  _voxel_update_skip_frames = 7;     // recenter the grids every (N+1) physics frames
        int  _voxel_region_cnt = 0;             // throttle counter for the recenter cadence

        // ── Lights ──
        Node*                     _lights_root = nullptr;     // cached scene root for the per-frame poll
        RID                       _light_buf;          // storage buffer, MAX_LIGHTS * sizeof(RCLightGPU)
        uint32_t                  _light_count = 0;
        std::vector<RCLightGPU>   _lights_accum;
        std::vector<uint8_t>      _light_dynamic;             // parallel to _lights_accum; 1 = BAKE_DYNAMIC

        // ── Relight (fade + dynamic tracking) ──
        // A light/sun change re-injects with a smoothstep cross-fade so GI eases
        // in instead of popping; moving dynamic lights track via a constant-alpha EMA.
        int   _relight_frames = 0;          // >0 = fading; counts down
        int   _relight_frames_max = 10;     // fade length (frames)
        float _relight_alpha = 0.10f;       // (vestigial since the smoothstep schedule; left as-is)
        int   _relight_track_frames = 0;     // >0 = a dynamic light is moving / settling
        float _relight_track_alpha = 0.35f; // per-frame EMA toward fresh Lo (~3-frame lag; tunable)
        int   _relight_track_settle = 8;     // frames of EMA after the last move → converge (≈97% at 0.35)
        int   _clip_relight_frames = 0;   // >0 = coarse sun fading (rotation), counts down

        // ── Sun / sky ──
        Vector3 _sun_dir = Vector3(-0.3f, -0.8f, -0.5f).normalized();  // travel dir (sun→scene)
        Vector3 _sun_color = Vector3(1.0f, 0.95f, 0.85f) * 3.0f;
        Vector3 _sky_color = Vector3(0.10f, 0.13f, 0.18f);

        // ── Irradiance: half-res gather → à-trous → upsample ──
        // The output chain: probes -> half-res gather -> edge-aware denoise ->
        // bilateral upsample -> full-res _irradiance_tex (the public output).
        RID  _irradiance_tex;
        RID  _irradiance_half;                    // half-res gather target
        RID  _irradiance_half_b;                  // ping-pong scratch (half-res, == _irradiance_half)
        RID  _atrous_shader, _atrous_pipeline;
        RID  _atrous_set0_h2s, _atrous_set0_s2h;  // static ping-pong: half→scratch / scratch→half
        RID  _atrous_set1;                        // per-frame: depth + normal
        int  _atrous_passes = 4;                  // EVEN → result lands back in _irradiance_half
        //   2 = steps {1,2} (~12 px full-res); 4 = {1,2,4,8} (wider)
        RID  _upsample_shader, _upsample_pipeline;
        RID  _upsample_set0;            // static: half-irradiance + full-res output
        RID  _upsample_set1;            // per-frame: depth + normal (rebuilt each frame)
        RID  _upsample_set2;            // c0 probes, for a direct full-res gather on depth/normal edges
        RID  _point_sampler;            // NEAREST/clamp for depth/normal/half (reuse if you have one)
        float sigma_z = 0.05;
        float normal_pow = 8.0;

        // ── Composite ──
        // Final pass: scene + irradiance*albedo*gi_intensity. Albedo is a white
        // 1×1 placeholder for now, so the term is currently scene + irradiance.
        RID _composite_shader;
        RID _composite_pipeline;
        RID _composite_set0;
        RID _composite_set1;         // per-frame: scene color (sampler + image)
        RID _dummy_albedo_tex;       // 1x1 white, swap for real later
        RID _albedo_sampler;
        RID _color_sampler;
        RID _dummy_color_tex;        // 1x1 black, fallback for missing color
        float _gi_intensity = 1.0f;  // exported property

        // ── Shared scene-input samplers ──
        RID _depth_sampler;
        RID _normal_sampler;

        // ── Debug ──
        RID _debug_tex;
        int _debug_view = 0;
        uint32_t _debug_cascade = 0;
        int  _debug_clip_level = 0;                  // 0 = level 0 (_voxel_tex), 1..4 = _clip_grid[L]
        uint64_t _dbg_frame = 0;                     // frame counter for throttling the readback
        bool _dbg_probe_counts = false;              // flip true to enable (or bind to a key)

        // ── Chunk system / async build state ──
        std::unordered_map<int64_t, Chunk> _chunks;   // key = _chunk_key(cx,cy,cz)
        std::shared_mutex _chunks_mutex;     // shared = find/read structure; unique = insert
        Vector3 _prefetch_last_pos;          // for deriving travel direction in the prefetch sweep
        SlabJob _slab_job[MAX_CLIP];         // one per level
        HashMap<RID, Color> _albedo_avg_cache;                       // texture RID → linear average
    };

} // namespace godot
