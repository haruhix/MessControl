"""Import the Painter reference pass and tune gum/palate/throat wet tissue."""
import unreal as u
from pathlib import Path

root = Path(u.Paths.project_dir()).resolve()
lib,edit = u.EditorAssetLibrary,u.MaterialEditingLibrary
for suffix in ('BaseColor','Normal','OcclusionRoughnessMetallic'):
    name = 'TissueSwatch_Tissue_'+suffix
    task = u.AssetImportTask()
    task.filename = str(root/'ArtSource/LivingThroat/Textures'/(name+'.png'))
    task.destination_path = '/Game/Gameplay/Throat/Textures'
    task.destination_name = name
    task.automated = True
    task.replace_existing = True
    task.save = True
    u.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    tex = lib.load_asset(task.destination_path+'/'+name)
    if suffix == 'Normal':
        tex.set_editor_property('compression_settings',u.TextureCompressionSettings.TC_NORMALMAP)
        tex.set_editor_property('srgb',False)
    elif suffix == 'OcclusionRoughnessMetallic':
        tex.set_editor_property('compression_settings',u.TextureCompressionSettings.TC_MASKS)
        tex.set_editor_property('srgb',False)
    else:
        tex.set_editor_property('srgb',True)
    lib.save_loaded_asset(tex,only_if_is_dirty=False)

for name in ('M_LivingTissue','M_ThroatSculpt'):
    mat = lib.load_asset('/Game/Art/Materials/'+name)
    for node in edit.get_material_expressions(mat):
        if not isinstance(node,u.MaterialExpressionCustom):
            continue
        desc = str(node.get_editor_property('description'))
        if desc == 'Wet film':
            # Preserve Painter's broad wetness variation instead of flattening it
            # into one uniformly glossy value.
            node.set_editor_property('code','return clamp(ORM.g+Bias,.16,.40);')
        elif desc == 'Subtle vascular mottling':
            node.set_editor_property('code',
                'float n=sin(P.x*.006+sin(P.y*.011))*sin(P.z*.009+P.y*.004); '
                'float m=sin(P.x*.021-P.z*.012+sin(P.y*.018)); '
                'return Base.rgb*Tint.rgb*(1+float3(.045,.10,.085)*(n+.3*m));')
        elif desc == 'Throat depth':
            node.set_editor_property('code','return C*(1.0-.91*smoothstep(180.0,850.0,P.x));')
    errors = edit.recompile_material(mat)
    assert not errors, str(errors)
    lib.save_loaded_asset(mat,only_if_is_dirty=False)

for name,tint,normal,bias in [
    ('MI_MouthGum',(1.08,1.55,1.36,1),.23,-.015),
    ('MI_MouthCheek',(.94,1.08,1.06,1),.18,.015),
    ('MI_MouthPalate',(.98,1.08,1.04,1),.20,.01),
    ('MI_LivingThroat',(1.02,.92,1.02,1),.32,-.005)]:
    mi = lib.load_asset('/Game/Art/Materials/'+name)
    edit.set_material_instance_vector_parameter_value(mi,'TissueTint',u.LinearColor(*tint))
    edit.set_material_instance_scalar_parameter_value(mi,'MicroNormalStrength',normal)
    edit.set_material_instance_scalar_parameter_value(mi,'RoughnessBias',bias)
    edit.set_material_instance_scalar_parameter_value(mi,'Specular',.50)
    edit.set_material_instance_scalar_parameter_value(mi,'ScatterDensity',.68)
    edit.update_material_instance(mi)
    lib.save_loaded_asset(mi,only_if_is_dirty=False)

# The gum ridges share the mucosa light, which previously lit only the throat
# and palate; this gives the wet upper gingival edge a readable highlight.
for actor in u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors():
    if actor.get_actor_label().startswith('ART | Continuous gums '):
        channels = u.LightingChannels()
        channels.set_editor_property('channel0',True)
        channels.set_editor_property('channel1',True)
        actor.static_mesh_component.set_editor_property('lighting_channels',channels)

u.log('MC_REFERENCE_TISSUE_FINISHED')
