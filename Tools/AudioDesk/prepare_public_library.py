"""Prepare reproducible portable CC0 seeds and an offline audio archive.

Inputs are explicit development paths; no user working profile is read or copied.
All files published by this helper are source metadata, the original seed profile,
CC0 media, or the project's own original audio.
"""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
import math
from pathlib import Path
import re
import shutil
import zipfile


def read(path: Path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def write(path: Path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2, allow_nan=False) + '\n', encoding='utf-8')


def normalized(value):
    return str(value).replace('\\', '/').casefold()


def digest(path):
    result = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024*1024), b''):
            result.update(block)
    return result.hexdigest()


def clean_catalog(value, project_root):
    if isinstance(value, dict):
        return {key: clean_catalog(child, project_root) for key, child in value.items()}
    if isinstance(value, list):
        return [clean_catalog(child, project_root) for child in value]
    if isinstance(value, str):
        canonical = value.replace('\\', '/')
        prefix = project_root.as_posix()
        if canonical.casefold().startswith(prefix.casefold()):
            return canonical[len(prefix):].lstrip('/') or '${PROJECT_ROOT}'
    return value


def tokens(value):
    value = re.sub(r'([a-z])([A-Z])', r'\1 \2', str(value))
    return re.findall(r'[a-zа-яё]+', value.casefold())


def term_match(term, words):
    parts = tokens(term)
    return bool(parts) and all(any(word == part or (len(part) >= 3 and word.startswith(part)) for word in words) for part in parts)


def context_score(slot, item):
    search = slot.get('search_keywords', {})
    name = tokens(item['name'])
    parts = item.get('relative_path', '').replace('\\', '/').split('/')
    folders = tokens('/'.join(parts[2:-1])) if len(parts) > 3 else []
    if any(term_match(term, name) or term_match(term, folders) for term in search.get('negative', [])):
        return -1
    positive = search.get('positive', [])
    if not positive:
        return 1
    result = 0
    for term in positive:
        if term_match(term, name):
            result += 12
        elif term_match(term, folders):
            result += 4
        elif any(len(word) >= 4 for word in tokens(term)) and term_match(term, tokens(item.get('pack', ''))):
            result += 1
    return result


def fits(slot, item, clip=None):
    if not item or not item.get('duration'):
        return False
    policy = {**slot.get('search_keywords', {}), **slot.get('timing_policy', {})}
    duration = float(item['duration'])
    clip = clip or {}
    start = float(clip.get('trim_start') or 0)
    end = min(float(clip.get('trim_end') or duration), duration)
    effective = end-start
    return math.isfinite(effective) and float(policy.get('min_duration', .025)) - 1e-6 <= effective <= float(policy.get('max_duration', 3.5)) + 1e-6


def sources_by_asset(value, result):
    if isinstance(value, dict):
        if value.get('unreal_asset') and isinstance(value.get('sources'), list):
            result.setdefault(value['unreal_asset'].split('.')[0], []).extend(value['sources'])
        for child in value.values():
            sources_by_asset(child, result)
    elif isinstance(value, list):
        for child in value:
            sources_by_asset(child, result)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-desk', required=True, type=Path)
    parser.add_argument('--audio-root', required=True, type=Path)
    parser.add_argument('--project-root', required=True, type=Path)
    parser.add_argument('--output', type=Path, default=Path(__file__).resolve().parent)
    parser.add_argument('--staging', required=True, type=Path)
    args = parser.parse_args()
    for path in (args.output, args.staging):
        path.mkdir(parents=True, exist_ok=True)
    metadata = read(args.audio_root/'Catalog/Library.json')
    cc0 = {pack['slug']: pack for pack in metadata['items'] if pack['license'].startswith('CC0')}
    original = read(args.source_desk/'data/library.json')
    source_catalog = read(args.source_desk/'catalog.json')
    catalog = clean_catalog(source_catalog, args.project_root)
    catalog['project_root'] = '${PROJECT_ROOT}'
    catalog['distribution'] = {'kind':'portable-public-cc0', 'audio_files':550,
        'note':'Public starter library: 527 CC0 files plus 23 project-authored originals. Creator-licensed raw packs are not redistributed.'}
    originals = [item for item in original['files'] if item.get('pack') == 'MessControl current']
    selected = [item for item in original['files'] if item.get('pack') in cc0 or item.get('pack') == 'MessControl current']
    if len(selected) != 550 or len(originals) != 23:
        raise SystemExit('Unexpected public library size; review explicit redistribution inputs.')
    files = []
    source_by_path = {}
    library_stage = args.staging/'library'
    for item in selected:
        path = Path(item['path'])
        actual_hash = digest(path)
        if item.get('sha256') and actual_hash != item['sha256']:
            raise SystemExit('Source audio hash changed: ' + str(path))
        relative = ('MessControl_Current/' + item['name']) if item['pack'] == 'MessControl current' else ('Sounds/' + item['relative_path'])
        target = library_stage/relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, target)
        record = {key:copy.deepcopy(item[key]) for key in ('id','name','relative_path','pack','bytes','duration','channels','sample_rate','format','sha256') if key in item}
        record['path'] = '${APP_ROOT}/library/' + relative
        record['portable_path'] = record['path']
        record['sha256'] = actual_hash
        pack = cc0.get(item['pack'])
        if pack:
            record['source_pack'] = {key:pack[key] for key in ('slug','title','author','url','license','attribution','conditions')}
            record['source_pack']['license_path'] = '${APP_ROOT}/library/Licenses/' + pack['slug']
        else:
            record['source_pack'] = {'slug':'MessControl_Current','title':'MessControl original audio',
                'author':'MessControl project','url':'https://github.com/haruhix/MessControl',
                'license':'Project-authored audio; repository terms apply','attribution':'MessControl project'}
        files.append(record)
        source_by_path[normalized(path)] = record
    by_id = {item['id']:item for item in files}
    source_assets = {}
    sources_by_asset(read(args.source_desk/'existing_assignments.json'), source_assets)
    compare_count = 0
    for slot in catalog['slots']:
        current = slot.get('current_assignment', {})
        references = [item.get('refPath',item.get('unreal_asset','')) for item in current.get('sounds',[])]
        if current.get('sound'):
            references.append(current['sound'])
        if slot['id'].startswith('direct.'):
            references.append(slot.get('target',{}).get('asset',''))
        resolved = []
        for reference in references:
            if isinstance(reference, dict):
                reference = reference.get('refPath','')
            for source in source_assets.get(str(reference).split('.')[0], []):
                item = source_by_path.get(normalized(source.get('candidate_source_path','')))
                if item and not any(row['file_id'] == item['id'] for row in resolved):
                    resolved.append({'file_id':item['id'],'name':item['name'],'path':item['path'],
                        'unreal_asset':reference,'source_matches_import_md5':source.get('source_matches_import_md5'),
                        'gain_db':round(20*math.log10(max(float(current.get('volume',1)),.000001)),3)})
        if resolved:
            current['source_files'] = resolved
            current['source_evidence'] = 'Original source metadata checked when the catalog was prepared.'
            slot['current_assignment'] = current
            compare_count += 1
    profile = read(args.source_desk/'initial_profile.json')
    profile['name'] = 'MessControl - portable CC0 starter'
    profile['distribution'] = catalog['distribution']
    changed, gaps = [], []
    for slot in catalog['slots']:
        assignment = copy.deepcopy(profile['assignments'].get(slot['id'], {'status':'empty','clips':[],'note':''}))
        kept = [clip for clip in assignment.get('clips',[]) if clip.get('file_id') in by_id and fits(slot,by_id[clip['file_id']],clip)]
        alternatives = [clip for clip in assignment.get('alternatives',[]) if clip.get('file_id') in by_id and fits(slot,by_id[clip['file_id']],clip)]
        replaced = kept != assignment.get('clips',[])
        if not kept and assignment.get('clips'):
            current_ids = {row['file_id'] for row in slot.get('current_assignment',{}).get('source_files',[])}
            candidates = [(context_score(slot,item) + (20 if item['id'] in current_ids else 0),item)
                          for item in files if fits(slot,item) and (context_score(slot,item)>0 or item['id'] in current_ids)]
            candidates.sort(key=lambda pair:(-pair[0], pair[1]['name']))
            if candidates:
                inherited_gain = assignment['clips'][0].get('gain_db',0)
                kept = [{'file_id':item['id'],'gain_db':inherited_gain,'trim_start':0,'trim_end':None} for _,item in candidates[:1]]
                alternatives = [{'file_id':item['id'],'gain_db':inherited_gain,'trim_start':0,'trim_end':None} for _,item in candidates[1:4]]
                assignment['reason'] = 'Кандидат из публичного CC0-комплекта или текущих оригиналов; подходит по контексту и длительности. Требует прослушивания и одобрения.'
                assignment['confidence'] = .5
            else:
                assignment['reason'] = 'В публичном комплекте не найден подходящий короткий источник. Подключи исходный пак или добавь свой звук.'
                assignment.pop('confidence',None)
        assignment['clips'] = kept
        selected_ids = {clip['file_id'] for clip in kept}
        assignment['alternatives'] = [clip for clip in alternatives if clip['file_id'] not in selected_ids]
        assignment['status'] = 'suggested' if kept else 'empty'
        assignment['selection_method'] = 'portable_cc0_seed'
        assignment['note'] = ''
        if replaced:
            changed.append(slot['id'])
        if not kept:
            gaps.append(slot['id'])
        profile['assignments'][slot['id']] = assignment
    seed = {'schema_version':1,'roots':['${APP_ROOT}/library/Sounds','${APP_ROOT}/library/MessControl_Current'],
            'files':files,'distribution':catalog['distribution']}
    write(args.output/'seed_library.json',seed)
    write(args.output/'seed_profile.json',profile)
    write(args.output/'catalog.json',catalog)
    lines = ['# Third-party audio in the public starter library','',
        'This starter contains 527 CC0 audio files from the sources below and 23 original MessControl audio files. It does not contain the creator-licensed 400 Sounds, Mini Whooshes, MGW Footsteps, or Nvthic Impact raw packs. Additional packs may be connected locally.','',
        'The CC0 files retain their original names and hashes. Attribution is not required by CC0, but creators and original source links are retained here and in each library record. No third-party sound is claimed as a MessControl creation.','',
        '| Pack | Creator | Files | Original source |','|---|---|---:|---|']
    for slug,pack in sorted(cc0.items()):
        lines.append(f"| {pack['title']} | {pack['author']} | {pack['audio_files']} | [{pack['title']}]({pack['url']}) |")
        license_dir = library_stage/'Licenses'/slug
        license_dir.mkdir(parents=True,exist_ok=True)
        origin_dir = Path(pack['license_path'])
        shutil.copy2(origin_dir/'LICENSE_AND_SOURCE.txt',license_dir/'LICENSE_AND_SOURCE.txt')
        for bundled in (origin_dir/'Bundled').rglob('*') if (origin_dir/'Bundled').is_dir() else []:
            if bundled.is_file() and bundled.suffix.lower() in {'.txt','.md'}:
                target = license_dir/'Bundled'/bundled.relative_to(origin_dir/'Bundled')
                target.parent.mkdir(parents=True,exist_ok=True)
                shutil.copy2(bundled,target)
    lines += ['', 'License: [CC0 1.0 Universal](https://creativecommons.org/publicdomain/zero/1.0/). Saved source/license summaries are included under `library/Licenses/`. Bundled creator license/readme text is preserved where supplied.',
              '', 'The project-authored originals are the existing synthesized UI/foley/piano and FogBrawl files from `ArtSource/Audio` and `ContentSource/Audio/FogBrawl`. They are included so this tool can be used from the Tools folder without loading Unreal.', '']
    (args.output/'THIRD_PARTY_AUDIO.md').write_text('\n'.join(lines),encoding='utf-8')
    (library_stage/'THIRD_PARTY_AUDIO.md').write_text('\n'.join(lines),encoding='utf-8')
    archive = args.output/'library.zip'
    with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as output:
        for path in sorted(library_stage.rglob('*')):
            if path.is_file():
                output.write(path,path.relative_to(args.staging).as_posix())
    with zipfile.ZipFile(archive) as check:
        error = check.testzip()
        if error:raise SystemExit('CRC failure: '+error)
    manifest = {'schema_version':1,'archive':'library.zip','sha256':digest(archive),'bytes':archive.stat().st_size,
                'audio_files':len(files),'cc0_files':527,'project_originals':23,'extracted_directory':'library',
                'source':'Public CC0 starter plus project-authored audio','license_notice':'THIRD_PARTY_AUDIO.md'}
    write(args.output/'library-manifest.json',manifest)
    report = {'audio_files':len(files),'compared_current_slots':compare_count,'suggested':sum(a['status']=='suggested' for a in profile['assignments'].values()),
              'empty':len(gaps),'replaced_seed_slots':changed,'gap_slots':gaps,'archive':manifest}
    write(args.staging/'preparation-report.json',report)
    print(json.dumps(report,ensure_ascii=False))


if __name__ == '__main__':
    main()
