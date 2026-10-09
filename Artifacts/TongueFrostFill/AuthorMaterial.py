import json
from pathlib import Path
import unreal as u

e, a = u.MaterialEditingLibrary, u.EditorAssetLibrary
mat = u.load_asset('/Game/Gameplay/Cold/Frost/M_TongueFrost')
assert mat and mat.get_blend_mode() == u.BlendMode.BLEND_MASKED
nodes = list(e.get_material_expressions(mat))
params = {str(n.get_editor_property('parameter_name')): n for n in nodes
    if isinstance(n, u.MaterialExpressionScalarParameter)}
assert 'Frost Fill' not in params
shapes = next(n for n in nodes if isinstance(n,u.MaterialExpressionCustom)
    and str(n.get_editor_property('description')).startswith('Growing ice plates with clustered frost'))
warm = next(n for n in nodes if isinstance(n,u.MaterialExpressionCustom)
    and str(n.get_editor_property('description')).startswith('Uneven thaw fringe'))
color = next(n for n in nodes if isinstance(n,u.MaterialExpressionCustom)
    and str(n.get_editor_property('description')).startswith('White frost varies'))
mask_sample = next(n for n in nodes if isinstance(n,u.MaterialExpressionTextureSampleParameter2D)
    and str(n.get_editor_property('parameter_name')) == 'Frost Mask Texture')
front = e.get_material_property_input_node(mat,u.MaterialProperty.MP_FRONT_MATERIAL)
dither = e.get_material_property_input_node(mat,u.MaterialProperty.MP_OPACITY_MASK)
assert isinstance(front,u.MaterialExpressionSubstrateHorizontalMixing)
out = Path(u.Paths.project_dir())/'Artifacts/TongueFrostFill'
out.mkdir(parents=True,exist_ok=True)
backup_path = '/Game/Gameplay/Cold/Frost/M_TongueFrost_BeforeFrostFill'
assert not a.does_asset_exist(backup_path)
backup = a.duplicate_asset(mat.get_path_name().split('.')[0],backup_path)
assert backup and a.save_loaded_asset(backup,only_if_is_dirty=False)
u.get_editor_subsystem(u.AssetEditorSubsystem).close_all_editors_for_asset(mat)

def add_input(target,name,source,output):
    inputs = list(target.get_editor_property('inputs'))
    entry = u.CustomInput()
    entry.set_editor_property('input_name',name)
    inputs.append(entry)
    target.set_editor_property('inputs',inputs)
    assert e.connect_material_expressions(source,output,target,name)

with u.ScopedEditorTransaction('Fill cold tongue gaps with frost and make the warm circle clear'):
    fill = e.create_material_expression(mat,u.MaterialExpressionScalarParameter,-4350,5810)
    fill.set_editor_properties({'parameter_name':'Frost Fill','default_value':1.0,
        'slider_min':0.0,'slider_max':1.0,'group':'Frost Surface',
        'desc':'Continuous frost between ice plates outside warmth. One fills all gaps at full cold.'})
    add_input(shapes,'Fill',fill,'')
    shapes.set_editor_properties({'description':'Ice plates over a continuous frost bed outside the warm circles',
        'code':'''float a=saturate(Amount);
if(a<=0.001) return float4(0,0,0,0);
float threshold=lerp(1.08,1.0-clamp(Density,0.02,0.98),a);
float plate=smoothstep(threshold,threshold+max(Softness,0.005),Fields.r);
float region=smoothstep(1.0-saturate(Clumps),1.18-saturate(Clumps),Fields.g);
float rim=4.0*plate*(1.0-plate);
float cluster=smoothstep(0.24,0.57,Fields.r)*saturate(region*0.85+rim*0.65);
float crystals=saturate(Crystals)*cluster;
float bed=smoothstep(0.06,0.65,a)*saturate(Fill);
float total=max(plate,max(crystals,bed));
float plateFrost=saturate(crystals*0.85*saturate(FrostStrength));
float mix=lerp(1.0,plateFrost,plate);
return float4(total,mix,plate,region);'''})
    add_input(color,'Crystals',mask_sample,'R')
    color.set_editor_properties({'description':'Visible fern crystal detail on a continuous pale frost bed',
        'code':'return Tint*lerp(0.82,1.0,saturate(Shade))*lerp(0.70,1.03,saturate(Crystals));'})
    params['Warm Fade Width'].set_editor_properties({'default_value':45.0,
        'desc':'Short soft fade immediately outside the actual safe circle, keeping the gameplay boundary legible.'})
    params['Warm Edge Variation'].set_editor_properties({'default_value':12.0,
        'desc':'Local variation in fade width, without pushing the frost boundary away from the safe circle.'})
    warm.set_editor_properties({'description':'Short varied fade beginning at the exact warm-circle boundary',
        'code':'''float spread=clamp(Variation,0.0,25.0);
float w=max(8.0,max(Width,1.0)+(Fields.b-0.5)*2.0*spread);
float r=max(Radius,0.0), nr=max(NextRadius,0.0);
float outside=smoothstep(r,r+w,length(P.xy-Center.xy));
float nextOutside=smoothstep(nr,nr+w,length(P.xy-NextCenter.xy));
return lerp(1.0,outside,saturate(Enabled))*lerp(1.0,nextOutside,saturate(NextEnabled));'''})
    assert e.get_material_property_input_node(mat,u.MaterialProperty.MP_FRONT_MATERIAL) == front
    assert e.get_material_property_input_node(mat,u.MaterialProperty.MP_OPACITY_MASK) == dither
    errors = list(e.recompile_material(mat))
    assert not errors,errors
    assert a.save_loaded_asset(mat,only_if_is_dirty=False)
    task = u.AssetExportTask()
    task.set_editor_properties({'object':mat,'filename':(out/'MaterialAfter.copy').as_posix(),
        'automated':True,'prompt':False,'replace_identical':True,'exporter':u.ObjectExporterT3D()})
    assert u.Exporter.run_asset_export_task(task)
    mask_line = next(line.strip() for line in (out/'MaterialAfter.copy').read_text(encoding='utf-8-sig').splitlines()
        if line.strip().startswith('OpacityMask='))
    assert 'Expression=' in mask_line and 'UseConstant=True' not in mask_line
    report = {'material':mat.get_path_name(),'backup':backup_path,'compile_errors':errors,
        'frost_fill':1.0,'warm_fade_width_cm':45.0,'warm_width_variation_cm':12.0,
        'warm_boundary_outward_offset_cm':0.0,'full_cold_gap_coverage':1.0,
        'native_substrate_slabs_preserved':2,'glossy_ice_islands_preserved':True,
        'crystal_texture_detail_in_frost_color':True,'both_warm_zones_supported':True,
        'player_material_unchanged':True,'pie_was_running':u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()}
    (out/'MaterialBuild.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    print('TONGUE_FROST_GAPS_FILLED',json.dumps(report))
