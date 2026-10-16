"""Reproducible per-handoff Painter authoring through the configured MCP bridge.

Run with the SubstancePainterMCP venv Python (which supplies the mcp client).
All mutations are guarded by the explicitly selected dedicated project path.
No Unreal packages, live Blender scenes, or unrelated Painter project is edited.
"""
from __future__ import annotations
import argparse
import asyncio
import hashlib
import json
import os
from pathlib import Path
import shutil
import tomllib

from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client

ROOT = Path(__file__).resolve().parents[3]
HANDOFF = ROOT / 'ArtSource/UI/MainMenu3D/20261015_refB_v1/Assembly/UnrealHandoff/v08_ReferenceFinish'
OUT = HANDOFF / 'Textures'
STAGE = Path('C:/Users/user/Documents/Adobe/Adobe Substance 3D Painter/MessControlMenu3D/v08')
PROJECT = STAGE / 'MC_DAY03_ReferenceFinish_UVTile_v08.spp'


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def write(name, data):
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / name).write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding='utf8')


async def main():
    global HANDOFF, OUT, STAGE, PROJECT
    p = argparse.ArgumentParser()
    p.add_argument('action', choices=['status', 'create', 'layers', 'masks', 'masks_plan', 'warm_body', 'color_probe', 'save', 'export', 'mesh_maps'])
    p.add_argument('--mesh', type=Path)
    p.add_argument('--revision', default='01', help='Immutable export candidate suffix, e.g. 01 or 02.')
    p.add_argument('--handoff', type=Path, default=HANDOFF, help='Approved UV source revision (e.g. sibling v08b_SeamUV).')
    p.add_argument('--stage', type=Path, default=STAGE)
    p.add_argument('--project', type=Path, default=PROJECT)
    p.add_argument('--texture-sets', nargs=2, default=['MC_Enamel', 'MC_Gum'])
    p.add_argument('--editable-name', help='Explicit editable copy name for separate material projects.')
    p.add_argument('--group-prefix', default='DAY03')
    args = p.parse_args()
    HANDOFF = args.handoff.resolve()
    OUT = HANDOFF / 'Textures'
    STAGE = args.stage.resolve()
    PROJECT = args.project.resolve()
    OUT.mkdir(parents=True, exist_ok=True)
    STAGE.mkdir(parents=True, exist_ok=True)
    config = tomllib.loads((Path.home() / '.codex/config.toml').read_text(encoding='utf8'))
    entry = config['mcp_servers']['substance_painter']
    parameters = StdioServerParameters(command=entry['command'], args=entry['args'], env={**os.environ, **entry['env']})
    async with stdio_client(parameters) as (read, stream):
        async with ClientSession(read, stream) as client:
            await client.initialize()

            async def call(name, data=None):
                response = await client.call_tool(name, data or {})
                raw = '\n'.join(c.text for c in response.content if c.type == 'text')
                if response.isError:
                    raise RuntimeError(f'{name}: {raw}')
                result = response.structuredContent if response.structuredContent is not None else json.loads(raw)
                with (OUT / 'painter_operations.jsonl').open('a', encoding='utf8') as log:
                    log.write(json.dumps({'tool': name, 'arguments': data or {}, 'result': result}, ensure_ascii=False) + '\n')
                print(json.dumps({'tool': name, 'result': result}, ensure_ascii=False), flush=True)
                return result

            if args.action == 'status':
                await call('painter_status')
                await call('get_project_info')
                return
            info = await call('get_project_info')
            if args.action == 'create':
                if info.get('open'):
                    raise RuntimeError('Project creation requires Painter to have no open project; never replace an unrelated project.')
                if args.mesh is None or not args.mesh.is_file():
                    raise RuntimeError('--mesh must point at the frozen v08 mesh supplied by its geometry owner.')
                staged_mesh = STAGE / args.mesh.name
                shutil.copy2(args.mesh, staged_mesh)
                # An adjacent OBJ material library is a required part of that mesh.
                for mtl in args.mesh.parent.glob('*.mtl'):
                    shutil.copy2(mtl, STAGE / mtl.name)
                settings = {'normal_map_format': 'DirectX', 'tangent_space_mode': 'PerFragment',
                            'project_workflow': 'UVTile', 'default_texture_resolution': 4096,
                            'import_cameras': False}
                data = {'mesh_file_path': staged_mesh.as_posix(), 'output_path': PROJECT.as_posix(), 'settings': settings}
                plan = await call('plan_project_creation', data)
                if not plan.get('ready'):
                    raise RuntimeError(f'Creation preflight rejected: {plan}')
                result = await call('create_project', {**data, 'confirm': True})
                write('painter_project_creation.json', {'mesh': str(args.mesh.resolve()), 'mesh_sha256': digest(args.mesh), 'result': result})
                return
            if not info.get('open') or Path(info.get('path') or '').resolve() != PROJECT.resolve():
                raise RuntimeError(f'Wrong Painter project; expected {PROJECT}, observed {info}')
            if sorted(info['texture_sets']) != sorted(args.texture_sets):
                raise RuntimeError(f'Unexpected texture-set contract: {info}')
            if args.action == 'warm_body':
                linkage = json.loads((OUT / 'source_frozen_linkage.json').read_text())
                for path, expected in linkage['source_frozen_files'].items():
                    if digest(path) != expected['sha256']:
                        raise RuntimeError(f'Frozen source changed: {path}')
                existing = await call('find_layers', {'query': 'WarmBody01 |', 'texture_set': 'MC_Enamel'})
                if existing.get('count', 0) or existing.get('layers'):
                    raise RuntimeError('Warm-body candidate already applied; inspect rather than duplicate it.')
                before = {name: await call('snapshot_layer_tree', {'texture_set': name}) for name in args.texture_sets}
                write('candidate_before_snapshots.json', before)
                # Preserve the original roughness, metalness, height and their
                # mask stacks; only color is reconstructed in a separate group.
                # Replacing active channels resets Painter uniform sources to
                # defaults. Reapply every retained source immediately; otherwise
                # a BaseColor-only candidate silently changes enamel roughness.
                for uid, channels, values in [
                    (113, ['Roughness', 'Metallic', 'Height'], {'Roughness': .235, 'Metallic': 0, 'Height': 0}),
                    (131, ['Roughness'], {'Roughness': .27}),
                    (146, ['Roughness'], {'Roughness': .205})]:
                    await call('set_active_channels', {'uid': uid, 'channels': channels})
                    await call('set_fill_channels', {'uid': uid, 'values': values})
                recipe = json.loads((OUT / 'warm_body_recipe.json').read_text())
                result = await call('create_layer_recipe', {'texture_set': 'MC_Enamel', 'recipe': recipe})
                write('warm_body_layer_state.json', result)
                state = {}
                roles = {'05': ('FangIncisal', .68), '04': ('OrdinaryIncisal', .35),
                         '03': ('Cervical', .62), '02': ('WarmCoronalBody', .75)}
                for layer in result['nodes'][0]['children']:
                    prefix = layer['name'].split(' |')[0]
                    if prefix not in roles:
                        continue
                    role, opacity = roles[prefix]
                    source = OUT / 'AnatomyMasks' / ('MC_Enamel_' + role + '.png')
                    target = STAGE / source.name
                    shutil.copy2(source, target)
                    imported = await call('import_project_resource', {'file_path': target.as_posix(), 'usage': 'TEXTURE', 'name': source.stem, 'confirm': True})
                    inserted = await call('insert_mask_effect', {'uid': layer['uid'], 'effect_type': 'fill', 'resource_url': imported['url'], 'name': role})
                    effect = inserted['effects'][0]
                    await call('set_fill_projection', {'uid': effect['uid'], 'mode': 'UV', 'scale': [1, 1]})
                    await call('set_layer_properties', {'uid': layer['uid'], 'channel': 'BaseColor', 'opacity': opacity})
                    state[prefix] = {'layer': layer, 'effect': effect, 'mask': str(source), 'mask_sha256': digest(source), 'opacity': opacity}
                    write('warm_body_mask_state.json', state)
                after = {name: await call('snapshot_layer_tree', {'texture_set': name}) for name in args.texture_sets}
                write('candidate_after_snapshots.json', after)
                await call('save_project', {'confirm': True})
            elif args.action == 'layers':
                recipe = json.loads((OUT / 'layer_recipe.json').read_text(encoding='utf8'))
                state = {}
                for texture_set in args.texture_sets:
                    snapshot = await call('snapshot_layer_tree', {'texture_set': texture_set})
                    existing = await call('find_layers', {'query': args.group_prefix + ' |', 'texture_set': texture_set})
                    if existing.get('count', 0) or existing.get('layers'):
                        raise RuntimeError('The requested day layer group already exists; inspect it instead of adding duplicate layers.')
                    await call('plan_layer_recipe', {'recipe': recipe[texture_set], 'texture_set': texture_set})
                    state[texture_set] = await call('create_layer_recipe', {'recipe': recipe[texture_set], 'texture_set': texture_set})
                write('painter_layer_state.json', state)
                await call('save_project', {'confirm': True})
            elif args.action == 'masks_plan':
                state = json.loads((OUT / 'painter_layer_state.json').read_text(encoding='utf8'))
                mask_path = OUT / 'painter_mask_state.json'
                mask_state = json.loads(mask_path.read_text(encoding='utf8')) if mask_path.exists() else {}
                plan = json.loads((OUT / 'painter_mask_plan.json').read_text(encoding='utf8'))
                cache = {}

                def descendants(nodes):
                    for node in nodes:
                        yield node
                        yield from descendants(node.get('children', []))

                async def source_url(texture_set, item):
                    if item.get('role'):
                        source = OUT / 'AnatomyMasks' / (texture_set + '_' + item['role'] + '.png')
                        target = STAGE / source.name
                        shutil.copy2(source, target)
                        result = await call('import_project_resource', {'file_path': target.as_posix(), 'usage': 'TEXTURE', 'name': source.stem, 'confirm': True})
                        return result['url']
                    name = item['resource']
                    if name not in cache:
                        matches = await call('search_resources', {'query': 'marble' if name.startswith('marble_') else 'noise', 'limit': 200})
                        exact = [r for r in matches['resources'] if r['name'] == name]
                        if len(exact) != 1:
                            raise RuntimeError(f'Expected one exact resource {name}, got {exact}')
                        cache[name] = exact[0]['url']
                    return cache[name]

                for item in plan['layers']:
                    texture_set = item['texture_set']
                    prefix = item['prefix']
                    matches = [n for n in descendants(state[texture_set]['nodes']) if n['name'].startswith(prefix + ' |')]
                    if len(matches) != 1:
                        raise RuntimeError(f'Ambiguous layer {texture_set}/{prefix}: {matches}')
                    layer = matches[0]
                    key = texture_set + '/' + prefix
                    entry = mask_state.setdefault(key, {'layer': layer, 'effects': []})
                    sources = [item] + ([{'role': item['multiply_role'], 'blend': 'Multiply'}] if item.get('multiply_role') else [])
                    for index, source in enumerate(sources):
                        if len(entry['effects']) <= index:
                            url = await source_url(texture_set, source)
                            result = await call('insert_mask_effect', {'uid': layer['uid'], 'effect_type': 'fill', 'resource_url': url, 'name': source.get('role') or source['resource'].split('/')[-1]})
                            entry['effects'].append({'effect': result['effects'][0], 'source_url': url, 'source': source})
                            write('painter_mask_state.json', mask_state)
                        uid = entry['effects'][index]['effect']['uid']
                        scale = source.get('scale', 1)
                        await call('set_fill_projection', {'uid': uid, 'mode': 'UV' if source.get('role') else 'Triplanar', 'scale': [scale, scale]})
                        if source.get('blend'):
                            await call('set_layer_properties', {'uid': uid, 'blending_mode': source['blend']})
                    for channel, opacity in item.get('opacities', {}).items():
                        await call('set_layer_properties', {'uid': layer['uid'], 'channel': channel, 'opacity': opacity})
                write('painter_mask_state.json', mask_state)
                await call('save_project', {'confirm': True})
            elif args.action == 'masks':
                state = json.loads((OUT / 'painter_layer_state.json').read_text(encoding='utf8'))
                mask_state_path = OUT / 'painter_mask_state.json'
                mask_state = json.loads(mask_state_path.read_text(encoding='utf8')) if mask_state_path.is_file() else {}

                async def find_resource(name):
                    # Painter shelf search indexes display-label words rather
                    # than literal machine paths containing underscores.
                    keyword = 'marble' if name.startswith('marble_') else 'noise'
                    matches = await call('search_resources', {'query': keyword, 'limit': 200})
                    resources = [r for r in matches['resources'] if r['name'] == name]
                    if len(resources) != 1:
                        raise RuntimeError(f'Expected one exact procedural resource for {name}; found {resources}')
                    return resources[0]['url']

                async def mask(texture_set, prefix, *, role=None, resource=None, scale=1, opacity=1, channel='BaseColor', mode='UV'):
                    key = texture_set + '/' + prefix
                    layers = state[texture_set]['nodes'][0]['children']
                    layer = next(node for node in layers if node['name'].startswith(prefix + ' |'))
                    if key not in mask_state:
                        if role:
                            source = OUT / 'AnatomyMasks' / f'{texture_set}_{role}.png'
                            if not source.is_file():
                                raise RuntimeError(f'Missing frozen-UV anatomy mask: {source}')
                            target = STAGE / source.name
                            shutil.copy2(source, target)
                            imported = await call('import_project_resource', {'file_path': target.as_posix(),
                                                  'usage': 'TEXTURE', 'name': source.stem, 'confirm': True})
                            resource_url = imported['url']
                        else:
                            resource_url = await find_resource(resource)
                        result = await call('insert_mask_effect', {'uid': layer['uid'], 'effect_type': 'fill',
                                            'resource_url': resource_url, 'name': role or resource.rsplit('/', 1)[-1]})
                        effect = result['effects'][0]
                        mask_state[key] = {'layer': layer, 'effect': effect, 'source_url': resource_url}
                        write('painter_mask_state.json', mask_state)
                    effect = mask_state[key]['effect']
                    await call('set_fill_projection', {'uid': effect['uid'], 'mode': mode, 'scale': [scale, scale]})
                    await call('set_layer_properties', {'uid': layer['uid'], 'channel': channel, 'opacity': opacity})
                    if not role:
                        parameters = await call('get_fill_parameters', {'uid': effect['uid']})
                        mask_state[key]['parameters_observed'] = parameters
                        write('painter_mask_state.json', mask_state)

                await mask('MC_Enamel', '02', role='Cervical', opacity=.62)
                await mask('MC_Enamel', '03', role='Incisal', opacity=.68)
                await mask('MC_Enamel', '04', resource='noise_anisotropic_noise/anisotropic_noise', scale=55,
                           opacity=.3, channel='Height', mode='Triplanar')
                await mask('MC_Enamel', '05', resource='noise_clouds_2/clouds_2', scale=3.2,
                           opacity=.35, channel='Roughness', mode='Triplanar')
                await mask('MC_Gum', '02', role='Cervical', opacity=.65)
                await mask('MC_Gum', '03', resource='noise_clouds_2/clouds_2', scale=3.4, opacity=.3, mode='Triplanar')
                await mask('MC_Gum', '04', resource='marble_veins/marble_veins', scale=4.8, opacity=.12, mode='Triplanar')
                await mask('MC_Gum', '05', resource='noise_bnw_spots_3/bnw_spots_3', scale=15,
                           opacity=.45, channel='Height', mode='Triplanar')
                await mask('MC_Gum', '06', resource='noise_moisture_noise/moisture_noise', scale=4.2,
                           opacity=.65, channel='Roughness', mode='Triplanar')
                write('painter_mask_state.json', mask_state)
                await call('save_project', {'confirm': True})
            elif args.action == 'color_probe':
                state = json.loads((OUT / 'painter_layer_state.json').read_text(encoding='utf8'))
                muted = []
                try:
                    for texture_set in args.texture_sets:
                        for layer in state[texture_set]['nodes'][0]['children']:
                            if not layer['name'].startswith('01 |'):
                                await call('set_layer_properties', {'uid': layer['uid'], 'visible': False})
                                muted.append(layer['uid'])
                    destination = STAGE / 'ColorProbe_BaseOnly'
                    destination.mkdir(parents=True, exist_ok=True)
                    result = await call('export_textures', {'output_directory': destination.as_posix(),
                                        'preset': 'Document channels + Normal + AO (No Alpha)',
                                        'size_log2': 9, 'file_format': 'png', 'bit_depth': '8',
                                        'texture_sets': args.texture_sets})
                    target = OUT / 'ColorProbe_BaseOnly'
                    target.mkdir(parents=True, exist_ok=True)
                    for raw in result['files']:
                        shutil.copy2(raw, target / Path(raw).name)
                    write('color_probe_export.json', result)
                finally:
                    for uid in muted:
                        await call('set_layer_properties', {'uid': uid, 'visible': True})
                await call('save_project', {'confirm': True})
            elif args.action == 'save':
                await call('audit_project')
                await call('save_project', {'confirm': True})
                destination = OUT / (args.editable_name or ('MC_DAY03_ReferenceFinish_' + HANDOFF.name.split('_', 1)[0] + '.spp'))
                shutil.copy2(PROJECT, destination)
                write('painter_saved_project.json', {'project': str(destination.resolve()), 'bytes': destination.stat().st_size, 'sha256': digest(destination)})
            elif args.action in ['export', 'mesh_maps']:
                name = ('PainterPBR' if args.action == 'export' else 'PainterMeshMaps') + '_' + args.revision
                destination = STAGE / name
                destination.mkdir(parents=True, exist_ok=True)
                preset = 'Document channels + Normal + AO (No Alpha)' if args.action == 'export' else 'Mesh Maps'
                data = {'output_directory': destination.as_posix(), 'preset': preset,
                        'size_log2': 12, 'file_format': 'png', 'bit_depth': '8',
                        'texture_sets': args.texture_sets}
                plan = await call('plan_texture_export', data)
                if plan.get('errors'):
                    raise RuntimeError(f'Export preflight rejected: {plan}')
                result = await call('export_textures', data)
                if result.get('status') != 'Success':
                    raise RuntimeError(f'Painter export did not succeed: {result}')
                target = OUT / name
                target.mkdir(parents=True, exist_ok=True)
                files = []
                for raw in result['files']:
                    path = Path(raw)
                    output = target / path.name
                    shutil.copy2(path, output)
                    files.append({'path': str(output.resolve()), 'bytes': output.stat().st_size, 'sha256': digest(output)})
                write(name + '_receipt.json', {'preset': preset, 'files': files, 'result': result})


if __name__ == '__main__':
    asyncio.run(main())
