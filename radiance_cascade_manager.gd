extends Node
## Runtime glue for the Radiance Cascade plugin (autoload registered by plugin.gd).
##
## For any scene whose WorldEnvironment compositor has an RCCompositorEffect, this:
##   1. spawns a CRadianceCascade node automatically (if one isn't already present),
##      so the user never has to add it by hand, and
##   2. points the effect at that node.
##
## The CRadianceCascade node finds the active Camera3D itself and uses the camera's
## CharacterBody3D parent as the player (or the camera itself if there is none), so
## no camera script or manual player wiring is needed.

func _ready() -> void:
	if Engine.is_editor_hint():
		return   # autoload logic (spawning the GI node, wiring) is game-runtime only
	# Scene loads add a WorldEnvironment / Camera3D / effect — re-check then.
	get_tree().node_added.connect(_on_node_added)
	_ensure_rc.call_deferred()

func _on_node_added(n: Node) -> void:
	if n is WorldEnvironment or n is Camera3D:
		_ensure_rc.call_deferred()

func _ensure_rc() -> void:
	var scene := get_tree().current_scene
	if scene == null:
		return

	var we := _find_world_environment(scene)
	if we == null or we.compositor == null:
		return

	# Only act on scenes that actually want GI (an RCCompositorEffect is present).
	var effects: Array = we.compositor.compositor_effects
	var has_rc_effect := false
	for effect in effects:
		if effect is RCCompositorEffect:
			has_rc_effect = true
			break
	if not has_rc_effect:
		return

	# Spawn the node once if the scene doesn't already provide one.
	var rc := scene.find_child("CRadianceCascade") as CRadianceCascade
	if rc == null:
		rc = CRadianceCascade.new()
		rc.name = "CRadianceCascade"
		scene.add_child(rc)

	# Wire every RC effect to the node.
	for effect in effects:
		if effect is RCCompositorEffect:
			effect.rc_node = rc

func _find_world_environment(root: Node) -> WorldEnvironment:
	if root == null:
		return null
	if root is WorldEnvironment:
		return root
	for child in root.find_children("*", "WorldEnvironment", true, false):
		return child as WorldEnvironment
	return null
