"""Reference a small Fab Niagara gallery in the VFX lab only.

Source systems keep their authored parameters and are never saved by this script.
Looping samples auto-activate when the lab is opened; bursts stay available for
manual activation in the Niagara component Details panel.
"""
import json
import unreal as u


LAB_PATH = '/Game/Tests/L_VFXLab'
SAMPLE_TAG = 'FabVFXLabSample'
SAMPLES = [
    ('BossCircle', '/Game/Free_Magic/VFX_Niagara/NS_Free_Magic_Circle1',
     'Free Magic - Circle1\nmanual: Details > Reset', (-2750, 1200, 12), 0.55, False),
    ('BossAura', '/Game/Free_Magic/VFX_Niagara/NS_Free_Magic_Aura',
     'Free Magic - Aura\nloop / auto start', (-2050, 1200, 12), 0.65, True),
    ('FeatherFall', '/Game/FreeParticle_SoftTofu/Niagara/NS_feather',
     'SoftTofu - feather fall study\nmanual: Details > Reset', (0, 1200, 650), 0.7, False),
    ('FreezeSparkling', '/Game/FreeParticle_SoftTofu/Niagara/NS_Sparkling_Glow',
     'SoftTofu - Sparkling Glow\nloop / auto start', (2400, 1200, 80), 0.65, True),
    ('LiquidBubbles', '/Game/NiagaraExamples/FX_Footstep/NS_Footstep_Bubbles',
     'Epic - Footstep Bubbles / liquid study\nmanual: Details > Reset', (-2400, -1200, 12), 0.8, False),
    ('ToolHit', '/Game/Free_Magic/VFX_Niagara/NS_Free_Magic_Hit1',
     'Free Magic - Hit1\nmanual: Details > Reset', (-350, -1200, 65), 0.55, False),
    ('ToolSparks', '/Game/NiagaraExamples/FX_Sparks/NS_Spark_Impact_Looping',
     'Epic - Spark Impact Looping\nloop / auto start', (350, -1200, 65), 0.65, True),
    ('NutFire', '/Game/Fire_EXP_Vol01_Free/Niagara/Fire/Loop/NS_Sub_FireSmall_Loop_001',
     'DeepBlues - Fire Small\nloop / auto start', (2400, -1200, 12), 0.55, True),
]
assert len(SAMPLES) <= 8
lib = u.EditorAssetLibrary
editor = u.get_editor_subsystem(u.UnrealEditorSubsystem)
actor_tools = u.get_editor_subsystem(u.EditorActorSubsystem)
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
current_world = editor.get_editor_world()
current_path = current_world.get_path_name()
selected = list(actor_tools.get_selected_level_actors())
world = lib.load_asset(LAB_PATH)
assert world and world.get_path_name() == LAB_PATH + '.L_VFXLab', 'Resolve only the requested lab'
level = world.get_world_settings().get_outer()
assert level.get_outer() == world

# Resolve only the eight samples; importing or discovering their source packs
# belongs to Fab/Launcher and is deliberately outside this authoring script.
resolved = []
for name, path, text, location, scale, auto_activate in SAMPLES:
    system = lib.load_asset(path)
    assert isinstance(system, u.NiagaraSystem), 'Missing Niagara System: ' + path
    resolved.append((name, path, text, location, scale, auto_activate, system))

for actor in list(u.GameplayStatics.get_all_actors_of_class(world, u.Actor)):
    if SAMPLE_TAG in [str(tag) for tag in actor.tags] or actor.get_name().startswith('FabVFXLabTemplate_'):
        actor.destroy_actor()

created = []


def add_actor(actor_class, name, location, scale=1.0, rotation=(0, 0, 0)):
    template = u.new_object(actor_class, outer=level, name='FabVFXLabTemplate_' + name)
    if isinstance(template, u.NiagaraActor):
        template.get_component_by_class(u.NiagaraComponent).set_auto_activate(False)
    actor = actor_tools.duplicate_actor(template, world, u.Vector())
    template.destroy_actor()
    assert actor and actor.get_world() == world, name
    actor.set_actor_label('VFXLab_Fab_' + name)
    actor.tags = [u.Name(SAMPLE_TAG)]
    actor.set_actor_location(u.Vector(*location), False, False)
    actor.set_actor_rotation(u.Rotator(pitch=rotation[0], yaw=rotation[1], roll=rotation[2]), False)
    actor.set_actor_scale3d(u.Vector(scale, scale, scale))
    created.append(actor)
    return actor


for name, path, text, location, scale, auto_activate, system in resolved:
    sample = add_actor(u.NiagaraActor, name, location, scale)
    component = sample.get_component_by_class(u.NiagaraComponent)
    # SetAutoActivate ignores calls after component registration. These are
    # editor-authored instances, so write the reflected property explicitly.
    component.set_editor_property('auto_activate', auto_activate)
    assert component.auto_activate == auto_activate, name + ': auto activation was not persisted'
    component.set_asset(system)
    sample.set_destroy_on_system_finish(False)
    if auto_activate and world == current_world:
        component.activate(True)
    x, y, _ = location
    label = add_actor(u.TextRenderActor, name + '_Label', (x, y - 580, 80), rotation=(0, -90, 0))
    label.text_render.set_text(text)
    label.text_render.set_world_size(32)
    label.text_render.set_horizontal_alignment(u.HorizTextAligment.EHTA_CENTER)
    label.text_render.set_text_render_color(u.Color(230, 240, 255, 255))

world.get_world_settings().set_editor_property('default_game_mode', u.GameModeBase)
assert editor.get_editor_world() == current_world
actor_tools.set_selected_level_actors(selected)
assert lib.save_loaded_asset(world, only_if_is_dirty=False), 'Lab save failed'
assert editor.get_editor_world().get_path_name() == current_path
print('FAB_VFX_LAB_SAMPLES ' + json.dumps({
    'saved_map': world.get_path_name(),
    'source_packages_saved': False,
    'current_map_unchanged': current_path,
    'new_actor_count': len(created),
    'niagara_actor_count': len(resolved),
    'auto_activate_count': sum(1 for sample in resolved if sample[5]),
    'samples': [dict(name=sample[0], system=sample[1], auto_activate=sample[5]) for sample in resolved],
}, ensure_ascii=False))
