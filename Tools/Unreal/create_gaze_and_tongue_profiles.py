"""Add gameplay face mapping and event presets. Existing artist assets/tuning are preserved."""
import unreal as u

lib=u.EditorAssetLibrary
assets=u.AssetToolsHelpers.get_asset_tools()
def profile(name,cls,values=None,label=None):
    path='/Game/Data/'+name
    if lib.does_asset_exist(path):
        return lib.load_asset(path)
    factory=u.DataAssetFactory(); factory.set_editor_property('data_asset_class',cls)
    obj=assets.create_asset(name,'/Game/Data',cls,factory)
    if values:
        settings=obj.get_editor_property('settings')
        for key,value in values.items(): settings.set_editor_property('Return' if key=='return_' else key,value)
        obj.set_editor_property('settings',settings)
    if label: obj.set_editor_property('label',label)
    assert lib.save_loaded_asset(obj,only_if_is_dirty=False)
    return obj

gaze=profile('DA_Gaze',u.MCGazeProfile)
appearance=lib.load_asset('/Game/Data/DA_PlayerAppearance')
mapping=dict(appearance.get_editor_property('bone_map'))
existing={str(k) for k in mapping}
for role,bone in {'gaze_head':'head_x','eye_l':'c_eye_l','eye_r':'c_eye_r',
                  'lid_top_l':'c_eyelid_top_l','lid_top_r':'c_eyelid_top_r',
                  'lid_bottom_l':'c_eyelid_bot_l','lid_bottom_r':'c_eyelid_bot_r'}.items():
    if role not in existing: mapping[role]=bone
appearance.set_editor_property('bone_map',mapping)
assert lib.save_loaded_asset(appearance,only_if_is_dirty=False)

tongue=lib.load_asset('/Game/Data/DA_Tongue')
s=tongue.get_editor_property('settings')
get=lambda key:s.get_editor_property(key)
pain=profile('DA_Tongue_Pain',u.MCTongueMotionProfile,dict(
    shape=u.MCTongueShape.RADIAL_WAVE,height=get('wave_height'),radius=get('wave_radius'),
    width=get('wave_width'),speed=get('wave_speed'),anticipation=0.0,redness=1.0,
    lift=get('lift_speed'),push=get('push_speed'),affect_height=get('affect_height'),
    rest_after=max(.2,get('cooldown')-(get('wave_radius')+get('wave_width'))/get('wave_speed'))),
    'Волна боли от точки')
jolt=profile('DA_Tongue_Jolt',u.MCTongueMotionProfile,dict(
    shape=u.MCTongueShape.FRONT_BEND,height=get('jolt_height'),anticipation=get('jolt_anticipation'),
    rise=get('jolt_rise'),return_=get('jolt_return'),lift=get('jolt_lift'),push=get('jolt_push'),
    affect_height=get('affect_height'),push_from_origin=False,food_lift_ignores_mass=True),
    'Сильный изгиб')
local=profile('DA_Tongue_LocalLift',u.MCTongueMotionProfile,dict(
    shape=u.MCTongueShape.LOCAL_LIFT,height=100.0,radius=450.0,lift=650.0,
    redness=.65,anticipation=.65),'Локальный толчок от точки')
travel=profile('DA_Tongue_Travel',u.MCTongueMotionProfile,dict(
    shape=u.MCTongueShape.DIRECTIONAL_WAVE,height=65.0,radius=1800.0,
    width=340.0,speed=650.0,lift=350.0,push=340.0,anticipation=.4,redness=.35,
    push_from_origin=False),'Волна в заданном направлении')
for name,value in [('pain_motion',pain),('jolt_motion',jolt)]:
    if not tongue.get_editor_property(name): tongue.set_editor_property(name,value)
presets=list(tongue.get_editor_property('dev_motions'))
for value in [local,travel]:
    if value not in presets: presets.append(value)
tongue.set_editor_property('dev_motions',presets)
assert lib.save_loaded_asset(tongue,only_if_is_dirty=False)
u.log('MC_GAZE_MOTION_ASSETS_PASS')
u.SystemLibrary.quit_editor()
