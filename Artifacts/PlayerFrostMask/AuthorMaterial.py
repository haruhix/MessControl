import json
from pathlib import Path
import unreal as u

e, a = u.MaterialEditingLibrary, u.EditorAssetLibrary
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
mat = u.load_asset('/Game/Gameplay/Cold/M_PlayerIceCoating')
assert isinstance(mat, u.Material)
folder = '/Game/Gameplay/Cold/Frost'
tex_path = folder + '/T_PlayerFrostMask'
backup_path = folder + '/M_PlayerIceCoating_BeforeFrostMask'
assert not a.does_asset_exist(tex_path), 'Inspect an existing frost texture before replacing it.'
nodes = list(e.get_material_expressions(mat))
amount = next(n for n in nodes if isinstance(n, u.MaterialExpressionScalarParameter) and str(n.get_editor_property('parameter_name')) == 'IceAmount')
growth = next(n for n in nodes if isinstance(n, u.MaterialExpressionCustom) and str(n.get_editor_property('description')) == 'Ice patches grow on the animated body as the freeze meter fills')
front = e.get_material_property_input_node(mat, u.MaterialProperty.MP_FRONT_MATERIAL)
front_output = str(e.get_material_property_input_node_output_name(mat, u.MaterialProperty.MP_FRONT_MATERIAL))
wpo = e.get_material_property_input_node(mat, u.MaterialProperty.MP_WORLD_POSITION_OFFSET)
old_mask = e.get_material_property_input_node(mat, u.MaterialProperty.MP_OPACITY_MASK)

with u.ScopedEditorTransaction('Reveal frost crystals as the player freezes'):
    assert not a.does_asset_exist(backup_path)
    backup = a.duplicate_asset(mat.get_path_name().split('.')[0], backup_path)
    assert backup and a.save_loaded_asset(backup, only_if_is_dirty=False)
    task = u.AssetImportTask()
    task.set_editor_properties({'filename': 'D:/PROJECT/GAME/WaifuTD/MessControl/ArtSource/Cold/Frost/T_PlayerFrostMask.png',
        'destination_path': folder, 'destination_name': 'T_PlayerFrostMask', 'automated': True, 'save': False, 'factory': u.TextureFactory()})
    u.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    tex = task.get_objects()[0]
    assert isinstance(tex, u.Texture2D)
    tex.set_editor_properties({'srgb': False, 'compression_settings': u.TextureCompressionSettings.TC_GRAYSCALE,
        'address_x': u.TextureAddress.TA_WRAP, 'address_y': u.TextureAddress.TA_WRAP})
    assert a.save_loaded_asset(tex, only_if_is_dirty=False)

    def node(cls, x, y, **values):
        n = e.create_material_expression(mat, cls, x, y)
        n.set_editor_properties(values)
        return n

    def link(source, output, target, pin):
        assert e.connect_material_expressions(source, output, target, pin), pin

    uv = node(u.MaterialExpressionTextureCoordinate, -2350, 1700, coordinate_index=0)
    tiling = node(u.MaterialExpressionScalarParameter, -2350, 1880,
        parameter_name='Frost Tiling', group='Player Freeze', default_value=1.0, slider_min=0.25, slider_max=4.0,
        desc='Crystal pattern repetitions in character UV0. Lower values make larger crystals.')
    scaled_uv = node(u.MaterialExpressionMultiply, -2120, 1700)
    link(uv, '', scaled_uv, 'A')
    link(tiling, '', scaled_uv, 'B')
    sample = node(u.MaterialExpressionTextureSampleParameter2D, -1860, 1650,
        parameter_name='Frost Mask Texture', group='Player Freeze', texture=tex,
        sampler_type=u.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE,
        desc='White frost crystals appear first; darker filaments fill in as IceAmount increases. Black gaps remain clear.')
    link(scaled_uv, '', sample, 'UVs')
    softness = node(u.MaterialExpressionScalarParameter, -1500, 1900,
        parameter_name='Frost Softness', group='Player Freeze', default_value=0.10, slider_min=0.01, slider_max=0.30,
        desc='Width of the gradual crystal reveal. No effect on gameplay freeze timing.')
    inputs = []
    for name in ['Frost', 'Amount', 'Softness']:
        entry = u.CustomInput()
        entry.set_editor_property('input_name', name)
        inputs.append(entry)
    growth.set_editor_properties({'inputs': inputs, 'description': 'Frost crystals grow with the replicated freeze meter',
        'code': 'float a=saturate(Amount);\nif(a<=0.001) return 0.0;\nfloat threshold=lerp(0.89,0.04,a);\nfloat crystals=smoothstep(threshold,threshold+clamp(Softness,0.005,0.30),saturate(Frost));\nreturn crystals*smoothstep(0.0,0.12,a);'})
    link(sample, 'R', growth, 'Frost')
    link(amount, '', growth, 'Amount')
    link(softness, '', growth, 'Softness')
    # The previous patch mask used these two dedicated nodes only.
    for n in nodes:
        if isinstance(n, (u.MaterialExpressionPreSkinnedPosition, u.MaterialExpressionVertexInterpolator)):
            e.delete_material_expression(mat, n)
    errors = list(e.recompile_material(mat))
    assert not errors, errors
    assert e.get_material_property_input_node(mat, u.MaterialProperty.MP_FRONT_MATERIAL) == front
    assert str(e.get_material_property_input_node_output_name(mat, u.MaterialProperty.MP_FRONT_MATERIAL)) == front_output
    assert e.get_material_property_input_node(mat, u.MaterialProperty.MP_WORLD_POSITION_OFFSET) == wpo
    assert e.get_material_property_input_node(mat, u.MaterialProperty.MP_OPACITY_MASK) == old_mask
    assert amount.get_editor_property('default_value') == 0.0
    a.set_metadata_tag(mat, 'PlayerFrost.Backup', backup_path)
    a.set_metadata_tag(mat, 'PlayerFrost.Texture', tex.get_path_name())
    assert a.save_loaded_asset(mat, only_if_is_dirty=False)
    report = {'material': mat.get_path_name(), 'mask': tex.get_path_name(), 'backup': backup.get_path_name(),
        'compile_errors': errors, 'original_ice_shading_preserved': True, 'driver': 'Existing replicated IceAmount',
        'tiling': 1.0, 'softness': 0.10, 'transparent_at_zero': True, 'crystal_growth_code': growth.get_editor_property('code')}
    out = Path(u.Paths.project_dir())/'Artifacts/PlayerFrostMask'
    out.mkdir(parents=True, exist_ok=True)
    (out/'MaterialBuild.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    u.get_editor_subsystem(u.LevelEditorSubsystem).editor_invalidate_viewports()
    print('PLAYER_FROST_MATERIAL_SAVED', json.dumps(report))
