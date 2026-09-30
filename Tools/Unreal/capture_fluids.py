"""Render fluid review stages in the existing editor; no OS input or extra RHI."""
import math, time, struct, zlib, gc
from pathlib import Path
import unreal as u

lib=u.EditorAssetLibrary
actors=u.get_editor_subsystem(u.EditorActorSubsystem)
world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
out=Path(u.Paths.project_dir()).resolve()/'Artifacts/Fluids'
out.mkdir(parents=True,exist_ok=True)
assert not u.EditorLevelLibrary.get_pie_worlds(False)
for obj in gc.get_objects():
    if type(obj).__name__=='Review' and hasattr(obj,'handle'):
        u.unregister_slate_post_tick_callback(obj.handle)
for a in actors.get_all_level_actors():
    if 'MCFluidReview' in [str(t) for t in a.tags]: actors.destroy_actor(a)
spawned=[]
def spawn(cls):
    a=actors.spawn_actor_from_class(cls,u.Vector())
    a.tags=[u.Name('MCFluidReview')]; a.set_actor_label('TEMP | Fluid review'); spawned.append(a); return a
def static(mesh,mat):
    a=spawn(u.StaticMeshActor); c=a.static_mesh_component
    c.set_static_mesh(lib.load_asset(mesh)); c.set_collision_enabled(u.CollisionEnabled.NO_COLLISION)
    mid=c.create_dynamic_material_instance(0,lib.load_asset(mat)); c.set_material(0,mid)
    c.set_cast_shadow(False)
    return a,c,mid
def scalar(mid,name,v): mid.set_scalar_parameter_value(name,v)
def vector(mid,name,v): mid.set_vector_parameter_value(name,u.LinearColor(*v))
def place(a,p,s=(1,1,1)):
    a.set_actor_location(u.Vector(*p),False,True); a.set_actor_scale3d(u.Vector(*s))
def smooth(x):
    x=max(0,min(1,x)); return x*x*(3-2*x)

water,wc,wm=static('/Game/Art/Meshes/SM_CoffeeSurface','/Game/Gameplay/Liquid/Stylized/MI_StylizedCoffee')
jet,jc,jm=static('/Game/Art/Meshes/SM_CoffeeJet','/Game/Art/Materials/MI_CoffeePour')
crown,cc,cm=static('/Game/Art/Meshes/SM_CoffeeCrown','/Game/Art/Materials/MI_CoffeePour')
drain,dc,dm=static('/Game/Art/Meshes/SM_CoffeeDrain','/Game/Art/Materials/MI_CoffeePour')
scalar(cm,'IsCrown',1); scalar(dm,'IsDrain',1)
jc.set_translucent_sort_priority(1); cc.set_translucent_sort_priority(2); dc.set_translucent_sort_priority(1)
tongue=next(a for a in actors.get_all_level_actors() if isinstance(a,u.MCTongue))
def floor(x,y):
    result=tongue.get_editor_property('surface').line_trace_component(u.Vector(x,y,1000),u.Vector(x,y,-1000),True,False,False)
    assert result is not None,(x,y)
    return result[0],result[1]
inlet_floor,_=floor(420,-100)
camera=spawn(u.CameraActor)
camera.camera_component.set_field_of_view(64)
capture=spawn(u.SceneCapture2D)
capture_component=capture.capture_component2d
target=u.RenderingLibrary.create_render_target2d(world,1600,900,u.TextureRenderTargetFormat.RTF_RGBA8,u.LinearColor(0,0,0,1),False,False)
capture_component.texture_target=target
capture_component.set_editor_property('capture_source',u.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
capture_component.set_editor_property('capture_every_frame',False)
capture_component.set_editor_property('capture_on_movement',False)
capture_component.set_editor_property('always_persist_rendering_state',True)
def look(eye,aim):
    camera.set_actor_location_and_rotation(u.Vector(*eye),u.MathLibrary.find_look_at_rotation(u.Vector(*eye),u.Vector(*aim)),False,True)
look((-1450,-40,560),(600,-20,20))

# Preview the actual wipeable material on a grid bound to the current tongue.
patch=spawn(u.MCMouthSurface)
patch.get_editor_property('visual').set_visibility(False)
patch.get_editor_property('label').set_visibility(False)
patch.get_editor_property('area').set_collision_enabled(u.CollisionEnabled.NO_COLLISION)
liquid=patch.get_editor_property('liquid')
mid=liquid.create_dynamic_material_instance(0,lib.load_asset('/Game/Gameplay/Hazards/MI_VomitPuddle'))
liquid.set_material(0,mid)
size=120
center,_=floor(650,-100)
place(patch,(center.x,center.y,center.z))
vertices=[]; normals=[]; uv=[]; tangents=[]; triangles=[]
steps=24
for y in range(steps+1):
    for x in range(steps+1):
        fx=(x/steps*2-1)*size; fy=(y/steps*2-1)*size
        p,n=floor(center.x+fx,center.y+fy)
        vertices.append(u.Vector(p.x-center.x+n.x*.45,p.y-center.y+n.y*.45,p.z-center.z+n.z*.45))
        normals.append(n); uv.append(u.Vector2D(x/steps,y/steps)); tangents.append(u.ProcMeshTangent(u.Vector(1,0,0),False))
        if x<steps and y<steps:
            i=y*(steps+1)+x; triangles.extend([i,i+steps+1,i+1,i+1,i+steps+1,i+steps+2])
liquid.create_mesh_section_linear_color(0,vertices,triangles,normals,uv,[],[],[],[],tangents,False,False)
scalar(mid,'Seed',14); scalar(mid,'WorldSize',size*2); scalar(mid,'BrushAge',3)
patch.set_actor_hidden_in_game(True)

# A single brush-like diagonal stroke exercises all shader layers with the same
# persistent mask used by the runtime. This is a preview asset, deleted below.
pixels=bytearray()
for y in range(64):
    pixels.append(0)
    for x in range(64):
        px=x/63-.23; py=y/63-.66
        along=max(0,min(1,(px*.48-py*.31)/(.48*.48+.31*.31)))
        distance=math.hypot(px-along*.48,py+along*.31)
        value=round(255*max(0,min(1,(distance-.10)/.025)))
        pixels.extend((value,value,value,255))
def png_chunk(kind,data): return struct.pack('!I',len(data))+kind+data+struct.pack('!I',zlib.crc32(kind+data)&0xffffffff)
mask_file=out/'BrushMask.png'
mask_file.write_bytes(b'\x89PNG\r\n\x1a\n'+png_chunk(b'IHDR',struct.pack('!2I5B',64,64,8,6,0,0,0))+png_chunk(b'IDAT',zlib.compress(pixels))+png_chunk(b'IEND',b''))
mask_path='/Game/Developers/Codex/T_FluidReviewMask'
assert not lib.does_asset_exist(mask_path),'Preview mask must not replace an existing asset'
import_task=u.AssetImportTask(); import_task.filename=str(mask_file); import_task.destination_path='/Game/Developers/Codex'; import_task.destination_name='T_FluidReviewMask'; import_task.automated=True; import_task.save=False
u.AssetToolsHelpers.get_asset_tools().import_asset_tasks([import_task])
wipe=lib.load_asset(mask_path); wipe.set_editor_property('srgb',False); wipe.set_editor_property('compression_settings',u.TextureCompressionSettings.TC_DEFAULT)
wipe.set_editor_property('address_x',u.TextureAddress.TA_CLAMP); wipe.set_editor_property('address_y',u.TextureAddress.TA_CLAMP)
wipe.set_editor_property('filter',u.TextureFilter.TF_BILINEAR)

def phase(t):
    fill=smooth(t/4) if t<4 else 1-smooth((t-4)/2)
    drain_amount=smooth((t-4)/.25) if t>=4 else 0
    jet_amount=smooth(t/.2)*(1-smooth((t-3.8)/.2)) if t<4 else 0
    level=-200+355*fill
    place(water,(100,-25,level),(27,17,1))
    for k,v in dict(WaterTime=t,RippleHeight=6,RippleLength=260,RippleSpeed=1.4,FillAmount=fill,DrainAmount=drain_amount,JetAmount=jet_amount,Filling=1 if t<4 else 0,FrontRadius=max(0,t-.25)*950,FrontWidth=110,FrontHeight=26,DrainRadius=320,DrainDepth=65).items(): scalar(wm,k,v)
    for k,v in dict(Inlet=(420,-100,0,0),Outlet=(1300,-25,0,0),ArenaSize=(1350,850,0,0),ArenaCenter=(100,-25,0,0)).items(): vector(wm,k,v)
    impact=max(inlet_floor.z,level+6)+2
    place(jet,(420,-100,impact),(1.7,1.7,max(1,1100-impact)/100))
    place(crown,(420,-100,impact),(85/65,85/65,.7+.3*jet_amount))
    place(drain,(1300,-25,level-65*drain_amount+4),(3.5,3.2,2.5))
    drain.set_actor_rotation(u.Rotator(0,math.degrees(math.atan2(75,880)),0),True)
    for m in (jm,cm,dm): scalar(m,'WaterTime',t)
    scalar(jm,'Strength',jet_amount); scalar(cm,'Strength',jet_amount); scalar(dm,'Strength',drain_amount*min(1,fill*5))
    jet.set_actor_hidden_in_game(jet_amount<.001); crown.set_actor_hidden_in_game(jet_amount<.001); drain.set_actor_hidden_in_game(drain_amount<.001)

stages=[('CoffeeFill',1.7),('CoffeeFull',3.7),('CoffeeDrain',4.65),('WaterFull',3.7),('WaterShallow',2.5),('Vomit',None),('VomitWiped',None),('VomitCleared',None)]
class Review:
    def __init__(self): self.i=0; self.task=None; self.at=time.monotonic(); self.ready=False; self.warm=0
    def tick(self,dt):
        global wm,jm,cm,dm
        try:
            if self.i==len(stages):
                u.unregister_slate_post_tick_callback(self.handle)
                capture_component.texture_target=None
                u.RenderingLibrary.release_render_target2d(target)
                for a in spawned: actors.destroy_actor(a)
                lib.delete_asset(mask_path)
                (out/'Capture.txt').write_text('MC_FLUID_REVIEW_PASS '+','.join(name for name,_ in stages),encoding='utf-8')
                u.log('MC_FLUID_REVIEW_PASS'); return
            name,t=stages[self.i]
            if not self.ready:
                if t is not None:
                    if name=='WaterFull':
                        wm=wc.create_dynamic_material_instance(0,lib.load_asset('/Game/Gameplay/Liquid/Stylized/MI_ClearWater'))
                        jm=jc.create_dynamic_material_instance(0,lib.load_asset('/Game/Gameplay/Liquid/Stylized/MI_ClearWaterPour'))
                        cm=cc.create_dynamic_material_instance(0,lib.load_asset('/Game/Gameplay/Liquid/Stylized/MI_ClearWaterPour'))
                        dm=dc.create_dynamic_material_instance(0,lib.load_asset('/Game/Gameplay/Liquid/Stylized/MI_ClearWaterPour'))
                        scalar(cm,'IsCrown',1); scalar(dm,'IsDrain',1)
                    if name=='WaterShallow':
                        look((-600,-480,450),(360,-60,0)); camera.camera_component.set_field_of_view(62)
                    phase(t)
                else:
                    for a in (water,jet,crown,drain): a.set_actor_hidden_in_game(True)
                    patch.set_actor_hidden_in_game(False)
                    look((center.x-220,center.y-270,center.z+250),(center.x,center.y,center.z+5))
                    camera.camera_component.set_field_of_view(52)
                    if name=='VomitWiped': mid.set_texture_parameter_value('WipeMask',wipe)
                    if name=='VomitCleared': scalar(mid,'Finish',1)
                capture.set_actor_transform(camera.get_actor_transform(),False,True)
                capture_component.set_editor_property('fov_angle',camera.camera_component.field_of_view)
                self.ready=True; self.at=time.monotonic(); self.warm=0
            if self.warm<10:
                capture_component.capture_scene(); self.warm+=1
            if time.monotonic()-self.at<5: return
            capture_component.capture_scene()
            u.RenderingLibrary.export_render_target(world,target,str(out),name+'.png')
            assert (out/(name+'.png')).exists()
            self.i+=1; self.ready=False; self.at=time.monotonic()
        except Exception:
            u.unregister_slate_post_tick_callback(self.handle)
            for a in spawned:
                if u.is_valid(a): actors.destroy_actor(a)
            lib.delete_asset(mask_path)
            raise
fluid_review=Review()
fluid_review.handle=u.register_slate_post_tick_callback(fluid_review.tick)
u.log('MC_FLUID_REVIEW_STARTED')
