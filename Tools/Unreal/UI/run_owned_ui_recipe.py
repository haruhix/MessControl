"""Run one owned UI recipe in a fresh Editor-Python namespace, then release wrappers."""
import json,sys,time
from pathlib import Path
sys.dont_write_bytecode=True
sys.path.insert(0,'E:/UE/UE_5.8/Engine/Plugins/Experimental/PythonScriptPlugin/Content/Python')
import remote_execution as remote
recipe=Path(sys.argv[1]).resolve()
assert recipe.drive.upper()=='E:' and recipe.is_file()
session=remote.RemoteExecution()
try:
    session.start();deadline=time.monotonic()+45;nodes=[]
    while time.monotonic()<deadline:
        nodes=[n for n in session.remote_nodes if n.get('project_name')=='MessControl']
        if len(nodes)==1:break
        time.sleep(.2)
    assert len(nodes)==1,'Expected exactly one MessControl Editor'
    session.open_command_connection(nodes[0]['node_id'])
    code="def _run_owned_ui_recipe():\n    from pathlib import Path\n    path="+repr(str(recipe).replace('\\','/'))+"\n    scope={'__name__':'__main__','__file__':path}\n    exec(compile(Path(path).read_text(encoding='utf-8-sig'),path,'exec'),scope,scope)\n_run_owned_ui_recipe()\ndel _run_owned_ui_recipe\nimport gc\ngc.collect()"
    result=session.run_command("exec("+repr(code)+", {})",exec_mode=remote.MODE_EXEC_STATEMENT,unattended=True,raise_on_failure=True)
    print(json.dumps(result,ensure_ascii=False))
finally:session.stop()
