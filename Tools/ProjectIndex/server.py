"""Local stdio MCP for saved MessControl assets. No third-party Python packages."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import sys
import threading
import time
import traceback
import uuid

from index import AssetIndex, process_lock, utc_now, write_json

OBJECT_PATH = {"type": "string", "description": "Full /Game package or object path, e.g. /Game/Data/DA_Equipment.DA_Equipment"}
PAGE = {"limit": {"type": "integer", "minimum": 1, "maximum": 100, "default": 30},
        "offset": {"type": "integer", "minimum": 0, "default": 0}}


def process_alive(pid):
    if not isinstance(pid, int) or pid <= 0:
        return False
    if pid == os.getpid():
        return True
    if os.name == "nt":
        import ctypes
        from ctypes import wintypes
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
        kernel.OpenProcess.restype = wintypes.HANDLE
        handle = kernel.OpenProcess(0x1000, False, pid)  # Query only; never signal/terminate the owner.
        if not handle:
            return ctypes.get_last_error() == 5  # Access denied: conservatively treat it as active.
        kernel.GetExitCodeProcess.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
        kernel.GetExitCodeProcess.restype = wintypes.BOOL
        kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        try:
            code = wintypes.DWORD()
            return bool(kernel.GetExitCodeProcess(handle, ctypes.byref(code))) and code.value == 259
        finally:
            kernel.CloseHandle(handle)
    try:
        os.kill(pid, 0)
        return True
    except ProcessLookupError:
        return False
    except PermissionError:
        return True


def tool(name, description, properties=None, required=None, read_only=True):
    return {"name": name, "description": description,
            "inputSchema": {"type": "object", "properties": properties or {}, "required": required or [], "additionalProperties": False},
            "annotations": {"readOnlyHint": read_only, "destructiveHint": False, "openWorldHint": False}}


TOOLS = [
    tool("asset_status", "Check catalog size, snapshot freshness, C++ build state and background refresh job. Always check freshness before absence/impact claims."),
    tool("asset_search", "Search catalog paths/classes and exported properties, graph nodes and function references. Unexported assets contain metadata only. Results include freshness and pagination.",
         {"query": {"type": "string", "default": ""}, "asset_class": {"type": "string", "default": ""}, **PAGE}),
    tool("asset_read", "Read effective saved asset properties, Blueprint CDO defaults, Blueprint/Niagara graph nodes/pins/links, or subobjects. Stale data is refused and refresh is queued automatically when possible. Native non-reflected payloads and external graphs are explicitly excluded.",
         {"path": OBJECT_PATH, "section": {"type": "string", "enum": ["summary", "properties", "class_defaults", "graphs", "subobjects", "data_table"], "default": "summary"},
          "graph": {"type": "string"}, "property_name": {"type": "string"}, "auto_refresh": {"type": "boolean", "default": True}, **PAGE}, ["path"]),
    tool("asset_references", "Find inbound/outbound package, UObject, class and Blueprint function references. Cross-source evidence includes freshness. A missing result is never proof of no usages; use codebase-memory-mcp and source checks too.",
         {"target": {"type": "string", "description": "Asset/native object path or C++ symbol, e.g. UMCEquipmentProfile or UMCInventoryComponent::ServerSelect"}, "direction": {"type": "string", "enum": ["inbound", "outbound"], "default": "inbound"}, **PAGE}, ["target"]),
    tool("asset_refresh", "Start a background read-only headless Unreal export. Refreshes catalog and stale cached snapshots, or requested assets. Can build the editor target first if C++ changed. Does not modify/save asset packages. Poll asset_job_status until complete before using the results.",
         {"assets": {"type": "array", "items": OBJECT_PATH}, "export_all": {"type": "boolean", "default": False}, "build": {"type": "boolean", "default": True}}, read_only=False),
    tool("asset_job_status", "Read the running/completed refresh job, result or error and log paths.", {"job_id": {"type": "string"}}),
]


class Server:
    def __init__(self, index, auto_watch=True):
        self.index = index
        self.job = None
        self.jobs = {}
        self.job_lock = threading.Lock()
        self.stop = threading.Event()
        self.auto_watch = auto_watch

    def job_status(self, job_id=None):
        with self.job_lock:
            if job_id in self.jobs:
                return dict(self.jobs[job_id])
            if job_id or self.job is None:
                if job_id and (len(job_id) != 32 or any(c not in "0123456789abcdef" for c in job_id)):
                    raise ValueError("Unknown job id: " + job_id)
                if not job_id:
                    active = self.index.cache / "active_job.json"
                    if active.exists():
                        owner = json.loads(active.read_text(encoding="utf-8"))
                        if process_alive(owner.get("owner_pid")):
                            job_id = owner["id"]
                path = self.index.cache / "jobs" / (job_id + ".json") if job_id else self.index.cache / "last_job.json"
                if job_id and not path.exists():
                    raise ValueError("Unknown job id: " + job_id)
                value = json.loads(path.read_text(encoding="utf-8")) if path.exists() else {"status": "idle"}
                if value.get("status") == "running" and not process_alive(value.get("owner_pid")):
                    value["status"] = "interrupted"
                    value["hint"] = "Previous server exited before recording completion. Check freshness and retry."
            else:
                value = dict(self.job)
            if job_id and value.get("id") != job_id:
                raise ValueError("Unknown job id: " + job_id)
            return value

    def record_job(self, job):
        write_json(self.index.cache / "jobs" / (job["id"] + ".json"), job)
        write_json(self.index.cache / "last_job.json", job)

    def refresh(self, assets=None, export_all=False, build=True):
        with self.job_lock:
            if self.job and self.job["status"] == "running":
                return {"job_id": self.job["id"], "status": "running", "already_running": True,
                        "hint": "Wait for this job, then retry your requested asset; concurrent requests are not silently merged."}
            export_lock = process_lock(self.index.cache / "refresh.lock")
            try:
                export_lock.__enter__()
            except RuntimeError:
                active = self.index.cache / "active_job.json"
                if active.exists():
                    owner = json.loads(active.read_text(encoding="utf-8"))
                    if process_alive(owner.get("owner_pid")):
                        return {"job_id": owner["id"], "status": "running", "already_running": True,
                                "hint": "Another MCP server is refreshing this project. Poll this job, then retry your requested assets; requests are not merged."}
                raise
            self.job = {"id": uuid.uuid4().hex, "status": "running", "started_utc": utc_now(),
                        "owner_pid": os.getpid(),
                        "assets": assets, "export_all": export_all, "build": build,
                        "log": str(self.index.cache / "export_console.log")}
            self.jobs[self.job["id"]] = self.job
            try:
                self.record_job(self.job)
                write_json(self.index.cache / "active_job.json", {"id": self.job["id"], "owner_pid": os.getpid()})
            except Exception:
                export_lock.__exit__()
                self.job["status"] = "failed"
                raise
            job_id = self.job["id"]

        def work():
            try:
                result = self.index.refresh(assets, export_all, build, _lock_held=True)
                fields = {"status": "complete", "result": result}
            except Exception as error:
                fields = {"status": "failed", "error": str(error)}
                traceback.print_exc(file=sys.stderr)
            try:
                with self.job_lock:
                    job = self.jobs[job_id]
                    job.update(fields, finished_utc=utc_now())
                    self.record_job(job)
                    (self.index.cache / "active_job.json").unlink(missing_ok=True)
            finally:
                export_lock.__exit__()

        threading.Thread(target=work, name="asset-refresh", daemon=False).start()
        return {"job_id": job_id, "status": "running"}

    def signature(self):
        h = __import__("hashlib").sha256()
        paths = [self.index.project / "MessControl.uproject"]
        for directory in ("Content", "Source", "Plugins", "Config", "Binaries"):
            paths.extend((self.index.project / directory).rglob("*"))
        for path in sorted(paths):
            if path.is_file() and path.suffix in {".uasset", ".umap", ".cpp", ".h", ".cs", ".ini", ".dll", ".uplugin", ".uproject"}:
                if "Intermediate" in path.relative_to(self.index.project).parts:
                    continue
                stat = path.stat()
                h.update(f"{path}:{stat.st_size}:{stat.st_mtime_ns}".encode())
        return h.hexdigest()

    def watch(self):
        baseline = None
        pending = None
        submitted = None
        retry_after = 0
        while not self.stop.wait(10):
            try:
                signature = self.signature()
                job = self.job_status()
                if submitted and job.get("status") != "running":
                    if job.get("status") == "complete":
                        baseline = submitted
                    else:
                        retry_after = time.monotonic() + 60
                    submitted = None
                if signature != pending:
                    pending = signature
                    continue  # Require two quiet polls; do not export half-written packages.
                if signature == baseline or job.get("status") == "running" or time.monotonic() < retry_after:
                    continue
                if self.index.build_current():
                    self.refresh(build=False)
                    submitted = signature
                # Changed C++ stays stale until an explicit refresh/build; never rebuild in the middle of editing.
            except Exception as error:
                print("Asset watcher: " + str(error), file=sys.stderr)

    def call(self, name, args):
        schemas = {t["name"]: t["inputSchema"] for t in TOOLS}
        if name not in schemas:
            raise ValueError("Unknown tool: " + name)
        schema = schemas[name]
        missing = set(schema["required"]) - args.keys()
        unknown = args.keys() - schema["properties"].keys()
        if missing or unknown:
            raise ValueError(f"Invalid arguments; missing={sorted(missing)}, unknown={sorted(unknown)}")
        for key in ("offset", "limit"):
            if key in args and (not isinstance(args[key], int) or args[key] < (1 if key == "limit" else 0)):
                raise ValueError(key + " must be a valid nonnegative page argument")
        if name == "asset_status":
            return {**self.index.status(), "auto_watch": self.auto_watch, "job": self.job_status()}
        if name == "asset_search":
            return self.index.search(**args)
        if name == "asset_references":
            return self.index.references(**args)
        if name == "asset_job_status":
            return self.job_status(**args)
        if name == "asset_refresh":
            return self.refresh(**args)
        if name == "asset_read":
            args = dict(args)
            auto_refresh = args.pop("auto_refresh", True)
            result = self.index.read(**args)
            if result.get("needs_refresh") and auto_refresh:
                if self.index.build_current():
                    result["refresh"] = self.refresh(assets=[args["path"]], build=False)
                else:
                    result["hint"] = "C++/configuration/build changed. Run asset_refresh(build=True), then poll asset_job_status."
            return result
        raise ValueError(name)

    def respond(self, message):
        request_id = message.get("id")
        method = message.get("method")
        if request_id is None:
            return None  # MCP notifications never receive a response.
        try:
            if method == "initialize":
                version = message.get("params", {}).get("protocolVersion", "2025-11-25")
                result = {"protocolVersion": version, "capabilities": {"tools": {"listChanged": False}},
                          "serverInfo": {"name": "messcontrol-assets", "version": "1.0.0"},
                          "instructions": "Saved Unreal project asset evidence. Use freshness and coverage fields. Await refresh completion. For refactors combine asset_references with codebase-memory graph and source/build/runtime validation. Never infer dead code or exhaustive impact from this index alone."}
            elif method == "ping":
                result = {}
            elif method == "tools/list":
                result = {"tools": TOOLS}
            elif method == "tools/call":
                params = message["params"]
                try:
                    data = self.call(params["name"], params.get("arguments", {}))
                    text = json.dumps(data, ensure_ascii=False)
                    if len(text.encode("utf-8")) > 150000:
                        raise ValueError("Result too large. Select a graph/property and use smaller limit/offset pages.")
                    result = {"content": [{"type": "text", "text": text}], "structuredContent": data, "isError": False}
                except Exception as error:
                    result = {"content": [{"type": "text", "text": str(error)}], "isError": True}
            else:
                return {"jsonrpc": "2.0", "id": request_id, "error": {"code": -32601, "message": "Method not found"}}
            return {"jsonrpc": "2.0", "id": request_id, "result": result}
        except Exception as error:
            return {"jsonrpc": "2.0", "id": request_id, "error": {"code": -32602, "message": str(error)}}

    def run(self):
        if self.auto_watch:
            threading.Thread(target=self.watch, name="asset-watcher", daemon=True).start()
        try:
            for line in sys.stdin.buffer:
                try:
                    message = json.loads(line)
                    response = self.respond(message)
                except (ValueError, TypeError) as error:
                    response = {"jsonrpc": "2.0", "id": None, "error": {"code": -32700, "message": str(error)}}
                if response is not None:
                    sys.stdout.buffer.write((json.dumps(response, ensure_ascii=False) + "\n").encode("utf-8"))
                    sys.stdout.buffer.flush()
        finally:
            self.stop.set()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--engine-root")
    parser.add_argument("--no-watch", action="store_true")
    args = parser.parse_args()
    Server(AssetIndex(args.project, args.engine_root), not args.no_watch).run()
