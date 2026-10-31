"""Bounded Editor-Python repair of the task smoke MIC; never delete assets.

Only owned study material references change: smoke parent/texture overrides and
the dental atlas's canonical study texture. Never modify imported resources.
MaterialFunctions, MPCs and CurveAtlas resources remain shared read-only. This
does not claim full recursive isolation. Outputs stay in the task directory on E:.
"""
import hashlib
import json
from pathlib import Path
import unreal as u

ROOT='/Game/Tests/VFXPractice'
TAG='MC.VFXPractice.20261030'
OUT=Path('E:/CodexWork/MessControl/Validation/VFXPracticeSandbox_20261030')
PROJECT=Path('E:/DEVGAME/MessControl')
SOURCE='/Game/Gameplay/ActiveSkills/VFX/Materials/MI_ActivePhysicalSmoke'
INSTANCE=ROOT+'/Materials/MI_ActivePhysicalSmoke_Study_978e67'
PARENT=ROOT+'/Materials/M_SmokeAndFire_Sprites_Study_a0bf0f'
EXPECTED_TEXTURES={
    '/Game/NiagaraExamples/Textures/Sprites/T_SmokePuffLight_EOO_Loop',
    '/Game/NiagaraExamples/Textures/Sprites/T_SmokePuffLight_Normals_Loop',
}


def package(obj):return obj.get_path_name().split('.',1)[0]
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def disk(package_name):return PROJECT/'Content'/Path(package_name.removeprefix('/Game/')+'.uasset')


def main():
    assert Path(u.Paths.project_dir()).resolve()==PROJECT
    assert not u.EditorLevelLibrary.get_pie_worlds(True),'Stop owned PIE before material repair'
    lib=u.EditorAssetLibrary;edit=u.MaterialEditingLibrary
    source=lib.load_asset(SOURCE);instance=lib.load_asset(INSTANCE);parent=lib.load_asset(PARENT)
    assert isinstance(source,u.MaterialInstanceConstant) and isinstance(instance,u.MaterialInstanceConstant)
    assert isinstance(parent,u.Material)
    for owned in (instance,parent):
        assert package(owned).startswith(ROOT+'/') and lib.get_metadata_tag(owned,'MC.VFXPracticeOwner')==TAG
    # This repair protects saved production bytes and records, without modifying,
    # the dirty source Blueprint state. Never compile/save/reload that Blueprint.
    protected=[disk(SOURCE),PROJECT/'Content/Blueprints/BP_PlayerCharacter.uasset']
    before_hashes={str(path):sha(path) for path in protected}
    dirty_before=[p.get_path_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
    overrides=[]
    edit.set_material_instance_parent(instance,parent)
    valid_names={str(name) for name in edit.get_texture_parameter_names(parent)}
    # Old imported MICs retain obsolete override rows for renamed parameters.
    # They are not authored inputs and must not keep production texture roots.
    instance.set_editor_property('texture_parameter_values',[row for row in instance.get_editor_property('texture_parameter_values')
        if str(row.get_editor_property('parameter_info').get_editor_property('name')) in valid_names])
    for row in source.get_editor_property('texture_parameter_values'):
        info=row.get_editor_property('parameter_info')
        name=info.get_editor_property('name');association=info.get_editor_property('association')
        texture=edit.get_material_instance_texture_parameter_value(source,name,association)
        if texture and str(name) in valid_names:
            overrides.append((name,association,texture))
    assert {package(texture) for _,_,texture in overrides}==EXPECTED_TEXTURES and len(overrides)>=2,overrides
    changes=[];saved=[];desired=[]
    for name,association,texture in overrides:
        old=package(texture)
        target=ROOT+'/Textures/'+old.rsplit('/',1)[-1]+'_Study_'+hashlib.sha1(old.encode()).hexdigest()[:6]
        if lib.does_asset_exist(target):
            copy=lib.load_asset(target)
            assert lib.get_metadata_tag(copy,'MC.VFXPracticeOwner')==TAG,target
        else:
            copy=lib.duplicate_asset(old,target);assert copy,target
            lib.set_metadata_tag(copy,'MC.VFXPracticeOwner',TAG)
        assert isinstance(copy,u.Texture)
        changes.append(dict(parameter=str(name),association=str(association),source=old,target=target))
        # UE5.8's setter always returns false (bResult is never assigned).
        # Verify the actual parameter readback instead of that return value.
        edit.set_material_instance_texture_parameter_value(instance,name,copy,association)
        assert edit.get_material_instance_texture_parameter_value(instance,name,association)==copy
        desired.append((name,association,copy))
        assert lib.save_loaded_asset(copy,only_if_is_dirty=False)
        saved.append(target)
    parent_before=package(instance.get_editor_property('parent'))
    edit.set_material_instance_parent(instance,parent)
    edit.update_material_instance(instance)
    assert instance.get_editor_property('parent')==parent
    assert all(edit.get_material_instance_texture_parameter_value(instance,name,association)==copy for name,association,copy in desired)
    assert lib.save_loaded_asset(instance,only_if_is_dirty=False)
    saved.append(INSTANCE)
    # Earlier reruns duplicated the already private foam texture. Keep the first
    # canonical study atlas so the learner sees one editable source.
    foam=lib.load_asset(ROOT+'/Textures/T_PracticeFoamClumps')
    assert foam and lib.get_metadata_tag(foam,'MC.VFXPracticeOwner')==TAG
    for asset_path in lib.list_assets(ROOT+'/Materials',recursive=True,include_folder=False):
        path=str(asset_path).split('.',1)[0]
        if path.count('_Study_')!=1:continue
        material=lib.load_asset(path)
        if not isinstance(material,u.Material):continue
        assert lib.get_metadata_tag(material,'MC.VFXPracticeOwner')==TAG
        altered=False
        for expression in u.ObjectIterator(u.MaterialExpressionTextureSample):
            if expression.get_outer()!=material:continue
            texture=expression.get_editor_property('texture')
            if texture and package(texture).startswith(ROOT+'/Textures/T_PracticeFoamClumps_Study_'):
                material.modify();expression.modify();expression.set_editor_property('texture',foam);altered=True
        if altered:
            edit.recompile_material(material)
            assert lib.save_loaded_asset(material,only_if_is_dirty=False)
            saved.append(path)
    assert all(sha(path)==old for path_name,old in before_hashes.items() for path in [Path(path_name)])

    # Query saved hard package references only. This is a candidate report, never
    # authorization to delete; dirty in-memory references are recorded separately.
    registry=u.AssetRegistryHelpers.get_asset_registry()
    options=u.AssetRegistryDependencyOptions()
    for name in ('include_soft_package_references','include_searchable_names',
                 'include_hard_management_references','include_soft_management_references'):
        options.set_editor_property(name,False)
    options.set_editor_property('include_hard_package_references',True)
    candidates=[]
    dirty_now={p.get_path_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
    for asset_path in sorted(lib.list_assets(ROOT,recursive=True,include_folder=False)):
        path=str(asset_path).split('.',1)[0]
        if path.count('_Study_')<2 and '/T_PracticeFoamClumps_Study_' not in path:continue
        obj=lib.load_asset(path)
        if not obj or lib.get_metadata_tag(obj,'MC.VFXPracticeOwner')!=TAG:continue
        refs=sorted(str(p) for p in (registry.get_referencers(u.Name(path),options) or []))
        candidates.append(dict(package=path,saved_hard_package_referencers=refs,
                               currently_dirty=path in dirty_now,
                               deletion_performed=False))
    result=dict(repaired_instance=INSTANCE,parent_before=parent_before,parent_after=PARENT,
                override_changes=changes,saved_only=saved,protected_hashes=before_hashes,
                protected_unchanged=True,dirty_before=dirty_before,
                dirty_after=[p.get_path_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()],
                candidates=candidates,deletions_performed=False,
                dependency_evidence='Saved hard package referencers from current Editor asset registry; dirty/unsaved and non-reflected references are not exhaustive.',
                shared_read_only_resources='Imported MaterialFunctions, MPCs and CurveAtlas resources remain shared; not full recursive isolation.',
                runtime_validation='Deferred to root preview if needed; no PIE/build/source asset save in this helper.')
    OUT.mkdir(parents=True,exist_ok=True)
    (OUT/'StudyMaterialRepair.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
    print('VFX_PRACTICE_STUDY_MATERIAL_REPAIRED',json.dumps(dict(saved=saved,candidate_count=len(candidates))))


if __name__=='__main__':main()
