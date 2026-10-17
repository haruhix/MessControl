"""Short real listen-server/client probe for Tank Roll or shield destruction.

Uses the saved lab fixtures and existing native damage path. No asset saves,
RPC shortcuts, broad suites, or fabricated poses. Restores editor settings.
Default case ShieldBreak; pass MC_TANK_V4_PROBE_CASE='Roll' through runpy.
"""
import builtins
import importlib.util
import math
import traceback
from pathlib import Path
import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve()
loader = importlib.util.spec_from_file_location('mc_tank_v4_network_base', ROOT/'Tools/Unreal/validate_nut_presentation_smoothing.py')
base = importlib.util.module_from_spec(loader)
loader.loader.exec_module(base)
CASE = globals().get('MC_TANK_V4_PROBE_CASE', 'ShieldBreak')
KEY = 'mc_tank_v4_network_probe'


def delta_quat(current, previous):
    x,y,z,w = current
    a,b,c,d = (-previous[0],-previous[1],-previous[2],previous[3])
    delta = [w*a+x*d+y*c-z*b,w*b-x*c+y*d+z*a,w*c+x*b-y*a+z*d,w*d-x*a-y*b-z*c]
    return [-v for v in delta] if delta[3]<0 else delta


class TankV4Probe(base.NutSmoothingProbe):
    def __init__(self):
        super().__init__()
        self.index = 3 if CASE == 'Roll' else 6
        self.path = ROOT/'Saved/Checks'/('TankV4Network'+CASE+'.json')
        self.report.update(station_index=self.index, scenario=CASE,
            scope='Real network Tank roll direction or shield-free mesh and follow-up frontal damage')
        self.rotation_history = {}

    def setup(self, worlds):
        servers = [w for w in worlds if u.SystemLibrary.is_server(w)]
        clients = [w for w in worlds if not u.SystemLibrary.is_server(w)]
        if len(servers)!=1 or len(clients)!=1:
            return False
        server,client = servers[0],clients[0]
        controllers = list(u.GameplayStatics.get_all_actors_of_class(server,u.MCVFXLabPlayerController))
        pc = u.GameplayStatics.get_player_controller(client,0)
        if len(controllers)!=2 or not all(c.get_controlled_pawn() for c in controllers) or not pc or not pc.get_controlled_pawn():
            return False
        labs = u.GameplayStatics.get_all_actors_of_class(server,u.MCVFXLab)
        if not labs or len(base.prop(labs[0],'results'))<=self.index:
            return False
        self.server,self.client,self.lab = server,client,labs[0]
        self.station = base.prop(base.prop(self.lab,'results')[self.index],'station')
        for controller in controllers:
            self.require(controller.has_authority(),'Need server-owned fixture controllers')
            controller.call_method('ServerSelectStation',args=(self.index,))
        self.lab.call_method('ResetStation',args=(self.index,))
        if CASE == 'ShieldBreak':
            # Keep the same fixture until this owned PIE ends, so final health
            # refers to the broken boss instead of the next automatic respawn.
            self.station.set_editor_property('cycle_seconds',120.)
        self.capture_start = float(u.GameplayStatics.get_time_seconds(server))
        self.report['worlds'] = dict(server=server.get_path_name(),client=client.get_path_name())
        return True

    def sample(self,boss):
        result = super().sample(boss)
        model = base.prop(boss,'tank_model')
        mesh = model.get_editor_property('skeletal_mesh_asset')
        result.update(health=float(base.prop(boss,'health')),shield_health=float(base.prop(boss,'shield_health')),
            shield_broken_at=float(base.prop(boss,'shield_broken_at')),mesh=mesh.get_path_name() if mesh else None,
            shield_front_scale=float(base.prop(base.prop(boss,'boss_settings'),'shield_front_damage_scale')),
            model_scale=base.xyz(base.prop(model,'relative_scale3d')),
            model_anchor=base.xyz(base.prop(model,'relative_location')))
        ball = base.prop(boss,'tank_ball')
        quat = ball.get_world_rotation().quaternion()
        rotation = [float(quat.x),float(quat.y),float(quat.z),float(quat.w)]
        key = (result['peer'],result['boss'])
        previous = self.rotation_history.get(key)
        if previous and 'EXECUTING' in result['state'].upper() and ball.is_visible():
            movement = [result['visual'][i]-previous['position'][i] for i in range(2)]
            length = math.hypot(*movement)
            delta = delta_quat(rotation,previous['rotation'])
            spin = math.sqrt(sum(v*v for v in delta[:3]))
            if length>.05 and length<100 and spin>1e-6:
                expected_axis = [-movement[1]/length,movement[0]/length,0.]
                result['roll_direction_dot'] = sum(delta[i]*expected_axis[i] for i in range(3))/spin
        self.rotation_history[key] = dict(position=result['visual'],rotation=rotation)
        return result

    def validate(self):
        frames = [p for s in self.report['samples'] for p in s['peers']]
        clients = [p for p in frames if p['peer']=='client']
        self.require(len(clients)>=120,'Too few real client samples')
        if CASE == 'Roll':
            super().validate()
            dots = [p['roll_direction_dot'] for p in clients if 'roll_direction_dot' in p]
            positive = sum(d>.9 for d in dots)
            negative = sum(d<-.1 for d in dots)
            self.require(positive>=30 and negative<=len(dots)*.1,'Client roll does not consistently match travel direction')
            self.report['checks'].update(roll_matches_travel=True,positive_roll_frames=positive,
                opposite_roll_frames=negative,measured_roll_frames=len(dots))
        else:
            broken = [p for p in clients if p['shield_health']<=0]
            self.require(len(broken)>=30 and all('SK_NutTank_NoShield' in p['mesh'] for p in broken),
                         'Broken client shield still uses the shielded mesh')
            self.require(any(p['shield_health']==500 for p in frames),'Initial intact shield was not observed')
            self.require(all(p['shield_broken_at']>0 for p in broken),'Client break timestamp was not replicated')
            servers = [p for p in frames if p['peer']=='server']
            self.require(servers[-1]['health']==clients[-1]['health'],'Final boss health differs between peers')
            self.require(servers[-1]['shield_health']==0 and clients[-1]['shield_health']==0,
                         'Fixture respawned before final shield-loss evidence')
            expected_health=servers[0]['health']-(500*servers[0]['shield_front_scale']+100+25)
            self.require(abs(clients[-1]['health']-expected_health)<.1,'Follow-up frontal hit health delta is not full25')
            self.require(int(base.prop(self.station,'passed'))>=1 and int(base.prop(self.station,'failed'))==0,
                         'Real shield fixture damage/mesh/follow-up assertions failed')
            scale = servers[0]['model_scale']
            anchor = servers[0]['model_anchor']
            self.require(all(base.distance(p['model_scale'],scale)<.001 and base.distance(p['model_anchor'],anchor)<.001
                             for p in frames),'Shield removal changed the body scale or anchor')
            self.report['checks'] = dict(two_network_peers=True,finite_transforms=True,
                client_samples=len(clients),client_shield_free_frames=len(broken),same_body_anchor=True,
                replicated_break_timestamp=True,subsequent_frontal_hit_full_damage=True,
                station_status=str(base.prop(self.station,'status')),final_health=clients[-1]['health'])


previous = getattr(builtins,KEY,None)
if previous is not None and not getattr(previous,'done',True):
    raise RuntimeError('A V4 probe is already active')
probe = TankV4Probe()
setattr(builtins,KEY,probe)
try:
    probe.start()
except Exception:
    probe.finish(traceback.format_exc())
    raise
