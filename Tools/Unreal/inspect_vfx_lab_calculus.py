"""Read only: rendered surviving calculus peaks and the actual posed tool tip."""
import json
from pathlib import Path
import unreal as u

def p(obj,name):
    try: return obj.get_editor_property(name)
    except Exception: return None

def v(point):
    return [float(point.x),float(point.y),float(point.z)]

def main():
    rows=[]
    for world in u.EditorLevelLibrary.get_pie_worlds(False):
        for station in u.GameplayStatics.get_all_actors_of_class(world,u.MCVFXLabToolStation):
            if not station.has_authority() or 'CALCULUS' not in str(p(station,'StationKind')): continue
            worker=p(station,'Worker')
            tooth=next((a for a in u.GameplayStatics.get_all_actors_of_class(world,u.MCArenaTooth) if a.get_owner()==station),None)
            if not worker or not tooth: continue
            calc=p(tooth,'Calculus'); state=p(calc,'State'); deposits=p(calc,'Deposits')
            stages=list(p(state,'Pieces') or []); alive=[i for i,stage in enumerate(stages) if stage>0]
            row=dict(case=str(p(station,'StationKind')),time=u.GameplayStatics.get_time_seconds(world),
                status=p(station,'Status'),worker=worker.get_path_name(),location=v(worker.get_actor_location()),
                velocity=v(worker.get_velocity()),rotation=str(worker.get_actor_rotation()),pieces=stages,alive=alive,
                movement_intent=str(p(worker.get_movement_component(),'MovementIntent')),peaks=[])
            row['calculus_state']=str(state)
            row['calculus_component']=str(calc)
            u.SystemLibrary.execute_console_command(world,'getall MCToothCalculusComponent State NAME='+calc.get_name()+' OUTER='+tooth.get_path_name())
            try:
                section=u.ProceduralMeshLibrary.get_section_from_procedural_mesh(deposits,0)
                vertices=section[0]; normals=section[2]
                transform=deposits.get_world_transform()
                scale=deposits.get_world_scale()
                for ordinal in range(len(vertices)//216):
                    # Each live piece emits 12 rim sectors with six triangles each.
                    normal=normals[ordinal*216+15]
                    normal=u.Vector(-normal.x/scale.x,-normal.y/scale.y,-normal.z/scale.z)
                    normal=u.MathLibrary.transform_direction(transform,normal)
                    row['peaks'].append(dict(ordinal=ordinal,point=v(u.MathLibrary.transform_location(transform,vertices[ordinal*216+12])),normal=v(normal)))
                row['vertex_count']=len(vertices)
            except Exception as exc: row['mesh_error']=str(exc)
            row['tool_components']=[]
            for tool in worker.get_components_by_class(u.StaticMeshComponent):
                if tool.get_name()!='InventoryTool': continue
                mesh=p(tool,'StaticMesh')
                info=dict(mesh=str(mesh),transform=str(tool.get_world_transform()))
                try:
                    if tool.does_socket_exist('PickaxeTip'): tip=tool.get_socket_location('PickaxeTip')
                    else:
                        box=mesh.get_bounding_box()
                        tip=u.MathLibrary.transform_location(tool.get_world_transform(),u.Vector(box.max.x,0,0))
                    info['tip']=v(tip)
                except Exception as exc: info['error']=str(exc)
                row['tool_components'].append(info)
            rows.append(row)
    out=Path(u.Paths.project_saved_dir())/'Codex'/'vfx_lab_calculus_latest.json'
    out.write_text(json.dumps(rows,indent=2),encoding='utf-8')
    print('CALCULUS_DIAGNOSTICS '+json.dumps(rows))

main()
