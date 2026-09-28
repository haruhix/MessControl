"""Run an automation filter through Epic's native MCP and preserve its report."""
import argparse
import json
from pathlib import Path
from native_mcp_client import NativeMCP

p = argparse.ArgumentParser()
p.add_argument('filter')
p.add_argument('--output', default='Saved/NativeTests.json')
args = p.parse_args()
client = NativeMCP()

def call(name, arguments=None):
    r = client.tool('call_tool', dict(toolset_name='AutomationTestToolset.AutomationTestToolset',
        tool_name=name, arguments=arguments or {}))
    if r.get('isError'):
        raise RuntimeError(r)
    value = json.loads(r['content'][0]['text'])['returnValue']
    return json.loads(value) if isinstance(value, str) else value

call('DiscoverTests')
report = call('RunTestsByFilter', {'filterExpression': args.filter})
Path(args.output).write_text(json.dumps(report, indent=2), encoding='utf-8')
print(json.dumps(report))
raise SystemExit(1 if report.get('failed') else 0)
