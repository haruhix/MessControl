"""Create ambient Niagara motes and place one editable Blueprint in L_Mouth.

Run after an editor build with PIE stopped. Existing artist edits are preserved.
"""
import json
from pathlib import Path
import unreal as u

ROOT = '/Game/Gameplay/VFX/Ambient'
SYSTEM = ROOT + '/NS_AmbientParticles'
BLUEPRINT = ROOT + '/BP_AmbientParticles'
lib = u.EditorAssetLibrary
edit = u.MaterialEditingLibrary
tools = u.AssetToolsHelpers.get_asset_tools()
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor(), 'Stop PIE first.'
world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
assert world and world.get_path_name().startswith('/Game/Maps/L_Mouth.'), 'Open L_Mouth first.'
lib.make_directory(ROOT)

mat = u.load_asset(ROOT + '/M_AmbientParticle')
if mat is None:
    mat = tools.create_asset('M_AmbientParticle', ROOT, u.Material, u.MaterialFactoryNew())
assert mat
if lib.get_metadata_tag(mat, 'Ambient.AuthoringComplete') != '2':
    edit.delete_all_material_expressions(mat)
    mat.set_editor_properties(dict(blend_mode=u.BlendMode.BLEND_TRANSLUCENT,
        shading_model=u.MaterialShadingModel.MSM_UNLIT, two_sided=True,
        used_with_niagara_sprites=True))

    def node(cls, x, y, **props):
        result = edit.create_material_expression(mat, cls, x, y)
        result.set_editor_properties(props)
        return result

    uv = node(u.MaterialExpressionTextureCoordinate, -600, 0)
    color = node(u.MaterialExpressionParticleColor, -600, 200)
    position = node(u.MaterialExpressionWorldPosition, -600, 600)
    camera = node(u.MaterialExpressionCameraPositionWS, -600, 800)
    opacity = node(u.MaterialExpressionCustom, -250, 100,
        description='Soft round mote and camera proximity fade; lifetime alpha comes from Niagara',
        output_type=u.CustomMaterialOutputType.CMOT_FLOAT1,
        code='float2 p=(UV-.5)*2;\n'
             'float soft=pow(saturate(1-dot(p,p)),2);\n'
             'float nearFade=smoothstep(70,180,length(Position-Camera));\n'
             'return soft*Color.a*nearFade;')
    inputs = []
    for name, source, pin in [('UV',uv,''), ('Color',color,'RGBA'),
                              ('Position',position,''), ('Camera',camera,'')]:
        entry = u.CustomInput()
        entry.set_editor_property('input_name', name)
        inputs.append(entry)
    opacity.set_editor_property('inputs', inputs)
    for name, source, pin in [('UV',uv,''), ('Color',color,'RGBA'),
                              ('Position',position,''), ('Camera',camera,'')]:
        assert edit.connect_material_expressions(source,pin,opacity,name)
    depth = node(u.MaterialExpressionDepthFade, 50, 100, fade_distance_default=20.0)
    assert edit.connect_material_expressions(opacity,'',depth,'Opacity')
    assert edit.connect_material_property(depth,'',u.MaterialProperty.MP_OPACITY)
    brightness = node(u.MaterialExpressionMultiply, -200, -200, const_b=.65)
    assert edit.connect_material_expressions(color,'RGB',brightness,'A')
    assert edit.connect_material_property(brightness,'',u.MaterialProperty.MP_EMISSIVE_COLOR)
    errors = edit.recompile_material(mat)
    assert not errors, list(errors)
    lib.set_metadata_tag(mat, 'Ambient.AuthoringComplete', '2')
    assert lib.save_loaded_asset(mat, False)

system = u.MCVFXAssetBuilder.create_ambient_particles()
assert system, 'Ambient Niagara asset creation failed.'
if lib.get_metadata_tag(system, 'Ambient.AuthoringComplete') != '1':
    emitter = u.find_object(system, 'FloatingMotes_0')
    assert emitter
    emitter.set_editor_property('FixedBounds', u.Box(
        min=u.Vector(-5000,-5000,-2500), max=u.Vector(5000,5000,2500)))
    modules = {m.get_class().get_name().removeprefix('NiagaraStatelessModule_'):m
               for m in emitter.get_editor_property('Modules')}
    # Lightweight emitters evaluate age inside modules. Material ParticleRelativeTime
    # is not available from this template's output attributes.
    scale = modules['ScaleColor']
    scale.set_editor_property('bModuleEnabled', True)
    fade = scale.get_editor_property('ScaleDistribution').copy()
    assert fade.import_text('(Mode=NonUniformCurve,ChannelConstantsAndRanges=,'
        'ChannelCurves=((Keys=((Time=0,Value=1))),(Keys=((Time=0,Value=1))),'
        '(Keys=((Time=0,Value=1))),(Keys=((InterpMode=RCIM_Cubic,Time=0,Value=0),'
        '(InterpMode=RCIM_Cubic,Time=.12,Value=1),(InterpMode=RCIM_Cubic,Time=.78,Value=1),'
        '(InterpMode=RCIM_Cubic,Time=1,Value=0)))))')
    scale.set_editor_property('ScaleDistribution', fade)
    lib.set_metadata_tag(system, 'Ambient.AuthoringComplete', '1')
    assert lib.save_loaded_asset(system, False)
bp = u.load_asset(BLUEPRINT)
if bp is None:
    factory = u.BlueprintFactory()
    factory.set_editor_property('parent_class', u.NiagaraActor)
    bp = tools.create_asset('BP_AmbientParticles', ROOT, u.Blueprint, factory)
assert bp
if lib.get_metadata_tag(bp, 'Ambient.AuthoringComplete') != '1':
    u.BlueprintEditorLibrary.compile_blueprint(bp)
    cls = u.load_class(None, BLUEPRINT + '.BP_AmbientParticles_C')
    defaults = u.get_default_object(cls)
    component = defaults.get_component_by_class(u.NiagaraComponent)
    assert component
    component.set_asset(system)
    component.set_editor_properties(dict(auto_activate=True, cast_shadow=False))
    u.BlueprintEditorLibrary.compile_blueprint(bp)
    lib.set_metadata_tag(bp, 'Ambient.AuthoringComplete', '1')
    assert lib.save_loaded_asset(bp, False)

cls = u.load_class(None, BLUEPRINT + '.BP_AmbientParticles_C')
actors = u.get_editor_subsystem(u.EditorActorSubsystem)
placed = [a for a in actors.get_all_level_actors() if a.get_class() == cls]
assert len(placed) <= 1, 'Multiple ambient volumes: inspect the level before rerunning.'
if placed:
    actor = placed[0]
else:
    with u.ScopedEditorTransaction('Add ambient floating particles'):
        actor = actors.spawn_actor_from_class(cls, u.Vector(600,-150,420))
        assert actor
        actor.set_actor_label('VFX | Ambient small particles')
        actor.set_folder_path('VFX')
assert actor.get_component_by_class(u.NiagaraComponent).get_asset() == system
assert actor.get_component_by_class(u.NiagaraComponent).get_editor_property('auto_activate')
assert u.get_editor_subsystem(u.LevelEditorSubsystem).save_current_level()
actors.set_selected_level_actors([actor])
report = dict(actor=actor.get_path_name(), blueprint=BLUEPRINT, system=SYSTEM,
              material=mat.get_path_name(), parameters={
                  'SpawnRate':40, 'ParticleSize':[3,3], 'ParticleColor':[.82,.9,1,.45],
                  'VolumeSize':[3400,2500,1000], 'DriftVelocity':[4,-2,5]},
              lifetime=[20,30], maximum_default_particles=1200)
Path(u.Paths.project_saved_dir()+'AmbientParticlesBuild.json').write_text(
    json.dumps(report,indent=2),encoding='utf-8')
print('MC_AMBIENT_PARTICLES_READY '+json.dumps(report))
