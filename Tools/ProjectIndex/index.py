"""Saved-asset index, freshness checks and headless Unreal export. Standard library only."""
from __future__ import annotations

import hashlib
import json
import os
import re
from pathlib import Path
import sqlite3
import subprocess
import sys
import time
from datetime import datetime, timezone
from contextlib import contextmanager, nullcontext

SCHEMA_VERSION = 1
ASSET_METADATA = """path,package,name,class,file,source_hash,schema_hash,dependency_hash,
                    captured_utc,export_error,(snapshot_json IS NOT NULL) AS snapshot_json"""
BOOTSTRAP_ASSETS = [
    "/Game/Data/DA_Equipment.DA_Equipment",
    "/Game/Gameplay/UI/WBP_GameplayHUD.WBP_GameplayHUD",
    "/Game/Gameplay/VFX/NS_BrushFoam.NS_BrushFoam",
]


def utc_now():
    return datetime.now(timezone.utc).isoformat()


def digest(path: Path):
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def write_json(path: Path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_suffix(path.suffix + ".tmp")
    temp.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding="utf-8")
    os.replace(temp, path)


def engine_root(project: Path, explicit=None):
    candidates = [explicit, os.getenv("UE_ROOT"), os.getenv("UE_ENGINE_ROOT")]
    manifest = Path(os.getenv("PROGRAMDATA", "C:/ProgramData")) / "Epic/UnrealEngineLauncher/LauncherInstalled.dat"
    version = json.loads((project / "MessControl.uproject").read_text(encoding="utf-8"))["EngineAssociation"]
    if manifest.exists():
        for row in json.loads(manifest.read_text(encoding="utf-8"))["InstallationList"]:
            if row.get("AppName") == "UE_" + version:
                candidates.append(row["InstallLocation"])
    candidates.append("E:/UE/UE_" + version)
    for candidate in candidates:
        if candidate and (Path(candidate) / "Engine/Binaries/Win64/UnrealEditor-Cmd.exe").is_file():
            return Path(candidate).resolve()
    raise RuntimeError("Unreal Engine not found. Set UE_ROOT or pass --engine-root.")


class DependencyState:
    """Hash saved transitive package dependencies, including inherited Blueprint defaults."""
    def __init__(self, assets):
        self.files = {}
        self.links = {}
        self.hashes = {}
        self.fingerprints = {}
        for asset in assets:
            package = asset["package"]
            self.files.setdefault(package, set()).add(asset["file"])
            self.links.setdefault(package, set()).update(asset["dependencies"])

    def file_hash(self, filename):
        if filename not in self.hashes:
            path = Path(filename)
            self.hashes[filename] = digest(path) if path.is_file() else "missing"
        return self.hashes[filename]

    def fingerprint(self, package):
        if package not in self.fingerprints:
            visited, pending = set(), [package]
            while pending:
                current = pending.pop()
                if current in visited:
                    continue
                visited.add(current)
                pending.extend(p for p in self.links.get(current, ()) if p.startswith("/Game/"))
            evidence = [(p, [(f, self.file_hash(f)) for f in sorted(self.files.get(p, ()))]) for p in sorted(visited)]
            self.fingerprints[package] = hashlib.sha256(json.dumps(evidence).encode()).hexdigest()
        return self.fingerprints[package]


class AssetIndex:
    def __init__(self, project: Path, engine=None):
        self.project = project.resolve()
        self.engine = engine
        self.cache = self.project / "Saved/ProjectIndex"
        self.cache.mkdir(parents=True, exist_ok=True)
        self.db_path = self.cache / "assets.sqlite3"
        with self.connect() as db:
            db.executescript("""
                CREATE TABLE IF NOT EXISTS assets (
                    path TEXT PRIMARY KEY, package TEXT, name TEXT, class TEXT,
                    file TEXT, snapshot_file TEXT, catalog_json TEXT,
                    snapshot_json TEXT, source_hash TEXT, schema_hash TEXT,
                    captured_utc TEXT, search_text TEXT, export_error TEXT, dependency_hash TEXT
                );
                CREATE TABLE IF NOT EXISTS dependencies (
                    source TEXT, target TEXT, kind TEXT,
                    PRIMARY KEY(source,target,kind)
                );
                CREATE INDEX IF NOT EXISTS dependency_target ON dependencies(target);
                CREATE TABLE IF NOT EXISTS metadata (key TEXT PRIMARY KEY, value TEXT);
            """)
            if "export_error" not in {r[1] for r in db.execute("PRAGMA table_info(assets)")}:
                db.execute("ALTER TABLE assets ADD COLUMN export_error TEXT")
            if "dependency_hash" not in {r[1] for r in db.execute("PRAGMA table_info(assets)")}:
                db.execute("ALTER TABLE assets ADD COLUMN dependency_hash TEXT")

    @contextmanager
    def connect(self):
        db = sqlite3.connect(self.db_path, timeout=30)
        db.row_factory = sqlite3.Row
        db.execute("PRAGMA journal_mode=WAL")
        try:
            with db:
                yield db
        finally:
            db.close()

    def schema_inputs(self):
        # Includes constructors, reflected declarations, config defaults and exporter implementation.
        paths = [self.project / "MessControl.uproject"]
        for directory in ("Source", "Plugins", "Config"):
            paths.extend(p for p in (self.project / directory).rglob("*")
                         if p.is_file() and p.suffix in {".h", ".cpp", ".cs", ".ini", ".uplugin"}
                         and not {"Binaries", "Intermediate"}.intersection(p.relative_to(self.project).parts))
        h = hashlib.sha256()
        for path in sorted(paths):
            h.update(path.relative_to(self.project).as_posix().encode())
            h.update(bytes.fromhex(digest(path)))
        return h.hexdigest()

    def binary_inputs(self):
        h = hashlib.sha256()
        dlls = list((self.project / "Binaries/Win64").glob("UnrealEditor-*.dll"))
        dlls += list((self.project / "Plugins").glob("*/Binaries/Win64/UnrealEditor-*.dll"))
        for path in sorted(dlls):
            h.update(path.relative_to(self.project).as_posix().encode())
            h.update(bytes.fromhex(digest(path)))
        return h.hexdigest()

    def stamp_build(self):
        write_json(self.cache / "build_state.json", {
            "schema_hash": self.schema_inputs(), "binary_hash": self.binary_inputs(), "built_utc": utc_now()
        })

    def build_current(self):
        path = self.cache / "build_state.json"
        if not path.exists():
            return False
        state = json.loads(path.read_text(encoding="utf-8"))
        return state["schema_hash"] == self.schema_inputs() and state["binary_hash"] == self.binary_inputs()

    def dependency_state(self):
        with self.connect() as db:
            return DependencyState([json.loads(r[0]) for r in db.execute("SELECT catalog_json FROM assets")])

    def freshness(self, row, schema_hash=None, build_current=None, dependencies=None):
        path = Path(row["file"])
        if not path.is_file():
            return "source_missing"
        if not row["snapshot_json"]:
            return "not_exported"
        if row["schema_hash"] != (schema_hash or self.schema_inputs()):
            return "schema_changed"
        dependencies = dependencies or self.dependency_state()
        if row["source_hash"] != dependencies.file_hash(row["file"]):
            return "asset_changed"
        if row["dependency_hash"] != dependencies.fingerprint(row["package"]):
            return "dependency_changed"
        if not (self.build_current() if build_current is None else build_current):
            return "build_changed"
        return "current"

    def resolve(self, path, db, metadata_only=False):
        columns = ASSET_METADATA if metadata_only else "*"
        rows = db.execute(f"SELECT {columns} FROM assets WHERE path=? OR package=?", (path, path)).fetchall()
        if not rows:
            raise ValueError("Asset is not in the catalog. Refresh the catalog first: " + path)
        if len(rows) != 1:
            raise ValueError("Use the full object path; this package contains multiple assets: " + path)
        return rows[0]

    def status(self):
        current_schema = self.schema_inputs()
        current_build = self.build_current()
        dependencies = self.dependency_state()
        with self.connect() as db:
            rows = db.execute(f"SELECT {ASSET_METADATA} FROM assets").fetchall()
            exported = [r for r in rows if r["snapshot_json"]]
            states = {}
            for row in exported:
                state = self.freshness(row, current_schema, current_build, dependencies)
                states[state] = states.get(state, 0) + 1
            metadata = {r["key"]: json.loads(r["value"]) for r in db.execute("SELECT * FROM metadata")}
            errors = {r["path"]: r["export_error"] for r in rows if r["export_error"]}
        return {"project": str(self.project), "catalog_assets": len(rows), "exported_assets": len(exported),
                "snapshot_states": states, "build_current": current_build, "metadata": metadata,
                "export_errors": errors,
                "limitations": "Saved packages only. Unexported assets have catalog metadata only. Native payloads and visual/runtime behavior need Unreal inspection."}

    def search(self, query="", asset_class="", limit=30, offset=0):
        # Each token must match; path/class/property/function names and exported graph titles are searchable.
        terms = query.lower().split()
        clauses, params = [], []
        for term in terms:
            clauses.append("instr(lower(search_text),?) > 0")
            params.append(term)
        if asset_class:
            clauses.append("instr(lower(class),?) > 0")
            params.append(asset_class.lower())
        where = " WHERE " + " AND ".join(clauses) if clauses else ""
        with self.connect() as db:
            total = db.execute("SELECT COUNT(*) FROM assets" + where, params).fetchone()[0]
            rows = db.execute(f"SELECT {ASSET_METADATA} FROM assets" + where + " ORDER BY path LIMIT ? OFFSET ?",
                              params + [min(max(limit, 1), 100), max(offset, 0)]).fetchall()
        schema = self.schema_inputs()
        current_build = self.build_current()
        dependencies = self.dependency_state()
        return {"total": total, "offset": offset, "has_more": offset + len(rows) < total,
                "assets": [{"path": r["path"], "class": r["class"], "freshness": self.freshness(r, schema, current_build, dependencies)} for r in rows]}

    def read(self, path, section="summary", graph=None, property_name=None, offset=0, limit=30):
        with self.connect() as db:
            row = self.resolve(path, db)
        state = self.freshness(row)
        result = {"path": row["path"], "freshness": state, "captured_utc": row["captured_utc"],
                  "source_hash": row["source_hash"], "schema_hash": row["schema_hash"]}
        result["dependency_hash"] = row["dependency_hash"]
        if row["export_error"]:
            result["export_error"] = row["export_error"]
        if state != "current":
            result.update({"needs_refresh": True, "catalog": json.loads(row["catalog_json"]),
                           "hint": "Run asset_refresh and wait for asset_job_status before treating snapshot data as current."})
            return result
        snapshot = json.loads(row["snapshot_json"])
        result["coverage"] = snapshot["coverage"]
        if section == "summary":
            result["asset"] = snapshot["asset"]
            result["parent_class"] = snapshot.get("parent_class")
            result["generated_class"] = snapshot.get("generated_class")
            result["cpp_class"] = snapshot.get("cpp_class")
            result["cpp_parent_class"] = snapshot.get("cpp_parent_class")
            result["table_rows"] = len(snapshot.get("data_table", []))
            result["properties"] = list(snapshot["properties"]["fields"])
            result["graphs"] = [{"path": g["path"], "class": g["class"], "nodes": len(g["nodes"])} for g in snapshot["graphs"]]
            objects = snapshot["subobjects"]
            selected = objects[offset:offset + min(max(limit, 1), 100)]
            result["subobjects"] = [{"path": o["path"], "class": o["class"]} for o in selected]
            result["subobjects_total"] = len(objects)
            result["subobjects_has_more"] = offset + len(selected) < len(objects)
        elif section in {"properties", "class_defaults"}:
            data = snapshot.get(section, {"fields": {}, "types": {}, "diagnostics": []})
            if property_name:
                if property_name not in data["fields"]:
                    raise ValueError("Property not exported: " + property_name)
                result["data"] = {"name": property_name, "type": data["types"].get(property_name), **data["fields"][property_name]}
            else:
                names = sorted(data["fields"])
                selected = names[offset:offset + min(max(limit, 1), 100)]
                result["data"] = {"total": len(names), "has_more": offset + len(selected) < len(names),
                                  "fields": {n: data["fields"][n] for n in selected}, "diagnostics": data["diagnostics"]}
        elif section == "graphs":
            graphs = snapshot["graphs"]
            if graph:
                graphs = [g for g in graphs if g["path"] == graph or g["path"].endswith(":" + graph) or g["path"].endswith("." + graph)]
                if not graphs:
                    raise ValueError("Graph not found: " + graph)
            result["data"] = []
            for g in graphs:
                nodes = g["nodes"][offset:offset + min(max(limit, 1), 100)]
                result["data"].append({**g, "nodes": nodes, "total_nodes": len(g["nodes"]), "has_more": offset + len(nodes) < len(g["nodes"])})
        elif section == "data_table":
            rows = snapshot.get("data_table", [])
            result["row_struct"] = snapshot.get("row_struct")
            result["data"] = {"rows": rows[offset:offset + min(max(limit, 1), 100)], "total": len(rows), "has_more": offset + min(max(limit, 1), 100) < len(rows)}
        elif section == "subobjects":
            objects = snapshot["subobjects"]
            if property_name:
                objects = [o for o in objects if property_name.lower() in o["path"].lower() or property_name in o["properties"]["fields"]]
            result["data"] = {"objects": objects[offset:offset + min(max(limit, 1), 100)], "total": len(objects)}
        else:
            raise ValueError("Unknown section: " + section)
        return result

    def references(self, target, direction="inbound", limit=100, offset=0):
        # Catalog package dependencies and exported object/function references remain distinct evidence types.
        with self.connect() as db:
            needle = target
            if target.startswith("/Game/"):
                matches = db.execute("SELECT package FROM assets WHERE path=? OR package=?", (target, target)).fetchall()
                if matches:
                    needle = matches[0]["package"]
                    if direction == "outbound":
                        needle = db.execute("SELECT path FROM assets WHERE package=? LIMIT 1", (needle,)).fetchone()["path"]
            column = "target" if direction == "inbound" else "source"
            query = f"SELECT * FROM dependencies WHERE {column}=? OR instr(lower({column}),lower(?)) > 0 ORDER BY source,target,kind"
            # An exact object/package query avoids substring collisions; symbol queries are explicitly partial.
            if target.startswith("/"):
                query = f"SELECT * FROM dependencies WHERE {column}=? OR {column}=? ORDER BY source,target,kind"
                params = (needle, target)
            else:
                symbol = target.split("::", 1)[0]
                if len(symbol) > 2 and symbol[0] in "UAFS" and symbol[1].isupper():
                    symbol = symbol[1:]
                query = f"SELECT * FROM dependencies WHERE instr(lower({column}),lower(?)) > 0"
                params = [symbol]
                if "::" in target:
                    query += f" AND instr(lower({column}),lower(?)) > 0"
                    params.append(target.split("::", 1)[1])
                query += " ORDER BY source,target,kind"
            rows = db.execute(query, params).fetchall()
            selected = rows[max(offset, 0):max(offset, 0) + min(max(limit, 1), 200)]
            schema = self.schema_inputs()
            current_build = self.build_current()
            dependencies = self.dependency_state()
            output = []
            for row in selected:
                source = db.execute(f"SELECT {ASSET_METADATA} FROM assets WHERE path=?", (row["source"],)).fetchone()
                output.append({**dict(row), "source_freshness": self.freshness(source, schema, current_build, dependencies) if source else "catalog"})
        return {"target": target, "direction": direction, "total": len(rows), "has_more": offset + len(output) < len(rows),
                "references": output, "limitations": "Catalog covers /Game package references. Object and function references require exported, current snapshots; absence is not proof of no usages."}

    def ingest(self, expected_hashes, schema_hash, failures=None, expected_dependencies=None):
        catalog = json.loads((self.cache / "catalog.json").read_text(encoding="utf-8"))
        dependencies = DependencyState(catalog["assets"])
        with self.connect() as db:
            live_paths = set()
            for asset in catalog["assets"]:
                path = asset["path"]
                live_paths.add(path)
                catalog_text = json.dumps(asset, ensure_ascii=False)
                db.execute("""INSERT INTO assets(path,package,name,class,file,snapshot_file,catalog_json,search_text)
                    VALUES(?,?,?,?,?,?,?,?) ON CONFLICT(path) DO UPDATE SET package=excluded.package,
                    name=excluded.name,class=excluded.class,file=excluded.file,snapshot_file=excluded.snapshot_file,
                    catalog_json=excluded.catalog_json,search_text=excluded.search_text || COALESCE(assets.snapshot_json,'')""", (path, asset["package"], asset["name"], asset["class"],
                                                           asset["file"], asset["snapshot_file"], catalog_text, catalog_text))
                db.execute("DELETE FROM dependencies WHERE source=? AND kind IN ('package','catalog_object')", (path,))
                for target in asset["dependencies"]:
                    db.execute("INSERT OR IGNORE INTO dependencies VALUES(?,?,'package')", (path, target))
                for target, _ in extract_references({"class": asset["class"], "tags": asset["tags"]}):
                    if target not in {path, asset["package"]} and not target.startswith(asset["package"] + "."):
                        db.execute("INSERT OR IGNORE INTO dependencies VALUES(?,?,'catalog_object')", (path, target))
                if failures and path in failures:
                    db.execute("UPDATE assets SET export_error=? WHERE path=?", (failures[path], path))
                if path not in expected_hashes:
                    continue
                snapshot_path = self.cache / asset["snapshot_file"]
                if not snapshot_path.exists() or digest(Path(asset["file"])) != expected_hashes[path]:
                    raise RuntimeError("Asset changed during export or snapshot missing: " + path)
                dependency_hash = dependencies.fingerprint(asset["package"])
                if expected_dependencies is not None and dependency_hash != expected_dependencies[path]:
                    raise RuntimeError("Asset dependencies changed during export: " + path)
                text = snapshot_path.read_text(encoding="utf-8")
                snapshot = json.loads(text)
                if snapshot["asset"]["path"] != path:
                    raise RuntimeError("Snapshot path mismatch: " + path)
                db.execute("UPDATE assets SET snapshot_json=?,source_hash=?,schema_hash=?,captured_utc=?,search_text=?,export_error=NULL,dependency_hash=? WHERE path=?",
                           (text, expected_hashes[path], schema_hash, snapshot["captured_utc"], catalog_text + text, dependency_hash, path))
                db.execute("DELETE FROM dependencies WHERE source=? AND kind NOT IN ('package','catalog_object')", (path,))
                for target, kind in extract_references(snapshot):
                    if target in {path, asset["package"]} or target.startswith(asset["package"] + "."):
                        continue
                    db.execute("INSERT OR IGNORE INTO dependencies VALUES(?,?,?)", (path, target, kind))
            for row in db.execute("SELECT path FROM assets").fetchall():
                if row["path"] not in live_paths:
                    db.execute("DELETE FROM assets WHERE path=?", (row["path"],))
                    db.execute("DELETE FROM dependencies WHERE source=?", (row["path"],))
            for key in ("captured_utc", "engine_version", "schema_version", "errors"):
                db.execute("INSERT OR REPLACE INTO metadata VALUES(?,?)", (key, json.dumps(catalog[key])))
        return catalog

    def refresh(self, assets=None, export_all=False, build=True, *, _lock_held=False):
        lock_path = self.cache / "refresh.lock"
        with (nullcontext() if _lock_held else process_lock(lock_path)):
            root = engine_root(self.project, self.engine)
            if not self.build_current():
                if not build:
                    raise RuntimeError("C++/configuration/build changed. Build MessControlEditor and refresh with build=True.")
                build_log = self.cache / "build.log"
                before_build = self.schema_inputs()
                with build_log.open("w", encoding="utf-8") as log:
                    result = subprocess.run(["powershell", "-NoProfile", "-File", str(self.project / "Tools/Build.ps1"),
                                             "-EngineRoot", str(root)], cwd=self.project, stdout=log, stderr=subprocess.STDOUT)
                if result.returncode:
                    raise RuntimeError("Unreal build failed. Read " + str(build_log))
                if before_build != self.schema_inputs():
                    raise RuntimeError("Source changed during the build. Retry after edits finish.")
                self.stamp_build()
            schema = self.schema_inputs()
            # Catalog first discovers additions/removals without loading every texture or map.
            self.run_export(root, None, catalog_only=True)
            if schema != self.schema_inputs() or not self.build_current():
                raise RuntimeError("Code or binaries changed during catalog export. Build and retry.")
            self.ingest({}, schema)
            with self.connect() as db:
                if export_all:
                    rows = db.execute(f"SELECT {ASSET_METADATA} FROM assets").fetchall()
                elif assets is not None:
                    rows = [self.resolve(path, db, metadata_only=True) for path in assets]
                else:
                    rows = db.execute(f"SELECT {ASSET_METADATA} FROM assets WHERE snapshot_json IS NOT NULL").fetchall()
                    if not rows:
                        rows = [self.resolve(path, db, metadata_only=True) for path in BOOTSTRAP_ASSETS]
            current_build = self.build_current()
            dependencies = self.dependency_state()
            selected = [r for r in rows if self.freshness(r, schema, current_build, dependencies) != "current"]
            hashes = {r["path"]: dependencies.file_hash(r["file"]) for r in selected}
            dependency_hashes = {r["path"]: dependencies.fingerprint(r["package"]) for r in selected}
            failures = {}
            if selected:
                errors = self.run_export(root, list(hashes))
                if schema != self.schema_inputs() or not self.build_current():
                    raise RuntimeError("Code or binaries changed during export. Retry after the build finishes.")
                failures = {path: error for path in hashes for error in errors if error.startswith(path + ":")}
                self.ingest({p: h for p, h in hashes.items() if p not in failures}, schema, failures, dependency_hashes)
            return {"exported": len(selected) - len(failures), "export_errors": failures, "partial": bool(failures), **self.status()}

    def run_export(self, root, assets, catalog_only=False):
        args = [str(root / "Engine/Binaries/Win64/UnrealEditor-Cmd.exe"), str(self.project / "MessControl.uproject"),
                "-run=MCProjectIndex", "-MCIndexOutput=" + str(self.cache), "-unattended", "-nop4", "-NullRHI", "-nosplash",
                "-DisablePython",  # Native exporter does not need the editor's embedded Python or startup scripts.
                "-stdout", "-FullStdOutLogOutput", "-abslog=" + str(self.cache / "unreal_export.log")]
        if catalog_only:
            args.append("-MCIndexCatalogOnly")
        elif assets is not None:
            request = self.cache / "request.json"
            write_json(request, {"assets": assets})
            args.append("-MCIndexRequest=" + str(request))
        with (self.cache / "export_console.log").open("w", encoding="utf-8") as log:
            result = subprocess.run(args, cwd=self.project, stdout=log, stderr=subprocess.STDOUT, timeout=1800)
        if result.returncode not in (0, 4):
            raise RuntimeError(f"Asset export failed ({result.returncode}). Read {self.cache / 'export_console.log'}")
        catalog = json.loads((self.cache / "catalog.json").read_text(encoding="utf-8"))
        if result.returncode == 4 and not catalog.get("errors"):
            raise RuntimeError("Exporter failed without recorded per-asset diagnostics.")
        return catalog.get("errors", [])


def extract_references(value):
    result = set()

    def walk(item, key=""):
        if isinstance(item, dict):
            for child_key, child in item.items():
                walk(child, child_key)
        elif isinstance(item, list):
            for child in item:
                walk(child, key)
        elif isinstance(item, str):
            if key == "resolved_function":
                result.add((item, "function"))
            elif key != "dependencies":
                for path in re.findall(r"/(?:Game|Script|Engine|Niagara)/[\w/.-]+(?::[\w.-]+)?", item):
                    result.add((path, "object"))
    walk(value)
    return result


class process_lock:
    """Kernel-owned lock: an interrupted process cannot leave a permanently held lock."""
    def __init__(self, path):
        self.path = path

    def __enter__(self):
        self.file = self.path.open("a+b")
        if self.file.tell() == 0:
            self.file.write(b"0")
            self.file.flush()
        self.file.seek(0)
        try:
            if os.name == "nt":
                import msvcrt
                msvcrt.locking(self.file.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(self.file, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError:
            self.file.close()
            raise RuntimeError("Another asset refresh is running.") from None
        return self

    def __exit__(self, *args):
        self.file.close()


def main():
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=["refresh", "status", "stamp-build", "read", "search", "references"])
    parser.add_argument("--project", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--engine-root")
    parser.add_argument("--asset", action="append")
    parser.add_argument("--all", action="store_true")
    parser.add_argument("--no-build", action="store_true")
    parser.add_argument("--query", default="")
    parser.add_argument("--section", default="summary")
    args = parser.parse_args()
    index = AssetIndex(args.project, args.engine_root)
    if args.command == "refresh":
        result = index.refresh(args.asset, args.all, not args.no_build)
    elif args.command == "stamp-build":
        index.stamp_build()
        result = {"build_current": index.build_current()}
    elif args.command == "status":
        result = index.status()
    elif args.command == "read":
        result = index.read(args.asset[0], args.section)
    elif args.command == "references":
        result = index.references(args.query)
    else:
        result = index.search(args.query)
    sys.stdout.buffer.write((json.dumps(result, ensure_ascii=False, indent=2) + "\n").encode("utf-8"))


if __name__ == "__main__":
    main()
