"""Read real station results from every active PIE world; never drives gameplay."""
import unreal as u,json
from pathlib import Path

def main():
    worlds=u.EditorLevelLibrary.get_pie_worlds(False)
    assert worlds,'Start L_VFXLab PIE first'
    reports=[]
    for world in worlds:
        catalogues=u.GameplayStatics.get_all_actors_of_class(world,u.load_class(None,'/Script/MessControl.MCVFXLab'))
        if not catalogues:continue
        catalogue=catalogues[0]
        rows=[]
        values=json.loads(u.ToolsetLibrary.get_object_properties(catalogue,['results','bRunning']))
        for row in values['results']:
            rows.append(dict(name=row['name'],status=row['status'],passed=row['passed'],failed=row['failed'],cycles=row['cycles'],running=row['bRunning']))
        heroes=u.GameplayStatics.get_all_actors_of_class(world,u.MCToothCharacter)
        modes=u.GameplayStatics.get_all_actors_of_class(world,u.GameModeBase)
        reports.append(dict(world=world.get_path_name(),authority=bool(modes),
                            game_seconds=u.GameplayStatics.get_time_seconds(world),running=values['bRunning'],scenario_count=len(rows),passed=sum(r['passed']>0 for r in rows),
                            failures=sum(r['failed'] for r in rows),bots=len(heroes),rows=rows))
    assert reports,'Active PIE has no VFX lab catalogue'
    out=Path(u.Paths.project_saved_dir())/'Codex'/'vfx_lab_report_latest.json'
    out.write_text(json.dumps(dict(worlds=reports),indent=2),encoding='utf-8')
    concise=[dict(world=r['world'],authority=r['authority'],game_seconds=r['game_seconds'],scenario_count=r['scenario_count'],passed=r['passed'],failures=r['failures'],bots=r['bots'],
                  incomplete=[x for x in r['rows'] if x['passed']==0 or x['failed']>0]) for r in reports]
    print('VFX_LAB_REPORT '+json.dumps(concise))

main()
