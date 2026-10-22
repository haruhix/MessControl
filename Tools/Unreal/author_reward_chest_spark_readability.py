"""Tune only the existing airborne reward-chest spark material.

The owner runs this in an idle clean Editor. No ring, marker, mesh, map,
Blueprint, depth-bypass or runtime gameplay change is authored. Re-running
keeps the same graph/defaults; byte-identical settings need no material save.
Each actual changed package receives a hash-verified E: backup before mutation.
"""
import hashlib
import json
from pathlib import Path
import shutil
import unreal as u

OUT=Path(globals().get('MC_CHEST_SPARK_AUTHOR_OUTPUT',
    'E:/CodexWork/HubChestWardrobe_20261022/ChestSparkReadability')).resolve()
assert OUT.drive.upper()=='E:'
assert not u.EditorLevelLibrary.get_pie_worlds(False),'Preserve an active Play'
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor(),'Preserve an active Play'
assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages(),'Preserve unsaved map edits'
assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages(),'Preserve unsaved content edits'
OUT.mkdir(parents=True,exist_ok=True)
ROOT=Path(u.Paths.project_dir()).resolve()
PATH='/Game/Gameplay/VFX/RewardChest/M_RewardChest_ClaimSpark'
PACKAGE=ROOT/'Content/Gameplay/VFX/RewardChest/M_RewardChest_ClaimSpark.uasset'
LIB,MEL=u.EditorAssetLibrary,u.MaterialEditingLibrary
material=LIB.load_asset(PATH)
assert isinstance(material,u.Material) and PACKAGE.is_file(),'Existing owned spark material is required'
CODE='''float2 p=abs((UV-.5)*2);
float aa=max(fwidth(p.x)+fwidth(p.y),.015);
float diamond=1-smoothstep(.30,.30+aa,p.x+p.y);
float cross=exp(-p.x*p.x*65)*exp(-p.y*p.y*5)
    +exp(-p.y*p.y*65)*exp(-p.x*p.x*5);
float core=exp(-dot(p,p)*18);
float edge=1-smoothstep(.85,1.,length(p));
return saturate(diamond+cross*.55+core*.35)*edge;'''
TINT=u.LinearColor(1.,.76,.38,1.)
INTENSITY=3.

def scalar(name):
    try:return float(MEL.get_material_default_scalar_parameter_value(material,name))
    except Exception:return None

before_sha=hashlib.sha256(PACKAGE.read_bytes()).hexdigest()
# Expressions are not publicly exposed as a Python property in every UE build.
# Query the editable graph through MaterialEditingLibrary instead.
expressions=list(MEL.get_material_expressions(material))
shapes=[e for e in expressions if isinstance(e,u.MaterialExpressionCustom)]
colors=[e for e in expressions if isinstance(e,u.MaterialExpressionVectorParameter)
    and str(e.get_editor_property('parameter_name'))=='BurstColor']
intensities=[e for e in expressions if isinstance(e,u.MaterialExpressionScalarParameter)
    and str(e.get_editor_property('parameter_name'))=='BurstIntensity']
opacities=[e for e in expressions if isinstance(e,u.MaterialExpressionScalarParameter)
    and str(e.get_editor_property('parameter_name'))=='BurstOpacity']
assert len(shapes)==1 and len(colors)==1 and len(opacities)==1,'Preserve an unexpected spark graph'
assert len(intensities)<=1,'Preserve duplicate authored intensity parameters'
shape,color,opacity=shapes[0],colors[0],opacities[0]
intensity=intensities[0] if intensities else None
opacity_node=MEL.get_material_property_input_node(material,u.MaterialProperty.MP_OPACITY)
opacity_inputs=list(MEL.get_inputs_for_material_expression(material,opacity_node)) if opacity_node else []
assert isinstance(opacity_node,u.MaterialExpressionMultiply) and shape in opacity_inputs and opacity in opacity_inputs,'Preserve unexpected opacity wiring'
shape_inputs=list(MEL.get_inputs_for_material_expression(material,shape))
assert any(isinstance(e,u.MaterialExpressionTextureCoordinate) for e in shape_inputs),'Preserve an unexpected UV source'
emissive_node=MEL.get_material_property_input_node(material,u.MaterialProperty.MP_EMISSIVE_COLOR)
emissive_inputs=list(MEL.get_inputs_for_material_expression(material,emissive_node)) if isinstance(emissive_node,u.MaterialExpressionMultiply) else []
emissive_wired=isinstance(emissive_node,u.MaterialExpressionMultiply) and color in emissive_inputs and intensity is not None and intensity in emissive_inputs
assert emissive_node==color or emissive_wired,'Preserve an unexpected emissive graph'
original_nodes={e.get_path_name() for e in expressions}
same=(shape.get_editor_property('code')==CODE
    and color.get_editor_property('default_value')==TINT and emissive_wired
    and scalar('BurstIntensity')==INTENSITY
    and material.get_editor_property('blend_mode')==u.BlendMode.BLEND_ADDITIVE
    and material.get_editor_property('shading_model')==u.MaterialShadingModel.MSM_UNLIT
    and material.get_editor_property('used_with_instanced_static_meshes')
    and material.get_editor_property('two_sided')
    and not material.get_editor_property('disable_depth_test'))
backup=None
if not same:
    backup=OUT/'BeforePackages'/('M_RewardChest_ClaimSpark_'+before_sha+'.uasset')
    backup.parent.mkdir(parents=True,exist_ok=True)
    if not backup.exists():shutil.copy2(PACKAGE,backup)
    assert hashlib.sha256(backup.read_bytes()).hexdigest()==before_sha,'Backup differs from the original package'
    material.set_editor_properties(dict(blend_mode=u.BlendMode.BLEND_ADDITIVE,
        shading_model=u.MaterialShadingModel.MSM_UNLIT,two_sided=True,disable_depth_test=False,
        used_with_instanced_static_meshes=True))
    # Update existing nodes IN PLACE. Never delete rooted expression UObjects
    # while live Python wrappers or an open Material Editor can reference them.
    shape.set_editor_property('code',CODE)
    color.set_editor_property('default_value',TINT)
    if intensity is None:
        intensity=MEL.create_material_expression(material,u.MaterialExpressionScalarParameter,-650,370)
        intensity.set_editor_property('parameter_name','BurstIntensity')
    intensity.set_editor_property('default_value',INTENSITY)
    if not emissive_wired:
        emissive_node=MEL.create_material_expression(material,u.MaterialExpressionMultiply,-250,260)
        emissive_node.set_editor_property('desc','Reward chest spark authored intensity')
        assert MEL.connect_material_expressions(color,'',emissive_node,'A')
        assert MEL.connect_material_expressions(intensity,'',emissive_node,'B')
        assert MEL.connect_material_property(emissive_node,'',u.MaterialProperty.MP_EMISSIVE_COLOR)
    assert MEL.get_material_property_input_node(material,u.MaterialProperty.MP_OPACITY)==opacity_node,'Opacity root must remain unchanged'
    assert list(MEL.get_inputs_for_material_expression(material,opacity_node))==opacity_inputs,'Opacity links must remain unchanged'
    assert list(MEL.get_inputs_for_material_expression(material,shape))==shape_inputs,'UV links must remain unchanged'
    assert original_nodes.issubset({e.get_path_name() for e in MEL.get_material_expressions(material)}),'Existing expressions must never be deleted'
    assert MEL.get_material_property_input_node(material,u.MaterialProperty.MP_EMISSIVE_COLOR)==emissive_node
    verified=list(MEL.get_inputs_for_material_expression(material,emissive_node))
    assert color in verified and intensity in verified,'Emissive must receive authored color times intensity'
    compile_errors=list(MEL.recompile_material(material))
    assert not compile_errors,'Spark shader failed: '+str(compile_errors)
    assert LIB.save_loaded_asset(material,only_if_is_dirty=False)
report=dict(status='unchanged' if same else 'saved',asset=PATH,before_sha256=before_sha,
    after_sha256=hashlib.sha256(PACKAGE.read_bytes()).hexdigest(),backup=str(backup) if backup else None,
    color=[1.,.76,.38],intensity=INTENSITY,diamond_radius=.30,cross_falloff=65,
    depth_test=True,instanced_static_mesh_usage=True,ring_unchanged=True,plane_budget=12,
    existing_expression_count=len(original_nodes),final_expression_count=len(MEL.get_material_expressions(material)),
    original_nodes_preserved=True,opacity_uv_links_preserved=True,
    scope='Spark-only material; runtime updates BurstOpacity, leaving authored BurstColor/BurstIntensity unchanged.',
    validation='Recompiled/saved when changed; owning-agent shader errors and gameplay-camera video review remain required.')
report_path=OUT/('Authoring_'+report['after_sha256']+'.json')
if not report_path.exists():report_path.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
u.log('MC_REWARD_CHEST_SPARK_READABILITY '+report['status']+' '+str(report_path))
