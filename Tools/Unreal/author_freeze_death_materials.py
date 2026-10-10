"""Create transparent fatal-freeze glass and static snapshot body materials."""
import json
from pathlib import Path
import unreal as u
e,lib=u.MaterialEditingLibrary,u.EditorAssetLibrary
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
folder='/Game/Gameplay/Cold/Death'
out=Path(u.Paths.project_dir())/'Artifacts/FreezeDeath'
out.mkdir(parents=True,exist_ok=True)
report={'materials':[]}
def save(asset):
    assert not list(e.recompile_material(asset))
    assert lib.save_loaded_asset(asset,only_if_is_dirty=False)
    report['materials'].append(asset.get_path_name())
def node(mat,cls,x,y,**props):
    n=e.create_material_expression(mat,cls,x,y);n.set_editor_properties(props);return n
def link(src,pin,dest,input_name=''):
    assert e.connect_material_expressions(src,pin,dest,input_name),input_name
def custom(mat,description,code,inputs,x,y,scalar=False):
    n=node(mat,u.MaterialExpressionCustom,x,y,description=description,code=code,
        output_type=u.CustomMaterialOutputType.CMOT_FLOAT1 if scalar else u.CustomMaterialOutputType.CMOT_FLOAT3)
    entries=[]
    for name in inputs:
        entry=u.CustomInput();entry.set_editor_property('input_name',name);entries.append(entry)
    n.set_editor_property('inputs',entries)
    for name,(src,pin) in inputs.items():link(src,pin,n,name)
    return n
for original,name in [('/Game/Art/Materials/M_TeethGameplay','M_FrozenDeathBody'),
        ('/Game/Gameplay/CharacterCurrent/Materials/M_BagGameplay','M_FrozenDeathBag')]:
    if lib.does_asset_exist(folder+'/'+name):continue
    mat=lib.duplicate_asset(original,folder+'/'+name);assert mat
    zero=node(mat,u.MaterialExpressionConstant,2500,1700,r=0.0,
        desc='Pose is baked into the death fragments; no skeletal shader displacement.')
    assert e.connect_material_property(zero,'',u.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    mat.set_editor_property('two_sided',True)
    mat.set_editor_property('tangent_space_normal',True)
    normal=node(mat,u.MaterialExpressionConstant3Vector,2700,1950,constant=u.LinearColor(0,0,1,1),
        desc='Use normals baked into the fragments, without skeletal normal reconstruction.')
    rough=node(mat,u.MaterialExpressionConstant,2700,2130,r=.58)
    assert e.connect_material_property(normal,'',u.MaterialProperty.MP_NORMAL)
    assert e.connect_material_property(rough,'',u.MaterialProperty.MP_ROUGHNESS)
    for n in e.get_material_expressions(mat):
        if isinstance(n,u.MaterialExpressionScalarParameter) and str(n.get_editor_property('parameter_name')) in ['BodyStretch','Damage','Coffee']:
            n.set_editor_property('default_value',0.0)
    save(mat)
path=folder+'/M_FrozenDeathGlass'
if not lib.does_asset_exist(path):
    mat=u.AssetToolsHelpers.get_asset_tools().create_asset('M_FrozenDeathGlass',folder,u.Material,u.MaterialFactoryNew())
    mat.set_editor_properties({'blend_mode':u.BlendMode.BLEND_TRANSLUCENT,
        'translucency_lighting_mode':u.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING,
        'two_sided':False})
    source=u.load_asset('/Game/Art/Materials/ice/M_Ice')
    template=e.get_material_property_input_node(source,u.MaterialProperty.MP_FRONT_MATERIAL)
    front=node(mat,u.MaterialExpressionMaterialFunctionCall,1100,0,
        desc='Substrate legacy surface: clear centre and frosted edges, to keep the player visible.')
    assert front.set_material_function(template.get_editor_property('material_function'))
    uv=node(mat,u.MaterialExpressionTextureCoordinate,-1200,0,coordinate_index=0)
    cracks=node(mat,u.MaterialExpressionScalarParameter,-1200,200,parameter_name='Cracks',
        group='Fatal Freeze',default_value=0.0,slider_min=0.0,slider_max=1.0)
    opacity=node(mat,u.MaterialExpressionScalarParameter,-1200,380,parameter_name='Glass Opacity',
        group='Fatal Freeze',default_value=.11,slider_min=.02,slider_max=.5)
    mask=custom(mat,'Ice block frosted rim and growing cracks',
        'float edge=1.0-smoothstep(0.015,0.095,min(min(UV.x,1-UV.x),min(UV.y,1-UV.y)));\n'
        'float line1=1-smoothstep(0.002,0.009,abs(UV.x-.49-sin(UV.y*25)*.026));\n'
        'float line2=1-smoothstep(0.002,0.008,abs(UV.y-.61-sin(UV.x*31)*.022));\n'
        'float fracture=max(line1,line2)*saturate(Cracks);\n'
        'return float3(edge,fracture,saturate(Opacity+edge*.52+fracture*.30));',
        {'UV':(uv,''),'Cracks':(cracks,''),'Opacity':(opacity,'')},-650,0)
    tint=node(mat,u.MaterialExpressionVectorParameter,-650,430,parameter_name='Ice Tint',
        group='Fatal Freeze',default_value=u.LinearColor(.32,.64,.86,1))
    color=custom(mat,'Clear blue ice with white frost along its edges',
        'return lerp(Tint,float3(.76,.93,1.0),saturate(Field.x*.75+Field.y*.7));',
        {'Tint':(tint,''),'Field':(mask,'')},-150,0)
    alpha=custom(mat,'Transparent ice block coverage','return Field.z;',{'Field':(mask,'')},-150,240,True)
    rough=custom(mat,'Glass centre with rough frost on the rim','return lerp(.12,.38,Field.x);',{'Field':(mask,'')},-150,420,True)
    spec=node(mat,u.MaterialExpressionConstant,300,600,r=.55)
    link(color,'',front,'Base Color');link(alpha,'',front,'Opacity')
    link(rough,'',front,'Roughness');link(spec,'',front,'Specular')
    rim=custom(mat,'Blue white ice rim and crack highlights',
        'return float3(.14,.58,1.1)*(Field.x*.75+Field.y*.55);',{'Field':(mask,'')},250,800)
    link(rim,'',front,'Emissive Color')
    assert e.connect_material_property(front,'Result',u.MaterialProperty.MP_FRONT_MATERIAL)
    assert e.connect_material_property(alpha,'',u.MaterialProperty.MP_OPACITY)
    save(mat)
mesh=u.load_asset('/Game/Data/DA_PlayerAppearance').get_editor_property('skeletal_mesh')
u.SkeletalMeshEditorSubsystem.set_allow_cpu_access(mesh,True)
assert lib.save_loaded_asset(mesh,only_if_is_dirty=False)
report['snapshot_mesh']=mesh.get_path_name()
(out/'MaterialBuild.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print('FREEZE_DEATH_MATERIALS_SAVED',json.dumps(report))
