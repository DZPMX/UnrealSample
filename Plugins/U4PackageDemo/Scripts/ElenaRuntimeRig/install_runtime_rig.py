"""Run in UE after a full C++ build/reload. Creates the assets but leaves saving to review.

Requires the project's existing VibeUE BlueprintService. Does not alter the
wrinkle EventGraph, skeletal mesh, animation sequences or material assets.
"""
import json
from pathlib import Path
import unreal


def install():
    plugin_root = Path(__file__).resolve().parents[2]
    source = plugin_root / 'Source/UnrealSample/ElenaRig/Data'
    binding = json.loads((source / 'RigBinding.json').read_text(encoding='utf-8'))
    folder = '/U4PackageDemo/Demo/Character/Elena/Animation'
    data_path = folder + '/DA_Elena_SourceRig'
    abp_path = folder + '/ABP_Elena_Wrinkles'
    graph_name = 'AnimGraph'
    unreal.load_asset(abp_path)
    graph = unreal.find_object(None, abp_path + '.ABP_Elena_Wrinkles:AnimGraph')
    data = unreal.load_asset(data_path)
    if data is None:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property('data_asset_class', unreal.ElenaRigData)
        data = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            'DA_Elena_SourceRig', folder, unreal.ElenaRigData, factory)
    data.set_editor_property('ProgramBytes', list((source / 'RigProgram.bin').read_bytes()))
    data.set_editor_property('BoneNames', binding['bone_names'])
    data.set_editor_property('DirectBones', binding['direct_bones'])
    data.set_editor_property('DerivedBones', binding['derived_bones'])

    service = unreal.BlueprintService
    nodes = service.get_nodes_in_graph(abp_path, graph_name)
    existing = next((n for n in nodes if n.node_type == 'AnimGraphNode_ElenaRig'), None)
    node_id = existing.node_id if existing else service.create_node_by_key(
        abp_path, graph_name, 'NODE AnimGraphNode_ElenaRig', 0, -32)
    node = next(n for n in unreal.ObjectIterator(unreal.AnimGraphNode_ElenaRig) if n.get_outer() == graph)
    settings = node.get_editor_property('Node')
    settings.set_editor_property('RigData', data)
    settings.set_editor_property('bEnabled', True)
    node.set_editor_property('Node', settings)
    service.refresh_node(abp_path, graph_name, node_id, False)

    if not service.variable_exists(abp_path, 'EnableRealtimeRig'):
        service.add_variable(abp_path, 'EnableRealtimeRig', 'bool', 'true')
    if existing is None:
        getter = service.add_get_variable_node(abp_path, graph_name, 'EnableRealtimeRig', -240, 160)
        service.connect_nodes(abp_path, graph_name, getter, 'EnableRealtimeRig', node_id, 'bEnabled')
    root = next(n for n in nodes if n.node_type == 'AnimGraphNode_Root')
    input_pose = next(n for n in nodes if n.node_type == 'AnimGraphNode_LinkedInputPose')
    service.disconnect_pin(abp_path, graph_name, root.node_id, 'Result')
    connected_input = service.connect_nodes(abp_path, graph_name, input_pose.node_id, 'Pose', node_id, 'InputPose')
    connected_output = service.connect_nodes(abp_path, graph_name, node_id, 'Pose', root.node_id, 'Result')
    service.set_node_position(abp_path, graph_name, root.node_id, 320, -32)
    compilation = service.compile_blueprint(abp_path)
    return dict(data_asset=data_path,anim_blueprint=abp_path,input_connected=connected_input,
                output_connected=connected_output,compile_result=str(compilation),saved=False)


if __name__ == '__main__':
    print(json.dumps(install(),ensure_ascii=False,indent=2))
