"""Create the editable player Blueprint with current native component classes."""
import unreal as u

path = '/Game/Blueprints/BP_PlayerCharacter'
bp = u.load_object(None, path + '.BP_PlayerCharacter')
if not bp:
    factory = u.BlueprintFactory()
    factory.set_editor_property('parent_class', u.MCToothCharacter)
    bp = u.AssetToolsHelpers.get_asset_tools().create_asset(
        'BP_PlayerCharacter', '/Game/Blueprints', u.Blueprint, factory)
assert bp
u.BlueprintEditorLibrary.compile_blueprint(bp)
cls = u.load_class(None, path + '.BP_PlayerCharacter_C')
hero = u.get_default_object(cls)
movement = hero.get_movement_component()
camera = hero.get_editor_property('camera')
boom = hero.get_editor_property('camera_boom')
assert movement.get_class().get_path_name() == '/Script/MessControl.MCToothMovementComponent'
assert boom.get_class().get_path_name() == '/Script/MessControl.MCOrbitSpringArmComponent'
assert camera.get_class().get_path_name() == '/Script/MessControl.MCPlayerCameraComponent'
assert camera.get_editor_property('editable_when_inherited')
assert u.EditorAssetLibrary.save_loaded_asset(bp, False)
print('MC_PLAYER_BLUEPRINT_SAVED ' + cls.get_path_name())
