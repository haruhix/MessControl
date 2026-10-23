"""Short local wardrobe purchase and attachment proof, with temporary pose stress fixtures.

Travels inside solo PIE, leaving the editor map untouched. The real pawn is
placed inside the real wardrobe interaction volume for UI checks. Test purchases
and settings are rolled back by finish_head_cosmetics_check.py after End PIE.
"""
import unreal as u,json,builtins,time,traceback,math,shutil
from pathlib import Path
ROOT=Path(u.Paths.project_dir()).resolve();OUT=ROOT/'Saved/HeadCosmetics20261024';ART=ROOT/'Artifacts/HeadCosmetics20261024';ART.mkdir(parents=True,exist_ok=True)
integration=json.loads((OUT/'Integration.json').read_text(encoding='utf8'));ITEMS=integration['items'];ls=u.get_editor_subsystem(u.LevelEditorSubsystem);assert not ls.is_in_play_in_editor()
UI_ONLY=bool(globals().get('HEAD_COSMETICS_UI_ONLY',False))
settings=u.load_object(None,'/Script/UnrealEd.Default__LevelEditorPlaySettings');perf=u.get_default_object(u.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'));solo={'playNetMode':'PIE_Standalone','playNumberOfClients':1,'runUnderOneProcess':True,'bLaunchSeparateServer':False}
profiles=ROOT/'Saved/HubProfiles/PIE';files=list(profiles.glob('*')) if profiles.exists() else [];saved={str(p.relative_to(ROOT)):str((OUT/'BeforeProfiles'/p.name).resolve()) for p in files if p.is_file()}
if not (OUT/'TestSettingsBefore.json').exists():
    for p in files:
        if p.is_file():dest=OUT/'BeforeProfiles'/p.name;dest.parent.mkdir(exist_ok=True);shutil.copy2(p,dest)
    (OUT/'TestSettingsBefore.json').write_text(json.dumps({'play':json.loads(u.ToolsetLibrary.get_object_properties(settings,list(solo))),'throttle':perf.get_editor_property('bThrottleCPUWhenNotForeground'),'profiles':saved},indent=2))
assert u.ToolsetLibrary.set_object_properties(settings,json.dumps(solo));perf.set_editor_property('bThrottleCPUWhenNotForeground',False)
def owned_by(obj,owner):
    for _ in range(16):
        if not obj:return False
        if obj==owner:return True
        obj=obj.get_outer()
    return False
def control(tile,name,cls):return next(x for x in u.ObjectIterator(cls) if x.get_name()==name and owned_by(x,tile))
def screenshot(world,name,ui=False):
    p=ART/(name+'.png')
    if ui:u.SystemLibrary.execute_console_command(world,'Shot SHOWUI filename="'+str(p).replace('\\','/')+'" -nosuffix')
    else:u.AutomationLibrary.take_high_res_screenshot(1600,900,str(p))
def cpd_trace(world,part,body,label):
    f=body.get_world_transform();s=body.get_material(0).get_scalar_parameter_value('BodyStretch');expected=[s,*f.translation.to_tuple(),0.,*f.rotation.to_tuple(),*f.scale3d.to_tuple(),0.]
    row={'label':label,'part':part.get_path_name(),'expected':list(expected)};u.log('HEAD_CPD_SAMPLE '+json.dumps(row));u.SystemLibrary.execute_console_command(world,'getall StaticMeshComponent CustomPrimitiveDataInternal OUTER='+part.get_outer().get_path_name()+' NAME='+part.get_name());return row
class Check:
    def __init__(self):self.start=time.monotonic();self.phase='travel';self.sent=False;self.step_at=0.;self.index=0;self.pose=0;self.records=[];self.cpd=[];self.purchases=[];self.inside=False;self.callback=u.register_slate_post_tick_callback(self.tick)
    def finish(self,error=None):
        u.unregister_slate_post_tick_callback(self.callback)
        r={'status':'failed' if error else 'passed','error':error,'phase':self.phase,'elapsed':time.monotonic()-self.start,'purchases':self.purchases,'poses':self.records,'cpd_samples':self.cpd,'network_test':False,'fixtures':['Actual local pawn temporarily placed inside the real wardrobe interaction volume; movement disabled during checks.','Actual tile buy/selection delegates and authority equip RPC; no ownership grant shortcut.','Fitting display head is rotated and scaled through its real poseable component, with BodyStretch set to both game extrema.'],'editor_map_unchanged':True}
        if UI_ONLY:r['fixtures']=['Actual pawn temporarily positioned in the real interaction volume.','Read live card labels/thumbnail regions and selected an unowned item; no purchases or pose stress in this UI-only check.']
        (OUT/('UILayoutValidation.json' if UI_ONLY else 'Validation.json')).write_text(json.dumps(r,ensure_ascii=False,indent=2),encoding='utf8');u.log('HEAD_COSMETICS_SOLO '+r['status']);ls.editor_request_end_play()
    def tile(self,ident):
        found=[x for x in u.ObjectIterator(u.MCCosmeticItemWidget) if x.get_owning_player()==self.pc and str(x.get_item_id())==ident and owned_by(x,self.widget)]
        assert len(found)==1,(ident,len(found));return found[0]
    def tick(self,dt):
        # Native screenshot capture pumps Slate. Never advance this state machine reentrantly.
        if self.inside:return
        self.inside=True
        try:self.run_tick(dt)
        finally:self.inside=False
    def run_tick(self,dt):
        try:
            if time.monotonic()-self.start>150:raise RuntimeError('Head cosmetic solo validation timeout: '+self.phase)
            worlds=u.EditorLevelLibrary.get_pie_worlds(False)
            if not worlds:return
            if self.phase=='travel':
                world=worlds[0]
                if u.GameplayStatics.get_current_level_name(world,True)!='Hub':
                    if not self.sent:u.GameplayStatics.open_level(world,'/Game/Maps/Hub');self.sent=True
                    return
                self.world=world;self.phase='ready';return
            now=u.GameplayStatics.get_time_seconds(self.world)
            if self.phase=='ready':
                pre=next(x for x in u.ObjectIterator(u.MCWorldPreloadSubsystem) if x.get_world()==self.world);assert not pre.has_failed()
                if not pre.is_ready():return
                self.pc=u.GameplayStatics.get_player_controller(self.world,0);self.hero=u.GameplayStatics.get_player_pawn(self.world,0)
                if not isinstance(self.hero,u.MCToothCharacter):return
                self.economy=self.pc.get_editor_property('PlayerState').get_editor_property('Economy');self.cosmetics=self.hero.get_editor_property('Cosmetics');self.coins=self.economy.get_editor_property('Coins')
                self.station=next(a for a in u.GameplayStatics.get_all_actors_of_class(self.world,u.MCHubWardrobeStation) if a.get_editor_property('bEnabled'))
                self.hero.character_movement.disable_movement();self.hero.set_actor_location(self.station.interaction_volume.get_world_location(),False,True);assert self.station.contains_pawn(self.hero)
                self.phase='open';self.step_at=now+.5;return
            if now<self.step_at:return
            if self.phase=='open':
                if not self.pc.try_open_wardrobe():return
                self.widget=next(x for x in u.ObjectIterator(u.MCHubWardrobeWidget) if x.get_owning_player()==self.pc and x.is_in_viewport());self.preview=next(x for x in u.GameplayStatics.get_all_actors_of_class(self.world,u.MCWardrobePreviewActor) if x.get_owner()==self.pc)
                for item in ITEMS:
                    tile=self.tile(item['id']);price=str(control(tile,'CosmeticPrice',u.TextBlock).get_text());assert price in ['Бесплатно','Free'],price
                    if UI_ONLY:assert u.Name('T_'+item['id']) in tile.get_editor_property('ThumbnailUVRegions')
                if UI_ONLY:assert str(control(self.tile('Face_Head_Brows'),'CosmeticName',u.TextBlock).get_text()) in ['Ресницы','Eyelashes']
                control(self.tile(ITEMS[0]['id']),'CosmeticButton',u.Button).on_clicked.broadcast();screenshot(self.world,'Wardrobe_FreeProducts',True)
                self.phase='ui_finish' if UI_ONLY else 'purchase';self.step_at=now+.5;return
            if self.phase=='ui_finish':self.phase='done';self.finish();return
            if self.phase=='purchase':
                if self.index<len(ITEMS):
                    item=ITEMS[self.index];tile=self.tile(item['id'])
                    if not self.economy.owns_cosmetic(item['id']):
                        button=control(tile,'CosmeticBuy',u.Button);assert button.get_is_enabled();button.on_clicked.broadcast()
                    assert self.economy.owns_cosmetic(item['id']),item['id'];assert self.economy.get_editor_property('Coins')==self.coins
                    self.purchases.append({'id':item['id'],'coins_before':self.coins,'coins_after':self.economy.get_editor_property('Coins'),'owned':True});self.index+=1;self.step_at=now+.12;return
                self.index=0;self.phase='item';self.original_head=self.preview.body.get_bone_transform_by_name('head_x',u.BoneSpaces.COMPONENT_SPACE);self.original_actor=self.preview.get_actor_rotation();self.preview.body.set_component_tick_enabled(True)
                # Hold native locomotion material writes so both real-player and display
                # components can be stressed deterministically at the game's exact extrema.
                self.hero.set_actor_tick_enabled(False);self.player_rotation=self.hero.get_actor_rotation();return
            if self.phase=='item':
                item=ITEMS[self.index];loadout=u.MCCosmeticCatalog.with_equipped_item(u.MCCharacterCosmeticLoadout(),item['slot'],item['id']);self.pc.call_method('ServerEquipHubCosmetics',args=(loadout,));assert str(u.MCCosmeticCatalog.get_equipped_item(self.economy.get_editor_property('EquippedLoadout'),item['slot']))==item['id']
                self.widget.configure_cosmetics(self.economy);tile=self.tile(item['id']);control(tile,'CosmeticButton',u.Button).on_clicked.broadcast();self.pose=0;self.phase='pose';self.step_at=now+.25;return
            if self.phase=='pose':
                # Identity attachment is important: bone nonuniform scale is applied in its own axes.
                spec=[(0.,u.Rotator(),u.Vector(1,1,1)),(.4,u.Rotator(pitch=18,yaw=38,roll=9),u.Vector(1.2,.8,1.35)),(-.35,u.Rotator(pitch=-12,yaw=-42,roll=-10),u.Vector(.85,1.15,.75))][self.pose]
                head=u.MathLibrary.compose_transforms(self.original_head,u.Transform(rotation=spec[1]));head.translation=self.original_head.translation;head.scale3d=self.original_head.scale3d*spec[2];self.preview.body.set_bone_transform_by_name('head_x',head,u.BoneSpaces.COMPONENT_SPACE)
                self.hero.set_actor_rotation(u.Rotator(yaw=self.player_rotation.yaw+spec[1].yaw),False)
                for body in [self.preview.body,self.hero.mesh]:
                    for m in body.get_materials():
                        if isinstance(m,u.MaterialInstanceDynamic):m.set_scalar_parameter_value('BodyStretch',spec[0])
                self.phase='measure';self.step_at=now+.35;return
            if self.phase=='measure':
                item=ITEMS[self.index];parts=[p for p in self.preview.get_components_by_class(u.StaticMeshComponent) if p.is_visible() and p.static_mesh and p.static_mesh.get_path_name()==item['mesh_path']];assert len(parts)==1,(item['id'],len(parts));part=parts[0]
                assert part.get_attach_parent()==self.preview.body and str(part.get_attach_socket_name())=='head_x'
                bone=self.preview.body.get_socket_transform('head_x',u.RelativeTransformSpace.RTS_WORLD);local=part.get_relative_transform();assert local.translation.length()<.0001
                points=[u.Vector(*x) for x in item['bounds_unreal_cm']]+[u.Vector()];error=max((part.get_world_transform().transform_location(p)-bone.transform_location(p)).length() for p in points);assert error<.001,error
                native=self.cosmetics.get_accessory_parts();assert len(native)==1 and native[0].static_mesh.get_path_name()==item['mesh_path'];assert native[0].get_attach_parent()==self.hero.mesh
                self.cpd.append(cpd_trace(self.world,part,self.preview.body,item['id']+':preview:'+str(self.pose)));self.cpd.append(cpd_trace(self.world,native[0],self.hero.mesh,item['id']+':player:'+str(self.pose)))
                self.records.append({'id':item['id'],'pose':self.pose,'max_vertex_attachment_error_cm':error,'head_scale':list(bone.scale3d.to_tuple()),'head_rotation':list(bone.rotation.to_tuple()),'body_stretch':self.preview.body.get_material(0).get_scalar_parameter_value('BodyStretch'),'native_part':native[0].get_path_name()})
                if self.pose==0:screenshot(self.world,item['slug']+'_Fit')
                if item['slug']=='FootballHelmet' and self.pose>0:screenshot(self.world,'Helmet_Stress_'+str(self.pose))
                self.pose+=1
                if self.pose<3:self.phase='pose';self.step_at=now+.15;return
                self.index+=1
                if self.index<len(ITEMS):self.phase='item';self.step_at=now+.2;return
                self.pc.call_method('CancelWardrobe');assert not self.pc.is_wardrobe_open();self.hero.set_actor_tick_enabled(True)
                assert {(x['id'],x['pose']) for x in self.records}=={(x['id'],p) for x in ITEMS for p in range(3)}
                self.phase='done';self.finish()
        except Exception:self.finish(traceback.format_exc())
builtins.mc_head_cosmetics_solo=Check();ls.editor_request_begin_play();print('HEAD_COSMETICS_SOLO_STARTED')
