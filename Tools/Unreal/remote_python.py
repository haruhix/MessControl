"""Run editor Python through Epic's loopback remote API, without UI input."""
import json
import sys
import time
from pathlib import Path

sys.path.insert(0, 'E:/UE/UE_5.8/Engine/Plugins/Experimental/PythonScriptPlugin/Content/Python')
import remote_execution as ue_remote

session = ue_remote.RemoteExecution()
try:
    session.start()
    deadline = time.monotonic() + 8
    while time.monotonic() < deadline:
        nodes = [n for n in session.remote_nodes if n.get('project_name') == 'MessControl']
        if nodes:
            break
        time.sleep(.25)
    if len(nodes) != 1:
        raise RuntimeError('Expected one MessControl editor: ' + json.dumps(session.remote_nodes))
    session.open_command_connection(nodes[0]['node_id'])
    source = Path(sys.argv[1]).resolve().as_posix()
    result = session.run_command(source, unattended=True, raise_on_failure=True)
    print(json.dumps(result, ensure_ascii=False))
finally:
    session.stop()
