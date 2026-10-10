"""Create the isolated VFX lab without loading or saving the active editor map."""
import json
import unreal as u


PATH = '/Game/Tests/L_VFXLab'
FOLDER = '/Game/Tests/VFXLab'
TAG = 'VFXLabAuthored'

def main():
    lib = u.EditorAssetLibrary
    asset_tools = u.AssetToolsHelpers.get_asset_tools()
    editor = u.get_editor_subsystem(u.UnrealEditorSubsystem)
    actor_tools = u.get_editor_subsystem(u.EditorActorSubsystem)
    level_editor = u.get_editor_subsystem(u.LevelEditorSubsystem)
    assert not level_editor.is_in_play_in_editor(), 'Stop PIE before authoring the lab'
    current_world = editor.get_editor_world()
    current_path = current_world.get_path_name()
    selected = list(actor_tools.get_selected_level_actors())
    world = lib.load_asset(PATH) if lib.does_asset_exist(PATH) else None
    if world is None:
        factory = u.WorldFactory()
        factory.set_editor_property('edit_after_new', False)
        world = asset_tools.create_asset('L_VFXLab', '/Game/Tests', u.World, factory)
    assert world and world != current_world, 'The lab must be an inactive World asset'
    level = world.get_world_settings().get_outer()
    assert level.get_outer() == world


    def owned(actor):
        return TAG in [str(tag) for tag in actor.tags] or actor.get_name().startswith('VFXLabTemplate') or actor.get_actor_label().startswith('VFXLab_')


    for actor in list(u.GameplayStatics.get_all_actors_of_class(world, u.Actor)):
        if owned(actor):
            actor.destroy_actor()


    created = []


    def add_actor(actor_class, name, location=(0, 0, 0), rotation=(0, 0, 0), scale=(1, 1, 1)):
        # The editor spawn helpers always target the active map. Duplication accepts
        # an explicit destination World and initializes/registers the actor there.
        template = u.new_object(actor_class, outer=level, name='VFXLabTemplate_' + name)
        actor = actor_tools.duplicate_actor(template, world, u.Vector())
        template.destroy_actor()
        assert actor and actor.get_world() == world, name
        actor.set_actor_label('VFXLab_' + name)
        actor.tags = [u.Name(TAG)]
        actor.set_actor_location(u.Vector(*location), False, False)
        actor.set_actor_rotation(u.Rotator(pitch=rotation[0], yaw=rotation[1], roll=rotation[2]), False)
        actor.set_actor_scale3d(u.Vector(*scale))
        created.append(actor)
        return actor


    def surface_material(name, color):
        path = FOLDER + '/' + name
        material = lib.load_asset(path) if lib.does_asset_exist(path) else None
        if material is None:
            material = asset_tools.create_asset(name, FOLDER, u.Material, u.MaterialFactoryNew())
            tint = u.MaterialEditingLibrary.create_material_expression(material, u.MaterialExpressionConstant3Vector, -320, -80)
            tint.set_editor_property('constant', u.LinearColor(*color, 1))
            roughness = u.MaterialEditingLibrary.create_material_expression(material, u.MaterialExpressionConstant, -320, 80)
            roughness.set_editor_property('r', 0.8)
            assert u.MaterialEditingLibrary.connect_material_property(tint, '', u.MaterialProperty.MP_BASE_COLOR)
            assert u.MaterialEditingLibrary.connect_material_property(roughness, '', u.MaterialProperty.MP_ROUGHNESS)
            u.MaterialEditingLibrary.recompile_material(material)
            assert lib.save_loaded_asset(material, only_if_is_dirty=False)
        return material


    cube = u.load_asset('/Engine/BasicShapes/Cube')
    assert cube
    floor = add_actor(u.StaticMeshActor, 'Floor', (0, 0, -20), scale=(72, 48, 0.4))
    assert floor.static_mesh_component.set_static_mesh(cube)
    floor.static_mesh_component.set_material(0, surface_material('M_LabFloor', (0.025, 0.03, 0.045)))
    floor.static_mesh_component.set_collision_profile_name('BlockAll')

    zones = [
        ('01_BossVortex', '01  BOSS VORTEX', (-2400, 1200), (0.07, 0.26, 0.36)),
        ('02_FallingIce', '02  FALLING ICE', (0, 1200), (0.16, 0.35, 0.50)),
        ('03_FreezeFeet', '03  FREEZE FEET', (2400, 1200), (0.12, 0.30, 0.38)),
        ('04_Coffee', '04  COFFEE WAVE / PUDDLE', (-2400, -1200), (0.22, 0.10, 0.045)),
        ('05_ToolImpact', '05  DRILL / BRUSH IMPACT', (0, -1200), (0.28, 0.22, 0.055)),
        ('06_NutAttacks', '06  NUT ATTACKS', (2400, -1200), (0.24, 0.065, 0.045)),
    ]
    for name, title, (x, y), color in zones:
        pad = add_actor(u.StaticMeshActor, name + '_Pad', (x, y, 2), scale=(20, 18, 0.04))
        assert pad.static_mesh_component.set_static_mesh(cube)
        pad.static_mesh_component.set_material(0, surface_material('M_Lab_' + name, color))
        pad.static_mesh_component.set_collision_enabled(u.CollisionEnabled.NO_COLLISION)
        label = add_actor(u.TextRenderActor, name + '_Label', (x, y + 760, 160), (0, -90, 0))
        label.text_render.set_text(title)
        label.text_render.set_world_size(55)
        label.text_render.set_horizontal_alignment(u.HorizTextAligment.EHTA_CENTER)
        label.text_render.set_text_render_color(u.Color(235, 245, 255, 255))
        marker = add_actor(u.TargetPoint, name + '_FXOrigin', (x, y, 8))
        marker.set_actor_enable_collision(False)

    sun = add_actor(u.DirectionalLight, 'Sun', (0, 0, 1800), (-45, -35, 0))
    sun.light_component.set_mobility(u.ComponentMobility.MOVABLE)
    sun.light_component.set_intensity(4.0)
    sun.light_component.set_light_color(u.LinearColor(0.85, 0.91, 1.0))
    for index, y in enumerate((-1200, 1200), 1):
        fill = add_actor(u.PointLight, 'Fill_' + str(index), (0, y, 1500))
        fill.light_component.set_mobility(u.ComponentMobility.MOVABLE)
        fill.light_component.set_intensity(4000000)
        fill.light_component.set_editor_property('attenuation_radius', 6500)
        fill.light_component.set_cast_shadows(False)

    add_actor(u.PlayerStart, 'PlayerStart', (0, -2200, 120), (0, 90, 0))

    # Neutral editor sandbox; opening this map later must not start the game director.
    world.get_world_settings().set_editor_property('default_game_mode', u.GameModeBase)
    assert editor.get_editor_world() == current_world, 'Active map changed during lab authoring'
    actor_tools.set_selected_level_actors(selected)
    assert lib.save_loaded_asset(world, only_if_is_dirty=False), 'L_VFXLab package save failed'
    assert editor.get_editor_world().get_path_name() == current_path
    report = {
        'asset': world.get_path_name(),
        'saved': True,
        'current_world_unchanged': current_path,
        'authored_actor_count': len(created),
        'zones': [title for _, title, _, _ in zones],
        'actors': [actor.get_actor_label() for actor in created],
    }
    print('VFX_LAB ' + json.dumps(report, ensure_ascii=False))


main()
