"""Portable-runtime regressions. Test fixtures and caches stay on E:."""
from contextlib import redirect_stdout
import copy
import hashlib
import http.client
import io
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile
import threading
import unittest
from unittest import mock
import wave

import app


class PortableTests(unittest.TestCase):
    def setUp(self):
        scratch = Path('E:/CodexTemp/MessControlAudioDesk/portable-tests')
        scratch.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix='portable-', dir=scratch)
        self.root = Path(self.temporary.name)
        self.install = self.root / 'Копия с пробелами' / 'Tools' / 'AudioDesk'
        (self.install / 'web').mkdir(parents=True)
        (self.install / 'web' / 'index.html').write_text('portable app', encoding='utf-8')
        app.atomic_json(self.install / 'catalog.json', {'slots':[{'id':'ui.click','kind':'sfx'}]})

    def tearDown(self):
        os.environ['TEMP'] = os.environ['TMP'] = 'E:/CodexTemp/MessControlAudioDesk'
        self.temporary.cleanup()

    def make_seed(self, *, exists=True):
        path = self.install / 'library' / 'click.wav'
        if exists:
            path.parent.mkdir()
            with wave.open(str(path), 'wb') as stream:
                stream.setparams((1,2,8000,0,'NONE','not compressed'))
                stream.writeframes(b'\x00\x00'*800)
        entry = {'id':'stable-seed-id','name':'click.wav','path':'${APP_ROOT}/library/click.wav',
                 'duration':.1,'channels':1,'sample_rate':8000,'format':'wav','pack':'Test'}
        if exists:
            entry.update(bytes=path.stat().st_size, mtime_ns=path.stat().st_mtime_ns,
                         sha256=hashlib.sha256(path.read_bytes()).hexdigest())
        app.atomic_json(self.install / 'seed_library.json', {'roots':['${APP_ROOT}/library'],'files':[entry]})
        profile = {'schema_version':1,'name':'Portable seed','assignments':{
            'ui.click':{'status':'suggested','clips':[{'file_id':'stable-seed-id','trim_start':0,'trim_end':None,'gain_db':0}]}}}
        app.atomic_json(self.install / 'seed_profile.json', profile)
        return profile

    def test_frozen_resources_do_not_select_temporary_data_folder(self):
        resources = self.root / 'bundle' / '_internal'
        executable = self.root / 'bundle' / 'AudioDesk.exe'
        with mock.patch.object(sys,'frozen',True,create=True), \
             mock.patch.object(sys,'_MEIPASS',str(resources),create=True), \
             mock.patch.object(sys,'executable',str(executable)):
            base, directory = app.runtime_paths()
        self.assertEqual(base,resources.resolve())
        self.assertEqual(directory,executable.parent.resolve())
        self.assertNotEqual(base,directory)

    def test_packaged_media_tools_win_over_system_path(self):
        resources = self.root / 'resources'
        (resources / 'media').mkdir(parents=True)
        name = 'ffmpeg.exe' if os.name == 'nt' else 'ffmpeg'
        packaged = resources / 'media' / name
        packaged.write_bytes(b'fixture, not executable')
        with mock.patch('app.shutil.which',return_value='do-not-use-PATH'):
            self.assertEqual(app.find_media_tool('ffmpeg',self.install,resources),str(packaged.resolve()))
        (self.install / 'media').mkdir()
        external = self.install / 'media' / name
        external.write_bytes(b'fixture override')
        self.assertEqual(app.find_media_tool('ffmpeg',self.install,resources),str(external.resolve()))

    def test_missing_seed_audio_starts_without_replacing_the_catalog(self):
        expected = self.make_seed(exists=False)
        desk = app.AudioDesk(self.install / 'data',self.install,app_dir=self.install)
        self.assertEqual(desk.profile['assignments'],expected['assignments'])
        self.assertFalse(desk.library['stable-seed-id']['available'])
        self.assertEqual(desk.library['stable-seed-id']['path'],str((self.install/'library/click.wav').resolve()))
        self.assertEqual(len(desk.state()['catalog']['slots']),1)
        self.assertTrue((self.install/'data/library.json').exists())
        with self.assertRaises(app.ApiError) as caught:
            desk.get_file('stable-seed-id')
        self.assertEqual(caught.exception.status,404)

    def test_move_installation_rebases_audio_and_rescan_preserves_ids(self):
        self.make_seed()
        first = app.AudioDesk(self.install/'data',self.install,app_dir=self.install)
        first.profile['assignments']['ui.click']['status']='approved'
        first.persist_profile()
        moved = self.root / 'Moved Tool'
        shutil.copytree(self.install,moved)
        reopened = app.AudioDesk(moved/'data',moved,app_dir=moved)
        self.assertEqual(reopened.profile['assignments']['ui.click']['status'],'approved')
        self.assertEqual(reopened.library['stable-seed-id']['path'],str((moved/'library/click.wav').resolve()))
        reopened.ffprobe=None
        reopened.scan(str(moved/'library'))
        self.assertEqual(set(reopened.library),{'stable-seed-id'})
        self.assertEqual(reopened.library['stable-seed-id']['duration'],.1)

    def test_new_seed_never_overwrites_existing_review(self):
        self.make_seed()
        first = app.AudioDesk(self.install/'data',self.install,app_dir=self.install)
        first.profile['name']='Local human review'
        first.profile['assignments']['ui.click']['status']='approved'
        first.profile['assignments']['ui.click']['note']='keep me'
        first.persist_profile()
        original=(self.install/'data/working.json').read_bytes()
        app.atomic_json(self.install/'seed_profile.json',{'schema_version':1,'name':'New default','assignments':{}})
        reopened=app.AudioDesk(self.install/'data',self.install,app_dir=self.install)
        self.assertEqual((self.install/'data/working.json').read_bytes(),original)
        self.assertEqual(reopened.profile['name'],'Local human review')
        self.assertEqual(reopened.profile['assignments']['ui.click']['note'],'keep me')

    def test_health_endpoint_does_not_read_the_catalog(self):
        desk=app.AudioDesk(self.install/'data',self.install,app_dir=self.install)
        (self.install/'catalog.json').write_text('invalid now',encoding='utf-8')
        server=app.DeskServer(('127.0.0.1',0),desk)
        thread=threading.Thread(target=server.serve_forever,daemon=True)
        thread.start()
        try:
            connection=http.client.HTTPConnection('127.0.0.1',server.server_port,timeout=5)
            connection.request('GET','/api/health')
            response=connection.getresponse()
            payload=json.loads(response.read())
            connection.close()
            self.assertEqual(response.status,200)
            self.assertEqual(payload['tool'],'MessControl Audio Desk')
            self.assertEqual(payload['data_dir'],str(desk.data_dir))
            self.assertEqual(payload['app_dir'],str(self.install.resolve()))
            self.assertTrue(payload['instance_id'])
        finally:
            server.shutdown()
            server.server_close()
            thread.join(2)

    def test_cli_default_state_is_next_to_explicit_installation(self):
        output=io.StringIO()
        with redirect_stdout(output):
            result=app.main(['--app-dir',str(self.install),'--inspect'])
        self.assertEqual(result,0)
        self.assertEqual(json.loads(output.getvalue())['data_dir'],str(self.install/'data'))
        self.assertTrue((self.install/'data/working.json').is_file())

    def test_local_paths_allow_other_drives_without_writing_to_them(self):
        if os.name!='nt':
            self.skipTest('drive paths are a Windows product behavior')
        # Merely resolving a path must not create any files or require drive E.
        self.assertEqual(app.e_path('C:/Windows').drive.casefold(),'c:')


if __name__=='__main__':
    unittest.main(verbosity=2)
