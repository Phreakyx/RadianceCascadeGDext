#include "CRadianceCascade.h"
#include "compat.h"

using namespace godot;

// ── Godot lifecycle ───────────────────────────────────────────────────────────

CRadianceCascade::~CRadianceCascade()
{
    _free_rids();
}

void CRadianceCascade::_bind_methods()
{
    // Expose the GI controls + geometry/light entry points to Godot (GDScript and
    // the host CompositorEffect). Setters paired with ADD_PROPERTY show in the inspector.
    ClassDB::bind_method(D_METHOD("get_debug_texture"), &CRadianceCascade::get_debug_texture);
    ClassDB::bind_method(D_METHOD("get_irradiance_texture"), &CRadianceCascade::get_irradiance_texture);
    ClassDB::bind_method(D_METHOD("set_camera_params", "z_near", "z_far"), &CRadianceCascade::set_camera_params);
    ClassDB::bind_method(D_METHOD("dispatch", "depth_texture", "color_texture", "screen_size"), &CRadianceCascade::dispatch);
    ClassDB::bind_method(D_METHOD("set_camera_matrices", "projection", "camera_transform"), &CRadianceCascade::set_camera_matrices);

    ClassDB::bind_method(D_METHOD("set_gi_intensity", "v"), &CRadianceCascade::set_gi_intensity);
    ClassDB::bind_method(D_METHOD("get_gi_intensity"), &CRadianceCascade::get_gi_intensity);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "gi_intensity"), "set_gi_intensity", "get_gi_intensity");

    ClassDB::bind_method(D_METHOD("set_dist_mult", "v"), &CRadianceCascade::set_dist_mult);
    ClassDB::bind_method(D_METHOD("get_dist_mult"), &CRadianceCascade::get_dist_mult);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "dist_mult", PROPERTY_HINT_RANGE, "0.5,3.0,0.05"),
        "set_dist_mult", "get_dist_mult");

    ClassDB::bind_method(D_METHOD("set_step_mult", "v"), &CRadianceCascade::set_step_mult);
    ClassDB::bind_method(D_METHOD("get_step_mult"), &CRadianceCascade::get_step_mult);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "step_mult", PROPERTY_HINT_RANGE, "0.25,4.0,0.05"),
        "set_step_mult", "get_step_mult");

    ClassDB::bind_method(D_METHOD("set_scale_mult", "v"), &CRadianceCascade::set_scale_mult);
    ClassDB::bind_method(D_METHOD("get_scale_mult"), &CRadianceCascade::get_scale_mult);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "scale_mult", PROPERTY_HINT_RANGE, "1.05,2.5,0.01"),
        "set_scale_mult", "get_scale_mult");

    ClassDB::bind_method(D_METHOD("set_interval_overlap", "v"), &CRadianceCascade::set_interval_overlap);
    ClassDB::bind_method(D_METHOD("get_interval_overlap"), &CRadianceCascade::get_interval_overlap);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "interval_overlap", PROPERTY_HINT_RANGE, "0.0,0.5,0.01"),
        "set_interval_overlap", "get_interval_overlap");

    ClassDB::bind_method(D_METHOD("set_local_transmittance", "v"), &CRadianceCascade::set_local_transmittance);
    ClassDB::bind_method(D_METHOD("get_local_transmittance"), &CRadianceCascade::get_local_transmittance);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "local_transmittance"),
        "set_local_transmittance", "get_local_transmittance");

    ClassDB::bind_method(D_METHOD("set_probe_seed_max_h", "v"), &CRadianceCascade::set_probe_seed_max_h);
    ClassDB::bind_method(D_METHOD("get_probe_seed_max_h"), &CRadianceCascade::get_probe_seed_max_h);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "probe_seed_max_h", PROPERTY_HINT_RANGE, "360,2160,1"),
        "set_probe_seed_max_h", "get_probe_seed_max_h");

    ClassDB::bind_method(D_METHOD("set_upsampler_sigma_z", "v"), &CRadianceCascade::set_upsampler_sigma_z);
    ClassDB::bind_method(D_METHOD("get_upsampler_sigma_z"), &CRadianceCascade::get_upsampler_sigma_z);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "upsampler_sigma_z"), "set_upsampler_sigma_z", "get_upsampler_sigma_z");

    ClassDB::bind_method(D_METHOD("set_upsampler_normal_pow", "v"), &CRadianceCascade::set_upsampler_normal_pow);
    ClassDB::bind_method(D_METHOD("get_upsampler_normal_pow"), &CRadianceCascade::get_upsampler_normal_pow);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "upsampler_normal_pow"), "set_upsampler_normal_pow", "get_upsampler_normal_pow");

    ClassDB::bind_method(D_METHOD("bake_now"), &CRadianceCascade::bake_now);
    ClassDB::bind_method(D_METHOD("request_revoxelize"), &CRadianceCascade::request_revoxelize);
    ClassDB::bind_method(D_METHOD("set_voxel_region", "player_pos"), &CRadianceCascade::set_voxel_region);
    ClassDB::bind_method(D_METHOD("set_voxel_resolution", "r"), &CRadianceCascade::set_voxel_resolution);
    ClassDB::bind_method(D_METHOD("get_voxel_resolution"), &CRadianceCascade::get_voxel_resolution);

    ClassDB::bind_method(D_METHOD("set_debug_view", "v"), &CRadianceCascade::set_debug_view);
    ClassDB::bind_method(D_METHOD("get_debug_view"), &CRadianceCascade::get_debug_view);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "debug_view"), "set_debug_view", "get_debug_view");

    ClassDB::bind_method(D_METHOD("set_trace_max_steps", "v"), &CRadianceCascade::set_trace_max_steps);
    ClassDB::bind_method(D_METHOD("get_trace_max_steps"), &CRadianceCascade::get_trace_max_steps);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "trace_max_steps"), "set_trace_max_steps", "get_trace_max_steps");

    ClassDB::bind_method(D_METHOD("clear_static_geometry"), &CRadianceCascade::clear_static_geometry);
    ClassDB::bind_method(D_METHOD("bake_static_geometry"), &CRadianceCascade::bake_static_geometry);
    ClassDB::bind_method(D_METHOD("add_static_surface", "verts", "indices", "xform", "emission", "albedo"), &CRadianceCascade::add_static_surface);

    ClassDB::bind_method(D_METHOD("get_voxel_size"), &CRadianceCascade::get_voxel_size);

    ClassDB::bind_method(D_METHOD("set_debug_cascade", "v"), &CRadianceCascade::set_debug_cascade);
    ClassDB::bind_method(D_METHOD("get_debug_cascade"), &CRadianceCascade::get_debug_cascade);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "debug_cascade"), "set_debug_cascade", "get_debug_cascade");

    ClassDB::bind_method(D_METHOD("set_debug_clip_level", "v"), &CRadianceCascade::set_debug_clip_level);
    ClassDB::bind_method(D_METHOD("get_debug_clip_level"), &CRadianceCascade::get_debug_clip_level);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "debug_clip_level"), "set_debug_clip_level", "get_debug_clip_level");

    ClassDB::bind_method(D_METHOD("set_sun", "dir", "color", "energy"), &CRadianceCascade::set_sun);
    ClassDB::bind_method(D_METHOD("set_sky", "color", "energy"), &CRadianceCascade::set_sky);

    ClassDB::bind_method(D_METHOD("clear_dynamic"), &CRadianceCascade::clear_dynamic);
    ClassDB::bind_method(D_METHOD("add_dynamic_surface", "verts", "indices", "xform"), &CRadianceCascade::add_dynamic_surface);
    ClassDB::bind_method(D_METHOD("update_dynamic"), &CRadianceCascade::update_dynamic);
    ClassDB::bind_method(D_METHOD("scan_static_geometry", "root"), &CRadianceCascade::scan_static_geometry);

    ClassDB::bind_method(D_METHOD("scan_dynamic_occluders", "root"), &CRadianceCascade::scan_dynamic_occluders);
    ClassDB::bind_method(D_METHOD("set_voxel_update_skip_frames", "v"), &CRadianceCascade::set_voxel_update_skip_frames);
    ClassDB::bind_method(D_METHOD("get_voxel_update_skip_frames"), &CRadianceCascade::get_voxel_update_skip_frames);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "voxel_update_skip_frames"), "set_voxel_update_skip_frames", "get_voxel_update_skip_frames");
}

void CRadianceCascade::_ready()
{
    if (Engine::get_singleton()->is_editor_hint()) return;
    add_to_group("radiance_cascade");                                  // host effect finds us via this group
    _rd = RenderingServer::get_singleton()->get_rendering_device();
    set_process(true);                                                 // _process: feed the active camera each frame
    set_physics_process(true);                                         // _physics_process: occluders + region recenter
}

void CRadianceCascade::_process(double delta)
{
    if (Engine::get_singleton()->is_editor_hint()) return;

    _current_active_camera = _find_camera();

    if (!_camera_is_set)
    {
        // Auto-feed the active camera every rendered frame — no external camera script
        // needed. dispatch() (render thread) consumes these via the _camera_dirty flag.

        if (_current_active_camera)
        {
            set_camera_params(_current_active_camera->get_near(), _current_active_camera->get_far());

            _camera_is_set = true;
        }
    }

    if (_current_active_camera)
    {
        set_camera_matrices(_current_active_camera->get_camera_projection(), _current_active_camera->get_global_transform().inverse());  // world->view
    }
}

// Main-thread per-frame driver. dispatch() runs on the render thread, so anything
// that walks the scene tree must live here instead: feed the dynamic occluders and
// recenter the voxel grids on the player at a throttled cadence.
void CRadianceCascade::_physics_process(double delta)
{
    if (Engine::get_singleton()->is_editor_hint()) return;

    if (!_initialized) return;                       // wait until dispatch() built the GPU pipelines

    if (!_dyn_occluders_scanned)                     // one-time scan of the scene's dynamic occluders
    {
        SceneTree* tree = get_tree();
        scan_dynamic_occluders(tree ? tree->get_current_scene() : nullptr);
        _dyn_occluders_scanned = true;
    }

    update_dynamic_occluders();                      // refresh moving occluders every frame

    // Recenter the grids on the player every (_voxel_update_skip_frames + 1) frames.
    if (_voxel_region_cnt == 0)
    {
        if (Node3D* p = _find_player()) { set_voxel_region(p->get_global_position()); _voxel_region_cnt = 1; }
    }
    else
    {
        if (_voxel_region_cnt >= _voxel_update_skip_frames) _voxel_region_cnt = 0;
        else ++_voxel_region_cnt;
    }
}

void CRadianceCascade::_notification(int p_what)
{
    if (p_what == NOTIFICATION_PREDELETE && _rd != nullptr)
        _free_rids();                                                  // release all GPU resources before deletion
}


// ── Init / teardown ───────────────────────────────────────────────────────────

// ── Pipeline init ──────────────────────────────────────────────────────────
// Allocate every GPU resource for the current screen size in one shot: the
// voxel grid (+ its full mip chain, 6-axis anisotropic mips, emission mips and
// half-res SDF), the coarse clip levels, all patch/probe SSBOs, the half- and
// full-res irradiance textures, every sampler, and one compute shader+pipeline
// per pass. Runs on first dispatch() and after any resize / voxel-res change
// (dispatch() calls _free_rids() first so this always starts clean).
void CRadianceCascade::_init_pipelines(Vector2i screen_size)
{
    _rd = RenderingServer::get_singleton()->get_rendering_device();
    _screen_size = screen_size;
    _camera_ubo = _rd->uniform_buffer_create(sizeof(RCCameraData), PackedByteArray());
    if (_tri_buffer.is_valid()) _rd->free_rid(_tri_buffer);
    _tri_buffer = _rd->storage_buffer_create(MAX_STATIC_TRIS * FLOATS_PER_TRI * sizeof(float));
    _light_buf = _rd->storage_buffer_create(MAX_LIGHTS * sizeof(RCLightGPU));
    ERR_FAIL_COND_MSG(!_light_buf.is_valid(), "RC: light buffer failed");

    // ── Voxel grid (3D RC scene representation): rgba16f, emission.rgb + density.a ──
    {
        Ref<RDTextureFormat> fmt; fmt.instantiate();
        fmt->set_format(RenderingDevice::DATA_FORMAT_R16G16B16A16_SFLOAT);
        fmt->set_texture_type(RenderingDevice::TEXTURE_TYPE_3D);
        fmt->set_width(_vox_res); fmt->set_height(_vox_res); fmt->set_depth(_vox_res);
        fmt->set_array_layers(1);
        fmt->set_mipmaps(1 + (int) Math::floor((Math::log((double) _vox_res) / Math::log((double) 2.0))));   // was 1
        fmt->set_usage_bits(RenderingDevice::TEXTURE_USAGE_STORAGE_BIT |
            RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT |
            RenderingDevice::TEXTURE_USAGE_CAN_UPDATE_BIT |
            RenderingDevice::TEXTURE_USAGE_CAN_COPY_TO_BIT |
            RenderingDevice::TEXTURE_USAGE_CAN_COPY_FROM_BIT);
        Ref<RDTextureView> v; v.instantiate();
        _voxel_tex = _rd->texture_create(fmt, v, {});
        ERR_FAIL_COND_MSG(!_voxel_tex.is_valid(), "RC: voxel tex failed");

        Ref<RDTextureFormat> fmt8; fmt8.instantiate();
        fmt8->set_format(RenderingDevice::DATA_FORMAT_R8G8B8A8_UNORM);
        fmt8->set_texture_type(RenderingDevice::TEXTURE_TYPE_3D);
        fmt8->set_width(_vox_res);
        fmt8->set_height(_vox_res);
        fmt8->set_depth(_vox_res);
        fmt8->set_usage_bits(RenderingDevice::TEXTURE_USAGE_STORAGE_BIT | RenderingDevice::TEXTURE_USAGE_CAN_UPDATE_BIT);
        _voxel_albedo = _rd->texture_create(fmt8, v, {});
        _voxel_normal = _rd->texture_create(fmt8, v, {});
        ERR_FAIL_COND_MSG(!_voxel_albedo.is_valid(), "RC: voxel albedo failed");
        ERR_FAIL_COND_MSG(!_voxel_normal.is_valid(), "RC: voxel normal failed");

        // Dynamic occupancy grid — single channel, cleared + revoxelized each frame.
        {
            Ref<RDTextureFormat> fmtd; fmtd.instantiate();
            fmtd->set_format(RenderingDevice::DATA_FORMAT_R8_UNORM);
            fmtd->set_texture_type(RenderingDevice::TEXTURE_TYPE_3D);
            fmtd->set_width(_vox_res); fmtd->set_height(_vox_res); fmtd->set_depth(_vox_res);
            fmtd->set_usage_bits(RenderingDevice::TEXTURE_USAGE_STORAGE_BIT |
                RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT |
                RenderingDevice::TEXTURE_USAGE_CAN_COPY_TO_BIT);          // CAN_COPY_TO → texture_clear
            Ref<RDTextureView> vd; vd.instantiate();
            _dyn_occ = _rd->texture_create(fmtd, vd, {});
            ERR_FAIL_COND_MSG(!_dyn_occ.is_valid(), "RC: dyn occ grid failed");
        }

        {   // temporal accumulator — same format/res as _dyn_occ; persists across frames
            Ref<RDTextureFormat> fmta; fmta.instantiate();
            fmta->set_format(RenderingDevice::DATA_FORMAT_R8_UNORM);
            fmta->set_texture_type(RenderingDevice::TEXTURE_TYPE_3D);
            fmta->set_width(_vox_res); fmta->set_height(_vox_res); fmta->set_depth(_vox_res);
            fmta->set_usage_bits(RenderingDevice::TEXTURE_USAGE_STORAGE_BIT |
                RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT |
                RenderingDevice::TEXTURE_USAGE_CAN_COPY_TO_BIT);
            Ref<RDTextureView> va; va.instantiate();
            _dyn_occ_acc = _rd->texture_create(fmta, va, {});
            ERR_FAIL_COND_MSG(!_dyn_occ_acc.is_valid(), "RC: dyn occ acc failed");
            _rd->texture_clear(_dyn_occ_acc, Color(0, 0, 0, 0), 0, 1, 0, 1);   // start empty
        }

        // SDF distance field + its two jump-flood ping-pong seeds — all at HALF
        // the grid resolution (the field is low-frequency, so this quarters cost).
        {
            Ref<RDTextureFormat> fmt; fmt.instantiate();
            fmt->set_format(RenderingDevice::DATA_FORMAT_R16_SFLOAT);
            fmt->set_texture_type(RenderingDevice::TEXTURE_TYPE_3D);
            fmt->set_width(_vox_res / 2);
            fmt->set_height(_vox_res / 2);
            fmt->set_depth(_vox_res / 2);
            fmt->set_usage_bits(RenderingDevice::TEXTURE_USAGE_STORAGE_BIT |
                RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT |
                RenderingDevice::TEXTURE_USAGE_CAN_UPDATE_BIT |
                RenderingDevice::TEXTURE_USAGE_CAN_COPY_TO_BIT);
            Ref<RDTextureView> v; v.instantiate();
            _sdf_tex = _rd->texture_create(fmt, v, {});
            ERR_FAIL_COND_MSG(!_sdf_tex.is_valid(), "RC: voxel tex failed");
        }

        {
            Ref<RDTextureFormat> fmt; fmt.instantiate();
            fmt->set_format(RenderingDevice::DATA_FORMAT_R16G16B16A16_SFLOAT);
            fmt->set_texture_type(RenderingDevice::TEXTURE_TYPE_3D);
            fmt->set_width(_vox_res / 2);
            fmt->set_height(_vox_res / 2);
            fmt->set_depth(_vox_res / 2);
            fmt->set_usage_bits(RenderingDevice::TEXTURE_USAGE_STORAGE_BIT |
                RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT |
                RenderingDevice::TEXTURE_USAGE_CAN_UPDATE_BIT |
                RenderingDevice::TEXTURE_USAGE_CAN_COPY_TO_BIT);
            Ref<RDTextureView> v; v.instantiate();
            _sdf_seed_a = _rd->texture_create(fmt, v, {});
            ERR_FAIL_COND_MSG(!_sdf_seed_a.is_valid(), "RC: voxel tex failed");
        }
        {
            Ref<RDTextureFormat> fmt; fmt.instantiate();
            fmt->set_format(RenderingDevice::DATA_FORMAT_R16G16B16A16_SFLOAT);
            fmt->set_texture_type(RenderingDevice::TEXTURE_TYPE_3D);
            fmt->set_width(_vox_res / 2);
            fmt->set_height(_vox_res / 2);
            fmt->set_depth(_vox_res / 2);
            fmt->set_usage_bits(RenderingDevice::TEXTURE_USAGE_STORAGE_BIT |
                RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT |
                RenderingDevice::TEXTURE_USAGE_CAN_UPDATE_BIT |
                RenderingDevice::TEXTURE_USAGE_CAN_COPY_TO_BIT);
            Ref<RDTextureView> v; v.instantiate();
            _sdf_seed_b = _rd->texture_create(fmt, v, {});
            ERR_FAIL_COND_MSG(!_sdf_seed_b.is_valid(), "RC: voxel tex failed");
        }
    }

    // after _voxel_tex is created: mip-level count drives the aniso + emission mip chains.
    _vox_mip_levels = 1 + (int) Math::floor((Math::log((double) _vox_res) / Math::log((double) 2.0)));

    {   // anisotropic mips — six directional radiance mip stacks (±X,±Y,±Z).
        // Cone tracing samples the stack facing the cone, so occlusion is
        // direction-aware; this is what avoids the leaking plain isotropic mips cause.
        _aniso_levels = CLIP_ANISO_M;
        uint32_t ares = (uint32_t) (_vox_res >> 1);
        for (int dir = 0; dir < 6; ++dir)
        {
            Ref<RDTextureFormat> fmt; fmt.instantiate();
            fmt->set_format(RenderingDevice::DATA_FORMAT_R16G16B16A16_SFLOAT);
            fmt->set_texture_type(RenderingDevice::TEXTURE_TYPE_3D);
            fmt->set_width(ares); fmt->set_height(ares); fmt->set_depth(ares);
            fmt->set_array_layers(1);
            fmt->set_mipmaps(_aniso_levels);
            fmt->set_usage_bits(RenderingDevice::TEXTURE_USAGE_STORAGE_BIT |
                RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT);
            Ref<RDTextureView> v; v.instantiate();
            _voxel_aniso[dir] = _rd->texture_create(fmt, v, {});
            ERR_FAIL_COND_MSG(!_voxel_aniso[dir].is_valid(), vformat("RC: aniso tex %d failed", dir));

            _aniso_views[dir].clear();
            for (int L = 0; L < _aniso_levels; ++L)
            {
                Ref<RDTextureView> tv; tv.instantiate();
                RID view = _rd->texture_create_shared_from_slice(tv, _voxel_aniso[dir], 0, L, 1,
                    RenderingDevice::TEXTURE_SLICE_3D);
                ERR_FAIL_COND_MSG(!view.is_valid(), vformat("RC: aniso view %d/%d failed", dir, L));
                _aniso_views[dir].push_back(view);
            }
        }
    }

    {
        // Emission mips
        Ref<RDTextureFormat> fmt; fmt.instantiate();
        fmt->set_format(RenderingDevice::DATA_FORMAT_R16G16B16A16_SFLOAT);
        fmt->set_texture_type(RenderingDevice::TEXTURE_TYPE_3D);
        fmt->set_width(_vox_res); fmt->set_height(_vox_res); fmt->set_depth(_vox_res);
        fmt->set_array_layers(1);
        fmt->set_mipmaps(1 + (int) Math::floor((Math::log((double) _vox_res) / Math::log((double) 2.0))));
        fmt->set_usage_bits(RenderingDevice::TEXTURE_USAGE_STORAGE_BIT |
            RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT |
            RenderingDevice::TEXTURE_USAGE_CAN_UPDATE_BIT |
            RenderingDevice::TEXTURE_USAGE_CAN_COPY_TO_BIT);   // ← add (for texture_clear)
        Ref<RDTextureView> v; v.instantiate();
        _voxel_emission = _rd->texture_create(fmt, v, {});
        ERR_FAIL_COND_MSG(!_voxel_emission.is_valid(), "RC: voxel emission failed");

        _emis_mip_views.clear();
        for (int L = 0; L < _vox_mip_levels; ++L)
        {
            Ref<RDTextureView> tv; tv.instantiate();
            RID view = _rd->texture_create_shared_from_slice(tv, _voxel_emission, 0, L, 1,
                RenderingDevice::TEXTURE_SLICE_3D);
            ERR_FAIL_COND_MSG(!view.is_valid(), vformat("RC: emis mip view %d failed", L));
            _emis_mip_views.push_back(view);
        }
    }

    // 1³ black dummy for unused coarse bindings 10..13
    {
        Ref<RDTextureFormat> fmt; fmt.instantiate();
        fmt->set_format(RenderingDevice::DATA_FORMAT_R16G16B16A16_SFLOAT);
        fmt->set_texture_type(RenderingDevice::TEXTURE_TYPE_3D);
        fmt->set_width(1); fmt->set_height(1); fmt->set_depth(1);
        fmt->set_array_layers(1); fmt->set_mipmaps(1);
        fmt->set_usage_bits(RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT |
            RenderingDevice::TEXTURE_USAGE_STORAGE_BIT |
            RenderingDevice::TEXTURE_USAGE_CAN_UPDATE_BIT |
            RenderingDevice::TEXTURE_USAGE_CAN_COPY_TO_BIT);
        Ref<RDTextureView> v; v.instantiate();
        _dummy_clip_tex = _rd->texture_create(fmt, v, {});
        _rd->texture_clear(_dummy_clip_tex, Color(0, 0, 0, 0), 0, 1, 0, 1);
    }
    // ClipParams UBO — allocate now; _update_trace_params fills it later
    {
        struct alignas(16) GpuLevel { float origin[3]; float voxel_size; float extent[3]; float _pad; };
        struct alignas(16) GpuClip { GpuLevel lvl[5]; uint32_t num_levels; uint32_t _p[3]; };
        PackedByteArray z; z.resize(sizeof(GpuClip)); z.fill(0);
        _clip_params_ubo = _rd->uniform_buffer_create(sizeof(GpuClip), z);
    }

    // Coarse clipmap radiance grids (levels 1..N): same voxel count as level 0,
    // but 2^L larger voxels, so each ring covers 2^L more world for long-range tracing.
    for (int L = 1; L < MAX_CLIP; ++L)
    {
        Ref<RDTextureFormat> fmt; fmt.instantiate();
        fmt->set_format(RenderingDevice::DATA_FORMAT_R16G16B16A16_SFLOAT);
        fmt->set_texture_type(RenderingDevice::TEXTURE_TYPE_3D);
        fmt->set_width(_vox_res); fmt->set_height(_vox_res); fmt->set_depth(_vox_res);
        fmt->set_array_layers(1); fmt->set_mipmaps(1);
        fmt->set_usage_bits(RenderingDevice::TEXTURE_USAGE_STORAGE_BIT |
            RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT |
            RenderingDevice::TEXTURE_USAGE_CAN_UPDATE_BIT |
            RenderingDevice::TEXTURE_USAGE_CAN_COPY_TO_BIT);
        Ref<RDTextureView> v; v.instantiate();
        _clip_grid[L] = _rd->texture_create(fmt, v, {});
    }

    // Voxel sampler — trilinear, clamp. The 3D gen pass's sample_scene() uses this.
    {
        Ref<RDSamplerState> s; s.instantiate();
        s->set_min_filter(RenderingDevice::SAMPLER_FILTER_LINEAR);
        s->set_mag_filter(RenderingDevice::SAMPLER_FILTER_LINEAR);
        s->set_mip_filter(RenderingDevice::SAMPLER_FILTER_LINEAR);
        s->set_repeat_u(RenderingDevice::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE);
        s->set_repeat_v(RenderingDevice::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE);
        s->set_repeat_w(RenderingDevice::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE);
        _voxel_sampler = _rd->sampler_create(s);
        ERR_FAIL_COND_MSG(!_voxel_sampler.is_valid(), "RC: voxel sampler failed");
    }

    {
        auto make3d = [&] (RenderingDevice::DataFormat f)
            {
                Ref<RDTextureFormat> fmt; fmt.instantiate();
                fmt->set_format(f);
                fmt->set_texture_type(RenderingDevice::TEXTURE_TYPE_3D);
                fmt->set_width(_vox_res); fmt->set_height(_vox_res); fmt->set_depth(_vox_res);
                fmt->set_array_layers(1); fmt->set_mipmaps(1);
                fmt->set_usage_bits(RenderingDevice::TEXTURE_USAGE_STORAGE_BIT |
                    RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT |
                    RenderingDevice::TEXTURE_USAGE_CAN_UPDATE_BIT |
                    RenderingDevice::TEXTURE_USAGE_CAN_COPY_TO_BIT);
                Ref<RDTextureView> v; v.instantiate();
                return _rd->texture_create(fmt, v, {});
            };
        _clip_albedo = make3d(RenderingDevice::DATA_FORMAT_R8G8B8A8_UNORM);
        _clip_normal = make3d(RenderingDevice::DATA_FORMAT_R8G8B8A8_UNORM);
        _clip_emission = make3d(RenderingDevice::DATA_FORMAT_R16G16B16A16_SFLOAT);
    }

    // ── Probe ("patch") store + all compute shaders ──────────────────────────
    // One slot space shared by every cascade: buckets (the spatial hash), keys,
    // world position and directional radiance are parallel arrays indexed by slot.
    // Sizes come straight from the cascade table; _patch_indirect_buf later feeds
    // indirect dispatch so trace/merge only run over probes that were actually live.
    {
        _build_cascade_table();   // fills _total_buckets / _total_probes / _total_rad + _cascade_buf

        _patch_alloc = _rd->storage_buffer_create(sizeof(uint32_t) * RC_CASCADES);   // one counter per cascade

        // probe_keys: ivec4 (16 B) per slot
        _patch_keys = _rd->storage_buffer_create(_total_probes * sizeof(int32_t) * 4);

        // probe_world: vec4 (16 B) per slot
        _patch_world = _rd->storage_buffer_create(_total_probes * sizeof(float) * 4);

        // probe_radiance: uvec2 (8 B) per (slot, direction)
        _probe_radiance = _rd->storage_buffer_create(_total_rad * sizeof(uint32_t) * 2);

        // buckets: uvec2 (8 B) per slot  — already sized by _total_buckets, unchanged
        _patch_buckets = _rd->storage_buffer_create(_total_buckets * sizeof(uint32_t) * 2);

        // live_list: one slot index per live probe (compact). Indexed by probe_off + live_i,
        // live_i < alloc_count[c] <= bucket_cap, so it fits the same per-cascade regions.
        _patch_live = _rd->storage_buffer_create(_total_buckets * sizeof(uint32_t));
        ERR_FAIL_COND_MSG(!_patch_live.is_valid(), "RC: live_list buffer failed");

        _patch_indirect_buf = _rd->storage_buffer_create(
            sizeof(uint32_t) * 3 * 2 * RC_CASCADES, PackedByteArray(),   // [N trace][N merge]
            RenderingDevice::STORAGE_BUFFER_USAGE_DISPATCH_INDIRECT);
        ERR_FAIL_COND_MSG(!_patch_buckets.is_valid(), "RC: patch buffers failed");

        // angular pre-reduce scratch: max over r>1 folds of (cn.bucket_cap * cd.dirs).
        // oct={4,4,8,8,16} → r=2 only at c=1 (c2→c1) and c=3 (c4→c3); peak is the c=1 fold.
        uint32_t reduced_max = 0;
        for (uint32_t c = 0; c + 1u < RC_CASCADES; ++c)
        {
            uint32_t r = MAX(_cascades[c + 1].oct_res / _cascades[c].oct_res, 1u);
            if (r > 1u)
                reduced_max = MAX(reduced_max, _cascades[c + 1].bucket_cap * _cascades[c].dirs);
        }
        _reduced_radiance = _rd->storage_buffer_create(reduced_max * sizeof(uint32_t) * 2u);  // uvec2
        ERR_FAIL_COND_MSG(!_reduced_radiance.is_valid(), "RC: reduced scratch buffer failed");

        Ref<RDSamplerState> s; s.instantiate();
        s->set_min_filter(RenderingDevice::SAMPLER_FILTER_LINEAR);
        s->set_mag_filter(RenderingDevice::SAMPLER_FILTER_LINEAR);
        s->set_mip_filter(RenderingDevice::SAMPLER_FILTER_LINEAR);
        s->set_repeat_u(RenderingDevice::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE);
        s->set_repeat_v(RenderingDevice::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE);
        s->set_repeat_w(RenderingDevice::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE);
        _voxel_linear_sampler = _rd->sampler_create(s);

        // Dynamic proxy triangle buffer — fixed capacity, buffer_update each frame.
        {
            PackedByteArray init; init.resize(sizeof(float) * 12 * MAX_DYN_TRIS);
            memset(init.ptrw(), 0, init.size());
            _dyn_tri_buffer = _rd->storage_buffer_create(init.size(), init);
            ERR_FAIL_COND_MSG(!_dyn_tri_buffer.is_valid(), "RC: dyn tri buffer failed");
        }

        auto load_cs = [&] (const char* path, RID& sh, RID& pipe)
            {
                Ref<RDShaderFile> f = ResourceLoader::get_singleton()->load(path, "RDShaderFile");
                ERR_FAIL_COND_MSG(f.is_null(), vformat("RC: %s not found", path));
                Ref<RDShaderSPIRV> spirv = f->get_spirv();
                String err = spirv->get_stage_compile_error(RenderingDevice::SHADER_STAGE_COMPUTE);
                ERR_FAIL_COND_MSG(err != "", vformat("RC %s error: %s", path, err));
                sh = _rd->shader_create_from_spirv(spirv);
                pipe = _rd->compute_pipeline_create(sh);
            };
        load_cs("res://addons/radiance_cascade/shaders/rc_patch_clear.glsl", _patch_clear_shader, _patch_clear_pipeline);
        load_cs("res://addons/radiance_cascade/shaders/rc_patch_add.glsl", _patch_add_shader, _patch_add_pipeline);
        load_cs("res://addons/radiance_cascade/shaders/rc_patch_lookup.glsl", _patch_lookup_shader, _patch_lookup_pipeline);
        load_cs("res://addons/radiance_cascade/shaders/rc_patch_indirect.glsl", _patch_indirect_shader, _patch_indirect_pipeline);
        load_cs("res://addons/radiance_cascade/shaders/rc_patch_trace.glsl", _patch_trace_shader, _patch_trace_pipeline);
        load_cs("res://addons/radiance_cascade/shaders/rc_voxelize_mesh.glsl", _voxelize_shader, _voxelize_pipeline);
        load_cs("res://addons/radiance_cascade/shaders/rc_voxel_debug.glsl", _voxel_debug_shader, _voxel_debug_pipeline);
        load_cs("res://addons/radiance_cascade/shaders/rc_patch_gather.glsl", _patch_gather_shader, _patch_gather_pipeline);
        load_cs("res://addons/radiance_cascade/shaders/rc_voxel_inject.glsl", _inject_shader, _inject_pipeline);
        load_cs("res://addons/radiance_cascade/shaders/rc_voxelize_dynamic.glsl", _dyn_voxelize_shader, _dyn_voxelize_pipeline);
        load_cs("res://addons/radiance_cascade/shaders/rc_patch_merge.glsl", _patch_merge_shader, _patch_merge_pipeline);
        load_cs("res://addons/radiance_cascade/shaders/rc3d_voxel_sdf.glsl", _sdf_shader, _sdf_pipeline);
        load_cs("res://addons/radiance_cascade/shaders/rc3d_voxel_mip_aniso.glsl", _mip_aniso_shader, _mip_aniso_pipeline);
        load_cs("res://addons/radiance_cascade/shaders/rc3d_voxel_emission_mip.glsl", _emis_mip_shader, _emis_mip_pipeline);
        load_cs("res://addons/radiance_cascade/shaders/rc_irradiance_upsample.glsl", _upsample_shader, _upsample_pipeline);
        load_cs("res://addons/radiance_cascade/shaders/rc_patch_reduce.glsl", _patch_reduce_shader, _patch_reduce_pipeline);
        load_cs("res://addons/radiance_cascade/shaders/rc_clip_inject.glsl", _clip_inject_shader, _clip_inject_pipeline);
        load_cs("res://addons/radiance_cascade/shaders/rc_slab_clear.glsl", _slab_clear_shader, _slab_clear_pipeline);
        load_cs("res://addons/radiance_cascade/shaders/rc_irradiance_atrous.glsl", _atrous_shader, _atrous_pipeline);
        load_cs("res://addons/radiance_cascade/shaders/rc_dyn_occ_temporal.glsl", _dyn_occ_temporal_shader, _dyn_occ_temporal_pipeline);
    }

    // Debug texture (rgba8, half-res)
    {
        uint32_t half_w = _screen_size.x;
        uint32_t half_h = _screen_size.y;
        Ref<RDTextureFormat> fmt; fmt.instantiate();
        fmt->set_format(RenderingDevice::DATA_FORMAT_R8G8B8A8_UNORM);
        fmt->set_texture_type(RenderingDevice::TEXTURE_TYPE_2D);
        fmt->set_width(half_w);
        fmt->set_height(half_h);
        fmt->set_depth(1); fmt->set_array_layers(1); fmt->set_mipmaps(1);
        fmt->set_usage_bits(RenderingDevice::TEXTURE_USAGE_STORAGE_BIT |
            RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT |
            RenderingDevice::TEXTURE_USAGE_CAN_COPY_FROM_BIT);
        Ref<RDTextureView> v; v.instantiate();
        _debug_tex = _rd->texture_create(fmt, v, {});
        ERR_FAIL_COND_MSG(!_debug_tex.is_valid(), "RC: debug tex failed");
    }

    // Irradiance output (full-res, rgba16f) — THE PUBLIC OUTPUT of the addon
    {
        Ref<RDTextureFormat> fmt; fmt.instantiate();
        fmt->set_format(RenderingDevice::DATA_FORMAT_R16G16B16A16_SFLOAT);
        fmt->set_texture_type(RenderingDevice::TEXTURE_TYPE_2D);
        fmt->set_width(_screen_size.x);
        fmt->set_height(_screen_size.y);
        fmt->set_depth(1); fmt->set_array_layers(1); fmt->set_mipmaps(1);
        fmt->set_usage_bits(RenderingDevice::TEXTURE_USAGE_STORAGE_BIT |
            RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT |
            RenderingDevice::TEXTURE_USAGE_CAN_COPY_FROM_BIT);
        Ref<RDTextureView> v; v.instantiate();
        _irradiance_tex = _rd->texture_create(fmt, v, {});
        ERR_FAIL_COND_MSG(!_irradiance_tex.is_valid(), "RC: irradiance tex failed");
    }

    _half_size = Vector2i((_screen_size.x + 1) / 2, (_screen_size.y + 1) / 2);

    {   // half-res gather target
        Ref<RDTextureFormat> fmt; fmt.instantiate();
        fmt->set_format(RenderingDevice::DATA_FORMAT_R16G16B16A16_SFLOAT);
        fmt->set_texture_type(RenderingDevice::TEXTURE_TYPE_2D);
        fmt->set_width(_half_size.x); fmt->set_height(_half_size.y);
        fmt->set_depth(1); fmt->set_array_layers(1); fmt->set_mipmaps(1);
        fmt->set_usage_bits(RenderingDevice::TEXTURE_USAGE_STORAGE_BIT |
            RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT |
            RenderingDevice::TEXTURE_USAGE_CAN_COPY_FROM_BIT);
        Ref<RDTextureView> v; v.instantiate();
        _irradiance_half = _rd->texture_create(fmt, v, {});
        ERR_FAIL_COND_MSG(!_irradiance_half.is_valid(), "RC: half irradiance tex failed");
    }

    {   // à-trous ping-pong scratch (identical to _irradiance_half)
        Ref<RDTextureFormat> fmt; fmt.instantiate();
        fmt->set_format(RenderingDevice::DATA_FORMAT_R16G16B16A16_SFLOAT);
        fmt->set_texture_type(RenderingDevice::TEXTURE_TYPE_2D);
        fmt->set_width(_half_size.x); fmt->set_height(_half_size.y);
        fmt->set_depth(1); fmt->set_array_layers(1); fmt->set_mipmaps(1);
        fmt->set_usage_bits(RenderingDevice::TEXTURE_USAGE_STORAGE_BIT |
            RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT |
            RenderingDevice::TEXTURE_USAGE_CAN_COPY_FROM_BIT);
        Ref<RDTextureView> v; v.instantiate();
        _irradiance_half_b = _rd->texture_create(fmt, v, {});
        ERR_FAIL_COND_MSG(!_irradiance_half_b.is_valid(), "RC: half irradiance scratch failed");
    }

    {   // NEAREST sampler (skip if you already have a point sampler — reuse its RID below)
        Ref<RDSamplerState> s; s.instantiate();
        s->set_min_filter(RenderingDevice::SAMPLER_FILTER_NEAREST);
        s->set_mag_filter(RenderingDevice::SAMPLER_FILTER_NEAREST);
        s->set_mip_filter(RenderingDevice::SAMPLER_FILTER_NEAREST);
        s->set_repeat_u(RenderingDevice::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE);
        s->set_repeat_v(RenderingDevice::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE);
        _point_sampler = _rd->sampler_create(s);
        ERR_FAIL_COND_MSG(!_point_sampler.is_valid(), "RC: point sampler failed");
    }

    // Sampler (nearest, clamp)
    {
        Ref<RDSamplerState> s; s.instantiate();
        s->set_min_filter(RenderingDevice::SAMPLER_FILTER_LINEAR);
        s->set_mag_filter(RenderingDevice::SAMPLER_FILTER_LINEAR);
        s->set_mip_filter(RenderingDevice::SAMPLER_FILTER_LINEAR);
        s->set_repeat_u(RenderingDevice::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE);
        s->set_repeat_v(RenderingDevice::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE);
        _depth_sampler = _rd->sampler_create(s);
        ERR_FAIL_COND_MSG(!_depth_sampler.is_valid(), "RC: sampler failed");
    }

    {
        Ref<RDSamplerState> s; s.instantiate();
        s->set_min_filter(RenderingDevice::SAMPLER_FILTER_NEAREST);
        s->set_mag_filter(RenderingDevice::SAMPLER_FILTER_NEAREST);
        s->set_repeat_u(RenderingDevice::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE);
        s->set_repeat_v(RenderingDevice::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE);
        _normal_sampler = _rd->sampler_create(s);
        ERR_FAIL_COND_MSG(!_normal_sampler.is_valid(), "RC: normal sampler failed");
    }

    // Color sampler — linear filtering, clamp to edge.
    // Used to read the lit HDR scene color buffer at ray hit points.
    {
        Ref<RDSamplerState> s; s.instantiate();
        s->set_min_filter(RenderingDevice::SAMPLER_FILTER_LINEAR);
        s->set_mag_filter(RenderingDevice::SAMPLER_FILTER_LINEAR);
        s->set_mip_filter(RenderingDevice::SAMPLER_FILTER_NEAREST);
        s->set_repeat_u(RenderingDevice::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE);
        s->set_repeat_v(RenderingDevice::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE);
        _color_sampler = _rd->sampler_create(s);
        ERR_FAIL_COND_MSG(!_color_sampler.is_valid(), "RC: color sampler failed");
    }

    // Dummy 1x1 black HDR color tex — fallback when scene color RID is missing.
    // Keeps the per-frame set binding valid; rays that hit "geometry" get black,
    // which simply contributes no bounce. Safe degradation.
    {
        Ref<RDTextureFormat> fmt; fmt.instantiate();
        fmt->set_format(RenderingDevice::DATA_FORMAT_R16G16B16A16_SFLOAT);
        fmt->set_texture_type(RenderingDevice::TEXTURE_TYPE_2D);
        fmt->set_width(1); fmt->set_height(1);
        fmt->set_depth(1); fmt->set_array_layers(1); fmt->set_mipmaps(1);
        fmt->set_usage_bits(RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT |
            RenderingDevice::TEXTURE_USAGE_CAN_UPDATE_BIT |
            RenderingDevice::TEXTURE_USAGE_CAN_COPY_FROM_BIT);
        Ref<RDTextureView> v; v.instantiate();
        _dummy_color_tex = _rd->texture_create(fmt, v, {});
        ERR_FAIL_COND_MSG(!_dummy_color_tex.is_valid(), "RC: dummy color tex failed");

        // Fill with HDR black (all zeros). half-float 0.0 = 0x0000.
        PackedByteArray black;
        black.resize(8);   // 4 channels * 2 bytes per half-float
        for (int i = 0; i < 8; ++i) black.set(i, 0);
        _rd->texture_update(_dummy_color_tex, 0, black);
    }

    // Composite shader + pipeline
    {
        Ref<RDShaderFile> f = ResourceLoader::get_singleton()->load(
            "res://addons/radiance_cascade/shaders/rc_composite.glsl", "RDShaderFile");
        ERR_FAIL_COND_MSG(f.is_null(), "RC: rc_composite.glsl not found");
        Ref<RDShaderSPIRV> spirv = f->get_spirv();
        String err = spirv->get_stage_compile_error(RenderingDevice::SHADER_STAGE_COMPUTE);
        ERR_FAIL_COND_MSG(err != "", vformat("RC composite shader error: %s", err));
        _composite_shader = _rd->shader_create_from_spirv(spirv);
        _composite_pipeline = _rd->compute_pipeline_create(_composite_shader);
        ERR_FAIL_COND_MSG(!_composite_pipeline.is_valid(), "RC: composite pipeline failed");
    }

    // Albedo sampler — linear, clamp. Used to read the (future) albedo buffer
    // at full-res pixel positions during compositing.
    {
        Ref<RDSamplerState> s; s.instantiate();
        s->set_min_filter(RenderingDevice::SAMPLER_FILTER_LINEAR);
        s->set_mag_filter(RenderingDevice::SAMPLER_FILTER_LINEAR);
        s->set_mip_filter(RenderingDevice::SAMPLER_FILTER_NEAREST);
        s->set_repeat_u(RenderingDevice::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE);
        s->set_repeat_v(RenderingDevice::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE);
        _albedo_sampler = _rd->sampler_create(s);
        ERR_FAIL_COND_MSG(!_albedo_sampler.is_valid(), "RC: albedo sampler failed");
    }

    // Dummy 1x1 white HDR albedo tex — placeholder until a real albedo buffer is
    // plumbed in. White means "no surface tinting" so the additive composite
    // formula is unchanged. Swap this RID for the real albedo later; nothing else
    // in the addon needs to change.
    {
        Ref<RDTextureFormat> fmt; fmt.instantiate();
        fmt->set_format(RenderingDevice::DATA_FORMAT_R16G16B16A16_SFLOAT);
        fmt->set_texture_type(RenderingDevice::TEXTURE_TYPE_2D);
        fmt->set_width(1); fmt->set_height(1);
        fmt->set_depth(1); fmt->set_array_layers(1); fmt->set_mipmaps(1);
        fmt->set_usage_bits(RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT |
            RenderingDevice::TEXTURE_USAGE_CAN_UPDATE_BIT);
        Ref<RDTextureView> v; v.instantiate();
        _dummy_albedo_tex = _rd->texture_create(fmt, v, {});
        ERR_FAIL_COND_MSG(!_dummy_albedo_tex.is_valid(), "RC: dummy albedo tex failed");

        // Fill with HDR white (1.0 each channel). half-float 1.0 = 0x3C00.
        PackedByteArray white;
        white.resize(8);   // 4 channels * 2 bytes per half-float
        const uint16_t one = 0x3C00;
        for (int i = 0; i < 4; ++i)
        {
            white.set(i * 2, one & 0xFF);
            white.set(i * 2 + 1, (one >> 8) & 0xFF);
        }
        _rd->texture_update(_dummy_albedo_tex, 0, white);
    }

    // Build static uniform sets (no depth RID yet — that comes in dispatch())
    _update_trace_params();   // creates _trace_params_ubo; set 2 (trace backend) references it
    _build_static_sets();

    _initialized = true;
}

// ── Static uniform sets (set=0) — built once, reused every frame ───────────
void CRadianceCascade::_build_static_sets()
{
    // Wire each pass's resources into descriptor sets once. set=0 holds the static
    // inputs that never change frame-to-frame; per-frame inputs (depth/normal/color)
    // live in set=1, rebuilt by _rebuild_per_frame_sets(). Convention across the
    // patch passes: cascade table SSBO at b7, camera UBO at b5.
    // Composite pass set=0: irradiance (read) + albedo sampler (static)
    {
        TypedArray<RDUniform> u;

        Ref<RDUniform> u0; u0.instantiate();
        u0->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
        u0->set_binding(0);
        u0->add_id(_irradiance_tex);
        u.append(u0);

        Ref<RDUniform> u1; u1.instantiate();
        u1->set_uniform_type(RenderingDevice::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE);
        u1->set_binding(1);
        u1->add_id(_albedo_sampler);
        u1->add_id(_dummy_albedo_tex);   // swap for real albedo later
        u.append(u1);

        Ref<RDUniform> u2; u2.instantiate();
        u2->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
        u2->set_binding(2);
        u2->add_id(_debug_tex);
        u.append(u2);

        _composite_set0 = _rd->uniform_set_create(u, _composite_shader, 0);
        ERR_FAIL_COND_MSG(!_composite_set0.is_valid(), "RC: composite set0 failed");
    }


    //PATCH
    auto ssbo = [] (int bind, RID id)
        {
            Ref<RDUniform> u; u.instantiate();
            u->set_uniform_type(RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER); u->set_binding(bind); u->add_id(id); return u;
        };

    {   // clear set0
        TypedArray<RDUniform> u;
        u.append(ssbo(0, _patch_buckets));
        u.append(ssbo(1, _patch_alloc));
        _patch_clear_set0 = _rd->uniform_set_create(u, _patch_clear_shader, 0);
        ERR_FAIL_COND_MSG(!_patch_clear_set0.is_valid(), "RC: patch clear set failed");
    }

    {   // add set0  (+ cascade table at b7; set1 stays depth-only)
        TypedArray<RDUniform> u;
        u.append(ssbo(0, _patch_buckets)); u.append(ssbo(1, _patch_alloc));
        u.append(ssbo(2, _patch_keys));    u.append(ssbo(3, _patch_world));
        u.append(ssbo(4, _patch_live));    // ← NEW: compact live-slot list (write)
        Ref<RDUniform> u5; u5.instantiate(); u5->set_uniform_type(RenderingDevice::UNIFORM_TYPE_UNIFORM_BUFFER);
        u5->set_binding(5); u5->add_id(_camera_ubo); u.append(u5);
        u.append(ssbo(7, _cascade_buf));
        _patch_add_set0 = _rd->uniform_set_create(u, _patch_add_shader, 0);
        ERR_FAIL_COND_MSG(!_patch_add_set0.is_valid(), "RC: patch add set failed");
    }

    {   // indirect set0  (cascade table at b2 here)
        TypedArray<RDUniform> u;
        u.append(ssbo(0, _patch_alloc));
        u.append(ssbo(1, _patch_indirect_buf));
        u.append(ssbo(2, _cascade_buf));
        _patch_indirect_set0 = _rd->uniform_set_create(u, _patch_indirect_shader, 0);
    }

    {   // trace set0  (+ cascade table at b7)
        TypedArray<RDUniform> u;
        u.append(ssbo(0, _patch_buckets));
        u.append(ssbo(1, _patch_alloc));
        u.append(ssbo(3, _patch_world));
        u.append(ssbo(4, _patch_live));
        u.append(ssbo(6, _probe_radiance));
        u.append(ssbo(7, _cascade_buf));
        _patch_trace_set0 = _rd->uniform_set_create(u, _patch_trace_shader, 0);
    }

    {   // merge set0
        TypedArray<RDUniform> u;
        u.append(ssbo(0, _patch_buckets)); u.append(ssbo(1, _patch_alloc));
        u.append(ssbo(2, _patch_keys));    u.append(ssbo(3, _patch_world));
        u.append(ssbo(6, _probe_radiance));
        u.append(ssbo(7, _cascade_buf));
        u.append(ssbo(8, _reduced_radiance));        // ← NEW: pre-reduced continuation
        _patch_merge_set0 = _rd->uniform_set_create(u, _patch_merge_shader, 0);
        ERR_FAIL_COND_MSG(!_patch_merge_set0.is_valid(), "RC: patch merge set failed");
    }

    {   // reduce set0  (buckets 0, probe_radiance 6, cascade table 7, scratch 8)
        TypedArray<RDUniform> u;
        u.append(ssbo(0, _patch_buckets));
        u.append(ssbo(6, _probe_radiance));
        u.append(ssbo(7, _cascade_buf));
        u.append(ssbo(8, _reduced_radiance));
        _patch_reduce_set0 = _rd->uniform_set_create(u, _patch_reduce_shader, 0);
        ERR_FAIL_COND_MSG(!_patch_reduce_set0.is_valid(), "RC: patch reduce set failed");
    }

    {   // trace set2 — the trace backend: everything a probe ray samples. Level-0
        // grid (b0), trace params (b1), dynamic-occupancy accumulator (b2), the six
        // anisotropic mip stacks (b3..8), emission (b9), coarse clip levels (b10..13)
        // with their params (b14), and the SDF (b15).
        auto clip_tex = [&] (int binding, int L)
            {
                Ref<RDUniform> u; u.instantiate();
                u->set_uniform_type(RenderingDevice::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE);
                u->set_binding(binding); u->add_id(_voxel_sampler);
                u->add_id((L < _clip_levels && _clip_grid[L].is_valid()) ? _clip_grid[L] : _dummy_clip_tex);
                return u;
            };

        TypedArray<RDUniform> u;
        Ref<RDUniform> s; s.instantiate(); s->set_uniform_type(RenderingDevice::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE);
        s->set_binding(0); s->add_id(_voxel_linear_sampler); s->add_id(_voxel_tex); u.append(s);
        Ref<RDUniform> p; p.instantiate(); p->set_uniform_type(RenderingDevice::UNIFORM_TYPE_UNIFORM_BUFFER);
        p->set_binding(1); p->add_id(_trace_params_ubo); u.append(p);
        Ref<RDUniform> d; d.instantiate(); d->set_uniform_type(RenderingDevice::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE);
        d->set_binding(2); d->add_id(_voxel_linear_sampler); d->add_id(_dyn_occ_acc); u.append(d);
        u.append(clip_tex(10, 1)); u.append(clip_tex(11, 2));
        u.append(clip_tex(12, 3)); u.append(clip_tex(13, 4));
        {
            Ref<RDUniform> ub; ub.instantiate();
            ub->set_uniform_type(RenderingDevice::UNIFORM_TYPE_UNIFORM_BUFFER);
            ub->set_binding(14); ub->add_id(_clip_params_ubo); u.append(ub);
        }

        {
            Ref<RDUniform> usdf; usdf.instantiate();
            usdf->set_uniform_type(RenderingDevice::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE);
            usdf->set_binding(15); usdf->add_id(_voxel_linear_sampler); usdf->add_id(_sdf_tex);
            u.append(usdf);
        }

        static const int aniso_bind[6] = { 3,4,5,6,7,8 };
        for (int dir = 0; dir < 6; ++dir)
        {
            Ref<RDUniform> ua; ua.instantiate();
            ua->set_uniform_type(RenderingDevice::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE);
            ua->set_binding(aniso_bind[dir]); ua->add_id(_voxel_linear_sampler);
            ua->add_id(_voxel_aniso[dir]); u.append(ua);
        }
        Ref<RDUniform> ue; ue.instantiate();
        ue->set_uniform_type(RenderingDevice::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE);
        ue->set_binding(9); ue->add_id(_voxel_linear_sampler); ue->add_id(_voxel_emission); u.append(ue);
        _trace_voxel_set2 = _rd->uniform_set_create(u, _patch_trace_shader, 2);
    }

    {   // lookup set0
        TypedArray<RDUniform> u; u.append(ssbo(0, _patch_buckets)); u.append(ssbo(2, _patch_keys));
        Ref<RDUniform> u4; u4.instantiate(); u4->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
        u4->set_binding(4); u4->add_id(_debug_tex); u.append(u4);
        Ref<RDUniform> u5; u5.instantiate(); u5->set_uniform_type(RenderingDevice::UNIFORM_TYPE_UNIFORM_BUFFER);
        u5->set_binding(5); u5->add_id(_camera_ubo); u.append(u5);
        u.append(ssbo(6, _probe_radiance));
        u.append(ssbo(7, _cascade_buf));
        _patch_lookup_set0 = _rd->uniform_set_create(u, _patch_lookup_shader, 0);
    }

    {   // debug set0
        TypedArray<RDUniform> u;
        Ref<RDUniform> s; s.instantiate(); s->set_uniform_type(RenderingDevice::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE);
        s->set_binding(0); s->add_id(_voxel_linear_sampler); s->add_id(_voxel_tex); u.append(s);
        Ref<RDUniform> d; d.instantiate(); d->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
        d->set_binding(1); d->add_id(_debug_tex); u.append(d);
        Ref<RDUniform> c; c.instantiate(); c->set_uniform_type(RenderingDevice::UNIFORM_TYPE_UNIFORM_BUFFER);
        c->set_binding(2); c->add_id(_camera_ubo); u.append(c);
        Ref<RDUniform> e; e.instantiate(); e->set_uniform_type(RenderingDevice::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE);
        e->set_binding(3); e->add_id(_voxel_linear_sampler); e->add_id(_voxel_emission); u.append(e);   // L0: real emission grid
        _voxel_debug_set0 = _rd->uniform_set_create(u, _voxel_debug_shader, 0);
    }

    for (int L = 1; L < MAX_CLIP; ++L)
    {   // debug set per coarse grid — same layout as debug set0, different grid at binding 0
        if (!_clip_grid[L].is_valid()) continue;
        TypedArray<RDUniform> u;
        Ref<RDUniform> s; s.instantiate(); s->set_uniform_type(RenderingDevice::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE);
        s->set_binding(0); s->add_id(_voxel_linear_sampler); s->add_id(_clip_grid[L]); u.append(s);
        Ref<RDUniform> d; d.instantiate(); d->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
        d->set_binding(1); d->add_id(_debug_tex); u.append(d);
        Ref<RDUniform> c; c.instantiate(); c->set_uniform_type(RenderingDevice::UNIFORM_TYPE_UNIFORM_BUFFER);
        c->set_binding(2); c->add_id(_camera_ubo); u.append(c);
        Ref<RDUniform> e; e.instantiate(); e->set_uniform_type(RenderingDevice::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE);
        e->set_binding(3); e->add_id(_voxel_linear_sampler); e->add_id(_clip_grid[L]); u.append(e);   // clip L: grid carries baked emission in rgb
        _voxel_debug_clip_set[L] = _rd->uniform_set_create(u, _voxel_debug_shader, 0);
    }

    {   // upsample set0 (static): 0 = half irradiance (sampler+tex), 1 = full-res output image
        TypedArray<RDUniform> u;
        Ref<RDUniform> u0; u0.instantiate();
        u0->set_uniform_type(RenderingDevice::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE);
        u0->set_binding(0); u0->add_id(_point_sampler); u0->add_id(_irradiance_half); u.append(u0);
        Ref<RDUniform> u1; u1.instantiate();
        u1->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
        u1->set_binding(1); u1->add_id(_irradiance_tex); u.append(u1);
        _upsample_set0 = _rd->uniform_set_create(u, _upsample_shader, 0);
        ERR_FAIL_COND_MSG(!_upsample_set0.is_valid(), "RC: upsample set0 failed");
    }

    {   // à-trous ping-pong set0 (static): H→S and S→H
        auto mkset = [&] (RID in_tex, RID out_img)
            {
                TypedArray<RDUniform> u;
                Ref<RDUniform> u0; u0.instantiate();
                u0->set_uniform_type(RenderingDevice::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE);
                u0->set_binding(0); u0->add_id(_point_sampler); u0->add_id(in_tex); u.append(u0);
                Ref<RDUniform> u1; u1.instantiate();
                u1->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
                u1->set_binding(1); u1->add_id(out_img); u.append(u1);
                return _rd->uniform_set_create(u, _atrous_shader, 0);
            };
        _atrous_set0_h2s = mkset(_irradiance_half, _irradiance_half_b);
        _atrous_set0_s2h = mkset(_irradiance_half_b, _irradiance_half);
        ERR_FAIL_COND_MSG(!_atrous_set0_h2s.is_valid() || !_atrous_set0_s2h.is_valid(),
            "RC: atrous set0 failed");
    }

    {   // upsample set2 — c0 probes for the edge full-res gather (mirrors gather set0's probe half)
        TypedArray<RDUniform> u;
        u.append(ssbo(0, _patch_buckets));
        u.append(ssbo(2, _patch_keys));
        Ref<RDUniform> c5; c5.instantiate(); c5->set_uniform_type(RenderingDevice::UNIFORM_TYPE_UNIFORM_BUFFER);
        c5->set_binding(5); c5->add_id(_camera_ubo); u.append(c5);
        u.append(ssbo(6, _probe_radiance));
        u.append(ssbo(7, _cascade_buf));
        _upsample_set2 = _rd->uniform_set_create(u, _upsample_shader, 2);
        ERR_FAIL_COND_MSG(!_upsample_set2.is_valid(), "RC: upsample set2 failed");
    }

    {   // gather set 0
        TypedArray<RDUniform> u;
        u.append(ssbo(0, _patch_buckets));
        u.append(ssbo(2, _patch_keys));
        Ref<RDUniform> i4; i4.instantiate(); i4->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
        i4->set_binding(4); i4->add_id(_irradiance_half); u.append(i4);
        Ref<RDUniform> c5; c5.instantiate(); c5->set_uniform_type(RenderingDevice::UNIFORM_TYPE_UNIFORM_BUFFER);
        c5->set_binding(5); c5->add_id(_camera_ubo); u.append(c5);
        u.append(ssbo(6, _probe_radiance));
        u.append(ssbo(7, _cascade_buf));
        _patch_gather_set0 = _rd->uniform_set_create(u, _patch_gather_shader, 0);
    }

    // inject set0
    {
        TypedArray<RDUniform> u;
        Ref<RDUniform> u0; u0.instantiate();
        u0->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
        u0->set_binding(0); u0->add_id(_voxel_tex); u.append(u0);
        Ref<RDUniform> u1; u1.instantiate();
        u1->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
        u1->set_binding(1); u1->add_id(_voxel_albedo); u.append(u1);
        Ref<RDUniform> u2; u2.instantiate();
        u2->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
        u2->set_binding(2); u2->add_id(_voxel_normal); u.append(u2);
        Ref<RDUniform> u3; u3.instantiate();
        u3->set_uniform_type(RenderingDevice::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE);
        u3->set_binding(3); u3->add_id(_voxel_sampler); u3->add_id(_sdf_tex);
        u.append(u3);
        Ref<RDUniform> u4; u4.instantiate();
        u4->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
        u4->set_binding(4); u4->add_id(_voxel_emission);   // ← your emission tex RID
        u.append(u4);
        u.append(ssbo(5, _light_buf));
        _inject_set0 = _rd->uniform_set_create(u, _inject_shader, 0);
        ERR_FAIL_COND_MSG(!_inject_set0.is_valid(), "RC: resolve set0 failed");
    }

    { // clipmap inject sets
        auto img = [&] (TypedArray<RDUniform>& u, int binding, RID tex)
            {
                Ref<RDUniform> ud; ud.instantiate();
                ud->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
                ud->set_binding(binding); ud->add_id(tex); u.append(ud);
            };
        for (int L = 1; L < MAX_CLIP; ++L)
        {
            {
                TypedArray<RDUniform> u;
                Ref<RDUniform> i0; i0.instantiate(); i0->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
                i0->set_binding(0); i0->add_id(_clip_grid[L]); u.append(i0);
                u.append(ssbo(1, _tri_buffer));
                Ref<RDUniform> u2; u2.instantiate();
                u2->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
                u2->set_binding(2); u2->add_id(_clip_albedo); u.append(u2);
                Ref<RDUniform> u3; u3.instantiate();
                u3->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
                u3->set_binding(3); u3->add_id(_clip_normal); u.append(u3);
                Ref<RDUniform> u4; u4.instantiate();
                u4->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
                u4->set_binding(4); u4->add_id(_clip_emission); u.append(u4);
                if (_clip_voxelize_set[L].is_valid()) _rd->free_rid(_clip_voxelize_set[L]);
                _clip_voxelize_set[L] = _rd->uniform_set_create(u, _voxelize_shader, 0);
            }

            // inject set for rc_clip_inject (bindings 0..3)
            TypedArray<RDUniform> u;
            img(u, 0, _clip_grid[L]);
            img(u, 1, _clip_albedo);
            img(u, 2, _clip_normal);
            img(u, 3, _clip_emission);
            u.append(ssbo(4, _light_buf));
            _clip_inject_set[L] = _rd->uniform_set_create(u, _clip_inject_shader, 0);

            {   // shell clear set: clip grid (0) + coarse scratch (1,2,3), matches _slab_clear_shader
                TypedArray<RDUniform> uc;
                img(uc, 0, _clip_grid[L]);
                img(uc, 1, _clip_albedo);
                img(uc, 2, _clip_normal);
                img(uc, 3, _clip_emission);
                if (_clip_slab_clear_set[L].is_valid()) _rd->free_rid(_clip_slab_clear_set[L]);
                _clip_slab_clear_set[L] = _rd->uniform_set_create(uc, _slab_clear_shader, 0);
                ERR_FAIL_COND_MSG(!_clip_slab_clear_set[L].is_valid(), "RC: clip slab clear set failed");
            }
        }
    }

    {   // voxelize set 0
        TypedArray<RDUniform> u;
        Ref<RDUniform> i0; i0.instantiate(); i0->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
        i0->set_binding(0); i0->add_id(_voxel_tex); u.append(i0);
        u.append(ssbo(1, _tri_buffer));
        Ref<RDUniform> u2; u2.instantiate();
        u2->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
        u2->set_binding(2); u2->add_id(_voxel_albedo); u.append(u2);
        Ref<RDUniform> u3; u3.instantiate();
        u3->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
        u3->set_binding(3); u3->add_id(_voxel_normal); u.append(u3);
        Ref<RDUniform> u4; u4.instantiate();
        u4->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
        u4->set_binding(4); u4->add_id(_voxel_emission); u.append(u4);
        if (_voxelize_set0.is_valid()) _rd->free_rid(_voxelize_set0);
        _voxelize_set0 = _rd->uniform_set_create(u, _voxelize_shader, 0);
    }

    {   // slab clear set — 4 grids, no Tris (its own layout, so no _voxelize_set0 mismatch)
        TypedArray<RDUniform> u;
        auto img = [&] (int b, RID t)
            {
                Ref<RDUniform> ud; ud.instantiate();
                ud->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE); ud->set_binding(b); ud->add_id(t); u.append(ud);
            };
        img(0, _voxel_tex);
        img(1, _voxel_albedo);
        img(2, _voxel_normal);
        img(3, _voxel_emission);
        _slab_clear_set = _rd->uniform_set_create(u, _slab_clear_shader, 0);
        ERR_FAIL_COND_MSG(!_slab_clear_set.is_valid(), "RC: slab_clear set failed");
    }

    {   // dynamic voxelize set0
        TypedArray<RDUniform> u;
        Ref<RDUniform> i0; i0.instantiate();
        i0->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
        i0->set_binding(0); i0->add_id(_dyn_occ); u.append(i0);
        Ref<RDUniform> b1; b1.instantiate();
        b1->set_uniform_type(RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER);
        b1->set_binding(1); b1->add_id(_dyn_tri_buffer); u.append(b1);
        _dyn_voxelize_set0 = _rd->uniform_set_create(u, _dyn_voxelize_shader, 0);
        ERR_FAIL_COND_MSG(!_dyn_voxelize_set0.is_valid(), "RC: dyn voxelize set0 failed");
    }

    {   // dyn occ temporal set0
        auto img = [&] (int b, RID t)
            {
                Ref<RDUniform> u; u.instantiate();
                u->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
                u->set_binding(b); u->add_id(t); return u;
            };
        TypedArray<RDUniform> u;
        u.append(img(0, _dyn_occ));        // current binary
        u.append(img(1, _dyn_occ_acc));    // accumulator (in-place)
        _dyn_occ_temporal_set0 = _rd->uniform_set_create(u, _dyn_occ_temporal_shader, 0);
        ERR_FAIL_COND_MSG(!_dyn_occ_temporal_set0.is_valid(), "RC: dyn occ temporal set failed");
    }

    {   // sdf set — write A (seed_out = a).  All four bindings are image3D.
        TypedArray<RDUniform> u;
        Ref<RDUniform> i0; i0.instantiate();
        i0->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
        i0->set_binding(0); i0->add_id(_sdf_seed_b); u.append(i0);   // seed_in
        Ref<RDUniform> i1; i1.instantiate();
        i1->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
        i1->set_binding(1); i1->add_id(_sdf_seed_a); u.append(i1);   // seed_out
        Ref<RDUniform> i2; i2.instantiate();
        i2->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
        i2->set_binding(2); i2->add_id(_voxel_tex); u.append(i2);    // voxel_in (.a = occupancy)
        Ref<RDUniform> i3; i3.instantiate();
        i3->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
        i3->set_binding(3); i3->add_id(_sdf_tex); u.append(i3);      // sdf_out
        _sdf_set_writeA = _rd->uniform_set_create(u, _sdf_shader, 0);
        ERR_FAIL_COND_MSG(!_sdf_set_writeA.is_valid(), "RC: sdf set writeA failed");
    }

    {   // sdf set — write B (seed_out = b).
        TypedArray<RDUniform> u;
        Ref<RDUniform> i0; i0.instantiate();
        i0->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
        i0->set_binding(0); i0->add_id(_sdf_seed_a); u.append(i0);   // seed_in
        Ref<RDUniform> i1; i1.instantiate();
        i1->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
        i1->set_binding(1); i1->add_id(_sdf_seed_b); u.append(i1);   // seed_out
        Ref<RDUniform> i2; i2.instantiate();
        i2->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
        i2->set_binding(2); i2->add_id(_voxel_tex); u.append(i2);    // voxel_in
        Ref<RDUniform> i3; i3.instantiate();
        i3->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
        i3->set_binding(3); i3->add_id(_sdf_tex); u.append(i3);      // sdf_out
        _sdf_set_writeB = _rd->uniform_set_create(u, _sdf_shader, 0);
        ERR_FAIL_COND_MSG(!_sdf_set_writeB.is_valid(), "RC: sdf set writeB failed");
    }
}

// ── Per-frame depth set (set=1) — 1 alloc + 1 free per frame ──────────────
void CRadianceCascade::_rebuild_per_frame_sets(RID depth, RID normal_rough, RID color)
{
    // set=1 for the passes that read this frame's compositor buffers (depth,
    // normal-roughness, scene color). Rebuilt every frame because those RIDs can
    // change; the previous set is freed first (1 alloc + 1 free per frame).
    if (!depth.is_valid()) return;

    // PATCH
    // depth b0 + normal b1, created against the lookup shader; reuse for add (identical set-1 layout)
    {
        TypedArray<RDUniform> u;
        Ref<RDUniform> d; d.instantiate(); d->set_uniform_type(RenderingDevice::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE);
        d->set_binding(0); d->add_id(_depth_sampler); d->add_id(depth); u.append(d);
        Ref<RDUniform> n; n.instantiate(); n->set_uniform_type(RenderingDevice::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE);
        n->set_binding(1); n->add_id(_normal_sampler); n->add_id(normal_rough); u.append(n);
        if (_patch_lookup_set1.is_valid()) _rd->free_rid(_patch_lookup_set1);
        _patch_lookup_set1 = _rd->uniform_set_create(u, _patch_lookup_shader, 1);
    }

    // PATCH
    // depth b0 + normal b1, created against the add shader; reuse for lookup (identical set-1 layout)
    {
        TypedArray<RDUniform> u;
        Ref<RDUniform> d; d.instantiate(); d->set_uniform_type(RenderingDevice::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE);
        d->set_binding(0); d->add_id(_depth_sampler); d->add_id(depth); u.append(d);
        //Ref<RDUniform> n; n.instantiate(); n->set_uniform_type(RenderingDevice::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE);
        //n->set_binding(1); n->add_id(_normal_sampler); n->add_id(normal_rough); u.append(n);
        if (_patch_add_set1.is_valid()) _rd->free_rid(_patch_add_set1);
        _patch_add_set1 = _rd->uniform_set_create(u, _patch_add_shader, 1);
    }

    // Composite per-frame set — scene color bound twice (sampler + image)
    {
        TypedArray<RDUniform> u;

        Ref<RDUniform> u0; u0.instantiate();
        u0->set_uniform_type(RenderingDevice::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE);
        u0->set_binding(0);
        u0->add_id(_color_sampler);
        u0->add_id(color.is_valid() ? color : _dummy_color_tex);
        u.append(u0);

        Ref<RDUniform> u1; u1.instantiate();
        u1->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
        u1->set_binding(1);
        u1->add_id(color.is_valid() ? color : _dummy_color_tex);
        u.append(u1);

        if (_composite_set1.is_valid()) _rd->free_rid(_composite_set1);
        _composite_set1 = _rd->uniform_set_create(u, _composite_shader, 1);
    }
}

// ── Cleanup ────────────────────────────────────────────────────────────────
void CRadianceCascade::_free_rids()
{
    // Release EVERY GPU resource. Runs on reinit (resize / voxel-res change) and on
    // delete, so it must track _init_pipelines/_build_static_sets exactly — a missed
    // handle leaks on every resize. safe_free() nulls each RID so a second call
    // (PREDELETE then dtor) is a harmless no-op.
    auto safe_free = [&] (RID& rid)
        {
            if (rid.is_valid())
            {
                _rd->free_rid(rid);
                rid = RID();  // always reset, even if free_rid prints an error
            }
        };

    // Free in reverse dependency order: sets → pipelines → shaders → textures
    safe_free(_voxel_sampler); safe_free(_voxel_tex);
    safe_free(_voxelize_shader); safe_free(_voxelize_pipeline); safe_free(_voxelize_set0); safe_free(_tri_buffer);
    safe_free(_voxel_debug_shader); safe_free(_voxel_debug_pipeline); safe_free(_voxel_debug_set0);
    safe_free(_trace_params_ubo); safe_free(_trace_voxel_set2);
    safe_free(_voxel_albedo);   // rgba8, same res as _voxel_tex
    safe_free(_voxel_normal);   // rgba8
    safe_free(_inject_pipeline); safe_free(_inject_shader); safe_free(_inject_set0);
    safe_free(_dyn_voxelize_set0); safe_free(_dyn_voxelize_pipeline); safe_free(_dyn_voxelize_shader);
    safe_free(_dyn_tri_buffer); safe_free(_dyn_occ); safe_free(_dyn_occ_acc);
    safe_free(_dyn_occ_temporal_set0); safe_free(_dyn_occ_temporal_pipeline); safe_free(_dyn_occ_temporal_shader);
    safe_free(_light_buf);
    safe_free(_slab_clear_set); safe_free(_slab_clear_shader); safe_free(_slab_clear_pipeline);
    for (int L = 0; L < MAX_CLIP; ++L)
    {
        safe_free(_clip_slab_clear_set[L]);
        safe_free(_clip_voxelize_set[L]);
        safe_free(_clip_inject_set[L]);
        safe_free(_voxel_debug_clip_set[L]);
        safe_free(_clip_grid[L]);
    }
    safe_free(_clip_albedo); safe_free(_clip_normal); safe_free(_clip_emission);
    safe_free(_clip_inject_shader); safe_free(_clip_inject_pipeline);
    safe_free(_clip_params_ubo); safe_free(_dummy_clip_tex);

    //PATCH
    safe_free(_patch_add_pipeline); safe_free(_patch_add_shader); safe_free(_patch_add_set0);
    safe_free(_patch_clear_pipeline); safe_free(_patch_clear_shader); safe_free(_patch_clear_set0);
    safe_free(_patch_lookup_pipeline); safe_free(_patch_lookup_shader); safe_free(_patch_lookup_set0);
    safe_free(_patch_indirect_pipeline); safe_free(_patch_indirect_shader); safe_free(_patch_indirect_set0);
    safe_free(_patch_trace_pipeline); safe_free(_patch_trace_shader); safe_free(_patch_trace_set0);
    safe_free(_patch_world); safe_free(_patch_keys); safe_free(_patch_alloc); safe_free(_patch_buckets); safe_free(_patch_live);
    safe_free(_patch_add_set1); safe_free(_patch_lookup_set1); safe_free(_probe_radiance); safe_free(_patch_indirect_buf); safe_free(_voxel_linear_sampler);
    safe_free(_patch_gather_shader); safe_free(_patch_gather_pipeline); safe_free(_patch_gather_set0);
    safe_free(_cascade_buf);
    safe_free(_patch_merge_pipeline); safe_free(_patch_merge_shader); safe_free(_patch_merge_set0);
    safe_free(_reduced_radiance); safe_free(_patch_reduce_set0); safe_free(_patch_reduce_shader); safe_free(_patch_reduce_pipeline);

    safe_free(_sdf_set_writeA); safe_free(_sdf_set_writeB);
    safe_free(_sdf_pipeline); safe_free(_sdf_shader); safe_free(_sdf_tex);
    safe_free(_sdf_seed_a); safe_free(_sdf_seed_b);

    safe_free(_color_sampler);
    safe_free(_dummy_color_tex);
    safe_free(_composite_set0);
    safe_free(_composite_set1);
    safe_free(_composite_pipeline);
    safe_free(_composite_shader);
    safe_free(_albedo_sampler);
    safe_free(_dummy_albedo_tex);

    safe_free(_depth_sampler);
    safe_free(_normal_sampler);
    safe_free(_debug_tex);
    safe_free(_camera_ubo);
    safe_free(_irradiance_tex);
    for (int dir = 0; dir < 6; ++dir)
    {
        for (int i = 0; i < (int) _aniso_views[dir].size(); ++i)
            if (_aniso_views[dir][i].is_valid()) _rd->free_rid(_aniso_views[dir][i]);
        _aniso_views[dir].clear();
        safe_free(_voxel_aniso[dir]);
    }
    safe_free(_mip_aniso_pipeline); safe_free(_mip_aniso_shader);
    for (int i = 0; i < (int) _emis_mip_views.size(); ++i)
        if (_emis_mip_views[i].is_valid()) _rd->free_rid(_emis_mip_views[i]);
    _emis_mip_views.clear();
    safe_free(_emis_mip_pipeline); safe_free(_emis_mip_shader);
    safe_free(_voxel_emission);
    safe_free(_irradiance_half);
    safe_free(_upsample_shader); safe_free(_upsample_pipeline);
    safe_free(_upsample_set0);
    safe_free(_upsample_set1);
    safe_free(_upsample_set2);
    safe_free(_point_sampler);

    safe_free(_atrous_shader); safe_free(_atrous_pipeline);
    safe_free(_irradiance_half_b);                  // ping-pong scratch (half-res, == _irradiance_half)
    safe_free(_atrous_set0_h2s); safe_free(_atrous_set0_s2h);  // static ping-pong: half→scratch / scratch→half
    safe_free(_atrous_set1);

    _initialized = false;
}


// ── Main entry point ──────────────────────────────────────────────────────────

// ── Dispatch entry point ───────────────────────────────────────────────────
void CRadianceCascade::dispatch(RID p_depth, RID p_normalRoughness, RID p_color, Vector2i p_size)
{
    if (Engine::get_singleton()->is_editor_hint()) return;   // GI runs only at game runtime, never in the editor
    // The per-frame driver. First call (or after a resize) lazily builds all GPU
    // resources, scans scene geometry + lights and does the initial bake, then
    // returns. Steady state runs the full GI chain in order:
    //   bake voxels if dirty -> dynamic voxelize -> probe clear/add/trace/merge ->
    //   gather (half-res) -> a-trous denoise -> upsample -> composite.
    // _debug_view short-circuits to a single visualization where useful.
    if (_screen_size != p_size)
    {
        _needs_reinit = true;
        _initialized = false;
    }

    if (!_initialized)
    {
        if (_needs_reinit)
        {
            _free_rids();
            _needs_reinit = false;
        }

        // First-time setup. Resolve the anchor (camera's CharacterBody3D parent, or the
        // camera itself) and the current scene root WITHOUT relying on this node's
        // parent/owner — so it works whether placed in a scene or spawned at runtime
        // by the plugin manager.
        Node3D* anchor = _find_player();
        Vector3 anchor_pos = anchor ? anchor->get_global_position() : Vector3();
        if (!_clip_origins_inited)
        {
            _init_clip_origins(anchor_pos);
            _clip_origins_inited = true;
        }
        set_voxel_region(anchor_pos);

        _init_pipelines(p_size);

        SceneTree* st = get_tree();
        Node* scene_root = st ? st->get_current_scene() : nullptr;
        scan_static_geometry(scene_root);
        _upload_lights(scene_root);
        _bake_voxels(); _voxel_dirty = false;
        return;
    }

    if (_gpu_profile)
    {
        uint32_t n = _rd->get_captured_timestamps_count();
        double prev = 0.0; bool have_prev = false;
        for (uint32_t i = 0; i < n; ++i)
        {
            String nm = _rd->get_captured_timestamp_name(i);
            if (!nm.begins_with("rc_")) continue;
            double t = (double) _rd->get_captured_timestamp_gpu_time(i);   // raw is ns on this build
            if (have_prev)
                print_line(vformat("[GPU] %-16s %.1f us", nm, (t - prev) / 1000.0));   // nm = pass ending here
            prev = t; have_prev = true;
        }
    }

    _sdf_amortize_step();        // advance the level-0 flood ~2 passes/frame

    _rebuild_per_frame_sets(p_depth, p_normalRoughness, p_color);
    if (!_patch_add_set1.is_valid() || !_patch_lookup_set1.is_valid() || !_composite_set1.is_valid()) return;
    if (_camera_dirty) { _update_camera_ubo(_pending_proj, _pending_view); _camera_dirty = false; }

    if (_cascade_dirty) { _build_cascade_table(); _cascade_dirty = false; }

    auto ensure_voxels = [&] { if ((_voxel_dirty || _relight_frames > 0 || _relight_track_frames > 0 || _clip_relight_frames > 0) && !_chunks.empty()) { _bake_voxels(); _voxel_dirty = false; } };

    _poll_dynamic_lights();   // moved DYNAMIC lights → arm a relight; ticked by ensure_voxels() below

    if (_debug_view == DEBUG_VOXEL) { ensure_voxels(); _dispatch_voxel_debug(); _dispatch_composite(); return; }
    if (_debug_view == DEBUG_PATCHES) { _dispatch_patch_clear(); _dispatch_patch_add(); _dispatch_patch_lookup(0); _dispatch_composite(); return; }
    if (_debug_view == DEBUG_PATCHES_RADIANCE) { _dispatch_patch_clear(); _dispatch_patch_add(); _dispatch_patch_lookup(1); _dispatch_composite(); return; }
    if (_debug_view == DEBUG_PROBE_TRACE) { ensure_voxels(); _dispatch_dynamic_voxelize(); _dispatch_patch_clear(); _dispatch_patch_add(); _dispatch_patch_trace(); _dispatch_patch_lookup(1); _dispatch_composite(); return; }

    // DEBUG_OFF and DEBUG_GATHER both run the full chain; composite decides blend vs raw
    if (_gpu_profile) _rd->capture_timestamp("rc_begin");
    ensure_voxels();
    _poll_slab_job();
    _dispatch_dynamic_voxelize();   if (_gpu_profile) _rd->capture_timestamp("rc_dyn_voxelize");
    _dispatch_dyn_occ_temporal();
    _dispatch_patch_clear();        if (_gpu_profile) _rd->capture_timestamp("rc_clear");
    _dispatch_patch_add();          if (_gpu_profile) _rd->capture_timestamp("rc_add");
    _dispatch_patch_trace();        if (_gpu_profile) _rd->capture_timestamp("rc_trace");
    _dispatch_patch_merge();        if (_gpu_profile) _rd->capture_timestamp("rc_merge");
    _dispatch_patch_gather();       if (_gpu_profile) _rd->capture_timestamp("rc_gather");
    _dispatch_irradiance_atrous(p_depth, p_normalRoughness);
    _dispatch_irradiance_upsample(p_depth, p_normalRoughness);  if (_gpu_profile) _rd->capture_timestamp("rc_upsample");
    _dispatch_composite();          if (_gpu_profile) _rd->capture_timestamp("rc_composite");

    _debug_print_probe_counts();
}


// ── Camera / cascade table ────────────────────────────────────────────────────

void CRadianceCascade::_update_camera_ubo(const Projection& proj, const Transform3D& view)
{
    // Pack both directions of the camera transform so probe passes can go
    // screen->world (inverse) to place probes and world->screen (forward) to
    // reproject. Stored column-major to match GLSL mat4 layout.
    if (!_camera_ubo.is_valid()) return;

    RCCameraData cam{};

    auto copy_proj = [] (float* dst, const Projection& p)
        {
            for (int col = 0; col < 4; ++col)
                for (int row = 0; row < 4; ++row)
                    dst[col * 4 + row] = p.columns[col][row];
        };

    auto copy_transform = [] (float* dst, const Transform3D& t)
        {
            for (int col = 0; col < 3; ++col)
                for (int row = 0; row < 3; ++row)
                    dst[col * 4 + row] = t.basis.rows[row][col];
            dst[0 * 4 + 3] = 0.0f; dst[1 * 4 + 3] = 0.0f; dst[2 * 4 + 3] = 0.0f;
            dst[3 * 4 + 0] = t.origin.x;
            dst[3 * 4 + 1] = t.origin.y;
            dst[3 * 4 + 2] = t.origin.z;
            dst[3 * 4 + 3] = 1.0f;
        };

    copy_proj(cam.inv_proj, proj.inverse());
    copy_transform(cam.inv_view, view.inverse());
    copy_proj(cam.fwd_proj, proj);
    copy_transform(cam.fwd_view, view);  // view transform = world→view when passed as camera_transform.inverse() — see note below

    PackedByteArray bytes;
    bytes.resize(sizeof(RCCameraData));
    memcpy(bytes.ptrw(), &cam, sizeof(RCCameraData));
    _rd->buffer_update(_camera_ubo, 0, sizeof(RCCameraData), bytes);
}

void CRadianceCascade::_build_cascade_table()
{
    // Define the cascade hierarchy and lay out its GPU buffers. Per cascade c:
    //   spacing       — probe spacing in world units (grows by _cascade_scale^c)
    //   t_start/t_end — the ray interval it covers; each cascade picks up where the
    //                   finer one stopped, so together they tile [0, far) exactly once
    //   oct_res/dirs/aperture — angular resolution (dirs = oct_res^2 octahedral dirs)
    //   *_off/*_cap   — this cascade's slice of the shared slot space (buckets, keys,
    //                   world, radiance). Coarser cascade = fewer probes, more dirs.
    // Cheap to rebuild, so the dist/step/scale knobs just re-run this.
    const float    ray0 = 0.25f;   // drives the INTERVAL schedule (coverage) — unchanged
    const float    interval_factor = 4.0f;
    const uint32_t oct[RC_CASCADES] = { 4,4,8,8,8 };

    const uint32_t pcap[RC_CASCADES] = { 1u << 20, 1u << 19, 1u << 16, 1u << 14, 1u << 12 };

    const float spacing0 = 0.25f;

    uint32_t boff = 0, roff = 0;
    float    t = 0.0f;
    for (uint32_t c = 0; c < RC_CASCADES; ++c)
    {
        CascadeDesc& cd = _cascades[c];
        float kc = powf(_cascade_scale, float(c));            // == 2^c when scale==2

        cd.spacing = spacing0 * kc * _dist_mult;              // was spc[c] * _dist_mult
        cd.oct_res = oct[c];
        cd.dirs = oct[c] * oct[c];
        cd.aperture = 2.0f / float(oct[c]);

        float len = ray0 * kc * interval_factor * _step_mult;   // nominal interval length (was ray0 * (1u<<c) * ...)
        cd.t_start = t;
        cd.t_end = t + len * (1.0f + _interval_overlap);   // near cone overruns the seam by `overlap` so it,
                                                           // not the offset coarse probes, owns occlusion there
        t += len;                                          // next cascade still starts at the un-extended seam

        cd.probe_cap = pcap[c];          // soft live-count cap (stats only; not a storage bound)
        cd.bucket_cap = pcap[c] * 2u;     // real slot count → load factor 0.5

        // SLOT-KEYED: buckets, probe_world/keys, and radiance all share ONE slot space.
        // probe_off MUST equal bucket_off, and every region advances by bucket_cap.
        cd.bucket_off = boff;
        cd.probe_off = boff;             // == bucket_off (parallel arrays indexed by slot_local)
        cd.rad_off = roff;
        cd._p0 = 0u;

        boff += cd.bucket_cap;                 // buckets / world / keys advance by bucket_cap
        roff += cd.bucket_cap * cd.dirs;       // radiance advances by bucket_cap * dirs
    }
    _total_buckets = boff;
    _total_probes = boff;                      // probe_world/keys sized = Σ bucket_cap (== _total_buckets)
    _total_rad = roff;                       // radiance sized = Σ (bucket_cap * dirs)

    PackedByteArray b; b.resize(sizeof(_cascades)); memcpy(b.ptrw(), _cascades, sizeof(_cascades));
    if (!_cascade_buf.is_valid()) _cascade_buf = _rd->storage_buffer_create(sizeof(_cascades), b);
    else                          _rd->buffer_update(_cascade_buf, 0, sizeof(_cascades), b);
    ERR_FAIL_COND_MSG(!_cascade_buf.is_valid(), "RC: cascade table buffer failed");
}


// ── Per-pipeline dispatch ─────────────────────────────────────────────────────

void CRadianceCascade::_dispatch_composite()
{
    RCCompositePushConstants pc{};
    pc.screen_width = (uint32_t) _screen_size.x;
    pc.screen_height = (uint32_t) _screen_size.y;
    pc.gi_intensity = _gi_intensity;

    auto composite_debug_mode = [&] () -> uint32_t
        {
            if (_debug_view == DEBUG_PATCHES || _debug_view == DEBUG_PATCHES_RADIANCE || _debug_view == DEBUG_PROBE_TRACE || _debug_view == DEBUG_VOXEL) return 1;
            if (_debug_view == DEBUG_GATHER) return 2;
            return 0;
        };
    pc.debug_mode = composite_debug_mode();

    PackedByteArray pc_bytes; pc_bytes.resize(sizeof(RCCompositePushConstants));
    memcpy(pc_bytes.ptrw(), &pc, sizeof(RCCompositePushConstants));

    int gx = Math::ceil((float) _screen_size.x / 8.0f);
    int gy = Math::ceil((float) _screen_size.y / 8.0f);

    int64_t list = _rd->compute_list_begin();
    _rd->compute_list_bind_compute_pipeline(list, _composite_pipeline);
    _rd->compute_list_bind_uniform_set(list, _composite_set0, 0);
    _rd->compute_list_bind_uniform_set(list, _composite_set1, 1);
    _rd->compute_list_set_push_constant(list, pc_bytes, pc_bytes.size());
    _rd->compute_list_dispatch(list, gx, gy, 1);
    _rd->compute_list_end();
}

void CRadianceCascade::_dispatch_patch_clear()
{
    RCPatchClearPC pc{}; pc.total_buckets = _total_buckets; pc.num_cascades = RC_CASCADES;
    PackedByteArray b; b.resize(sizeof(pc)); memcpy(b.ptrw(), &pc, sizeof(pc));
    int64_t l = _rd->compute_list_begin();
    _rd->compute_list_bind_compute_pipeline(l, _patch_clear_pipeline);
    _rd->compute_list_bind_uniform_set(l, _patch_clear_set0, 0);
    _rd->compute_list_set_push_constant(l, b, b.size());
    _rd->compute_list_dispatch(l, Math::ceil((float) _total_buckets / 256.0f), 1, 1);
    _rd->compute_list_end();
}

void CRadianceCascade::_dispatch_patch_add()
{
    // One list, two dispatches. They touch DISJOINT cascade hash regions (c0 vs c1..N-1)
    // and disjoint alloc_count[] entries, so no barrier between them is needed.
    int64_t l = _rd->compute_list_begin();
    _rd->compute_list_bind_compute_pipeline(l, _patch_add_pipeline);
    _rd->compute_list_bind_uniform_set(l, _patch_add_set0, 0);
    _rd->compute_list_bind_uniform_set(l, _patch_add_set1, 1);   // SAME per-frame depth the gather samples

    // ── Pass A — cascade 0 ONLY, on the GATHER's exact lattice ───────────────────────────
    // The gather reads only c0 and reconstructs world from this depth at _half_size. Spawn c0
    // on that identical grid + depth + sampler so every cell the gather can read exists. This
    // is the run you already verified (magenta vanished). c0 needs no quality knob — its
    // coverage is dictated by its consumer, not by a density choice.
    {
        RCPatchAddPC pc{};
        pc.screen_width = (uint32_t) _half_size.x;
        pc.screen_height = (uint32_t) _half_size.y;
        pc.cascade_begin = 0u;
        pc.cascade_end = 1u;
        pc.z_near = _z_near; pc.z_far = _z_far;
        PackedByteArray b; b.resize(sizeof(pc)); memcpy(b.ptrw(), &pc, sizeof(pc));
        _rd->compute_list_set_push_constant(l, b, b.size());
        _rd->compute_list_dispatch(l, Math::ceil((float) _half_size.x / 8.0f),
            Math::ceil((float) _half_size.y / 8.0f), 1);
    }

    // ── Pass B — coarse cascades 1..N-1, on the SEED lattice ─────────────────────────────
    // _probe_seed_max_h stays the quality knob HERE. Coarse cells are large, so a seed-vs-
    // gather sub-lattice offset lands inside the same cell and never opens a hole. c0 skipped.
    {
        RCPatchAddPC pc{};
        uint32_t seed_h = MIN((uint32_t) _screen_size.y, (uint32_t) _probe_seed_max_h);
        uint32_t seed_w = (uint32_t) lround(double(_screen_size.x) * double(seed_h) / double(_screen_size.y));
        pc.screen_width = seed_w;
        pc.screen_height = seed_h;
        pc.cascade_begin = 1u;
        pc.cascade_end = RC_CASCADES;
        pc.z_near = _z_near; pc.z_far = _z_far;
        PackedByteArray b; b.resize(sizeof(pc)); memcpy(b.ptrw(), &pc, sizeof(pc));
        _rd->compute_list_set_push_constant(l, b, b.size());
        _rd->compute_list_dispatch(l, Math::ceil((float) seed_w / 8.0f),
            Math::ceil((float) seed_h / 8.0f), 1);
    }

    _rd->compute_list_end();
}

void CRadianceCascade::_dispatch_patch_trace()
{
    // Two steps: turn the live per-cascade probe counts into indirect dispatch
    // args, then trace each cascade over exactly its live probes (indirect, so the
    // CPU never reads back the count). Each probe cone-traces the voxel grid.
    {   // build N indirect arg-sets from the live per-cascade counts
        RCPatchIndirectPC pc{}; pc.num_cascades = RC_CASCADES; pc.local_size = 64;
        PackedByteArray b; b.resize(sizeof(pc)); memcpy(b.ptrw(), &pc, sizeof(pc));
        int64_t l = _rd->compute_list_begin();
        _rd->compute_list_bind_compute_pipeline(l, _patch_indirect_pipeline);
        _rd->compute_list_bind_uniform_set(l, _patch_indirect_set0, 0);
        _rd->compute_list_set_push_constant(l, b, b.size());
        _rd->compute_list_dispatch(l, 1, 1, 1);   // local_size_x=16 covers up to 16 cascades
        _rd->compute_list_end();
    }
    {   // trace each cascade — disjoint radiance regions, one list, no inter-cascade barrier
        int64_t l = _rd->compute_list_begin();
        _rd->compute_list_bind_compute_pipeline(l, _patch_trace_pipeline);
        _rd->compute_list_bind_uniform_set(l, _patch_trace_set0, 0);
        _rd->compute_list_bind_uniform_set(l, _trace_voxel_set2, 2);
        for (uint32_t c = 0; c < RC_CASCADES; ++c)
        {
            RCPatchTracePC pc{}; pc.cascade = c; pc.local_trans = _local_transmittance ? 1u : 0u;
            PackedByteArray b; b.resize(sizeof(pc)); memcpy(b.ptrw(), &pc, sizeof(pc));
            _rd->compute_list_set_push_constant(l, b, b.size());
            _rd->compute_list_dispatch_indirect(l, _patch_indirect_buf, c * 12);  // 3 uints/cascade
        }
        _rd->compute_list_end();
    }
}

void CRadianceCascade::_dispatch_patch_merge()
{
    // The Radiance Cascades merge: walk far->near, folding cascade c+1's radiance
    // into c so the cheap near field inherits the far field's angular detail. When
    // c+1 has more directions than c (oct ratio > 1), a pre-reduce pass first
    // averages the extra directions into scratch and merge reads that.
    for (int c = (int) RC_CASCADES - 2; c >= 0; --c)
    {
        uint32_t r = MAX(_cascades[c + 1].oct_res / _cascades[c].oct_res, 1u);

        if (r > 1u)   // pre-reduce c+1 → cascade-c oct resolution into the scratch (c1<-c2, c3<-c4)
        {
            RCPatchMergePC rp{}; rp.cascade = (uint32_t) c;
            PackedByteArray rb; rb.resize(sizeof(rp)); memcpy(rb.ptrw(), &rp, sizeof(rp));
            uint32_t threads = _cascades[c + 1].bucket_cap * _cascades[c].dirs;   // one per (c+1 slot, reduced dir)
            int64_t lr = _rd->compute_list_begin();
            _rd->compute_list_bind_compute_pipeline(lr, _patch_reduce_pipeline);
            _rd->compute_list_bind_uniform_set(lr, _patch_reduce_set0, 0);
            _rd->compute_list_set_push_constant(lr, rb, rb.size());
            _rd->compute_list_dispatch(lr, (threads + 63u) / 64u, 1u, 1u);   // local_size_x = 64
            _rd->compute_list_end();   // barrier: reduce writes scratch → merge reads it
        }

        RCPatchMergePC pc{}; pc.cascade = (uint32_t) c;
        PackedByteArray b; b.resize(sizeof(pc)); memcpy(b.ptrw(), &pc, sizeof(pc));
        int64_t l = _rd->compute_list_begin();
        _rd->compute_list_bind_compute_pipeline(l, _patch_merge_pipeline);
        _rd->compute_list_bind_uniform_set(l, _patch_merge_set0, 0);
        _rd->compute_list_set_push_constant(l, b, b.size());
        _rd->compute_list_dispatch_indirect(l, _patch_indirect_buf, (RC_CASCADES + (uint32_t) c) * 12);
        _rd->compute_list_end();   // barrier before the next (finer) cascade
    }
}

void CRadianceCascade::_dispatch_patch_gather()
{
    // For each half-res pixel: reconstruct its world point from depth, find the
    // covering cascade-0 probe and sample its directional radiance into the half-res
    // irradiance buffer. Pixels whose rays escaped the scene fall back to sky color.
    RCPatchGatherPC pc{};
    pc.screen_width = _half_size.x;          // ← HALF (drives grid + UV denominator)
    pc.screen_height = _half_size.y;
    pc.z_near = _z_near; pc.z_far = _z_far;
    pc.sky_color[0] = _sky_color.x; pc.sky_color[1] = _sky_color.y; pc.sky_color[2] = _sky_color.z;
    PackedByteArray b; b.resize(sizeof(pc)); memcpy(b.ptrw(), &pc, sizeof(pc));

    int64_t l = _rd->compute_list_begin();
    _rd->compute_list_bind_compute_pipeline(l, _patch_gather_pipeline);
    _rd->compute_list_bind_uniform_set(l, _patch_gather_set0, 0);
    _rd->compute_list_bind_uniform_set(l, _patch_lookup_set1, 1);   // your per-frame depth/normal set
    _rd->compute_list_set_push_constant(l, b, b.size());
    _rd->compute_list_dispatch(l, Math::ceil((float) _half_size.x / 8.0f),
        Math::ceil((float) _half_size.y / 8.0f), 1);
    _rd->compute_list_end();
}

void CRadianceCascade::_dispatch_patch_lookup(uint32_t kind)
{
    RCPatchLookupPC pc{};
    pc.screen_width = _screen_size.x; pc.screen_height = _screen_size.y;
    pc.debug_kind = kind; pc.cascade = MIN(_debug_cascade, RC_CASCADES - 1u);
    pc.z_near = _z_near; pc.z_far = _z_far;
    pc.sky_color[0] = _sky_color.x; pc.sky_color[1] = _sky_color.y; pc.sky_color[2] = _sky_color.z;
    PackedByteArray b; b.resize(sizeof(pc)); memcpy(b.ptrw(), &pc, sizeof(pc));
    int64_t l = _rd->compute_list_begin();
    _rd->compute_list_bind_compute_pipeline(l, _patch_lookup_pipeline);
    _rd->compute_list_bind_uniform_set(l, _patch_lookup_set0, 0);
    _rd->compute_list_bind_uniform_set(l, _patch_lookup_set1, 1);
    _rd->compute_list_set_push_constant(l, b, b.size());
    _rd->compute_list_dispatch(l, Math::ceil((float) _screen_size.x / 8.0f), Math::ceil((float) _screen_size.y / 8.0f), 1);
    _rd->compute_list_end();
}

void CRadianceCascade::_dispatch_voxel_mips()
{
    // Build the 6-axis anisotropic radiance mip chain from the level-0 grid. Each
    // level reads the previous (finer) one, so the loop must run in order. These
    // directional mips are what the cone trace samples to keep occlusion sharp.
    // aniso level L (0..levels-1) writes grid-mip (L+1); reads grid-mip L.
    for (int L = 0; L < _aniso_levels; ++L)
    {
        uint32_t dst_res = (uint32_t) (_vox_res >> (L + 1));

        TypedArray<RDUniform> u;
        // dst 0..5 = this aniso level's six views
        for (int dir = 0; dir < 6; ++dir)
        {
            Ref<RDUniform> ud; ud.instantiate();
            ud->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
            ud->set_binding(dir); ud->add_id(_aniso_views[dir][L]); u.append(ud);
        }
        // src grid (binding 6): real grid's mip-L view  (only sampled when src_is_aniso==0, i.e. L==0)
        {
            Ref<RDUniform> us; us.instantiate();
            us->set_uniform_type(RenderingDevice::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE);
            us->set_binding(6); us->add_id(_voxel_sampler);
            us->add_id(_voxel_tex);   // real grid; texelFetch LOD 0 = mip 0. Only read when src_is_aniso==0 (L==0)
            u.append(us);
        }
        // src aniso 7..12 = previous aniso level (L-1); for L==0 bind level 0 as placeholder (unused)
        for (int dir = 0; dir < 6; ++dir)
        {
            Ref<RDUniform> us; us.instantiate();
            us->set_uniform_type(RenderingDevice::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE);
            us->set_binding(7 + dir); us->add_id(_voxel_sampler);
            us->add_id(_aniso_views[dir][L == 0 ? 0 : (L - 1)]);
            u.append(us);
        }
        RID set = _rd->uniform_set_create(u, _mip_aniso_shader, 0);
        ERR_FAIL_COND_MSG(!set.is_valid(), vformat("RC: aniso mip set %d failed", L));

        RCMipAnisoPC pc{}; pc.dst_res = dst_res; pc.src_is_aniso = (L == 0) ? 0u : 1u;
        PackedByteArray b; b.resize(sizeof(pc)); memcpy(b.ptrw(), &pc, sizeof(pc));

        uint32_t g = (dst_res + 3) / 4;
        int64_t l = _rd->compute_list_begin();
        _rd->compute_list_bind_compute_pipeline(l, _mip_aniso_pipeline);
        _rd->compute_list_bind_uniform_set(l, set, 0);
        _rd->compute_list_set_push_constant(l, b, b.size());
        _rd->compute_list_dispatch(l, g, g, g);
        _rd->compute_list_end();
        _rd->free_rid(set);   // per-level transient set
    }
}

void CRadianceCascade::_dispatch_emission_mips()
{
    // Isotropic mean mip of the emission grid. Level L reads view[L], writes view[L+1].
    // dst_res halves each level. One transient set + one compute list (auto-barrier) per level.
    for (int L = 1; L < _vox_mip_levels; ++L)
    {
        uint32_t dst_res = (uint32_t) (_vox_res >> L);   // resolution of the level being written

        TypedArray<RDUniform> u;
        // binding 0: src = finer level (L-1), sampled
        Ref<RDUniform> us; us.instantiate();
        us->set_uniform_type(RenderingDevice::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE);
        us->set_binding(0); us->add_id(_voxel_sampler); us->add_id(_emis_mip_views[L - 1]);
        u.append(us);
        // binding 1: dst = this level (L), image
        Ref<RDUniform> ud; ud.instantiate();
        ud->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
        ud->set_binding(1); ud->add_id(_emis_mip_views[L]);
        u.append(ud);

        RID set = _rd->uniform_set_create(u, _emis_mip_shader, 0);
        ERR_FAIL_COND_MSG(!set.is_valid(), vformat("RC: emis mip set %d failed", L));

        RCVoxelMipPC pc{};            // reuse your existing mip PC: {uint dst_res; uint _p0,_p1,_p2;}
        pc.dst_res = dst_res;
        PackedByteArray b; b.resize(sizeof(pc)); memcpy(b.ptrw(), &pc, sizeof(pc));

        uint32_t g = (dst_res + 3) / 4;   // local_size 4³
        int64_t l = _rd->compute_list_begin();
        _rd->compute_list_bind_compute_pipeline(l, _emis_mip_pipeline);
        _rd->compute_list_bind_uniform_set(l, set, 0);
        _rd->compute_list_set_push_constant(l, b, b.size());
        _rd->compute_list_dispatch(l, g, g, g);
        _rd->compute_list_end();          // end-per-level = barrier; level L+1 reads L's output
        _rd->free_rid(set);
    }
}

void CRadianceCascade::_dispatch_voxel_debug()
{
    int L = CLAMP(_debug_clip_level, 0, _clip_levels - 1);
    RID set = (L == 0) ? _voxel_debug_set0 : _voxel_debug_clip_set[L];
    if (!set.is_valid()) { L = 0; set = _voxel_debug_set0; }     // coarse not built yet → level 0
    LevelGeom g = _level_geom(L);

    RCVoxelDebugPC pc{};
    pc.sw = _screen_size.x; pc.sh = _screen_size.y; pc.res = (uint32_t) _vox_res; pc.max_steps = 512;
    pc.vox_origin[0] = g.origin.x; pc.vox_origin[1] = g.origin.y; pc.vox_origin[2] = g.origin.z;
    pc.vox_extent[0] = g.extent;   pc.vox_extent[1] = g.extent;   pc.vox_extent[2] = g.extent;
    pc.voxel_size = g.voxel_size;  pc.occ_threshold = 0.3f;
    PackedByteArray b; b.resize(sizeof(pc)); memcpy(b.ptrw(), &pc, sizeof(pc));
    int64_t l = _rd->compute_list_begin();
    _rd->compute_list_bind_compute_pipeline(l, _voxel_debug_pipeline);
    _rd->compute_list_bind_uniform_set(l, set, 0);
    _rd->compute_list_set_push_constant(l, b, b.size());
    _rd->compute_list_dispatch(l, Math::ceil((float) _screen_size.x / 8.0f), Math::ceil((float) _screen_size.y / 8.0f), 1);
    _rd->compute_list_end();
}

void CRadianceCascade::_dispatch_dynamic_voxelize()
{
    // Rasterize this frame's moving proxy tris into the binary dynamic-occupancy
    // grid (rebuilt from scratch each frame). _dispatch_dyn_occ_temporal then smears
    // it over time so fast movers leave a short occlusion trail instead of flickering.
    _rd->texture_clear(_dyn_occ, Color(0, 0, 0, 0), 0, 1, 0, 1);     // wipe last frame
    if (_dyn_tri_count == 0) return;
    RCDynVoxelizePC pc{};
    pc.vox_origin[0] = _vox_origin.x; pc.vox_origin[1] = _vox_origin.y; pc.vox_origin[2] = _vox_origin.z;
    pc.vox_extent[0] = _vox_extent.x; pc.vox_extent[1] = _vox_extent.y; pc.vox_extent[2] = _vox_extent.z;
    pc.res = (uint32_t) _vox_res; pc.tri_count = _dyn_tri_count;
    PackedByteArray bb; bb.resize(sizeof(pc)); memcpy(bb.ptrw(), &pc, sizeof(pc));
    int64_t l = _rd->compute_list_begin();
    _rd->compute_list_bind_compute_pipeline(l, _dyn_voxelize_pipeline);
    _rd->compute_list_bind_uniform_set(l, _dyn_voxelize_set0, 0);
    _rd->compute_list_set_push_constant(l, bb, bb.size());
    _rd->compute_list_dispatch(l, Math::ceil((float) _dyn_tri_count / 64.0f), 1, 1);
    _rd->compute_list_end();
}

void CRadianceCascade::_dispatch_dyn_occ_temporal()
{
    RCDynOccTemporalPC pc{}; pc.res = (uint32_t) _vox_res; pc.decay = _dyn_occ_decay;
    PackedByteArray b; b.resize(sizeof(pc)); memcpy(b.ptrw(), &pc, sizeof(pc));
    int64_t l = _rd->compute_list_begin();
    _rd->compute_list_bind_compute_pipeline(l, _dyn_occ_temporal_pipeline);
    _rd->compute_list_bind_uniform_set(l, _dyn_occ_temporal_set0, 0);
    _rd->compute_list_set_push_constant(l, b, b.size());
    uint32_t g = (uint32_t) Math::ceil(_vox_res / 4.0f);
    _rd->compute_list_dispatch(l, g, g, g);
    _rd->compute_list_end();
}

void CRadianceCascade::_dispatch_irradiance_atrous(RID depth_tex, RID normal_tex)
{
    // Edge-aware à-trous denoise of the half-res irradiance: a few ping-pong passes
    // with a doubling hole size (1,2,4,...), weighted by depth + normal so it blurs
    // noise without bleeding across geometry edges. Even pass count ends in _irradiance_half.
    if (_atrous_passes <= 0) return;

    {   // per-frame set1: depth(0) + normal(1), NEAREST — same RIDs/sampler the upsample uses
        TypedArray<RDUniform> u;
        Ref<RDUniform> ud; ud.instantiate();
        ud->set_uniform_type(RenderingDevice::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE);
        ud->set_binding(0); ud->add_id(_point_sampler); ud->add_id(depth_tex); u.append(ud);
        Ref<RDUniform> un; un.instantiate();
        un->set_uniform_type(RenderingDevice::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE);
        un->set_binding(1); un->add_id(_point_sampler); un->add_id(normal_tex); u.append(un);
        if (_atrous_set1.is_valid()) _rd->free_rid(_atrous_set1);
        _atrous_set1 = _rd->uniform_set_create(u, _atrous_shader, 1);
    }

    RCAtrousPC pc{};
    pc.half_w = _half_size.x; pc.half_h = _half_size.y;
    pc.z_near = _z_near; pc.z_far = _z_far;
    pc.sigma_z = sigma_z; pc.normal_pow = normal_pow;
    int gx = (int) Math::ceil(_half_size.x / 8.0f);
    int gy = (int) Math::ceil(_half_size.y / 8.0f);

    for (int i = 0; i < _atrous_passes; ++i)
    {        // EVEN count → ends in _irradiance_half
        pc.step = 1 << i;                            // à-trous hole size: 1,2,4,8,...
        RID  s0 = (i & 1) ? _atrous_set0_s2h : _atrous_set0_h2s;
        PackedByteArray b; b.resize(sizeof(pc)); memcpy(b.ptrw(), &pc, sizeof(pc));
        int64_t l = _rd->compute_list_begin();        // separate list per pass = barrier (ping-pong safe)
        _rd->compute_list_bind_compute_pipeline(l, _atrous_pipeline);
        _rd->compute_list_bind_uniform_set(l, s0, 0);
        _rd->compute_list_bind_uniform_set(l, _atrous_set1, 1);
        _rd->compute_list_set_push_constant(l, b, b.size());
        _rd->compute_list_dispatch(l, gx, gy, 1);
        _rd->compute_list_end();
    }
}

void CRadianceCascade::_dispatch_irradiance_upsample(RID depth_tex, RID normal_tex)
{
    // Bilateral upsample of the half-res irradiance to full-res, weighted by depth +
    // normal so it stays sharp at edges; strong discontinuities additionally do a
    // direct full-res cascade-0 gather (set2) to avoid haloing on silhouettes.
    {   // per-frame set1: depth(0) + normal(1) — the SAME RIDs you feed gather set1
        TypedArray<RDUniform> u;
        Ref<RDUniform> ud; ud.instantiate();
        ud->set_uniform_type(RenderingDevice::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE);
        ud->set_binding(0); ud->add_id(_point_sampler); ud->add_id(depth_tex); u.append(ud);
        Ref<RDUniform> un; un.instantiate();
        un->set_uniform_type(RenderingDevice::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE);
        un->set_binding(1); un->add_id(_point_sampler); un->add_id(normal_tex); u.append(un);
        if (_upsample_set1.is_valid()) _rd->free_rid(_upsample_set1);
        _upsample_set1 = _rd->uniform_set_create(u, _upsample_shader, 1);
    }

    RCUpsamplePC pc{};
    pc.full_w = _screen_size.x; pc.full_h = _screen_size.y;
    pc.half_w = _half_size.x;   pc.half_h = _half_size.y;
    pc.z_near = _z_near; pc.z_far = _z_far;
    pc.sigma_z = sigma_z; pc.normal_pow = normal_pow;
    pc.sky_color[0] = _sky_color.x; pc.sky_color[1] = _sky_color.y; pc.sky_color[2] = _sky_color.z;
    pc._pad = 0.0f;
    PackedByteArray b; b.resize(sizeof(pc)); memcpy(b.ptrw(), &pc, sizeof(pc));

    int64_t l = _rd->compute_list_begin();
    _rd->compute_list_bind_compute_pipeline(l, _upsample_pipeline);
    _rd->compute_list_bind_uniform_set(l, _upsample_set0, 0);
    _rd->compute_list_bind_uniform_set(l, _upsample_set1, 1);
    _rd->compute_list_bind_uniform_set(l, _upsample_set2, 2);
    _rd->compute_list_set_push_constant(l, b, b.size());
    _rd->compute_list_dispatch(l, Math::ceil((float) _screen_size.x / 8.0f),
        Math::ceil((float) _screen_size.y / 8.0f), 1);
    _rd->compute_list_end();
}

void CRadianceCascade::_dispatch_slab_clear(Vector3i lo, Vector3i dim, RID set)
{
    RID s = set.is_valid() ? set : _slab_clear_set;
    RCSlabClearPC pc{ {lo.x,lo.y,lo.z}, (uint32_t) _vox_res, {dim.x,dim.y,dim.z}, 0 };
    PackedByteArray b; b.resize(sizeof(pc)); memcpy(b.ptrw(), &pc, sizeof(pc));
    int64_t l = _rd->compute_list_begin();
    _rd->compute_list_bind_compute_pipeline(l, _slab_clear_pipeline);
    _rd->compute_list_bind_uniform_set(l, s, 0);
    _rd->compute_list_set_push_constant(l, b, b.size());
    _rd->compute_list_dispatch(l, (uint32_t) Math::ceil(dim.x / 4.0f), (uint32_t) Math::ceil(dim.y / 4.0f), (uint32_t) Math::ceil(dim.z / 4.0f));
    _rd->compute_list_end();
}

void CRadianceCascade::_dispatch_voxelize(Vector3i lo, Vector3i dim, const Vector3& extent, RID set)
{
    if (_tri_count == 0) return;
    RCVoxelizePC pc{ {extent.x,extent.y,extent.z}, (uint32_t) _vox_res,
                     {lo.x,lo.y,lo.z}, _tri_count, {dim.x,dim.y,dim.z}, 0 };
    PackedByteArray b; b.resize(sizeof(pc)); memcpy(b.ptrw(), &pc, sizeof(pc));
    int64_t l = _rd->compute_list_begin();
    _rd->compute_list_bind_compute_pipeline(l, _voxelize_pipeline);
    _rd->compute_list_bind_uniform_set(l, set, 0);
    _rd->compute_list_set_push_constant(l, b, b.size());
    _rd->compute_list_dispatch(l, (uint32_t) Math::ceil(_tri_count / 64.0f), 1, 1);
    _rd->compute_list_end();
}

void CRadianceCascade::_dispatch_slab_inject(Vector3i lo, Vector3i dim, float blend_alpha)
{
    // Inject direct light into a level-0 voxel sub-region [lo, lo+dim). phase carries
    // the toroidal wrap so the shader maps world voxels onto the scrolled grid;
    // blend_alpha < 1 cross-fades over the previous lighting (relight schedule).
    RCInjectSlabPC pc{};
    pc.light_count = _light_count;
    pc.vox_origin[0] = _vox_origin.x; pc.vox_origin[1] = _vox_origin.y; pc.vox_origin[2] = _vox_origin.z;
    pc.voxel_size = _vox_extent.x / float(_vox_res);
    pc.blend_alpha = blend_alpha;
    pc.slab_lo[0] = lo.x; pc.slab_lo[1] = lo.y; pc.slab_lo[2] = lo.z; pc.res = (uint32_t) _vox_res;
    pc.slab_dim[0] = dim.x; pc.slab_dim[1] = dim.y; pc.slab_dim[2] = dim.z;
    pc.phase[0] = _vox_phase.x; pc.phase[1] = _vox_phase.y; pc.phase[2] = _vox_phase.z;
    PackedByteArray b; b.resize(sizeof(pc)); memcpy(b.ptrw(), &pc, sizeof(pc));
    int64_t l = _rd->compute_list_begin();
    _rd->compute_list_bind_compute_pipeline(l, _inject_pipeline);
    _rd->compute_list_bind_uniform_set(l, _inject_set0, 0);
    _rd->compute_list_set_push_constant(l, b, b.size());
    _rd->compute_list_dispatch(l, (uint32_t) Math::ceil(dim.x / 4.0f), (uint32_t) Math::ceil(dim.y / 4.0f), (uint32_t) Math::ceil(dim.z / 4.0f));
    _rd->compute_list_end();
}

void CRadianceCascade::_dispatch_clip_slab_inject(int L, Vector3i lo, Vector3i dim, float blend_alpha)
{
    RCClipInjectSlabPC pc{};
    pc.light_count = (uint32_t) MIN(_lights_accum.size(), (size_t) MAX_LIGHTS);
    pc.sun_dir[0] = _sun_dir.x;   pc.sun_dir[1] = _sun_dir.y;   pc.sun_dir[2] = _sun_dir.z;
    pc.sun_color[0] = _sun_color.x; pc.sun_color[1] = _sun_color.y; pc.sun_color[2] = _sun_color.z;
    pc.blend_alpha = blend_alpha;
    pc.slab_lo[0] = lo.x;  pc.slab_lo[1] = lo.y;  pc.slab_lo[2] = lo.z;  pc.res = (uint32_t) _vox_res;
    pc.slab_dim[0] = dim.x; pc.slab_dim[1] = dim.y; pc.slab_dim[2] = dim.z;
    LevelGeom g = _level_geom(L);
    pc.voxel_size = g.voxel_size;

    int R = _vox_res;
    Vector3i ov(
        (int) Math::floor(g.origin.x / g.voxel_size),
        (int) Math::floor(g.origin.y / g.voxel_size),
        (int) Math::floor(g.origin.z / g.voxel_size));
    pc.phase[0] = ((ov.x % R) + R) % R;
    pc.phase[1] = ((ov.y % R) + R) % R;
    pc.phase[2] = ((ov.z % R) + R) % R;
    PackedByteArray b; b.resize(sizeof(pc)); memcpy(b.ptrw(), &pc, sizeof(pc));
    int64_t l = _rd->compute_list_begin();
    _rd->compute_list_bind_compute_pipeline(l, _clip_inject_pipeline);
    _rd->compute_list_bind_uniform_set(l, _clip_inject_set[L], 0);
    _rd->compute_list_set_push_constant(l, b, b.size());
    _rd->compute_list_dispatch(l, (uint32_t) Math::ceil(dim.x / 4.0f), (uint32_t) Math::ceil(dim.y / 4.0f), (uint32_t) Math::ceil(dim.z / 4.0f));
    _rd->compute_list_end();
}

void CRadianceCascade::_dispatch_relight(float alpha)
{
    int R = _vox_res; float vs = _vox_extent.x / float(R);
    Vector3i ov((int) Math::floor(_vox_origin.x / vs), (int) Math::floor(_vox_origin.y / vs), (int) Math::floor(_vox_origin.z / vs));
    Vector3i dim(R, R, R);
    _dispatch_slab_inject(ov, dim, alpha);   // blend _voxel_source → Lo, mirror to _voxel_tex
    _dispatch_voxel_mips();                  // mips for the bounce cones
}

void CRadianceCascade::_dispatch_clip_relight(float alpha)
{
    int R = _vox_res;
    for (int L = 1; L < _clip_levels; ++L)
    {
        LevelGeom g = _level_geom(L);
        Vector3i ov(
            (int) Math::floor(g.origin.x / g.voxel_size),
            (int) Math::floor(g.origin.y / g.voxel_size),
            (int) Math::floor(g.origin.z / g.voxel_size));
        _dispatch_clip_slab_inject(L, ov, Vector3i(R, R, R), alpha);   // reads baked attrs, EMAs sun
    }
}


// ── Voxel baking / clipmap ────────────────────────────────────────────────────

void CRadianceCascade::_bake_voxels()
{
    // Consume the per-level dirty flags: re-voxelize + re-inject whichever levels
    // changed. If nothing baked this frame, advance an in-progress relight instead —
    // a smoothstep cross-fade after a light change, a constant-alpha EMA while
    // dynamic lights move, and a separate coarse fade for sun rotation.
    bool baked = false;
    if (_level_dirty[0]) { _full_revoxelize(); _level_dirty[0] = false; baked = true; }
    for (int L = 1; L < _clip_levels; ++L)
    {
        if (!_level_dirty[L]) continue;
        _rebuild_level_tris(L);
        if (_tri_count > 0) _bake_coarse_level(L);
        _level_dirty[L] = false; baked = true;
    }
    _update_trace_params();

    if (baked) { _relight_frames = _relight_frames_max; }   // bake holds prior; arm smoothstep
    else if (_relight_frames > 0)
    {
        const int N = _relight_frames_max;
        const int i = N - _relight_frames + 1;
        auto ss = [] (float t) { t = Math::clamp(t, 0.0f, 1.0f); return t * t * (3.0f - 2.0f * t); };
        float s0 = ss(float(i - 1) / float(N));
        float s1 = ss(float(i) / float(N));
        float a = (s1 - s0) / Math::max(1.0f - s0, 1e-4f);
        _dispatch_relight(a);
        --_relight_frames;
    }
    else if (_relight_track_frames > 0)                     // NEW: continuous dynamic-light tracking
    {
        _dispatch_relight(_relight_track_alpha);
        --_relight_track_frames;
    }

    if (!baked && _clip_relight_frames > 0)            // sun rotation → coarse GI tracks too
    {
        _dispatch_clip_relight(_relight_track_alpha);
        --_clip_relight_frames;
    }
}

void CRadianceCascade::_full_revoxelize()                         // first bake / geometry change / teleport
{
    // Rebuild level 0 from scratch: pack its tris, clear the grid, voxelize, build the
    // SDF, inject direct light, then regenerate the radiance + emission mips.
    float vs = _vox_extent.x / float(_vox_res);
    auto ph = [&] (float o) { int v = (int) Math::floor(o / vs); return ((v % _vox_res) + _vox_res) % _vox_res; };
    _vox_phase = Vector3i(ph(_vox_origin.x), ph(_vox_origin.y), ph(_vox_origin.z));
    int R = _vox_res;
    Vector3i ov(int(Math::floor(_vox_origin.x / vs)), int(Math::floor(_vox_origin.y / vs)), int(Math::floor(_vox_origin.z / vs)));
    Vector3i dim(R, R, R);
    _rebuild_level_tris(0);
    _dispatch_slab_clear(ov, dim);
    _dispatch_voxelize(ov, dim, _vox_extent, _voxelize_set0);
    _build_sdf();
    _dispatch_slab_inject(ov, dim, 0.0f);                  // CHANGED: fade alpha (was implicit 1.0)
    _dispatch_voxel_mips(); _dispatch_emission_mips();
}

void CRadianceCascade::_bake_coarse_level(int L)   // full rebuild: geometry change / teleport
{
    LevelGeom g = _level_geom(L);
    float vs = g.voxel_size; int R = _vox_res;
    Vector3i ov(int(Math::floor(g.origin.x / vs)), int(Math::floor(g.origin.y / vs)), int(Math::floor(g.origin.z / vs)));
    Vector3  extent(g.extent, g.extent, g.extent);
    Vector3i dim(R, R, R);
    _dispatch_slab_clear(ov, dim, _clip_slab_clear_set[L]);          // NEW: wipe whole grid (fixes ghost)
    _dispatch_voxelize(ov, dim, extent, _clip_voxelize_set[L]);     // cell = world_voxel % res
    _dispatch_clip_slab_inject(L, ov, dim);                         // was _dispatch_inject_into
}

void CRadianceCascade::_update_trace_params()
{
    // Refresh the UBOs the trace reads: level-0 placement (_trace_params_ubo) and the
    // per-level origin/size table for every clip level (_clip_params_ubo). Called after
    // any bake or recenter so the shader's world<->voxel mapping stays correct.
    RCTraceParams p{};
    p.vox_origin[0] = _vox_origin.x; p.vox_origin[1] = _vox_origin.y; p.vox_origin[2] = _vox_origin.z;
    p.vox_extent[0] = _vox_extent.x; p.vox_extent[1] = _vox_extent.y; p.vox_extent[2] = _vox_extent.z;
    p.voxel_size = _vox_extent.x / float(_vox_res); p.max_steps = (uint32_t) _trace_max_steps;
    PackedByteArray b; b.resize(sizeof(p)); memcpy(b.ptrw(), &p, sizeof(p));
    if (!_trace_params_ubo.is_valid()) _trace_params_ubo = _rd->uniform_buffer_create(sizeof(p), b);
    else _rd->buffer_update(_trace_params_ubo, 0, sizeof(p), b);

    struct alignas(16) GpuLevel { float origin[3]; float voxel_size; float extent[3]; float _pad; };
    struct alignas(16) GpuClip { GpuLevel lvl[5]; uint32_t num_levels; uint32_t _p[3]; } gc{};
    float base = _vox_extent.x / float(_vox_res);
    for (int L = 0; L < MAX_CLIP; ++L)
    {
        float vs = base * float(1 << L);
        float ext = float(_vox_res) * vs;
        Vector3 o = (L == 0) ? _vox_origin : _clip_origin[L];
        gc.lvl[L] = { {o.x,o.y,o.z}, vs, {ext,ext,ext}, 0.0f };
    }
    gc.num_levels = (uint32_t) _clip_levels;
    PackedByteArray cb; cb.resize(sizeof(gc)); memcpy(cb.ptrw(), &gc, sizeof(gc));
    _rd->buffer_update(_clip_params_ubo, 0, cb.size(), cb);
}

void CRadianceCascade::_rebuild_level_tris(int L)
{
    // Gather every static triangle overlapping level L's world window into _tri_buffer:
    // pull each chunk's cached subdivision when ready, subdivide on the fly otherwise.
    // This is the CPU feed for the voxelize pass.
    LevelGeom g = _level_geom(L);
    Vector3 mar(_voxel_tri_cap, _voxel_tri_cap, _voxel_tri_cap);   // gather margin = cache clip band
    Vector3 wlo = g.origin - mar;
    Vector3 whi = g.origin + Vector3(g.extent, g.extent, g.extent) + mar;

    _tri_accum.clear();
    for (int cz = _chunk_coord(wlo.z); cz <= _chunk_coord(whi.z); ++cz)
        for (int cy = _chunk_coord(wlo.y); cy <= _chunk_coord(whi.y); ++cy)
            for (int cx = _chunk_coord(wlo.x); cx <= _chunk_coord(whi.x); ++cx)
            {
                auto it = _chunks.find(_chunk_key(cx, cy, cz));
                if (it == _chunks.end()) continue;
                Chunk& ch = it->second;
                if (ch.sub0_state.load(std::memory_order_acquire) == (uint8_t) Sub0State::Built)
                    _tri_accum.insert(_tri_accum.end(), ch.sub0.begin(), ch.sub0.end());
                else
                    _emit_chunk_into(ch, cx, cy, cz, _tri_accum);
            }

    _tri_count = (uint32_t) (_tri_accum.size() / FLOATS_PER_TRI);
    if (_tri_count > MAX_STATIC_TRIS) { _tri_count = MAX_STATIC_TRIS; WARN_PRINT("RC: window tri cap hit"); }
    if (_tri_count == 0) return;
    PackedByteArray b; b.resize(_tri_count * FLOATS_PER_TRI * sizeof(float));
    memcpy(b.ptrw(), _tri_accum.data(), b.size());
    _rd->buffer_update(_tri_buffer, 0, b.size(), b);

    if (L == 0) _evict_sub0(g.origin, g.extent);   // unchanged: keep eviction level-0-centred
}


// ── SDF flood ─────────────────────────────────────────────────────────────────

void CRadianceCascade::_build_sdf()
{
    // Jump-flood the half-res distance field in one shot: seed from occupancy (mode 0),
    // flood with halving step sizes R/2..1 (mode 1, ping-pong between the two seeds),
    // then finalize to distances (mode 2). _sdf_amortize_step spreads this over frames.
    const int R = _vox_res / 2;
    const uint32_t g = (uint32_t) ((R + 3) / 4);
    auto run = [&] (RID set, uint32_t mode, int step)
        {
            RCSdfPC pc{}; pc.mode = mode; pc.step = step; pc.res = (uint32_t) R;
            pc.phase[0] = _vox_phase.x / 2; pc.phase[1] = _vox_phase.y / 2; pc.phase[2] = _vox_phase.z / 2;
            PackedByteArray b; b.resize(sizeof(pc)); memcpy(b.ptrw(), &pc, sizeof(pc));
            int64_t l = _rd->compute_list_begin();
            _rd->compute_list_bind_compute_pipeline(l, _sdf_pipeline);
            _rd->compute_list_bind_uniform_set(l, set, 0);
            _rd->compute_list_set_push_constant(l, b, b.size());
            _rd->compute_list_dispatch(l, g, g, g);
            _rd->compute_list_end();
        };
    run(_sdf_set_writeA, 0u, 0);
    bool seed_in_a = true;
    for (int step = R / 2; step >= 1; step >>= 1) { run(seed_in_a ? _sdf_set_writeB : _sdf_set_writeA, 1u, step); seed_in_a = !seed_in_a; }
    run(seed_in_a ? _sdf_set_writeB : _sdf_set_writeA, 2u, 0);
}

// amortized: one INIT, then FLOODs (descending step), then FINAL — 2 passes/frame
void CRadianceCascade::_sdf_amortize_begin() { _sdf_pass = 0; _sdf_seed_in_a = true; }

void CRadianceCascade::_sdf_amortize_step()
{
    if (_sdf_pass < 0) return;
    const int R = _vox_res / 2;                                               // SDF runs at HALF resolution
    int flood = 0; for (int s = R / 2; s >= 1; s >>= 1) ++flood;              // ~log2(R) flood passes
    int total = 1 + flood + 1;
    const uint32_t g = (uint32_t) ((R + 3) / 4);
    auto run = [&] (RID set, uint32_t mode, int step)
        {
            RCSdfPC pc{}; pc.mode = mode; pc.step = step; pc.res = (uint32_t) R;
            pc.phase[0] = _vox_phase.x / 2; pc.phase[1] = _vox_phase.y / 2; pc.phase[2] = _vox_phase.z / 2;
            PackedByteArray b; b.resize(sizeof(pc)); memcpy(b.ptrw(), &pc, sizeof(pc));
            int64_t l = _rd->compute_list_begin();
            _rd->compute_list_bind_compute_pipeline(l, _sdf_pipeline);
            _rd->compute_list_bind_uniform_set(l, set, 0);
            _rd->compute_list_set_push_constant(l, b, b.size());
            _rd->compute_list_dispatch(l, g, g, g);
            _rd->compute_list_end();
        };
    for (int n = 0; n < 2 && _sdf_pass < total; ++n, ++_sdf_pass)
    {
        if (_sdf_pass == 0) { run(_sdf_set_writeA, 0u, 0); _sdf_seed_in_a = true; }
        else if (_sdf_pass <= flood)
        {
            int step = 1 << (flood - _sdf_pass);                      // res/2 … 1
            run(_sdf_seed_in_a ? _sdf_set_writeB : _sdf_set_writeA, 1u, step);
            _sdf_seed_in_a = !_sdf_seed_in_a;
        }
        else
        {
            run(_sdf_seed_in_a ? _sdf_set_writeB : _sdf_set_writeA, 2u, 0);
        }
    }
    if (_sdf_pass >= total) _sdf_pass = -1;
}


// ── Async slab recenter ───────────────────────────────────────────────────────

void CRadianceCascade::_enqueue_recenter(int L, const Vector3& o_old, const Vector3& o_new)
{
    // Build the async recenter job for level L: work out which 1..3 voxel shells the
    // origin shift exposes, record the new origin/phase, and hand the (CPU-only) tri
    // build to a worker. NO _rd here — the GPU commit happens later in _poll_slab_job.
    // A shift >= the whole grid is a teleport: L0 full-rebuilds, coarse falls back to a sync bake.
    LevelGeom g = _level_geom(L);
    int   R = _vox_res;
    float vs = g.voxel_size;                          // ← per-level, was _vox_extent.x/R (== level 0)
    Vector3i d(int(Math::round((o_new.x - o_old.x) / vs)),
        int(Math::round((o_new.y - o_old.y) / vs)),
        int(Math::round((o_new.z - o_old.z) / vs)));
    Vector3i ovn(int(Math::floor(o_new.x / vs)), int(Math::floor(o_new.y / vs)), int(Math::floor(o_new.z / vs)));

    SlabJob& job = _slab_job[L];
    job = SlabJob{};
    job.level = L;
    job.new_origin = o_new;
    auto ph = [&] (float o) { int v = (int) Math::floor(o / vs); return ((v % R) + R) % R; };
    job.new_phase = Vector3i(ph(o_new.x), ph(o_new.y), ph(o_new.z));

    for (int axis = 0; axis < 3; ++axis)
    {
        int delta = (axis == 0) ? d.x : (axis == 1) ? d.y : d.z;
        if (delta == 0) continue;
        if (Math::abs(delta) >= R)
        {
            // teleport: L0 does a full revoxelize in the commit; coarse falls back to the sync bake path.
            if (L == 0) { job.full_rebuild = true; job.slabs.clear(); break; }
            else { _level_dirty[L] = true; _voxel_dirty = true; job = SlabJob{}; return; }  // no async job
        }
        Vector3i lo = ovn, hi = ovn + Vector3i(R, R, R) - Vector3i(1, 1, 1);
        int base = (axis == 0) ? ovn.x : (axis == 1) ? ovn.y : ovn.z;
        if (delta > 0) { int v = base + R - delta;   (axis == 0 ? lo.x : axis == 1 ? lo.y : lo.z) = v; }
        else { int v = base - delta - 1;   (axis == 0 ? hi.x : axis == 1 ? hi.y : hi.z) = v; }
        SlabJob::Slab s; s.lo = lo; s.hi = hi; s.dim = hi - lo + Vector3i(1, 1, 1);
        job.slabs.push_back(std::move(s));
    }

    job.active = true;
    job.task = WorkerThreadPool::get_singleton()->add_task(
        callable_mp(this, &CRadianceCascade::_slab_job_worker).bind(L), true, "RC slab build");
}

void CRadianceCascade::_slab_job_worker(int L)
{
    // Worker thread: build each shell's triangle list from the chunks (shared-locked).
    // Pure CPU — touches no RenderingDevice state — so it runs off the render thread.
    float vs = _level_geom(L).voxel_size;             // ← per-level
    SlabJob& job = _slab_job[L];
    for (SlabJob::Slab& s : job.slabs)
    {
        s.tris.clear();
        Vector3 wlo(s.lo.x * vs, s.lo.y * vs, s.lo.z * vs);
        Vector3 whi((s.hi.x + 1) * vs, (s.hi.y + 1) * vs, (s.hi.z + 1) * vs);
        for (int cz = _chunk_coord(wlo.z); cz <= _chunk_coord(whi.z); ++cz)
            for (int cy = _chunk_coord(wlo.y); cy <= _chunk_coord(whi.y); ++cy)
                for (int cx = _chunk_coord(wlo.x); cx <= _chunk_coord(whi.x); ++cx)
                {
                    Chunk* ch = nullptr;
                    {
                        std::shared_lock<std::shared_mutex> rl(_chunks_mutex);
                        auto it = _chunks.find(_chunk_key(cx, cy, cz)); if (it == _chunks.end()) continue; ch = &it->second;
                    }
                    if (ch->sub0_state.load(std::memory_order_acquire) == (uint8_t) Sub0State::Built)
                        s.tris.insert(s.tris.end(), ch->sub0.begin(), ch->sub0.end());
                    else _emit_chunk_into(*ch, cx, cy, cz, s.tris);
                }
        s.tri_count = (uint32_t) (s.tris.size() / FLOATS_PER_TRI);
        if (s.tri_count > MAX_STATIC_TRIS) { s.tri_count = MAX_STATIC_TRIS; WARN_PRINT("RC: async slab tri cap hit"); }
        s.tris_bytes.resize(s.tris.size() * sizeof(float)); memcpy(s.tris_bytes.ptrw(), s.tris.data(), s.tris_bytes.size());
    }
}

void CRadianceCascade::_poll_slab_job()
{
    // Render thread: commit the first finished recenter job. Advances that level's
    // origin/phase, uploads the worker's tris and re-voxelizes only the exposed shells
    // (or full-rebuilds on teleport). One commit per frame — _tri_buffer is shared.
    for (int L = 0; L < _clip_levels; ++L)
    {
        SlabJob& job = _slab_job[L];
        if (!job.active) continue;
        if (job.task != -1 && !WorkerThreadPool::get_singleton()->is_task_completed(job.task)) continue;

        // advance this level's origin/phase
        if (L == 0) { _vox_origin = job.new_origin; _vox_phase = job.new_phase; }
        else { _clip_origin[L] = job.new_origin; }     // coarse phase is derived in the inject from origin

        if (job.full_rebuild)
        {                                // L0 teleport only
            _full_revoxelize(); _sdf_amortize_begin();
        }
        else
        {
            LevelGeom g = _level_geom(L);
            Vector3 extent(g.extent, g.extent, g.extent);
            for (SlabJob::Slab& s : job.slabs)
            {
                if (s.tri_count == 0) continue;
                _rd->buffer_update(_tri_buffer, 0, s.tris_bytes.size(), s.tris_bytes);
                _tri_count = s.tri_count;
                if (L == 0)
                {
                    _dispatch_slab_clear(s.lo, s.dim);
                    _dispatch_voxelize(s.lo, s.dim, _vox_extent, _voxelize_set0);
                    _dispatch_slab_inject(s.lo, s.dim, _relight_alpha);
                }
                else
                {
                    _dispatch_slab_clear(s.lo, s.dim, _clip_slab_clear_set[L]);
                    _dispatch_voxelize(s.lo, s.dim, extent, _clip_voxelize_set[L]);
                    _dispatch_clip_slab_inject(L, s.lo, s.dim);
                }
            }
            if (L == 0) _sdf_amortize_begin();                 // SDF is the L0 flood only
        }

        _update_trace_params();
        request_relight();                                     // Stage A keeps the global relight; Stage B scopes it
        job = SlabJob{};
        return;                                                // ONE commit per frame — _tri_buffer is shared
    }
}


// ── Chunk system (static geometry storage + subdivision) ──────────────────────

void CRadianceCascade::_scan_into_chunks(Node* root)
{
    // Walk the subtree for MeshInstance3D surfaces and add their world-space tris to
    // the chunk map. Skips dynamic bodies, GI_DISABLED meshes, the "gi_ignore" group
    // and the editor HighlightMesh; pulls per-surface emission + averaged albedo.
    if (!root) return;
    TypedArray<Node> nodes = root->find_children("*", "MeshInstance3D", true, false);
    for (int n = 0; n < nodes.size(); ++n)
    {
        MeshInstance3D* mi = Object::cast_to<MeshInstance3D>(nodes[n]);
        if (!mi) continue;

        Node* par = mi->get_parent();
        if (Object::cast_to<RigidBody3D>(par) || Object::cast_to<CharacterBody3D>(par) ||
            Object::cast_to<Skeleton3D>(par)) continue;
        if (mi->is_in_group("gi_ignore")) continue;
        if (mi->get_gi_mode() == GeometryInstance3D::GI_MODE_DISABLED) continue;
        if (mi->get_name() == StringName("HighlightMesh")) continue;

        Ref<Mesh> mesh = mi->get_mesh();
        if (mesh.is_null()) continue;

        Transform3D xform = mi->get_global_transform();
        for (int s = 0; s < mesh->get_surface_count(); ++s)
        {
            Array arr = mesh->surface_get_arrays(s);
            if (arr.is_empty()) continue;
            PackedVector3Array verts = arr[Mesh::ARRAY_VERTEX];
            if (verts.is_empty()) continue;
            PackedInt32Array idx;
            if (arr[Mesh::ARRAY_INDEX].get_type() != Variant::NIL) idx = arr[Mesh::ARRAY_INDEX];

            Color emission(0, 0, 0), albedo(0.8f, 0.8f, 0.8f);
            Ref<BaseMaterial3D> sm = mi->get_active_material(s);   // was StandardMaterial3D — catches ORM too
            if (sm.is_valid())
            {
                albedo = _material_albedo_linear(sm);
                if (sm->get_feature(BaseMaterial3D::FEATURE_EMISSION))
                    emission = sm->get_emission().srgb_to_linear() * sm->get_emission_energy_multiplier();
            }
            add_static_surface(verts, idx, xform, emission, albedo);
        }
    }
}

void CRadianceCascade::scan_static_geometry(Node* root)
{
    if (!root) return;
    clear_static_geometry();
    _scan_into_chunks(root);
    bake_static_geometry();
}

void CRadianceCascade::add_static_surface(PackedVector3Array verts, PackedInt32Array idx,
    Transform3D xform, Color emission, Color albedo)
{
    // Transform each tri to world space and file it into every chunk its AABB touches,
    // invalidating those chunks' subdivision cache. Holds the chunk write lock briefly.
    const Vector3* vp = verts.ptr();
    auto add = [&] (Vector3 a, Vector3 b, Vector3 c)
        {
            a = xform.xform(a); b = xform.xform(b); c = xform.xform(c);
            SrcTri t{ a, b, c, emission, albedo };
            Vector3 mn(MIN(a.x, MIN(b.x, c.x)), MIN(a.y, MIN(b.y, c.y)), MIN(a.z, MIN(b.z, c.z)));
            Vector3 mx(MAX(a.x, MAX(b.x, c.x)), MAX(a.y, MAX(b.y, c.y)), MAX(a.z, MAX(b.z, c.z)));
            for (int cz = _chunk_coord(mn.z); cz <= _chunk_coord(mx.z); ++cz)
                for (int cy = _chunk_coord(mn.y); cy <= _chunk_coord(mx.y); ++cy)
                    for (int cx = _chunk_coord(mn.x); cx <= _chunk_coord(mx.x); ++cx)
                    {
                        Chunk& ch = _chunks[_chunk_key(cx, cy, cz)];
                        ch.raw.push_back(t);
                        ch.sub0_state.store((uint8_t) Sub0State::Unbuilt, std::memory_order_relaxed);  // invalidate
                    }
        };
    {
        std::unique_lock<std::shared_mutex> wl(_chunks_mutex);   // brief: structural inserts only
        if (idx.size() > 0)
        {
            const int32_t* ip = idx.ptr();
            for (int i = 0; i + 2 < idx.size(); i += 3) add(vp[ip[i]], vp[ip[i + 1]], vp[ip[i + 2]]);
        }
        else { for (int i = 0; i + 2 < verts.size(); i += 3) add(vp[i], vp[i + 1], vp[i + 2]); }
    }
}

void CRadianceCascade::bake_static_geometry()
{
    for (int L = 0; L < MAX_CLIP; ++L) _level_dirty[L] = true;
    _voxel_dirty = true;
}

void CRadianceCascade::_emit_tri(std::vector<float>& out, const Vector3& a, const Vector3& b,
    const Vector3& c, const Color& e, const Color& albedo, float max_edge,
    const Vector3& gmin, const Vector3& gmax)
{
    // Recursively split a triangle until every edge <= max_edge, appending each leaf as
    // 5 vec4s (a,b,c, emission, albedo) to `out`. Tris outside [gmin,gmax] are culled.
    // The edge cap guarantees the voxelizer never skips a cell a long thin tri crosses.
    Vector3 tmin(MIN(a.x, MIN(b.x, c.x)), MIN(a.y, MIN(b.y, c.y)), MIN(a.z, MIN(b.z, c.z)));
    Vector3 tmax(MAX(a.x, MAX(b.x, c.x)), MAX(a.y, MAX(b.y, c.y)), MAX(a.z, MAX(b.z, c.z)));
    if (tmax.x<gmin.x || tmin.x>gmax.x || tmax.y<gmin.y || tmin.y>gmax.y || tmax.z<gmin.z || tmin.z>gmax.z) return;

    float lab = a.distance_to(b), lbc = b.distance_to(c), lca = c.distance_to(a);
    float lmax = MAX(lab, MAX(lbc, lca));
    if (lmax <= max_edge)
    {
        auto push = [&] (Vector3 v) { out.push_back(v.x); out.push_back(v.y); out.push_back(v.z); out.push_back(0.0f); };
        push(a); push(b); push(c);
        Color em = e.srgb_to_linear();      out.push_back(em.r); out.push_back(em.g); out.push_back(em.b); out.push_back(0.0f);
        Color al = albedo.srgb_to_linear(); out.push_back(al.r); out.push_back(al.g); out.push_back(al.b); out.push_back(0.0f);
        return;
    }
    if (lab >= lbc && lab >= lca) { Vector3 m = (a + b) * 0.5f; _emit_tri(out, a, m, c, e, albedo, max_edge, gmin, gmax); _emit_tri(out, m, b, c, e, albedo, max_edge, gmin, gmax); }
    else if (lbc >= lca) { Vector3 m = (b + c) * 0.5f; _emit_tri(out, a, b, m, e, albedo, max_edge, gmin, gmax); _emit_tri(out, a, m, c, e, albedo, max_edge, gmin, gmax); }
    else { Vector3 m = (c + a) * 0.5f; _emit_tri(out, a, b, m, e, albedo, max_edge, gmin, gmax); _emit_tri(out, b, c, m, e, albedo, max_edge, gmin, gmax); }
}

void CRadianceCascade::_emit_chunk_into(const Chunk& ch, int cx, int cy, int cz, std::vector<float>& out)
{
    float max_edge = _voxel_tri_cap;
    Vector3 lo(cx * CHUNK_SIZE, cy * CHUNK_SIZE, cz * CHUNK_SIZE);
    Vector3 m(max_edge, max_edge, max_edge);
    Vector3 gmin = lo - m, gmax = lo + Vector3(CHUNK_SIZE, CHUNK_SIZE, CHUNK_SIZE) + m;
    for (const SrcTri& t : ch.raw)
        _emit_tri(out, t.a, t.b, t.c, t.e, t.al, max_edge, gmin, gmax);
}

void CRadianceCascade::_evict_sub0(const Vector3& l0_origin, float l0_extent)
{
    // Over the cache budget? Drop the subdivision (sub0) of chunks far from the level-0
    // box, keeping their raw tris so they rebuild on demand. CAS guards against racing a
    // worker mid-build.
    if (_chunks.size() < MAX_SUB0_CHUNKS) return;      // cheap-out until over budget
    Vector3 c = l0_origin + Vector3(l0_extent, l0_extent, l0_extent) * 0.5f;
    float keep2 = (l0_extent * 1.5f) * (l0_extent * 1.5f);
    for (auto& kv : _chunks)
    {
        Chunk& ch = kv.second;
        if (ch.sub0_state.load(std::memory_order_acquire) != (uint8_t) Sub0State::Built) continue;  // skip Building/Unbuilt
        // decode key → chunk centre; drop sub0 if far (keep raw)
        int64_t k = kv.first;
        auto d = [] (int64_t v) { return (int) ((v & 0x1FFFFF)) - (1 << 20); };
        Vector3 cc((d(k) + 0.5f) * CHUNK_SIZE, (d(k >> 21) + 0.5f) * CHUNK_SIZE, (d(k >> 42) + 0.5f) * CHUNK_SIZE);
        if ((cc - c).length_squared() > keep2)
        {
            uint8_t exp = (uint8_t) Sub0State::Built;
            if (ch.sub0_state.compare_exchange_strong(exp, (uint8_t) Sub0State::Unbuilt))
            {
                ch.sub0.clear(); ch.sub0.shrink_to_fit();
            }
        }
    }
}

void CRadianceCascade::_kick_sub0_build(int64_t key)
{
    // Atomically claim one chunk (Unbuilt -> Building) and queue its subdivision on a
    // worker. No-op if the chunk is missing or already building/built, so it's safe to
    // spam from the prefetch sweep.
    Chunk* ch = nullptr;
    {
        std::shared_lock<std::shared_mutex> rl(_chunks_mutex);
        auto it = _chunks.find(key);
        if (it == _chunks.end()) return;                    // not generated yet
        ch = &it->second;                                   // node-stable pointer
    }
    uint8_t expected = (uint8_t) Sub0State::Unbuilt;
    if (!ch->sub0_state.compare_exchange_strong(expected, (uint8_t) Sub0State::Building))
        return;                                             // already building or built
    // SEAM A confirmed (you use callable_mp). If .bind marshalling errors, bind _build_sub0_task in
    // _bind_methods and use Callable(this, "_build_sub0_task").bind(key).
    WorkerThreadPool::get_singleton()->add_task(
        callable_mp(this, &CRadianceCascade::_build_sub0_task).bind(key), false, "RC sub0 prebuild");
}

void CRadianceCascade::_build_sub0_task(int64_t key)
{
    Chunk* ch = nullptr;
    {
        std::shared_lock<std::shared_mutex> rl(_chunks_mutex);
        auto it = _chunks.find(key);
        if (it == _chunks.end()) return;                    // evicted/gone before we ran
        ch = &it->second;
    }
    int cx, cy, cz; _decode_chunk_key(key, cx, cy, cz);
    ch->sub0.clear();
    _emit_chunk_into(*ch, cx, cy, cz, ch->sub0);            // reads immutable raw, writes claimed sub0
    ch->sub0_state.store((uint8_t) Sub0State::Built, std::memory_order_release);
}

void CRadianceCascade::_prefetch_sub0_ahead(const Vector3& player_pos)
{
    // Warm the cache: kick subdivision for a small block of chunks ahead of the player's
    // travel direction so they're Built by the time a recenter needs them.
    Vector3 vel = player_pos - _prefetch_last_pos;
    _prefetch_last_pos = player_pos;
    Vector3 dir = vel.length() > 1e-3f ? vel.normalized() : Vector3(0, 0, -1);
    float   reach = _vox_extent.x * 0.75f;                  // tune: must exceed build-latency × top speed
    Vector3 c = player_pos + dir * reach;
    int bx = _chunk_coord(c.x), by = _chunk_coord(c.y), bz = _chunk_coord(c.z);
    const int r = 2;
    for (int dz = -r; dz <= r; ++dz)
        for (int dy = -r; dy <= r; ++dy)
            for (int dx = -r; dx <= r; ++dx)
                _kick_sub0_build(_chunk_key(bx + dx, by + dy, bz + dz));   // no-op if missing / building / built
}

void CRadianceCascade::_decode_chunk_key(int64_t k, int& cx, int& cy, int& cz)
{   // inverse of _chunk_key — same decode used in _evict_sub0
    auto d = [] (int64_t v) { return (int) (v & 0x1FFFFF) - (1 << 20); };
    cx = d(k); cy = d(k >> 21); cz = d(k >> 42);
}

Color CRadianceCascade::_material_albedo_linear(const Ref<BaseMaterial3D>& sm)
{
    // Approximate a surface's albedo as its tint × the 1px average of its albedo texture
    // (cached per texture RID). Voxels are coarse, so one averaged colour per surface is
    // plenty and avoids sampling textures on the GPU during inject.
    Color tint = sm->get_albedo().srgb_to_linear();                 // albedo_color, linear
    Ref<Texture2D> tex = sm->get_texture(BaseMaterial3D::TEXTURE_ALBEDO);
    if (tex.is_null())
        return tint;                                                // flat-colored surface

    RID rid = tex->get_rid();
    if (_albedo_avg_cache.has(rid))
        return _albedo_avg_cache[rid] * tint;

    Color avg(0.6f, 0.6f, 0.6f);                                    // fallback if not CPU-readable
    Ref<Image> img = tex->get_image();
    if (img.is_valid())
    {
        img = img->duplicate();
        if (img->is_compressed()) img->decompress();
        img->resize(1, 1, Image::INTERPOLATE_LANCZOS);             // box-average → 1 px
        avg = img->get_pixel(0, 0).srgb_to_linear();               // average, linear
    }
    _albedo_avg_cache[rid] = avg;
    return avg * tint;
}

void CRadianceCascade::clear_dynamic()
{
    _dyn_tri_accum.clear();
}

void CRadianceCascade::add_dynamic_surface(PackedVector3Array verts, PackedInt32Array idx, Transform3D xform)
{
    const Vector3* vp = verts.ptr();
    auto push = [&] (const Vector3& v)
        {
            _dyn_tri_accum.push_back(v.x); _dyn_tri_accum.push_back(v.y);
            _dyn_tri_accum.push_back(v.z); _dyn_tri_accum.push_back(0.0f);   // 4 floats/vert (xyz + pad)
        };
    if (idx.size() > 0)
    {
        const int32_t* ip = idx.ptr();
        for (int i = 0; i + 2 < idx.size(); i += 3)
        {
            push(xform.xform(vp[ip[i]])); push(xform.xform(vp[ip[i + 1]])); push(xform.xform(vp[ip[i + 2]]));
        }
    }
    else
    {
        for (int i = 0; i + 2 < verts.size(); i += 3)
        {
            push(xform.xform(vp[i])); push(xform.xform(vp[i + 1])); push(xform.xform(vp[i + 2]));
        }
    }
}

void CRadianceCascade::update_dynamic()
{
    _dyn_tri_count = (uint32_t) (_dyn_tri_accum.size() / 12);
    if (_dyn_tri_count > (uint32_t) MAX_DYN_TRIS) _dyn_tri_count = MAX_DYN_TRIS;
    if (_dyn_tri_count == 0) return;
    PackedByteArray b; b.resize(_dyn_tri_count * 12 * sizeof(float));
    memcpy(b.ptrw(), _dyn_tri_accum.data(), b.size());
    _rd->buffer_update(_dyn_tri_buffer, 0, b.size(), b);
}

void CRadianceCascade::scan_dynamic_occluders(Node* root)
{
    // Cache the CollisionShape3Ds we treat as dynamic occluders: a shape under a
    // RigidBody3D/CharacterBody3D whose visual is GI_MODE_DISABLED (so it occludes GI
    // but isn't itself baked as static geometry). update_dynamic_occluders() reads this.
    _dyn_occluder_ids.clear();
    if (!root) return;
    TypedArray<Node> shapes = root->find_children("*", "CollisionShape3D", true, false);
    for (int i = 0; i < shapes.size(); ++i)
    {
        CollisionShape3D* cs = Object::cast_to<CollisionShape3D>(shapes[i]);
        if (!cs || cs->is_disabled() || cs->get_shape().is_null()) continue;

        Node* body = cs->get_parent();
        if (!(Object::cast_to<RigidBody3D>(body) || Object::cast_to<CharacterBody3D>(body))) continue;

        // require a sibling/child visual that is GI-disabled (occluder-only objects)
        GeometryInstance3D* vis = nullptr;
        TypedArray<Node> gis = body->find_children("*", "GeometryInstance3D", true, false);
        if (gis.size() > 0) vis = Object::cast_to<GeometryInstance3D>(gis[0]);
        if (!vis || vis->get_gi_mode() != GeometryInstance3D::GI_MODE_DISABLED) continue;

        _dyn_occluder_ids.push_back(cs->get_instance_id());
    }
}

void CRadianceCascade::update_dynamic_occluders()
{
    // Per-frame: rebuild the dynamic proxy tris from each cached occluder's collision
    // shape (its debug/shadow mesh, transformed to world) and upload them.
    clear_dynamic();
    for (uint64_t id : _dyn_occluder_ids)
    {
        CollisionShape3D* cs = Object::cast_to<CollisionShape3D>(ObjectDB::get_instance(id));  // null if freed
        if (!cs || cs->is_disabled() || cs->get_shape().is_null()) continue;

        Ref<ArrayMesh> dbg = cs->get_shape()->get_debug_mesh();
        if (dbg.is_null()) continue;
        Ref<ArrayMesh> mesh = dbg->get_shadow_mesh();                 // cheaper hull when available
        if (mesh.is_null()) mesh = dbg;

        Transform3D xf = cs->get_global_transform();
        for (int s = 0; s < mesh->get_surface_count(); ++s)
        {
            Array arr = mesh->surface_get_arrays(s);
            if (arr.is_empty()) continue;
            PackedVector3Array verts = arr[Mesh::ARRAY_VERTEX];
            if (verts.is_empty()) continue;
            PackedInt32Array idx;
            if (arr[Mesh::ARRAY_INDEX].get_type() != Variant::NIL) idx = arr[Mesh::ARRAY_INDEX];
            if (idx.is_empty())                                       // non-indexed mesh → trivial index list
            {
                idx.resize(verts.size());
                for (int k = 0; k < verts.size(); ++k) idx[k] = k;
            }
            add_dynamic_surface(verts, idx, xf);
        }
    }
    update_dynamic();
}

Camera3D* CRadianceCascade::_find_camera()
{
    Viewport* vp = get_viewport();
    return vp ? vp->get_camera_3d() : nullptr;
}

Node3D* CRadianceCascade::_find_player()
{
    // The player is the active camera's CharacterBody3D parent (typical FPS rig).
    // If the camera has no such parent, the camera itself is both camera and anchor.
    Camera3D* cam = _find_camera();
    if (!cam) return nullptr;
    CharacterBody3D* body = Object::cast_to<CharacterBody3D>(cam->get_parent());
    return body ? static_cast<Node3D*>(body) : static_cast<Node3D*>(cam);
}


// ── Lights ────────────────────────────────────────────────────────────────────

static bool _rc_light_changed(const RCLightGPU& a, const RCLightGPU& b)
{
    auto d3 = [] (const float* x, const float* y) { return fabsf(x[0] - y[0]) + fabsf(x[1] - y[1]) + fabsf(x[2] - y[2]); };
    const float EPS = 1e-3f;
    return d3(a.position, b.position) > EPS || d3(a.direction, b.direction) > EPS ||
        d3(a.color, b.color) > EPS || fabsf(a.inv_range - b.inv_range) > EPS ||
        fabsf(a.spot_cos_in - b.spot_cos_in) > EPS || fabsf(a.spot_cos_out - b.spot_cos_out) > EPS;
}

bool CRadianceCascade::_light_to_gpu(Light3D* light, RCLightGPU& g)
{
    // Pack a Godot Light3D into the GPU record: linear color × energy × indirect-energy,
    // plus type-specific fields (dir / position+range / spot cones). type encodes the kind
    // (0 dir, 1 omni, 2 spot, 3 area). Returns false for bake-disabled / unhandled lights.
    if (light->get_bake_mode() == Light3D::BAKE_DISABLED) return false;
    g = RCLightGPU{};
    Transform3D xf = light->get_global_transform();
    Color c = light->get_color().srgb_to_linear();
    float e = light->get_param(Light3D::PARAM_ENERGY) * light->get_param(Light3D::PARAM_INDIRECT_ENERGY);
    g.color[0] = c.r * e; g.color[1] = c.g * e; g.color[2] = c.b * e;

    if (Object::cast_to<DirectionalLight3D>(light))
    {
        Vector3 d = xf.basis.xform(Vector3(0, 0, -1)).normalized();
        g.direction[0] = d.x; g.direction[1] = d.y; g.direction[2] = d.z; g.type = 0.0f;
    }
    else if (Object::cast_to<OmniLight3D>(light))
    {
        Vector3 p = xf.origin; float range = light->get_param(Light3D::PARAM_RANGE);
        g.position[0] = p.x; g.position[1] = p.y; g.position[2] = p.z;
        g.inv_range = (range > 0.0f) ? 1.0f / range : 0.0f; g.type = 1.0f;
    }
    else if (Object::cast_to<SpotLight3D>(light))
    {
        Vector3 p = xf.origin; Vector3 d = xf.basis.xform(Vector3(0, 0, -1)).normalized();
        float range = light->get_param(Light3D::PARAM_RANGE);
        float ang = Math::deg_to_rad(light->get_param(Light3D::PARAM_SPOT_ANGLE));
        g.position[0] = p.x; g.position[1] = p.y; g.position[2] = p.z;
        g.direction[0] = d.x; g.direction[1] = d.y; g.direction[2] = d.z;
        g.inv_range = (range > 0.0f) ? 1.0f / range : 0.0f; g.type = 2.0f;
        g.spot_cos_out = Math::cos(ang); g.spot_cos_in = Math::cos(ang * 0.85f);
    }
    else if (Object::cast_to<AreaLight3D>(light))
    {
        Vector3 p = xf.origin; Vector3 n = xf.basis.xform(Vector3(0, 0, -1)).normalized();
        float range = light->get_param(Light3D::PARAM_RANGE);
        g.position[0] = p.x; g.position[1] = p.y; g.position[2] = p.z;
        g.direction[0] = n.x; g.direction[1] = n.y; g.direction[2] = n.z;
        g.inv_range = (range > 0.0f) ? 1.0f / range : 0.0f; g.type = 3.0f;
    }
    else return false;   // a Light3D subtype we don't handle
    return true;
}

void CRadianceCascade::_scan_lights_into(Node* node, std::vector<RCLightGPU>& out, std::vector<uint8_t>& dyn)
{
    if (auto* light = Object::cast_to<Light3D>(node))
    {
        RCLightGPU g;
        if (out.size() < (size_t) MAX_LIGHTS && _light_to_gpu(light, g))
        {
            out.push_back(g);
            dyn.push_back(light->get_bake_mode() == Light3D::BAKE_DYNAMIC ? 1 : 0);
        }
    }
    for (int i = 0; i < node->get_child_count(); ++i) _scan_lights_into(node->get_child(i), out, dyn);
}

void CRadianceCascade::_ensure_fallback_light()
{
    if (!_lights_accum.empty()) return;
    RCLightGPU g{}; Vector3 d = _sun_dir.normalized();
    g.direction[0] = d.x; g.direction[1] = d.y; g.direction[2] = d.z; g.type = 0.0f;
    g.color[0] = _sun_color.x; g.color[1] = _sun_color.y; g.color[2] = _sun_color.z;
    _lights_accum.push_back(g); _light_dynamic.push_back(0);
}

void CRadianceCascade::_upload_light_buffer()
{
    _light_count = (uint32_t) _lights_accum.size();
    PackedByteArray b; b.resize(_light_count * sizeof(RCLightGPU));
    memcpy(b.ptrw(), _lights_accum.data(), b.size());
    _rd->buffer_update(_light_buf, 0, b.size(), b);
}

void CRadianceCascade::_poll_dynamic_lights()
{
    // Per-frame light watch. A structural change (light added/removed/bake-mode flip)
    // triggers a full rescan + relight; otherwise only BAKE_DYNAMIC lights are checked
    // for movement and re-uploaded, arming a relight (and a sun resync if it was the sun).
    if (!_lights_root) return;

    std::vector<RCLightGPU> cur; std::vector<uint8_t> cur_dyn;
    _scan_lights_into(_lights_root, cur, cur_dyn);

    // Structural change (added/removed light, or a bake_mode flip) → full refresh.
    if (cur.size() != _lights_accum.size() || cur_dyn != _light_dynamic)
    {
        _lights_accum.swap(cur); _light_dynamic.swap(cur_dyn);
        _ensure_fallback_light();
        _sync_sun_from_lights();
        _upload_light_buffer(); request_relight();
        _clip_relight_frames = _relight_track_settle;
        return;
    }

    // Otherwise: only DYNAMIC lights are watched for movement. STATIC stays at its last-uploaded
    // (baked) state even if it moves — matching the user's bake_mode intent.
    bool changed = false, sun_changed = false;
    for (size_t i = 0; i < cur.size(); ++i)
    {
        if (!_light_dynamic[i]) continue;
        if (_rc_light_changed(cur[i], _lights_accum[i]))
        {
            _lights_accum[i] = cur[i];
            changed = true;
            if (cur[i].type < 0.5f) sun_changed = true;   // directional = sun
        }
    }
    if (changed)
    {
        _upload_light_buffer();
        _relight_track_frames = _relight_track_settle;
        if (sun_changed) _sync_sun_from_lights();
        _clip_relight_frames = _relight_track_settle;
    }
}

void CRadianceCascade::_upload_lights(Node* root)
{
    // Full (re)scan of the scene's lights into the GPU buffer; caches the root for the
    // per-frame poll, ensures a fallback sun, and syncs the sun direction/color.
    _lights_root = root;                                  // cached for the per-frame poll
    _lights_accum.clear(); _light_dynamic.clear();
    _scan_lights_into(root, _lights_accum, _light_dynamic);
    print_line(vformat("RC lights: %d", (int) _lights_accum.size()));
    for (size_t i = 0; i < _lights_accum.size(); ++i)
        print_line(vformat("  [%d] type=%.0f col=(%.2f,%.2f,%.2f) pos=(%.1f,%.1f,%.1f)",
            (int) i, _lights_accum[i].type,
            _lights_accum[i].color[0], _lights_accum[i].color[1], _lights_accum[i].color[2],
            _lights_accum[i].position[0], _lights_accum[i].position[1], _lights_accum[i].position[2]));
    _ensure_fallback_light();
    _sync_sun_from_lights();
    _upload_light_buffer();
}

void CRadianceCascade::_sync_sun_from_lights()
{
    // The first directional light is treated as "the sun"; its dir/color drive the
    // coarse-level sun inject and the fallback light.
    for (const RCLightGPU& g : _lights_accum)
        if (g.type < 0.5f)
        {                                          // first directional = the sun
            _sun_dir = Vector3(g.direction[0], g.direction[1], g.direction[2]).normalized();
            _sun_color = Vector3(g.color[0], g.color[1], g.color[2]);
            return;
        }
}


// ── Debug ─────────────────────────────────────────────────────────────────────

void CRadianceCascade::_debug_print_probe_counts()
{
    // Gated diagnostic: every ~30 frames read back the per-cascade live probe counts and
    // print load factors. Blocking GPU readback — keep it off in production.
    if (!_dbg_probe_counts || !_patch_alloc.is_valid()) return;
    if ((_dbg_frame++ % 30ull) != 0ull) return;             // ~once every 30 frames

    PackedByteArray data = _rd->buffer_get_data(_patch_alloc, 0, sizeof(uint32_t) * RC_CASCADES);
    if (data.size() < (int) (sizeof(uint32_t) * RC_CASCADES)) return;
    const uint32_t* live = (const uint32_t*) data.ptr();

    uint64_t total_live = 0, total_slots = 0;
    String line = "[RC probes] ";
    for (uint32_t c = 0; c < RC_CASCADES; ++c)
    {
        uint32_t bcap = _cascades[c].bucket_cap;            // real slot count (= pcap*2)
        uint32_t pcap = _cascades[c].probe_cap;             // soft live cap (= pcap)
        float    lf = bcap ? (100.0f * float(live[c]) / float(bcap)) : 0.0f;
        total_live += live[c];
        total_slots += bcap;
        line += vformat("c%d=%d/%d(%.0f%%%s) ", c, live[c], bcap, lf,
            live[c] >= pcap ? " OVER-PCAP" : "");
    }
    line += vformat("| total_live=%d / slots=%d", (uint64_t) total_live, (uint64_t) total_slots);
    UtilityFunctions::print(line);
}
