"""Create only the separate, inactive practice World and study resources. Run in Editor Python."""
import hashlib,json,math,os
from pathlib import Path
import unreal as u

ROOT='/Game/Tests/VFXPractice'
MAP=ROOT+'/L_VFXPractice'
OUT=Path('E:/CodexWork/MessControl/Validation/VFXPracticeSandbox_20261030')
PROJECT=Path('E:/DEVGAME/MessControl')
TAG='MC.VFXPractice.20261030'
lib=u.EditorAssetLibrary
tools=u.AssetToolsHelpers.get_asset_tools()
actors=u.get_editor_subsystem(u.EditorActorSubsystem)

def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def native(name):
    c=u.load_class(None,'/Script/MessControl.'+name);assert c,name
    return c

def main():
    initial=json.loads((OUT/'Before.json').read_text(encoding='utf8'))
    assert os.getpid()==initial['pid'] and not u.EditorLevelLibrary.get_pie_worlds(True)
    current=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
    assert current.get_path_name()==initial['world']
    assert {p.get_path_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_map_packages()}.issubset({MAP})
    allowed=set(initial['dirty_assets'])
    assert all(p.get_path_name() in allowed or p.get_path_name().startswith(ROOT+'/') for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    if lib.does_asset_exist(MAP):
        assert lib.get_metadata_tag(lib.load_asset(MAP),'MC.VFXPracticeOwner')==TAG,'Do not overwrite a student map'
    assert lib.does_asset_exist(ROOT+'/BP_PracticeStation') and lib.does_asset_exist(ROOT+'/BP_PracticeBootstrap')
    saved=[];copies={};materials={};textures={}

    def duplicate(source,target):
        if lib.does_asset_exist(target):
            obj=lib.load_asset(target)
            assert lib.get_metadata_tag(obj,'MC.VFXPracticeOwner')==TAG,'Keep existing study asset: '+target
        else:
            obj=lib.duplicate_asset(source,target);assert obj,(source,target)
            lib.set_metadata_tag(obj,'MC.VFXPracticeOwner',TAG)
        saved.append(obj);copies[source]=target;return obj

    texture=duplicate('/Game/Gameplay/ActiveSkills/VFX/Textures/T_DentalFoamClumps',ROOT+'/Textures/T_PracticeFoamClumps')
    textures['/Game/Gameplay/ActiveSkills/VFX/Textures/T_DentalFoamClumps']=texture
    def study_texture(old):
        key=old.get_path_name().split('.',1)[0]
        if key.startswith(ROOT+'/'):
            assert lib.get_metadata_tag(old,'MC.VFXPracticeOwner')==TAG,key
            return old
        if not key.startswith('/Game/'):return old
        if key not in textures:
            textures[key]=duplicate(key,ROOT+'/Textures/'+key.rsplit('/',1)[-1]+'_Study_'+hashlib.sha1(key.encode()).hexdigest()[:6])
        return textures[key]
    def study_material(source):
        source=source.split('.',1)[0]
        if source.startswith(ROOT+'/'):
            owned=lib.load_asset(source);assert owned and lib.get_metadata_tag(owned,'MC.VFXPracticeOwner')==TAG,source
            return owned
        if source in materials:return materials[source]
        src=lib.load_asset(source);assert src,source
        suffix=hashlib.sha1(source.encode()).hexdigest()[:6]
        dest=ROOT+'/Materials/'+source.rsplit('/',1)[-1]+'_Study_'+suffix
        copy=duplicate(source,dest);materials[source]=copy
        if isinstance(copy,u.MaterialInstanceConstant):
            # Existing study instances may already have a copied parent. Always
            # derive the canonical chain from the untouched original instance.
            parent=src.get_editor_property('parent')
            if parent and parent.get_path_name().startswith('/Game/'):
                parent_copy=study_material(parent.get_path_name().split('.',1)[0])
                u.MaterialEditingLibrary.set_material_instance_parent(copy,parent_copy)
            valid_names={str(name) for name in u.MaterialEditingLibrary.get_texture_parameter_names(copy)}
            copy.set_editor_property('texture_parameter_values',[row for row in copy.get_editor_property('texture_parameter_values')
                if str(row.get_editor_property('parameter_info').get_editor_property('name')) in valid_names])
            for override in src.get_editor_property('texture_parameter_values'):
                info=override.get_editor_property('parameter_info')
                name=info.get_editor_property('name');association=info.get_editor_property('association')
                old=u.MaterialEditingLibrary.get_material_instance_texture_parameter_value(src,name,association)
                if old and str(name) in valid_names:
                    copied_texture=study_texture(old)
                    u.MaterialEditingLibrary.set_material_instance_texture_parameter_value(copy,name,copied_texture,association)
                    assert u.MaterialEditingLibrary.get_material_instance_texture_parameter_value(copy,name,association)==copied_texture
            u.MaterialEditingLibrary.update_material_instance(copy)
        elif isinstance(copy,u.Material):
            for expr in u.ObjectIterator(u.MaterialExpressionTextureSample):
                if expr.get_outer()==copy:
                    old=expr.get_editor_property('texture')
                    if old:expr.set_editor_property('texture',study_texture(old))
            u.MaterialEditingLibrary.recompile_material(copy)
        return copy

    for name in ('M_ActiveSoftPuff','M_ActivePulse','M_ActiveFoamLit','M_ActiveDentalFoamAtlas','MI_ActivePhysicalSmoke'):
        study_material('/Game/Gameplay/ActiveSkills/VFX/Materials/'+name)
    systems={}
    for name,source in [('Burst','NS_Hook_Impact'),('Trail','NS_Sponge_Trail'),('HealSeed','NS_Sponge_End'),('Impact','NS_Hook_Impact')]:
        path=ROOT+'/Effects/NS_Practice_'+name
        systems[name]=duplicate('/Game/Gameplay/ActiveSkills/VFX/'+source,path)
        systems[name].set_editor_property('effect_type',None)
    beginner_path=ROOT+'/Effects/NS_Practice_FoamBasics'
    if lib.does_asset_exist(beginner_path):
        beginner=lib.load_asset(beginner_path)
        assert lib.get_metadata_tag(beginner,'MC.VFXPracticeOwner')==TAG
        systems['Basics']=beginner

    # One simple, editable PBR parent; every swatch is map-referenced before readiness.
    surface=lib.load_asset(ROOT+'/Materials/M_PracticeSurface')
    if surface:
        assert lib.get_metadata_tag(surface,'MC.VFXPracticeOwner')==TAG
    else:
        surface=tools.create_asset('M_PracticeSurface',ROOT+'/Materials',u.Material,u.MaterialFactoryNew());assert surface
        lib.set_metadata_tag(surface,'MC.VFXPracticeOwner',TAG)
        color=u.MaterialEditingLibrary.create_material_expression(surface,u.MaterialExpressionVectorParameter,-260,-80)
        color.set_editor_property('parameter_name','Color');color.set_editor_property('default_value',u.LinearColor(.14,.18,.19,1))
        rough=u.MaterialEditingLibrary.create_material_expression(surface,u.MaterialExpressionScalarParameter,-260,80)
        rough.set_editor_property('parameter_name','Roughness');rough.set_editor_property('default_value',.78)
        assert u.MaterialEditingLibrary.connect_material_property(color,'',u.MaterialProperty.MP_BASE_COLOR)
        assert u.MaterialEditingLibrary.connect_material_property(rough,'',u.MaterialProperty.MP_ROUGHNESS)
        u.MaterialEditingLibrary.recompile_material(surface)
    saved.append(surface)
    palette={}
    for name,value in dict(Ground=(.075,.10,.115),Pad=(.17,.22,.24),Mint=(.28,.64,.51),Cream=(.72,.69,.52),Pink=(.28,.035,.075),Dark=(.028,.04,.055),Light=(.72,.74,.70)).items():
        mat=lib.load_asset(ROOT+'/Materials/MI_Practice_'+name)
        if mat:assert lib.get_metadata_tag(mat,'MC.VFXPracticeOwner')==TAG
        else:
            mat=tools.create_asset('MI_Practice_'+name,ROOT+'/Materials',u.MaterialInstanceConstant,u.MaterialInstanceConstantFactoryNew());assert mat
            lib.set_metadata_tag(mat,'MC.VFXPracticeOwner',TAG)
        u.MaterialEditingLibrary.set_material_instance_parent(mat,surface)
        u.MaterialEditingLibrary.set_material_instance_vector_parameter_value(mat,'Color',u.LinearColor(*value,1))
        palette[name]=mat;saved.append(mat)

    factory=u.WorldFactory();factory.set_editor_property('edit_after_new',False)
    world=lib.load_asset(MAP) if lib.does_asset_exist(MAP) else tools.create_asset('L_VFXPractice',ROOT,u.World,factory)
    assert world and world!=current
    lib.set_metadata_tag(world,'MC.VFXPracticeOwner',TAG)
    level=world.get_world_settings().get_outer();created=[]
    for old in list(u.GameplayStatics.get_all_actors_of_class(world,u.Actor)):
        if old.actor_has_tag(TAG):old.destroy_actor()
    def actor(cls,name,point=(0,0,0),rotation=(0,0,0),scale=(1,1,1),folder='Environment'):
        template=u.new_object(cls,outer=level,name='PracticeTemplate_'+name)
        a=actors.duplicate_actor(template,world,u.Vector());template.destroy_actor();assert a and a.get_world()==world,name
        a.set_actor_label(name);a.tags=[u.Name(TAG)];a.set_folder_path('VFX Practice/'+folder)
        a.set_actor_location(u.Vector(*point),False,False)
        a.set_actor_rotation(u.Rotator(pitch=rotation[0],yaw=rotation[1],roll=rotation[2]),False)
        a.set_actor_scale3d(u.Vector(*scale));created.append(a);return a
    cube=lib.load_asset('/Engine/BasicShapes/Cube');cylinder=lib.load_asset('/Engine/BasicShapes/Cylinder');assert cube and cylinder
    def shape(name,point,scale,material,mesh=None,rotation=(0,0,0),folder='Environment'):
        a=actor(u.StaticMeshActor,name,point,rotation,scale,folder)
        c=a.static_mesh_component;assert c.set_static_mesh(mesh or cube)
        c.set_material(0,palette[material]);c.set_collision_profile_name('BlockAll');return a
    def text(name,value,point,size=36,color=(.76,.9,.86),folder='Signs'):
        a=actor(u.TextRenderActor,name,point,rotation=(0,-45,0),folder=folder)
        c=a.get_component_by_class(u.TextRenderComponent);c.set_text(value);c.set_world_size(size)
        c.set_horizontal_alignment(u.HorizTextAligment.EHTA_CENTER)
        c.set_text_render_color(u.Color(int(color[0]*255),int(color[1]*255),int(color[2]*255),255));return a
    shape('Practice_Ground',(3500,1750,-65),(110,85,1),'Ground')
    for x,y,sx,sy in [(3500,0,70,1.4),(3500,3500,70,1.4),(0,1750,1.4,35),(7000,1750,1.4,35)]:
        shape('Path_'+str(x)+'_'+str(y),(x,y,-8),(sx,sy,.10),'Mint')
    station_class=u.load_class(None,ROOT+'/BP_PracticeStation.BP_PracticeStation_C');assert station_class
    lab=actor(native('MCVFXLab'),'Practice_Catalogue')
    lab.set_running(False);lab.set_editor_property('bProximityActivation',True)
    lab.set_actor_tick_interval(.1)
    lab.set_editor_property('ActivationDistance',750);lab.set_editor_property('ReleaseDistance',1100)
    rows=[]
    def subject(name,point,yaw=-45):
        a=actor(native('MCToothCharacter'),name,(point[0],point[1],point[2]+60),rotation=(0,yaw,0),folder='Subjects')
        a.set_editor_property('auto_possess_ai',u.AutoPossessAI.DISABLED)
        for c in a.get_components_by_class(u.TextRenderComponent):c.set_visibility(False)
        for prop in ('Brush','BrushCover','ToolGhost','PlayerNameLabel'):
            try:
                c=a.get_editor_property(prop)
                if c:c.set_visibility(False,True)
            except Exception:pass
        return a
    layout=[('01','FREE PLAY',(0,0,0),'Basics' if 'Basics' in systems else 'Burst','Mint',False),('02','GROUND TRAIL',(3500,0,0),'Trail','Cream',True),
        ('03','HEAL STUDIO',(7000,0,0),'HealSeed','Mint',False),('04','IMPACT',(7000,3500,0),'Impact','Cream',False),
        ('05','SLOPE + WALL',(3500,3500,400),'Trail','Mint',True)]
    labels={'01':'01 Свободная проба','02':'02 След на земле','03':'03 Студия лечения · образец пены','04':'04 Попадание','05':'05 Склон и стена'}
    for number,title,point,system_key,tint,trail in layout:
        x,y,z=point;folder=number+' '+title
        slope=20 if number=='05' else 0
        floor=shape('Pad_'+number,(x,y,z-12),(18,18,.24),'Pad',cylinder,rotation=(slope,0,0),folder=folder)
        s=actor(station_class,'Practice_'+number,(x,y,z+5),folder=folder)
        s.tags=[u.Name('MCVFXLabStation'),u.Name(labels[number]),u.Name(TAG)]
        s.set_editor_property('Floor',floor);s.set_editor_property('bTrail',trail);s.set_editor_property('bAutoRepeat',not trail)
        if system_key=='Basics':s.set_editor_property('bAutoRepeat',False)
        s.set_editor_property('RepeatSeconds',4.0)
        if number=='05':
            angle=math.radians(20);s.set_editor_property('TrailDirection',u.Vector(math.cos(angle),0,math.sin(angle)))
            s.set_editor_property('SurfaceNormal',u.Vector(-math.sin(angle),0,math.cos(angle)))
            shape('Wall_05',(x+650,y,z+100),(.25,11,3),'Dark',folder=folder)
        fx=next(c for c in s.get_components_by_class(u.NiagaraComponent) if c.get_name()=='PracticeFX')
        fx.set_asset(systems[system_key]);fx.set_auto_activate(False)
        # Stand-ins use the native current appearance; no interaction with the dirty player Blueprint.
        if number in ('03','04'):
            who=subject('Tooth_'+number,(x,y,z))
            fx.set_relative_location(u.Vector(0,-40 if number=='04' else 0,75 if number=='04' else 65),False,False)
        else:
            who=subject('Tooth_'+number,(x-240,y-140,0 if number=='05' else z))
        text('Title_'+number,number+'  /  '+title,(x,y+540,z+280),48,folder=folder)
        help_line='Edit PracticeFX or its STUDY Niagara.  R: reset  P: pause'
        if number=='03':help_line='FOAM SEED ONLY / place your healing NS here. No HP change.'
        text('Hint_'+number,help_line,(x,y+540,z+210),21,folder=folder)
        if trail:
            for tick in range(-3,4):shape('Meter_'+number+'_'+str(tick),(x+tick*100,y-140,z+2),(0.04,.35,.03),'Cream',folder=folder)
        rows.append(dict(id=number,name=labels[number],actor=s.get_path_name(),floor=floor.get_path_name(),subject=who.get_path_name(),system=systems[system_key].get_path_name(),trail=trail))
    floor=shape('Pad_06',(0,3500,-12),(18,18,.24),'Pad',cylinder,folder='06 CONTRAST')
    for index,(offset,swatch) in enumerate([(-600,'Dark'),(0,'Pink'),(600,'Light')]):
        x,y=offset,3500
        shape('Contrast_Back_'+swatch,(x,y+460,230),(5.8,.14,4.6),swatch,folder='06 CONTRAST')
        s=actor(station_class,'Practice_06_'+swatch,(x,y,5),folder='06 CONTRAST')
        s.tags=[u.Name('MCVFXLabStation'),u.Name('06'+chr(65+index)+' Контраст · '+swatch),u.Name(TAG)]
        s.set_editor_property('Floor',floor);s.set_editor_property('bAutoRepeat',True);s.set_editor_property('RepeatSeconds',4.0)
        fx=next(c for c in s.get_components_by_class(u.NiagaraComponent) if c.get_name()=='PracticeFX');fx.set_asset(systems['Burst']);fx.set_auto_activate(False)
        fx.set_relative_location(u.Vector(0,0,135),False,False)
        who=subject('Tooth_06_'+swatch,(x-170,y-90,0))
        text('Contrast_Title_'+swatch,swatch.upper(),(x,y+430,480),35,folder='06 CONTRAST')
        rows.append(dict(id='06'+chr(65+index),name='Контраст '+swatch,actor=s.get_path_name(),floor=floor.get_path_name(),subject=who.get_path_name(),system=systems['Burst'].get_path_name(),trail=False))
    bootstrap_class=u.load_class(None,ROOT+'/BP_PracticeBootstrap.BP_PracticeBootstrap_C');assert bootstrap_class
    bootstrap=actor(bootstrap_class,'Practice_Readiness');bootstrap.set_editor_property('Lab',lab)
    player=actor(u.PlayerStart,'Practice_Start',(900,-1000,650),rotation=(-20,130,0))
    key=actor(u.DirectionalLight,'Practice_Key',(0,0,1800),rotation=(-50,135,0))
    light=key.get_component_by_class(u.DirectionalLightComponent);light.set_editor_property('intensity',4.0);light.set_editor_property('light_source_angle',5.0)
    light.set_editor_property('atmosphere_sun_light',True)
    actor(u.SkyAtmosphere,'Practice_Atmosphere')
    sky=actor(u.SkyLight,'Practice_Fill',(0,0,1600));fill=sky.get_component_by_class(u.SkyLightComponent)
    fill.set_editor_property('source_type',u.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP);fill.set_editor_property('cubemap',lib.load_asset('/Engine/MapTemplates/Sky/SunsetAmbientCubemap'));fill.set_editor_property('intensity',1.0)
    post=actor(u.PostProcessVolume,'Practice_Exposure')
    post.set_editor_property('unbound',True)
    settings=post.get_editor_property('settings')
    # This project uses luminance brightness limits, not the extended EV100 range.
    settings.set_editor_properties(dict(override_auto_exposure_min_brightness=True,auto_exposure_min_brightness=1.,
        override_auto_exposure_max_brightness=True,auto_exposure_max_brightness=1.))
    post.set_editor_property('settings',settings)
    # Normal lab gameplay is opt-in, no day scheduler or economy flow.
    world.get_world_settings().set_editor_property('default_game_mode',native('MCVFXLabGameMode'))
    text('Practice_Welcome','VFX PRACTICE / WASH N SMASH',(-1200,-600,260),40)
    text('Practice_Controls','F3: stations   PageUp / PageDown: jump   R: reset   P: pause',(-1200,-600,200),20)
    for name in ('BP_PracticeStation','BP_PracticeBootstrap'):
        bp=lib.load_asset(ROOT+'/'+name)
        u.BlueprintEditorLibrary.compile_blueprint(bp)
        assert lib.save_loaded_asset(bp,only_if_is_dirty=False)
    for obj in saved:assert lib.save_loaded_asset(obj,only_if_is_dirty=False),obj.get_path_name()
    assert lib.save_loaded_asset(world,only_if_is_dirty=False)
    assert u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()==current
    assert all(digest(PROJECT/name)==old for name,old in initial['protected'].items())
    result=dict(map=MAP,station_count=len(rows),zones=6,rows=rows,actors=len(created),source_copies=copies,
        material_copies={k:v.get_path_name() for k,v in materials.items()},texture_copies={k:v.get_path_name() for k,v in textures.items()},
        controls='Native VFXLab spectator: F3, PageUp/PageDown, R, P, WASD+mouse',
        boundaries=['Study map, no day/director/economy changes.','Subjects are native current tooth characters, not a controlled player.','Heal station is an explicitly labelled foam seed, not a gameplay HP heal.','Production sources were not saved/compiled.'],
        dirty_after=[p.get_path_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()],protected_unchanged=True)
    (OUT/'MapAuthoring.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
    print('VFX_PRACTICE_MAP_SAVED',MAP,len(rows))

if __name__=='__main__':main()
