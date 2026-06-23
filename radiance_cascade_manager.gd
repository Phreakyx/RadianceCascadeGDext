extends Node
## Runtime glue for the Radiance Cascade plugin (registered as an autoload by plugin.gd).
##
## Wires the CRadianceCascade node (group "radiance_cascade") into the active
## WorldEnvironment's RCCompositorEffect. RCCompositorEffect also self-discovers the
## node via the group, so this is the explicit, debuggable hookup point — game-specific
## setup (sky color, ambient, input) stays in your own scene script.

func _ready() -> void:
	# Re-wire whenever a fresh scene brings in the RC node or a WorldEnvironment.
	get_tree().node_added.connect(_on_node_added)
	_wire.call_deferred()

func _on_node_added(n: Node) -> void:
	if n is CRadianceCascade or n is WorldEnvironment:
		_wire.call_deferred()

func _wire() -> void:
	var rc := get_tree().get_first_node_in_group("radiance_cascade") as CRadianceCascade
	if rc == null:
		return
	var we := _find_world_environment(get_tree().current_scene)
	if we == null or we.compositor == null:
		return
	for effect in we.compositor.compositor_effects:
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
