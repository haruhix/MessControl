"""Timing policies, trim-aware review warnings, and conservative suggestions."""
import copy
import os
from pathlib import Path
import tempfile
import unittest
import wave

from app import AudioDesk, atomic_json, timing_fit, timing_policy


STEP = {"id": "event.Step", "search_keywords": {"positive": ["step"]}, "timing_policy": {
    "min_duration": .04, "max_duration": .65, "ideal_min": .12, "ideal_max": .45,
    "mode": "one_shot", "reason": "Один короткий шаг."}}


class TimingFitTests(unittest.TestCase):
    def test_fallback_kinds_and_custom_legacy_ranges(self):
        for slot, expected in (({}, (.025, 3.5)), ({"kind": "ambience"}, (2, 900)),
                               ({"kind": "music"}, (30, 1800)), ({"type": "ambience"}, (2, 900)),
                               ({"kind": "", "type": "music"}, (30, 1800))):
            with self.subTest(slot=slot):
                policy = timing_policy(slot)
                self.assertEqual((policy["min_duration"], policy["max_duration"]), expected)
        custom = {"kind": "ambience", "search_keywords": {"min_duration": .3, "max_duration": 30}}
        self.assertEqual(timing_policy(custom)["min_duration"], .3)
        self.assertEqual(timing_policy(custom)["max_duration"], 30)
        self.assertEqual(timing_policy({"timing_policy": {"mode": "loop", "max_duration": 7}})["max_duration"], 7)

    def test_policy_precedence_and_legacy_fallback(self):
        slot = copy.deepcopy(STEP)
        slot["search_keywords"].update(min_duration=2, max_duration=10)
        self.assertEqual(timing_policy(slot)["max_duration"], .65)
        old_slot = {"search_keywords": {"min_duration": 2, "max_duration": 10}}
        self.assertFalse(timing_fit(old_slot, {"duration": 1.9})["fits"])
        self.assertTrue(timing_fit(old_slot, {"duration": 2})["fits"])
        self.assertFalse(timing_fit(old_slot, {"duration": 10.1})["fits"])

    def test_boundaries_unknowns_and_ideal_preference(self):
        for duration in (.04, .65):
            self.assertTrue(timing_fit(STEP, {"duration": duration})["fits"])
        self.assertEqual(timing_fit(STEP, {"duration": .039})["code"], "too_short")
        self.assertEqual(timing_fit(STEP, {"duration": .651})["code"], "too_long")
        for duration in (None, 0, float("nan"), float("inf")):
            self.assertEqual(timing_fit(STEP, {"duration": duration})["code"], "unknown_duration")
        self.assertGreater(timing_fit(STEP, {"duration": .2})["score"],
                           timing_fit(STEP, {"duration": .62})["score"])

    def test_actual_trim_and_shortened_source(self):
        self.assertTrue(timing_fit(STEP, {"duration": 12}, {"trim_start": 2, "trim_end": 2.3})["fits"])
        self.assertEqual(timing_fit(STEP, {"duration": 12}, {"trim_start": 2, "trim_end": None})["code"], "too_long")
        self.assertEqual(timing_fit(STEP, {"duration": 12}, {"trim_start": 2, "trim_end": 2.01})["code"], "too_short")
        self.assertEqual(timing_fit(STEP, {"duration": .5}, {"trim_start": 0, "trim_end": 2})["duration"], .5)
        self.assertEqual(timing_fit(STEP, {"duration": .5}, {"trim_start": 1})["code"], "invalid_trim")


class TimingDeskTests(unittest.TestCase):
    def setUp(self):
        scratch = Path("E:/CodexTemp/MessControlAudioDesk/tests")
        scratch.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix="timing-", dir=scratch)
        self.root = Path(self.temporary.name)
        self.base = self.root / "app"
        self.base.mkdir()
        atomic_json(self.base / "catalog.json", {"slots": [STEP]})
        self.desk = AudioDesk(self.root / "data", self.base)
        self.audio = self.root / "source.wav"
        with wave.open(str(self.audio), "wb") as stream:
            stream.setparams((1, 2, 8000, 0, "NONE", "not compressed"))
            stream.writeframes(b"\0\0" * 8000)

    def tearDown(self):
        os.environ["TEMP"] = os.environ["TMP"] = "E:/CodexTemp/MessControlAudioDesk"
        self.temporary.cleanup()

    def item(self, identifier, duration, name="step.wav"):
        self.desk.library[identifier] = {"id": identifier, "name": name, "duration": duration,
                                         "path": str(self.audio), "relative_path": "footsteps/" + name}
        return self.desk.library[identifier]

    def test_suggest_excludes_long_multitake_and_prefers_ideal(self):
        self.item("long", 8, "step step heavy.wav")
        self.item("near_limit", .62)
        self.item("ideal", .2)
        self.item("unknown", None)
        self.item("too_short", .01)
        result = self.desk.suggest()
        self.assertEqual(result["count"], 1)
        assignment = self.desk.profile["assignments"]["event.Step"]
        self.assertEqual(assignment["clips"][0]["file_id"], "ideal")
        self.assertIsNone(assignment["clips"][0]["trim_end"])
        self.assertEqual([clip["file_id"] for clip in assignment["alternatives"]], ["near_limit"])

    def test_manual_approval_survives_audit_export_and_suggestion(self):
        self.item("long", 8)
        response = self.desk.assign("event.Step", {"status": "approved", "note": "Мой выбор",
                "clips": [{"file_id": "long", "trim_start": 0, "trim_end": None}]})
        self.assertEqual(response["assignment"]["status"], "approved")
        self.assertEqual(response["timing_issues"][0]["code"], "too_long")
        before = copy.deepcopy(self.desk.profile)
        disk_before = self.desk.working_path.read_bytes()
        self.assertEqual(self.desk.suggest()["count"], 0)
        state = self.desk.state()
        exported = self.desk.export_profile()
        warning = state["timing_issues"][0]
        self.assertEqual((warning["slot_id"], warning["role"], warning["effective_duration"]), ("event.Step", "clip", 8))
        plan = exported["import_plan"]["approved"][0]
        self.assertEqual(plan["assignment"]["status"], "approved")
        self.assertEqual(plan["timing_warnings"], exported["timing_issues"])
        self.assertEqual(exported["import_plan"]["timing_issues"], exported["timing_issues"])
        self.assertEqual(self.desk.profile, before)
        self.assertEqual(self.desk.working_path.read_bytes(), disk_before)

    def test_audit_respects_manual_crop_and_flags_alternatives_separately(self):
        self.item("long", 8)
        self.item("bad_alternative", 1.2)
        self.desk.assign("event.Step", {"status": "approved",
            "clips": [{"file_id": "long", "trim_start": 2, "trim_end": 2.25}],
            "alternatives": [{"file_id": "bad_alternative"}]})
        issues = self.desk.state()["timing_issues"]
        self.assertEqual(len(issues), 1)
        self.assertEqual((issues[0]["file_id"], issues[0]["role"]), ("bad_alternative", "alternative"))
        self.assertEqual(issues[0]["effective_duration"], 1.2)


if __name__ == "__main__":
    unittest.main(verbosity=2)
