@tool
extends EditorPlugin
## Editor-side entry for the Radiance Cascade plugin.
## The GI work lives in the GDExtension (CRadianceCascade) + RCCompositorEffect;
## this just registers the runtime manager autoload that wires them together.

const AUTOLOAD_NAME := "RadianceCascadeManager"
const AUTOLOAD_PATH := "res://addons/radiance_cascade/radiance_cascade_manager.gd"

func _enter_tree() -> void:
	add_autoload_singleton(AUTOLOAD_NAME, AUTOLOAD_PATH)

func _exit_tree() -> void:
	remove_autoload_singleton(AUTOLOAD_NAME)
