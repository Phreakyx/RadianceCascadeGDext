@tool
class_name RCCompositorEffect
extends CompositorEffect

var rc_node: CRadianceCascade = null

func _init() -> void:
	effect_callback_type = CompositorEffect.EFFECT_CALLBACK_TYPE_POST_TRANSPARENT
	access_resolved_depth = true
	needs_normal_roughness = true

func _render_callback(_effect_callback_type: int, render_data: RenderData) -> void:
	# This effect is @tool, so it also fires for the editor's 3D viewport. GI must
	# run only at game runtime, so bail out in the editor.
	if Engine.is_editor_hint():
		return

	var scene_buffers := render_data.get_render_scene_buffers() as RenderSceneBuffersRD
	if scene_buffers == null:
		return

	if rc_node == null:
		rc_node = Engine.get_main_loop().get_first_node_in_group("radiance_cascade") as CRadianceCascade
	if rc_node == null:
		return
	
	var size: Vector2i = scene_buffers.get_target_size()
	var depth_rid: RID = scene_buffers.get_texture("render_buffers", "depth")
	if not depth_rid.is_valid():
		push_warning("RC: depth invalid")
		return

	# Lit scene color — exact accessor TBD pending your autocomplete check
	var color_rid: RID = scene_buffers.get_texture("render_buffers", "color")
	if not color_rid.is_valid():
		push_warning("RC: color invalid")
		return

	var normal_tex: RID = scene_buffers.get_texture("forward_clustered", "normal_roughness")
	rc_node.dispatch(depth_rid, normal_tex, color_rid, size)
