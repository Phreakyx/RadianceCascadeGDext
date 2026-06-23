// addons/radiance_cascade/src/compat.h
// Include surface for the standalone Radiance Cascade GDExtension plugin.
// (Copied from the game's compat.h, minus the GeometricTools/Mathematics headers
//  that only the game's movement classes used — CRadianceCascade needs none of them.)
#ifndef COMPAT_H
#define COMPAT_H

#ifdef IS_MODULE_BUILD
    // --- MODULE BUILD INCLUDES AND ALIASES ---
#include "scene/2d/node_2d.h"
#include "scene/main/node.h"
#include "core/string/string_name.h"
//... include other necessary engine headers

// Create aliases within a 'godot' namespace to match godot-cpp's convention.
namespace godot
{
    using Node2D = ::Node2D;
    using Node = ::Node;
    using StringName = ::StringName;
    //... add other aliases as needed
}
#else
    // --- GDEXTENSION BUILD INCLUDES ---
    // For GDExtensions, just include the godot-cpp headers.
    // The classes are already in the 'godot' namespace.
#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/geometry_instance3d.hpp>
#include <godot_cpp/classes/rigid_body3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/character_body3d.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/math.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/collision_shape3d.hpp>
#include <godot_cpp/classes/shape3d.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/base_material3d.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/classes/skeleton3d.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/rd_sampler_state.hpp>
#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/rd_uniform.hpp>
#include <godot_cpp/classes/rd_texture_view.hpp>
#include <godot_cpp/classes/rd_shader_file.hpp>
#include <godot_cpp/classes/rd_texture_format.hpp>
#include <godot_cpp/classes/rd_shader_spirv.hpp>
#include <godot_cpp/classes/light3d.hpp>
#include <godot_cpp/classes/directional_light3d.hpp>
#include <godot_cpp/classes/spot_light3d.hpp>
#include <godot_cpp/classes/omni_light3d.hpp>
#include <godot_cpp/classes/area_light3d.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/classes/worker_thread_pool.hpp>   // WorkerThreadPool
#include <mutex>                                       // std::mutex / std::lock_guard
#include <atomic>
#include <shared_mutex>
#include <vector>
#include <unordered_map>
#include <cstring>
#endif

#endif // COMPAT_H
