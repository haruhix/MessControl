"""Read-only tool station diagnostics in live PIE; all world references stay local."""
import json
from pathlib import Path
import unreal as u


def prop(obj, name):
    if obj is None:
        return None
    try:
        return obj.get_editor_property(name)
    except Exception:
        return None


def vec(value):
    return [round(float(value.x), 2), round(float(value.y), 2), round(float(value.z), 2)] if value is not None else None


def call(obj, name):
    try:
        return getattr(obj, name)()
    except Exception:
        return None


def main():
    rows=[]
    for world in u.EditorLevelLibrary.get_pie_worlds(False):
        for station in u.GameplayStatics.get_all_actors_of_class(world,u.MCVFXLabToolStation):
            if not station.has_authority():
                continue
            worker=prop(station,'Worker'); inventory=prop(worker,'Inventory'); status=prop(worker,'Status')
            physics=prop(worker,'ToothPhysics'); state=prop(status,'State'); collection=prop(worker,'FoodCollection')
            row=dict(world=world.get_path_name(),time=u.GameplayStatics.get_time_seconds(world),
                case=str(prop(station,'StationKind')), status=str(prop(station,'Status')),progress=prop(station,'Progress'),
                worker=worker.get_path_name() if worker else None)
            if worker:
                try:
                    inventory_private=json.loads(u.ToolsetLibrary.get_object_properties(inventory,['Hero','Settings']))
                    worker_private=json.loads(u.ToolsetLibrary.get_object_properties(worker,['bPrimaryHeld','SwingStartedAt','NextSwingTime']))
                    fixture_private=json.loads(u.ToolsetLibrary.get_object_properties(station,['Fixtures']))
                except Exception as exc:
                    inventory_private=worker_private=fixture_private={'error':str(exc)}
                row.update(location=vec(worker.get_actor_location()),velocity=vec(worker.get_velocity()),rotation=str(worker.get_actor_rotation()),
                    tool=str(prop(inventory,'Selected')),profile=str(prop(inventory,'Profile')), primary=prop(worker,'bPrimaryHeld'),
                    brushing=prop(worker,'bBrushing'),handling=prop(worker,'bHandling'),self_care=prop(worker,'bSelfCare'),
                    collecting=prop(collection,'bCollecting'),in_coffee=prop(worker,'bInCoffee'),frozen=prop(worker,'IceLegHealth'),
                    held_food=str(prop(worker,'HeldFood')),body=str(call(physics,'get_body_state')),can_act=call(physics,'can_act'),
                    health=prop(state,'Health'),coffee=prop(state,'CoffeeLeft'),swing_started=prop(worker,'SwingStartedAt'),
                    animation_attack=prop(worker,'AnimationAttack'),spray_aim=vec(call(inventory,'spray_aim')),
                    healing_target=str(prop(inventory,'HealingTarget')),fire_target=str(prop(inventory,'FireTarget')),
                    pressure=prop(inventory,'bPressureMode'),ammo=prop(inventory,'WaterAmmo'),last_water_shot=prop(inventory,'LastWaterShotAt'))
                row['inventory_owner']=inventory.get_owner().get_path_name() if inventory and inventory.get_owner() else None
                row['inventory_path']=inventory.get_path_name() if inventory else None
                row['inventory_components']=[c.get_path_name() for c in worker.get_components_by_class(u.MCInventoryComponent)]
                row['inventory_private']=inventory_private;row['worker_private']=worker_private;row['fixture_private']=fixture_private
                row['component_refs']={}
                for name in ('Status','BrushContact','Expression','ToothPhysics','FoodCollection','Gaze','Grip'):
                    component=prop(worker,name)
                    row['component_refs'][name]=dict(path=component.get_path_name() if component else None,
                        owner=component.get_owner().get_path_name() if component and component.get_owner() else None)
                contact=prop(worker,'BrushContact'); controller=worker.get_controller()
                row['brush_contact']={name:str(prop(contact,name)) for name in
                    ('Target','LocalPoint','LocalFacingPoint','ContactAt','ApproachStartedAt','SurfaceReach','MaxHandTravel','MaxHandVerticalTravel')}
                row['bot']={name:str(prop(controller,name)) for name in
                    ('Hero','Target','WorkStand','WorkAim','bHasWorkApproach','LastWorkProgressAt','PathFailures','bLabControlled')}
            row['fixtures']=[]
            # Private station references are not exposed by Python. Query their
            # actual actor ownership instead of treating an unreadable property as empty.
            owned=[a for a in u.GameplayStatics.get_all_actors_of_class(world,u.Actor) if a.get_owner()==station]
            for fixture in owned:
                info=dict(name=fixture.get_name(),cls=fixture.get_class().get_name(),position=vec(fixture.get_actor_location()))
                for name in ('Health','bBroken','Healing','Heat','Phase','bActive'):
                    value=prop(fixture,name)
                    if value is not None: info[name]=str(value)
                visual=prop(fixture,'Visual') or prop(fixture,'Body')
                if visual is not None:
                    info['bounds']=str(call(visual,'get_component_bounds'))
                fixture_state=prop(prop(fixture,'Status'),'State')
                if fixture_state is not None:
                    info['care']={name:prop(fixture_state,name) for name in ('Health','CoffeeLeft','RepairLeft')}
                row['fixtures'].append(info)
            rows.append(row)
    out=Path(u.Paths.project_saved_dir())/'Codex'/'vfx_lab_tools_diagnostics_latest.json'
    out.write_text(json.dumps(rows,indent=2),encoding='utf-8')
    print('TOOL_LAB_DIAGNOSTICS '+json.dumps([dict(case=r['case'],time=r['time'],tool=r.get('tool'),
        inventory_owner=r.get('inventory_owner'),component_refs=r.get('component_refs')) for r in rows[:2]]))


main()
