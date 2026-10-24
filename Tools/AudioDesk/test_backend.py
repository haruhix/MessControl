"""Meaningful persistence and HTTP boundary checks; fixtures stay on E:."""
import concurrent.futures
import copy
import http.client
import json
import math
import os
from pathlib import Path
import struct
import tempfile
import threading
import unittest
import wave

from app import ApiError, AudioDesk, DeskServer, atomic_json


class BackendTests(unittest.TestCase):
    def setUp(self):
        scratch = Path("E:/CodexTemp/MessControlAudioDesk/tests")
        scratch.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix="backend-", dir=scratch)
        self.root = Path(self.temporary.name)
        self.base = self.root / "app"
        (self.base / "web").mkdir(parents=True)
        (self.base / "web" / "index.html").write_text("Audio Desk test", encoding="utf-8")
        (self.base / "private.txt").write_text("private", encoding="utf-8")
        atomic_json(self.base / "catalog.json", {"version": 1, "groups": [], "slots": [
            {"id": "ui.click", "keywords": ["click"]}, {"id": "impact.soft", "keywords": ["impact"]}]})
        self.audio_root = self.root / "sounds"
        self.audio_root.mkdir()
        self.audio = self.audio_root / "click.wav"
        self.audio_bytes = self.make_wave(self.audio)
        self.desk = AudioDesk(self.root / "data", self.base)
        self.desk.ffprobe = None
        self.desk.scan(str(self.audio_root))
        self.identifier = next(iter(self.desk.library))
        self.server = DeskServer(("127.0.0.1", 0), self.desk)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()

    @staticmethod
    def make_wave(path):
        with wave.open(str(path), "wb") as stream:
            stream.setparams((1, 2, 8000, 0, "NONE", "not compressed"))
            stream.writeframes(b"".join(struct.pack("<h", int(math.sin(index * .2) * 8000)) for index in range(800)))
        return path.read_bytes()

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(2)
        # AudioDesk changes process-local TEMP; avoid leaving it on a removed fixture.
        os.environ["TEMP"] = os.environ["TMP"] = "E:/CodexTemp/MessControlAudioDesk"
        self.temporary.cleanup()

    def request(self, method, path, payload=None, headers=None):
        connection = http.client.HTTPConnection("127.0.0.1", self.server.server_port, timeout=15)
        if payload is not None and not isinstance(payload, (bytes, str)):
            payload = json.dumps(payload).encode()
        connection.request(method, path, body=payload, headers=headers or {})
        response = connection.getresponse()
        result = response.status, dict(response.headers), response.read()
        connection.close()
        return result

    def assignment(self, status="suggested"):
        return {"clips": [{"file_id": self.identifier, "gain_db": -3, "trim_start": 0, "trim_end": None}], "status": status, "note": "choice"}

    def test_scan_metadata_is_persistent(self):
        self.assertEqual(len(self.desk.library), 1)
        record = self.desk.library[self.identifier]
        self.assertAlmostEqual(record["duration"], .1)
        self.assertEqual(record["channels"], 1)
        reopened = AudioDesk(self.root / "data", self.base)
        self.assertEqual(reopened.library[self.identifier]["path"], str(self.audio))
        status, _, body = self.request("GET", "/api/state")
        self.assertEqual(status, 200)
        self.assertEqual(json.loads(body)["project_write_enabled"], False)

    def test_byte_ranges_and_head(self):
        url = "/api/audio/" + self.identifier
        status, headers, body = self.request("GET", url, headers={"Range": "bytes=10-29"})
        self.assertEqual(status, 206)
        self.assertEqual(body, self.audio_bytes[10:30])
        self.assertEqual(headers["Content-Range"], f"bytes 10-29/{len(self.audio_bytes)}")
        status, _, body = self.request("GET", url, headers={"Range": "bytes=-20"})
        self.assertEqual((status, body), (206, self.audio_bytes[-20:]))
        status, _, _ = self.request("GET", url, headers={"Range": "bytes=999999-"})
        self.assertEqual(status, 416)
        status, headers, body = self.request("HEAD", url)
        self.assertEqual(status, 200)
        self.assertEqual(body, b"")
        self.assertEqual(int(headers["Content-Length"]), len(self.audio_bytes))

    def test_invalid_assignment_does_not_replace_work(self):
        self.desk.assign("ui.click", self.assignment("approved"))
        before = copy.deepcopy(self.desk.profile)
        bad = self.assignment()
        bad["clips"][0]["gain_db"] = float("nan")
        status, _, _ = self.request("POST", "/api/assign", {"slot_id": "ui.click", "assignment": bad})
        self.assertEqual(status, 400)
        self.assertEqual(self.desk.profile, before)
        self.assertTrue((self.root / "data" / "working.json.bak").exists())
        self.assertEqual(AudioDesk(self.root / "data", self.base).profile["assignments"]["ui.click"]["status"], "approved")

    def test_export_separates_approval_and_records_file_hash(self):
        self.desk.assign("ui.click", self.assignment("approved"))
        self.desk.assign("impact.soft", self.assignment("suggested"))
        status, _, body = self.request("GET", "/api/profile/export")
        self.assertEqual(status, 200)
        exported = json.loads(body)
        self.assertFalse(exported["import_plan"]["applied_to_unreal"])
        self.assertEqual([item["slot_id"] for item in exported["import_plan"]["approved"]], ["ui.click"])
        self.assertEqual([item["slot_id"] for item in exported["import_plan"]["proposed"]], ["impact.soft"])
        self.assertEqual(len(exported["files"][0]["sha256"]), 64)
        status, _, body = self.request("POST", "/api/profile/export", {"name": "Review"})
        self.assertEqual(status, 200)
        written = json.loads(body)
        self.assertEqual(written["approved_count"], 1)
        self.assertTrue(Path(written["path"]).is_relative_to(self.desk.data_dir / "exports"))
        self.assertEqual(json.loads(Path(written["path"]).read_text(encoding="utf-8"))["files"][0]["sha256"], exported["files"][0]["sha256"])
        saved = self.desk.save_profile("Мой профиль")
        self.assertTrue(Path(saved["path"]).is_file())
        self.desk.assign("ui.click", self.assignment("rejected"))
        loaded = self.desk.load_profile("Мой профиль")
        self.assertEqual(loaded["profile"]["assignments"]["ui.click"]["status"], "approved")

    def test_profile_import_validation_is_atomic(self):
        self.desk.assign("ui.click", self.assignment())
        before = copy.deepcopy(self.desk.profile)
        invalid = {"schema_version": 1, "assignments": {"ui.click": {"status": "approved", "clips": [{"file_id": "missing"}]}}}
        status, _, _ = self.request("POST", "/api/profile/import", invalid)
        self.assertEqual(status, 400)
        self.assertEqual(before, self.desk.profile)
        invalid = copy.deepcopy(before)
        invalid["assignments"]["ui.click"]["alternatives"] = [{"file_id": "unknown_alternative"}]
        status, _, _ = self.request("POST", "/api/profile/import", invalid)
        self.assertEqual(status, 400)
        self.assertEqual(before, self.desk.profile)

    def test_suggestions_apply_duration_exclusion_and_preserve_gaps(self):
        atomic_json(self.base / "catalog.json", {"slots": [
            {"id": "ui.click", "search_keywords": {"positive": ["click"], "negative": ["long"], "min_duration": .03, "max_duration": .2}},
            {"id": "impact.soft", "search_keywords": {"positive": ["click"], "min_duration": 1, "max_duration": 10}}]})
        response = self.desk.suggest()
        self.assertEqual(response["count"], 1)
        self.assertEqual(self.desk.profile["assignments"]["ui.click"]["clips"][0]["file_id"], self.identifier)
        self.assertNotIn("impact.soft", self.desk.profile["assignments"])
        self.desk.profile["assignments"]["ui.click"] = {"status": "empty", "clips": [], "selection_method": "curated_gap"}
        self.assertEqual(self.desk.suggest()["count"], 0)

    def test_upload_uses_basename_and_persists(self):
        status, _, body = self.request("POST", "/api/upload", self.audio_bytes, {"X-Filename": "..%2F..%2Fnew.wav", "Content-Type": "application/octet-stream"})
        self.assertEqual(status, 200)
        record = json.loads(body)["files"][0]
        self.assertTrue(Path(record["path"]).is_relative_to(self.desk.upload_dir))
        self.assertEqual(record["name"], "new.wav")
        self.assertEqual(len(record["sha256"]), 64)

    def test_request_and_path_boundaries(self):
        status, _, _ = self.request("GET", "/%2e%2e/private.txt")
        self.assertEqual(status, 404)
        status, _, _ = self.request("GET", "/api/state", headers={"Host": "evil.example:8765"})
        self.assertEqual(status, 403)
        status, _, _ = self.request("POST", "/api/suggest", {}, {"Origin": "https://evil.example"})
        self.assertEqual(status, 403)
        status, _, _ = self.request("POST", "/api/library/scan", {"path": "C:/Windows"})
        self.assertEqual(status, 400)

    def test_parallel_mutations_keep_both_assignments(self):
        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
            outcomes = list(pool.map(lambda slot: self.desk.assign(slot, self.assignment()), ["ui.click", "impact.soft"]))
        self.assertTrue(all(item["ok"] for item in outcomes))
        reopened = AudioDesk(self.root / "data", self.base)
        self.assertEqual(set(reopened.profile["assignments"]), {"ui.click", "impact.soft"})


if __name__ == "__main__":
    unittest.main(verbosity=2)
