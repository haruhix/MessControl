"""Audit final shader bindings, mask contract, arena coverage and shader stats."""
import json
from pathlib import Path
import unreal as u
lib=u.EditorAssetLibrary; edit=u.MaterialEditingLibrary
paths=['/Game/Gameplay/Liquid/Stylized/MI_StylizedCoffee','/Game/Gameplay/Liquid/Stylized/MI_ClearWater','/Game/Art/Materials/MI_CoffeePour','/Game/Art/Materials/MI_CoffeeDrops','/Game/Gameplay/Hazards/MI_VomitPuddle']
report={}
for path in paths:
    mi=lib.load_asset(path); assert mi
    stats=edit.get_statistics(mi)
    report[path]={k:stats.get_editor_property(k) for k in ['num_pixel_shader_instructions','num_vertex_shader_instructions','num_samplers','num_pixel_texture_samples','num_vertex_texture_samples']}
    assert report[path]['num_pixel_shader_instructions']>0
vomit=lib.load_asset(paths[-1]).get_editor_property('parent')
assert vomit.get_path_name()=='/Game/Gameplay/Hazards/M_VomitPuddle.M_VomitPuddle'
assert vomit.get_editor_property('blend_mode')==u.BlendMode.BLEND_MASKED
assert edit.get_material_property_input_node(vomit,u.MaterialProperty.MP_OPACITY_MASK)
params=[str(n.get_editor_property('parameter_name')) for n in edit.get_material_expressions(vomit) if isinstance(n,(u.MaterialExpressionScalarParameter,u.MaterialExpressionVectorParameter,u.MaterialExpressionTextureObjectParameter))]
for name in ['WipeMask','Finish','Brush','BrushAge','Seed','WorldSize','FoamAmount','ChunkAmount']: assert name in params,name
assert len(params)==len(set(params)),params
old=json.loads(Path(u.Paths.project_saved_dir(),'FluidAudit.json').read_text(encoding='utf-8'))
prior=next(n['code'] for n in old['customs']['/Game/Art/Materials/M_CoffeeSurface'] if n['description'].startswith('Shared height:'))
surface=lib.load_asset('/Game/Gameplay/Liquid/Stylized/M_StylizedLiquidSurface')
current=next(n.get_editor_property('code') for n in edit.get_material_expressions(surface) if isinstance(n,u.MaterialExpressionCustom) and n.get_editor_property('description').startswith('Shared height:'))
assert current==prior,'Server-shared surface math must remain identical'
clear=lib.load_asset('/Game/Gameplay/Liquid/Stylized/M_ClearLiquidSurface')
assert clear.get_editor_property('shading_model')==u.MaterialShadingModel.MSM_THIN_TRANSLUCENT
assert clear.get_editor_property('translucency_lighting_mode')==u.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING
clear_height=next(n.get_editor_property('code') for n in edit.get_material_expressions(clear) if isinstance(n,u.MaterialExpressionCustom) and n.get_editor_property('description').startswith('Shared height:'))
assert clear_height==prior,'Water preset must preserve the same server height contract'
water_profile=lib.load_asset('/Game/Data/DA_ClearWater')
for field,expected in [('surface_material',paths[1]),('pour_material','/Game/Gameplay/Liquid/Stylized/MI_ClearWaterPour'),('drop_material','/Game/Gameplay/Liquid/Stylized/MI_ClearWaterDrops')]:
    assert expected in str(water_profile.get_editor_property(field)),field
water_settings=water_profile.get_editor_property('settings')
coffee_settings=lib.load_asset('/Game/Data/DA_CoffeeWater').get_editor_property('settings')
for name in ['ripple_height','dry_height','ripple_length','ripple_speed','fill_seconds','drain_seconds','cycles','inlet','drain_point']:
    assert water_settings.get_editor_property(name)==coffee_settings.get_editor_property(name),name
assert '/Game/Gameplay/Liquid/Stylized/MI_StylizedCoffee.MI_StylizedCoffee' in str(lib.load_asset('/Game/Data/DA_CoffeeWater').get_editor_property('surface_material'))
plan=lib.load_asset('/Game/Data/DA_Day01')
c=plan.get_editor_property('arena_center'); e=plan.get_editor_property('arena_half_size')
assert c.x==100 and c.y==-25 and e.x==1350 and e.y==850
report['arena']=dict(center=[c.x,c.y,c.z],half_size=[e.x,e.y,e.z])
report['coffee_wpo_matches_server']=True
report['water_wpo_matches_server']=True
report['clear_water_profile_bound']=True
report['vomit_shares_persistent_cleaning_mask']=True
Path(u.Paths.project_saved_dir(),'FluidShaderAudit.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
u.log('MC_FLUID_SHADER_AUDIT_PASS')
