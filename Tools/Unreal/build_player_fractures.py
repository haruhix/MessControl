"""Author health-driven enamel fractures on the current player material.

Run in a stopped Unreal Editor. Back up and save packages through the editor;
the reference pattern, colors, relief and small crown notches are parametric.
"""
import json
import shutil
from datetime import datetime, timezone
from pathlib import Path

import unreal as u


MATERIAL = '/Game/Art/Materials/M_TeethGameplay'
SCALARS = {
    'DamageCellSize': 22.0, 'DamageHairlineWidth': 1.0,
    'DamageMajorWidth': 1.05, 'DamageRelief': 1.0, 'DamageChipDepth': 2.8,
}
COLORS = {
    'DamageHairlineColor': (.27,.24,.20,1),
    'DamageDeepColor': (.075,.065,.052,1),
}

assert not u.EditorLevelLibrary.get_pie_worlds(False), 'Stop PIE before authoring fractures.'
lib = u.EditorAssetLibrary
edit = u.MaterialEditingLibrary
mat = lib.load_asset(MATERIAL)
assert isinstance(mat,u.Material)
nodes = list(edit.get_material_expressions(mat))
by_name = {n.get_name(): n for n in nodes}
color = by_name['MaterialExpressionCustom_0']
original_code = color.get_editor_property('code')
assert 'float crack' in original_code and 'saturate(Flash)' in original_code
prefix = original_code.split('float crack',1)[0]
assert 'float3 color' in prefix and 'GrimeOpacity' in prefix
normal = by_name['MaterialExpressionNormalize_1']
body_offset = by_name['MaterialExpressionTransform_0']
enamel = by_name['MaterialExpressionLinearInterpolate_1']
position = by_name['MaterialExpressionVertexInterpolator_0']
pre_skin = by_name['MaterialExpressionPreSkinnedPosition_0']
params = {str(n.get_editor_property('parameter_name')): n for n in nodes
          if isinstance(n,(u.MaterialExpressionScalarParameter,u.MaterialExpressionVectorParameter))}
assert 'Damage' in params
grime_rough = next(n for n in nodes if isinstance(n,u.MaterialExpressionCustom)
                   and n.get_editor_property('description')=='Matte player grime')
backup = Path(u.Paths.project_saved_dir())/'PlayerDamageBackups'/datetime.now(timezone.utc).strftime('%Y%m%d_%H%M%S_%f')
backup.mkdir(parents=True)
shutil.copy2(Path(u.Paths.project_dir())/'Content/Art/Materials/M_TeethGameplay.uasset',backup/'M_TeethGameplay.uasset')
(backup/'PreviousColor.hlsl').write_text(original_code,encoding='utf-8')


def parameter(name,value,vector=False):
    node = params.get(name)
    if node is None:
        cls = u.MaterialExpressionVectorParameter if vector else u.MaterialExpressionScalarParameter
        node = edit.create_material_expression(mat,cls,700,3600+len(params)*100)
        node.set_editor_property('parameter_name',name)
        params[name] = node
    node.set_editor_property('default_value',u.LinearColor(*value) if vector else value)
    node.set_editor_property('group','Enamel damage')
    return node


def expression(cls,description,x,y):
    node = next((n for n in nodes if isinstance(n,cls) and n.get_editor_property('desc')==description),None)
    if node is None:
        node = edit.create_material_expression(mat,cls,x,y)
    node.set_editor_property('desc',description)
    return node


def connect(source,target,pin):
    assert edit.connect_material_expressions(source,'',target,pin),pin


def custom(description,code,inputs,output_type,x,y,node=None):
    if node is None:
        node = next((n for n in nodes if isinstance(n,u.MaterialExpressionCustom)
                     and n.get_editor_property('description')==description),None)
    if node is None:
        node = edit.create_material_expression(mat,u.MaterialExpressionCustom,x,y)
    node.set_editor_property('description',description)
    node.set_editor_property('output_type',output_type)
    entries = [entry for entry in node.get_editor_property('inputs')
               if str(entry.get_editor_property('input_name')) not in ('','None')]
    names = {str(entry.get_editor_property('input_name')) for entry in entries}
    for name in inputs:
        if name not in names:
            entry = u.CustomInput()
            entry.set_editor_property('input_name',name)
            entries.append(entry)
    node.set_editor_property('inputs',entries)
    node.set_editor_property('code',code)
    for name,source in inputs.items():
        connect(source,node,name)
    return node


with u.ScopedEditorTransaction('Reference enamel fractures'):
    for name,value in SCALARS.items():
        parameter(name,value)
    for name,value in COLORS.items():
        parameter(name,value,True)
    fracture = custom('Branched enamel fractures',
                      Path(__file__).with_name('player_fracture_mask.hlsl').read_text(encoding='utf-8'),
                      {'P':position,'Enamel':enamel,'Damage':params['Damage'],
                       'CellSize':params['DamageCellSize'],'HairlineWidth':params['DamageHairlineWidth'],
                       'MajorWidth':params['DamageMajorWidth']},
                      u.CustomMaterialOutputType.CMOT_FLOAT4,1500,3650)
    damage_code = '''float crack = max(Fracture.x,Fracture.y);
color = lerp(color,HairlineColor,Fracture.x*.72);
color = lerp(color,DeepColor,Fracture.y*.94);
color = lerp(color,min(color*1.08+.025,1.0),Fracture.w*.75);
return lerp(color,float3(1,.3,.08),saturate(Flash)*.55);
'''
    custom('Gameplay stains over the original textured enamel',prefix+damage_code,
           {'Fracture':fracture,'HairlineColor':params['DamageHairlineColor'],'DeepColor':params['DamageDeepColor']},
           u.CustomMaterialOutputType.CMOT_FLOAT3,1800,2600,color)

    # Differentiate the recessed height on the rendered surface in world space.
    # Transform the original artist normal out and back to preserve its detail.
    world_normal = expression(u.MaterialExpressionTransform,'Artist normal for enamel relief',1500,3900)
    world_normal.set_editor_property('transform_source_type',u.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_TANGENT)
    world_normal.set_editor_property('transform_type',u.MaterialVectorCoordTransform.TRANSFORM_WORLD)
    connect(normal,world_normal,'')
    world_position = expression(u.MaterialExpressionWorldPosition,'World surface for enamel relief',1500,4100)
    relief = custom('Recessed enamel surface normal','''
float3 n = normalize(N);
float3 dx = ddx(WP), dy = ddy(WP);
float3 rx = cross(dy,n), ry = cross(n,dx);
float determinant = dot(dx,rx);
float3 gradient = sign(determinant)*(ddx(Fracture.z)*rx+ddy(Fracture.z)*ry);
return normalize(max(abs(determinant),.00001)*n-max(Depth,0)*gradient);
''', {'N':world_normal,'WP':world_position,'Fracture':fracture,'Depth':params['DamageRelief']},
                    u.CustomMaterialOutputType.CMOT_FLOAT3,1800,3900)
    tangent_normal = expression(u.MaterialExpressionTransform,'Enamel relief to tangent normal',2100,3900)
    tangent_normal.set_editor_property('transform_source_type',u.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_WORLD)
    tangent_normal.set_editor_property('transform_type',u.MaterialVectorCoordTransform.TRANSFORM_TANGENT)
    connect(relief,tangent_normal,'')
    assert edit.connect_material_property(tangent_normal,'',u.MaterialProperty.MP_NORMAL)
    rough = custom('Matte broken enamel','return lerp(Base,max(Base,.68),max(Fracture.x*.4,Fracture.y));',
                   {'Base':grime_rough,'Fracture':fracture},u.CustomMaterialOutputType.CMOT_FLOAT1,2100,3650)
    assert edit.connect_material_property(rough,'',u.MaterialProperty.MP_ROUGHNESS)

    # Small dents at the two crown fault entries alter the silhouette. They are
    # cosmetic vertex offsets, combined with the existing squash/stretch.
    chip = custom('Small damaged crown notches','''
float amount = smoothstep(.30,.92,saturate(Damage));
float center = (1-smoothstep(.35,2.1,abs(P.x-.4))) * smoothstep(96,106,P.z);
float side = (1-smoothstep(.45,2.3,abs(P.x+29.5))) * smoothstep(99,110,P.z);
return float3(0,0,-max(Depth,0)*amount*max(center,side));
''', {'P':pre_skin,'Damage':params['Damage'],'Depth':params['DamageChipDepth']},
                  u.CustomMaterialOutputType.CMOT_FLOAT3,1500,4400)
    chip_world = expression(u.MaterialExpressionTransform,'Crown chips to world offset',1800,4400)
    chip_world.set_editor_property('transform_source_type',u.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL)
    chip_world.set_editor_property('transform_type',u.MaterialVectorCoordTransform.TRANSFORM_WORLD)
    connect(chip,chip_world,'')
    offset = expression(u.MaterialExpressionAdd,'Body stretch plus crown damage',2100,4400)
    connect(body_offset,offset,'A')
    connect(chip_world,offset,'B')
    assert edit.connect_material_property(offset,'',u.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    errors = list(edit.recompile_material(mat))
    assert not errors,errors
    assert lib.save_loaded_asset(mat,only_if_is_dirty=False)

instances = []
for path in ('/Game/Gameplay/CharacterCurrent/Materials/MI_Character',
             '/Game/Gameplay/CharacterCurrent/Materials/MI_Bag','/Game/Art/Materials/MI_TeethPlayer'):
    instance = lib.load_asset(path)
    assert instance and instance.get_editor_property('parent')==mat,path
    edit.update_material_instance(instance)
    assert lib.save_loaded_asset(instance,only_if_is_dirty=True)
    instances.append(path)
report = {'material':MATERIAL,'instances':instances,'backup':str(backup),'compile_errors':errors,
          'scalar_defaults':SCALARS,'color_defaults_linear':COLORS,
          'preserved':['artist textures','Coffee grime','HitFlash','BodyStretch','health-driven Damage'],
          'protected':['eye ellipsoids','mouth ellipsoid','dark face and blue costume colors'],
          'effect':'Warped 3D cell fractures, art-directed major faults, recessed normals and small crown notches.'}
Path(u.Paths.project_saved_dir(),'PlayerFractures.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
u.log('PLAYER_FRACTURES_SAVED '+json.dumps(report))
