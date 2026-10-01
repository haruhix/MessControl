"""Meaningful contracts: stale snapshots, schema invalidation, references and MCP transport."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import threading
import unittest
from unittest.mock import Mock, patch

from index import AssetIndex, digest, write_json
from server import Server


class AssetIndexTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.project = Path(self.temp.name)
        (self.project / "Source").mkdir()
        (self.project / "Content/Data").mkdir(parents=True)
        (self.project / "MessControl.uproject").write_text('{"EngineAssociation":"5.8"}')
        (self.project / "Source/Profile.h").write_text("float Mass = 28;")
        self.index = AssetIndex(self.project)
        self.index.stamp_build()
        self.path = "/Game/Data/DA_Food.DA_Food"
        self.asset_file = self.project / "Content/Data/DA_Food.uasset"
        self.asset_file.write_bytes(b"fixture-asset")
        self.asset = {"path": self.path, "package": "/Game/Data/DA_Food", "name": "DA_Food",
                      "class": "/Script/MessControl.MCFoodProfile", "file": str(self.asset_file),
                      "snapshot_file": "snapshots/Game/Data/DA_Food.DA_Food.json", "tags": {},
                      "dependencies": ["/Game/Meshes/SM_Food", "/Script/MessControl"]}
        self.snapshot = {"asset": self.asset, "schema_version": 1, "captured_utc": "2026-09-30T00:00:00Z",
                         "properties": {"fields": {"Mass": {"value": 28, "text": "28"}}, "types": {"Mass": "float"}, "diagnostics": []},
                         "graphs": [{"path": self.path + ":EventGraph", "class": "/Script/Engine.EdGraph",
                                     "nodes": [{"resolved_function": "/Script/MessControl.MCInventoryComponent.ServerSelect", "pins": []}]}],
                         "subobjects": [], "coverage": {"native_binary_payloads_decoded": False}}
        write_json(self.index.cache / self.asset["snapshot_file"], self.snapshot)
        self.catalog([self.asset])
        self.index.ingest({self.path: digest(self.asset_file)}, self.index.schema_inputs())

    def tearDown(self):
        self.temp.cleanup()

    def catalog(self, assets):
        write_json(self.index.cache / "catalog.json", {"schema_version": 1, "engine_version": "5.8.1",
                   "captured_utc": "2026-09-30T00:00:00Z", "assets": assets, "errors": []})

    def test_asset_change_refuses_old_values(self):
        self.assertEqual(self.index.read(self.path)["freshness"], "current")
        self.asset_file.write_bytes(b"changed-asset")
        data = self.index.read(self.path, "properties")
        self.assertEqual(data["freshness"], "asset_changed")
        self.assertNotIn("data", data)

    def test_cpp_change_invalidates_unchanged_asset(self):
        (self.project / "Source/Profile.h").write_text("float Mass = 32;")
        self.assertEqual(self.index.read(self.path)["freshness"], "schema_changed")
        self.assertFalse(self.index.build_current())

    def dependency_fixture(self):
        assets = [self.asset]
        for name in ("Parent", "Grandparent"):
            filename = self.project / ("Content/Data/DA_" + name + ".uasset")
            filename.write_bytes(name.encode())
            package = "/Game/Data/DA_" + name
            assets.append({**self.asset, "path": package + ".DA_" + name, "package": package,
                           "file": str(filename), "dependencies": []})
        assets[0]["dependencies"] = [assets[1]["package"]]
        assets[1]["dependencies"] = [assets[2]["package"]]
        assets[2]["dependencies"] = [assets[0]["package"]]  # Cycles must not recurse forever.
        self.catalog(assets)
        self.index.ingest({self.path: digest(self.asset_file)}, self.index.schema_inputs())
        return Path(assets[2]["file"])

    def test_parent_dependency_change_invalidates_unchanged_child(self):
        dependency = self.dependency_fixture()
        self.assertEqual(self.index.read(self.path)["freshness"], "current")
        dependency.write_bytes(b"changed inherited defaults")
        data = self.index.read(self.path, "class_defaults")
        self.assertEqual(data["freshness"], "dependency_changed")
        self.assertNotIn("data", data)

    def test_dependency_change_during_export_is_rejected(self):
        dependency = self.dependency_fixture()
        expected = {self.path: self.index.dependency_state().fingerprint(self.asset["package"])}
        dependency.write_bytes(b"changed during export")
        with self.assertRaisesRegex(RuntimeError, "dependencies changed during export"):
            self.index.ingest({self.path: digest(self.asset_file)}, self.index.schema_inputs(), expected_dependencies=expected)

    def test_current_effective_properties_and_function_references(self):
        field = self.index.read(self.path, "properties", property_name="Mass")["data"]
        self.assertEqual(field["value"], 28)
        refs = self.index.references("serverselect")
        self.assertEqual(refs["total"], 1)
        self.assertEqual(refs["references"][0]["kind"], "function")
        self.assertEqual(refs["references"][0]["source_freshness"], "current")
        self.assertEqual(self.index.references("/Game/Data/DA_Food", "outbound")["references"][0]["source"], self.path)
        self.assertEqual(self.index.references(self.path)["total"], 0)  # No fabricated self-reference.
        self.assertGreater(self.index.references("UMCFoodProfile")["total"], 0)
        self.assertEqual(self.index.references("UMCInventoryComponent::ServerSelect")["total"], 1)

    def test_failed_export_preserves_old_evidence_with_diagnostic(self):
        self.asset_file.write_bytes(b"new-data-but-export-failed")
        self.index.ingest({}, self.index.schema_inputs(), {self.path: "could not load asset"})
        data = self.index.read(self.path, "properties")
        self.assertEqual(data["freshness"], "asset_changed")
        self.assertEqual(data["export_error"], "could not load asset")
        self.assertNotIn("data", data)
        self.catalog([self.asset])
        self.index.ingest({}, self.index.schema_inputs())
        self.assertEqual(self.index.status()["export_errors"][self.path], "could not load asset")

    def test_catalog_deletion_prunes_assets_and_edges(self):
        self.catalog([])
        self.index.ingest({}, self.index.schema_inputs())
        self.assertEqual(self.index.status()["catalog_assets"], 0)
        self.assertEqual(self.index.references("serverselect")["total"], 0)

    def test_missing_and_unexported_assets_never_claim_full_coverage(self):
        self.asset_file.unlink()
        self.assertEqual(self.index.read(self.path)["freshness"], "source_missing")
        self.assertFalse(self.index.read(self.path)["catalog"].get("complete", False))

    def test_mcp_requests_notifications_and_invalid_tools(self):
        server = Server(self.index, auto_watch=False)
        self.assertIsNone(server.respond({"jsonrpc": "2.0", "method": "notifications/initialized"}))
        message = {"jsonrpc": "2.0", "id": 1, "method": "tools/call",
                   "params": {"name": "asset_read", "arguments": {"path": self.path, "auto_refresh": False}}}
        response = server.respond(message)
        self.assertFalse(response["result"]["isError"])
        self.assertEqual(response["result"]["structuredContent"]["freshness"], "current")
        message["params"]["name"] = "unknown"
        self.assertTrue(server.respond(message)["result"]["isError"])

    def test_stdio_transport_has_only_json_on_stdout(self):
        messages = [{"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"protocolVersion": "2025-11-25"}},
                    {"jsonrpc": "2.0", "method": "notifications/initialized"},
                    {"jsonrpc": "2.0", "id": 2, "method": "tools/list"}]
        process = subprocess.run([sys.executable, str(Path(__file__).with_name("server.py")), "--project", str(self.project), "--no-watch"],
                                 input="".join(json.dumps(m) + "\n" for m in messages), text=True, capture_output=True, timeout=15)
        self.assertEqual(process.returncode, 0, process.stderr)
        replies = [json.loads(line) for line in process.stdout.splitlines()]
        self.assertEqual([r["id"] for r in replies], [1, 2])
        self.assertEqual(len(replies[1]["result"]["tools"]), 6)

    def test_watcher_waits_for_settled_changes_and_tracks_success(self):
        server = Server(self.index)
        server.stop = Mock()
        server.stop.wait.side_effect = [False] * 6 + [True]
        server.signature = Mock(side_effect=["A", "A", "A", "B", "B", "B"])

        def complete(**kwargs):
            server.job = {"status": "complete"}

        server.refresh = Mock(side_effect=complete)
        server.watch()
        self.assertEqual(server.refresh.call_count, 2)
        server.refresh.assert_called_with(build=False)

    def test_completed_job_remains_pollable_after_next_refresh(self):
        server = Server(self.index, auto_watch=False)

        def wait_complete(job_id):
            until = time.monotonic() + 3
            while time.monotonic() < until:
                value = server.job_status(job_id)
                if value["status"] != "running":
                    self.assertEqual(value["status"], "complete", value)
                    return
                time.sleep(0.01)
            self.fail("Refresh did not finish")

        with patch.object(self.index, "refresh", return_value={"exported": 1}):
            first = server.refresh()["job_id"]
            wait_complete(first)
            second = server.refresh()["job_id"]
            wait_complete(second)
        self.assertEqual(server.job_status(first)["result"]["exported"], 1)
        restarted = Server(self.index, auto_watch=False)
        self.assertEqual(restarted.job_status(first)["status"], "complete")

    def test_two_mcp_servers_join_the_same_active_export(self):
        first, second = Server(self.index, False), Server(self.index, False)
        release = threading.Event()

        def blocked_export(*args, **kwargs):
            release.wait(3)
            return {"exported": 1}

        with patch.object(self.index, "refresh", side_effect=blocked_export) as refresh:
            job_id = first.refresh()["job_id"]
            try:
                joined = second.refresh(assets=[self.path])
                self.assertEqual(joined["job_id"], job_id)
                self.assertTrue(joined["already_running"])
                self.assertEqual(second.job_status(job_id)["status"], "running")
            finally:
                release.set()
            until = time.monotonic() + 3
            while first.job_status(job_id)["status"] == "running" and time.monotonic() < until:
                time.sleep(0.01)
            self.assertEqual(second.job_status(job_id)["status"], "complete")
            self.assertEqual(refresh.call_count, 1)

    def test_watcher_defers_until_cpp_build_is_current(self):
        server = Server(self.index)
        server.stop = Mock()
        server.stop.wait.side_effect = [False] * 3 + [True]
        server.signature = Mock(return_value="changed")
        server.refresh = Mock()
        with patch.object(self.index, "build_current", return_value=False):
            server.watch()
        server.refresh.assert_not_called()


if __name__ == "__main__":
    unittest.main()
