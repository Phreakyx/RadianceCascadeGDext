// register_types.cpp — standalone GDExtension entry for the Radiance Cascade plugin.
// Registers only CRadianceCascade (the game's other native classes live in the
// separate PeerlessCPP extension).
#include "register_types.h"
#include "CRadianceCascade.h"

#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>

using namespace godot;

void initialize_radiance_cascade_module(ModuleInitializationLevel p_level)
{
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) return;
    ClassDB::register_class<CRadianceCascade>();
}

void uninitialize_radiance_cascade_module(ModuleInitializationLevel p_level)
{
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) return;
}

extern "C" {
    // Entry point named in radiance_cascade.gdextension.
    GDExtensionBool GDE_EXPORT radiance_cascade_library_init(
        GDExtensionInterfaceGetProcAddress p_get_proc_address,
        const GDExtensionClassLibraryPtr p_library,
        GDExtensionInitialization* r_initialization)
    {
        GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);
        init_obj.register_initializer(initialize_radiance_cascade_module);
        init_obj.register_terminator(uninitialize_radiance_cascade_module);
        init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
        return init_obj.init();
    }
}
