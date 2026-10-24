"""Distribution checks for the portable public starter, without Unreal."""
from pathlib import Path
import hashlib
import json
import re
import unittest
import zipfile

ROOT = Path(__file__).resolve().parent


def read(name):
    return json.loads((ROOT/name).read_text(encoding='utf-8'))


class PublicLibraryTests(unittest.TestCase):
    def test_public_scope_and_seed_references(self):
        library = read('seed_library.json')
        profile = read('seed_profile.json')
        ids = {item['id'] for item in library['files']}
        self.assertEqual(len(ids), 550)
        self.assertEqual(sum(item['source_pack']['license'] == 'CC0 1.0 Universal' for item in library['files']), 527)
        self.assertEqual(sum(item['pack'] == 'MessControl current' for item in library['files']), 23)
        self.assertFalse(any('ChequeredInk' in item['path'] or 'GeneChaban' in item['path'] or 'MGW_' in item['path'] or 'Nvthic' in item['path'] for item in library['files']))
        for assignment in profile['assignments'].values():
            self.assertNotEqual(assignment['status'], 'approved')
            self.assertEqual(assignment.get('note', ''), '')
            for key in ('clips', 'alternatives'):
                for clip in assignment.get(key, []):
                    self.assertIn(clip['file_id'], ids)

    def test_portable_json_has_no_developer_drive(self):
        for name in ('catalog.json', 'seed_library.json', 'seed_profile.json'):
            with self.subTest(name=name):
                raw = (ROOT/name).read_text(encoding='utf-8')
                self.assertIsNone(re.search(r'(?<![A-Za-z])[A-Za-z]:[\\/]', raw))
        catalog = read('catalog.json')
        self.assertEqual(len(catalog['slots']), 107)
        self.assertTrue(all(slot.get('timing_policy') for slot in catalog['slots']))

    def test_archive_and_every_media_hash(self):
        manifest = read('library-manifest.json')
        archive = ROOT/manifest['archive']
        self.assertEqual(archive.stat().st_size, manifest['bytes'])
        self.assertEqual(hashlib.sha256(archive.read_bytes()).hexdigest(), manifest['sha256'])
        with zipfile.ZipFile(archive) as packed:
            for name in packed.namelist():
                self.assertTrue(name.startswith('library/'))
                self.assertNotIn('..', Path(name).parts)
            self.assertIn('library/THIRD_PARTY_AUDIO.md', packed.namelist())
            for item in read('seed_library.json')['files']:
                member = item['path'].replace('${APP_ROOT}/', '', 1)
                payload = packed.read(member)
                self.assertEqual(len(payload), item['bytes'])
                self.assertEqual(hashlib.sha256(payload).hexdigest(), item['sha256'])


if __name__ == '__main__':
    unittest.main()
