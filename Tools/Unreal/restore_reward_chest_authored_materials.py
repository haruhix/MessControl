"""Restore/choose only existing BP_RewardChest's gold surface and bounded glow.

Preserves meshes, UVs, dimensions, graph and VFX component structure. Saves
BP_RewardChest and, for explicit glow moderation, new owned MIC overrides only.
MC_REWARD_CHEST_MATERIAL_OVERRIDE optionally selects a root-reviewed material;
default remains the original MI_RewardChest_Glow. No model/texture substitution.
Requires an idle clean editor; backup is verified on E: before modification.
"""
import hashlib
import json
import shutil
from pathlib import Path
import unreal as u

OUT=Path(globals().get('MC_REWARD_CHEST_RESTORE_OUTPUT',
    'E:/CodexWork/HubChestWardrobe_20261022/ChestBlueprintRestore')).resolve()
assert OUT.drive.upper()=='E:'
assert not u.EditorLevelLibrary.get_pie_worlds(False), 'Preserve an active Play'
assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages(), 'Preserve unsaved maps'
assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages(), 'Preserve unsaved content'
OUT.mkdir(parents=True,exist_ok=True)
assert not (OUT/'Authoring.json').exists(), 'Use a fresh output folder for a new authoring pass'
ROOT=Path(u.Paths.project_dir()).resolve()
package=ROOT/'Content/Gameplay/Roguelike/BP_RewardChest.uasset'
backup=OUT/'BeforePackages'/package.name
backup.parent.mkdir(parents=True,exist_ok=True)
before_hash=hashlib.sha256(package.read_bytes()).hexdigest()
if not backup.exists():shutil.copy2(package,backup)
assert hashlib.sha256(backup.read_bytes()).hexdigest()==before_hash,'Existing backup differs; use a fresh output folder'
bp=u.load_asset('/Game/Gameplay/Roguelike/BP_RewardChest')
material_path=globals().get('MC_REWARD_CHEST_MATERIAL_OVERRIDE',
    '/Game/Gameplay/VFX/RewardChest/MI_RewardChest_Glow')
material=u.load_asset(material_path)
assert bp and material
u.BlueprintEditorLibrary.compile_blueprint(bp)
cdo=u.get_default_object(bp.generated_class())
before_surface_override=cdo.get_editor_property('SurfaceMaterialOverride')
before_surface_override=before_surface_override.get_path_name() if before_surface_override else None
cdo.set_editor_property('SurfaceMaterialOverride',material)
before={}
for name in ['body','lid']:
    comp=cdo.get_editor_property(name)
    before[name]=dict(mesh=comp.static_mesh.get_path_name(),
        material=comp.get_material(0).get_path_name() if comp.get_material(0) else None,
        relative_transform=str(comp.get_relative_transform()))
    comp.set_material(0,material)
effect_changes=[]
moderate=bool(globals().get('MC_REWARD_CHEST_MODERATE_GLOW',False))
glow_scale=float(globals().get('MC_REWARD_CHEST_GLOW_SCALE',.35))
assert .05<=glow_scale<=1., 'Keep the cosmetic light scale bounded'
if moderate:
    # Blueprint SCS decorations are not all present on the actor CDO. Obtain
    # their editable Blueprint templates without touching their hierarchy/meshes.
    subsystem=u.get_engine_subsystem(u.SubobjectDataSubsystem)
    functions=u.SubobjectDataBlueprintFunctionLibrary
    handles=subsystem.k2_gather_subobject_data_for_blueprint(bp)
    for handle in handles:
        data=functions.get_data(handle)
        variable=str(functions.get_variable_name(data))
        obj=functions.get_object_for_blueprint(data,bp)
        if not isinstance(obj,u.SceneComponent):continue
        label=variable or obj.get_name().removesuffix('_GEN_VARIABLE')
        if label not in ['RewardGlowRays','RewardSpotlightBeam','RewardFloorHalo',
                'RewardInnerGlow','RewardOverheadSpot']:continue
        if isinstance(obj,u.StaticMeshComponent):
            original=obj.get_material(0)
            if not original:continue
            parameters=[str(p) for p in u.MaterialEditingLibrary.get_scalar_parameter_names(original)]
            if 'GlowIntensity' not in parameters:continue
            if isinstance(original,u.MaterialInstanceConstant):
                old=u.MaterialEditingLibrary.get_material_instance_scalar_parameter_value(original,'GlowIntensity')
            else:old=u.MaterialEditingLibrary.get_material_default_scalar_parameter_value(original,'GlowIntensity')
            name='MI_RewardChest_'+label+'_Subtle'
            asset='/Game/Gameplay/VFX/RewardChest/'+name
            instance=u.load_asset(asset)
            if instance:
                parent=instance.get_editor_property('parent')
                if original==instance:
                    original=parent
                    old=u.MaterialEditingLibrary.get_material_instance_scalar_parameter_value(parent,'GlowIntensity') if isinstance(parent,u.MaterialInstanceConstant) else u.MaterialEditingLibrary.get_material_default_scalar_parameter_value(parent,'GlowIntensity')
                file=ROOT/'Content/Gameplay/VFX/RewardChest'/(name+'.uasset')
                effect_backup=OUT/'BeforePackages'/file.name
                if file.exists():shutil.copy2(file,effect_backup)
            else:
                instance=u.AssetToolsHelpers.get_asset_tools().create_asset(name,
                    '/Game/Gameplay/VFX/RewardChest',u.MaterialInstanceConstant,u.MaterialInstanceConstantFactoryNew())
            u.MaterialEditingLibrary.set_material_instance_parent(instance,original)
            u.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(instance,'GlowIntensity',old*glow_scale)
            u.MaterialEditingLibrary.update_material_instance(instance)
            assert u.EditorAssetLibrary.save_loaded_asset(instance,only_if_is_dirty=False)
            obj.set_material(0,instance)
            effect_changes.append(dict(component=label,material=instance.get_path_name(),
                original_material=original.get_path_name(),original_glow=old,glow=old*glow_scale))
        elif isinstance(obj,u.LightComponent):
            old=float(obj.get_editor_property('intensity'))
            obj.set_intensity(old*glow_scale)
            effect_changes.append(dict(component=label,original_intensity=old,intensity=old*glow_scale))
u.BlueprintEditorLibrary.compile_blueprint(bp)
cdo=u.get_default_object(bp.generated_class())
assert cdo.get_editor_property('SurfaceMaterialOverride')==material,'Blueprint compilation lost the explicit surface override'
after={}
for name in ['body','lid']:
    comp=cdo.get_editor_property(name)
    after[name]=dict(mesh=comp.static_mesh.get_path_name(),material=comp.get_material(0).get_path_name(),
        relative_transform=str(comp.get_relative_transform()))
    assert after[name]['mesh']==before[name]['mesh'],'Unexpected model replacement'
    assert after[name]['relative_transform']==before[name]['relative_transform'],'Unexpected seating or scale change'
    assert after[name]['material']==material.get_path_name(),'Blueprint compilation lost the authored material'
assert u.EditorAssetLibrary.save_loaded_asset(bp,only_if_is_dirty=False)
report=dict(status='saved',asset=bp.get_path_name(),before=before,after=after,
    before_surface_override=before_surface_override,surface_material_override=material.get_path_name(),
    before_sha256=before_hash,after_sha256=hashlib.sha256(package.read_bytes()).hexdigest(),
    backup=str(backup),scope='Gold material on existing body/lid; optional bounded scalar/light changes. Meshes, UV, scale, graphs and VFX structure unchanged.',
    selected_material=material.get_path_name(),effect_changes=effect_changes,
    validation='Blueprint compiled; CDO component materials and explicit actor SurfaceMaterialOverride verified. Fresh spawned/PIE BeginPlay material still requires runtime check.')
(OUT/'Authoring.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
u.log('MC_REWARD_CHEST_AUTHORED_RESTORED '+str(OUT/'Authoring.json'))
