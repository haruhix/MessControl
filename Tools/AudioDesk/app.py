"""MessControl Audio Desk: a private, dependency-free local audio review server.

All mutable state lives under --data-dir. The project is read only.
"""
from __future__ import annotations

import argparse
import concurrent.futures
import copy
import hashlib
import json
import math
import mimetypes
import os
from pathlib import Path, PureWindowsPath
import re
import shutil
import struct
import subprocess
import sys
import threading
import time
import uuid
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import quote, unquote, urlsplit


def runtime_paths() -> tuple[Path, Path]:
    """Keep immutable PyInstaller resources separate from portable user state."""
    if getattr(sys, "frozen", False):
        return Path(getattr(sys, "_MEIPASS", Path(sys.executable).parent)).resolve(), Path(sys.executable).resolve().parent
    source = Path(__file__).resolve().parent
    return source, source


BASE, APP_DIR = runtime_paths()
VERSION = "1.1.0"
AUDIO_EXTENSIONS = {".wav", ".wave", ".ogg", ".oga", ".mp3", ".flac", ".aif", ".aiff", ".m4a", ".aac", ".opus", ".wma"}
MAX_UPLOAD = 512 * 1024 * 1024
MAX_JSON = 8 * 1024 * 1024
PROFILE_VERSION = 1


class ApiError(Exception):
    def __init__(self, message: str, status: int = 400):
        super().__init__(message)
        self.status = status


def e_path(value: str | Path, *, must_exist: bool = False) -> Path:
    """Resolve local paths on any drive; the original helper name is API-compatible."""
    path = Path(value).expanduser().resolve()
    if must_exist and not path.exists():
        raise ApiError("Путь не найден: " + str(path), 404)
    return path


def find_media_tool(name: str, app_dir: Path, resource_dir: Path) -> str | None:
    """Use the packaged tools even on a machine without an edited PATH."""
    filename = name + (".exe" if os.name == "nt" else "")
    candidates = [app_dir / "media" / filename, app_dir / "runtime" / "media" / filename,
                  resource_dir / "media" / filename, resource_dir / filename]
    for candidate in candidates:
        if candidate.is_file():
            return str(candidate.resolve())
    return shutil.which(name)


def atomic_json(path: Path, value: object, *, backup: bool = False) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    with temporary.open("w", encoding="utf-8", newline="\n") as stream:
        json.dump(value, stream, ensure_ascii=False, indent=2, allow_nan=False)
        stream.write("\n")
        stream.flush()
        os.fsync(stream.fileno())
    if backup and path.exists():
        shutil.copy2(path, path.with_name(path.name + ".bak"))
    os.replace(temporary, path)


def read_json(path: Path, fallback: object) -> object:
    if not path.exists():
        return copy.deepcopy(fallback)
    try:
        with path.open(encoding="utf-8-sig") as stream:
            return json.load(stream)
    except (OSError, ValueError) as error:
        raise ApiError(f"Не удалось прочитать {path.name}: {error}", 500) from error


def file_id(path: Path) -> str:
    return hashlib.sha256(str(path.resolve()).casefold().encode("utf-8")).hexdigest()[:24]


def file_hash(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while block := stream.read(1024 * 1024):
            digest.update(block)
    return digest.hexdigest()


def safe_name(value: object) -> str:
    name = str(value or "").strip()
    if name.lower().endswith(".json"):
        name = name[:-5]
    name = re.sub(r"[^\w .-]+", "_", name, flags=re.UNICODE).strip(" .")[:100]
    if not name or name.upper() in {"CON", "PRN", "AUX", "NUL", *(f"COM{i}" for i in range(1, 10)), *(f"LPT{i}" for i in range(1, 10))}:
        raise ApiError("Укажите имя профиля из букв, цифр, пробелов, дефисов или подчёркиваний.")
    return name


def number(value: object, name: str, minimum: float, maximum: float) -> float:
    try:
        result = float(value)
    except (TypeError, ValueError) as error:
        raise ApiError(f"Некорректное число: {name}.") from error
    if not math.isfinite(result) or result < minimum or result > maximum:
        raise ApiError(f"{name}: допустимый диапазон {minimum}…{maximum}.")
    return result


def now_iso() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


def timing_policy(slot: dict) -> dict:
    """Normalize a catalog rule, with compatibility for older search_keywords."""
    policy = slot.get("timing_policy") or {}
    search = slot.get("search_keywords") or {}
    kind = str(slot.get("kind") or slot.get("type") or "sfx").lower()
    mode = policy.get("mode")
    if mode not in {"one_shot", "loop", "music"}:
        mode = "music" if kind == "music" else "loop" if slot.get("loop") else "one_shot"

    def value(key: str, fallback: float) -> float:
        candidate = policy.get(key, search.get(key, fallback))
        try:
            candidate = float(candidate)
            return candidate if math.isfinite(candidate) and candidate >= 0 else fallback
        except (TypeError, ValueError):
            return fallback

    minimum = value("min_duration", 30.0 if kind == "music" else 2.0 if kind == "ambience" else 0.025)
    maximum = max(minimum, value("max_duration", 1800.0 if kind == "music" else 900.0 if kind == "ambience" else 3.5))
    ideal_min = min(maximum, max(minimum, value("ideal_min", minimum)))
    ideal_max = min(maximum, max(ideal_min, value("ideal_max", maximum)))
    return {"min_duration": minimum, "max_duration": maximum, "ideal_min": ideal_min,
            "ideal_max": ideal_max, "reason": str(policy.get("reason") or "Длительность выбирается под характер игрового события."), "mode": mode}


def timing_fit(slot: dict, item: dict | None, clip: dict | None = None) -> dict:
    """Assess the audible segment; suggestion callers pass no crop or auto-trim."""
    policy = timing_policy(slot)
    clip = clip or {}

    def finite(value: object) -> float | None:
        try:
            value = float(value)
            return value if math.isfinite(value) else None
        except (TypeError, ValueError):
            return None

    source_duration = finite((item or {}).get("duration"))
    if source_duration is not None and source_duration <= 0:
        source_duration = None
    start = finite(clip.get("trim_start", 0) or 0)
    raw_end = clip.get("trim_end")
    end = source_duration if raw_end is None or raw_end == "" else finite(raw_end)
    if end is not None and source_duration is not None:
        end = min(end, source_duration)
    invalid = start is None or start < 0 or (end is not None and end <= start)
    duration = None if invalid or end is None else end - start
    known = duration is not None
    code, reason = "fits", "Длительность подходит."
    if item is None:
        code, reason = "missing_file", "Файл отсутствует в библиотеке."
    elif invalid:
        code, reason = "invalid_trim", "Границы фрагмента не задают доступный звук."
    elif not known:
        code, reason = "unknown_duration", "Длительность неизвестна: нужен анализ файла."
    elif duration + 0.000001 < policy["min_duration"]:
        code, reason = "too_short", "Фрагмент короче допустимого для этого события."
    elif duration - 0.000001 > policy["max_duration"]:
        code, reason = "too_long", "Фрагмент длиннее допустимого для этого события."
    fits = code == "fits"
    score = 0.0
    if fits:
        score = 1.0
        if duration < policy["ideal_min"]:
            score = 0.5 + 0.5 * (duration - policy["min_duration"]) / max(0.000001, policy["ideal_min"] - policy["min_duration"])
        elif duration > policy["ideal_max"]:
            score = 0.5 + 0.5 * (policy["max_duration"] - duration) / max(0.000001, policy["max_duration"] - policy["ideal_max"])
        score = max(0.5, min(1.0, score))
    return {**policy, "fits": fits, "known": known, "duration": round(duration, 6) if known else None,
            "source_duration": source_duration, "policy_reason": policy["reason"], "reason": reason,
            "code": code, "score": round(score, 6)}


class AudioDesk:
    def __init__(self, data_dir: Path, base: Path = BASE, app_dir: Path | None = None):
        self.base = base.resolve()
        self.app_dir = (app_dir or (APP_DIR if base == BASE else base)).resolve()
        self.project_root = self.app_dir.parent.parent
        self.is_project_checkout = (self.project_root / "MessControl.uproject").is_file()
        self.instance_id = str(uuid.uuid4())
        self.data_dir = e_path(data_dir)
        self.data_dir.mkdir(parents=True, exist_ok=True)
        self.temp_dir = self.data_dir / "temp"
        self.cache_dir = self.data_dir / "cache"
        self.upload_dir = self.data_dir / "uploads"
        self.profile_dir = self.data_dir / "profiles"
        for directory in (self.temp_dir, self.cache_dir, self.upload_dir, self.profile_dir):
            directory.mkdir(parents=True, exist_ok=True)
        os.environ["TEMP"] = os.environ["TMP"] = str(self.temp_dir)
        os.environ["PYTHONPYCACHEPREFIX"] = str(self.data_dir / "pycache")
        self.lock = threading.RLock()
        self.scan_lock = threading.Lock()
        self.media_locks: dict[str, threading.Lock] = {}
        self.ffprobe = find_media_tool("ffprobe", self.app_dir, self.base)
        self.ffmpeg = find_media_tool("ffmpeg", self.app_dir, self.base)
        self.inventory: dict[str, dict] = {}
        self.pack_metadata: dict[str, dict] = {}
        library_path = self.data_dir / "library.json"
        seeded_library = read_json(self.resource_file("seed_library.json"), {"roots": [], "files": []}) if not library_path.exists() else {}
        saved_library = read_json(library_path, seeded_library)
        self.root_templates = list(saved_library.get("root_templates", saved_library.get("roots", [])))
        self.roots = [str(self.expand_portable_path(value)) for value in self.root_templates if isinstance(value, str)]
        self.library = {}
        for original in saved_library.get("files", []):
            if not isinstance(original, dict) or not original.get("id") or not original.get("path"):
                continue
            item = copy.deepcopy(original)
            template = item.get("portable_path") or item["path"]
            if "${" in str(template) or not Path(str(template)).is_absolute():
                item["portable_path"] = str(template)
                item["path"] = str(self.expand_portable_path(template))
            item.setdefault("name", Path(item["path"]).name)
            item.setdefault("relative_path", item["name"])
            item.setdefault("pack", "Библиотека")
            item["available"] = Path(item["path"]).is_file()
            self.library[item["id"]] = item
        self.reindex_paths()
        self.working_path = self.data_dir / "working.json"
        default_profile = {"schema_version": PROFILE_VERSION, "name": "MessControl · черновик", "assignments": {}, "created_at": now_iso(), "updated_at": now_iso()}
        seed_path = self.resource_file("seed_profile.json")
        if not seed_path.exists():
            seed_path = self.resource_file("initial_profile.json")
        self.profile = read_json(self.working_path, read_json(seed_path, default_profile))
        self.profile.setdefault("assignments", {})
        self.profile.setdefault("schema_version", PROFILE_VERSION)
        if not self.working_path.exists():
            self.persist_profile()
        if not library_path.exists() and self.library:
            self.persist_library()

    def resource_file(self, name: str) -> Path:
        external = self.app_dir / name
        return external if external.exists() else self.base / name

    def expand_portable_path(self, value: str | Path) -> Path:
        text = str(value).replace("${APP_ROOT}", str(self.app_dir)).replace("${PROJECT_ROOT}", str(self.project_root))
        if "${" in text:
            raise ApiError("Неизвестный корень переносимой библиотеки: " + text)
        path = Path(text)
        return (path if path.is_absolute() else self.app_dir / path).resolve()

    def portable_path(self, path: Path) -> str | None:
        path = path.resolve()
        for token, root in (("${APP_ROOT}", self.app_dir), ("${PROJECT_ROOT}", self.project_root)):
            if path.is_relative_to(root):
                return token + "/" + path.relative_to(root).as_posix()
        return None

    def reindex_paths(self) -> None:
        self.path_ids = {str(Path(item["path"]).resolve()).casefold(): identifier for identifier, item in self.library.items()}

    def health(self) -> dict:
        return {"tool": "MessControl Audio Desk", "version": VERSION, "app_dir": str(self.app_dir),
                "data_dir": str(self.data_dir), "instance_id": self.instance_id, "process_id": os.getpid(),
                "project_write_enabled": False}

    def run_media(self, args: list[str], *, timeout: int = 45, binary: bool = False) -> subprocess.CompletedProcess:
        environment = os.environ.copy()
        environment["TEMP"] = environment["TMP"] = str(self.temp_dir)
        try:
            return subprocess.run(args, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                  timeout=timeout, check=False, text=not binary, encoding=None if binary else "utf-8",
                                  errors=None if binary else "replace", env=environment,
                                  creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        except (OSError, subprocess.TimeoutExpired) as error:
            raise ApiError("Ошибка обработки аудио: " + str(error), 422) from error

    def catalog(self) -> dict:
        catalog_path = self.resource_file("catalog.json")
        if not catalog_path.exists():
            catalog_path = self.data_dir / "catalog.json"
        catalog = read_json(catalog_path, {"version": 1, "groups": [], "slots": []})
        if not isinstance(catalog, dict):
            raise ApiError("catalog.json должен содержать JSON-объект.", 500)
        catalog.setdefault("groups", [])
        catalog.setdefault("slots", [])
        audit = read_json(self.resource_file("existing_assignments.json"), {})
        source_by_asset: dict[str, list[dict]] = {}
        def collect_sources(value: object) -> None:
            if isinstance(value, dict):
                asset = value.get("unreal_asset")
                if asset and isinstance(value.get("sources"), list):
                    source_by_asset.setdefault(str(asset).split(".")[0], []).extend(value["sources"])
                for child in value.values():
                    collect_sources(child)
            elif isinstance(value, list):
                for child in value:
                    collect_sources(child)
        collect_sources(audit)
        slots = catalog["slots"].values() if isinstance(catalog["slots"], dict) else catalog["slots"]
        for slot in slots:
            current = slot.get("current_assignment", {})
            references = [item.get("refPath", item.get("unreal_asset", "")) for item in current.get("sounds", [])]
            if current.get("sound"):
                references.append(current["sound"])
            if str(slot.get("id", "")).startswith("direct."):
                references.append(slot.get("target", {}).get("asset", ""))
            source_files = []
            for existing in current.get("source_files", []):
                item = self.library.get(existing.get("file_id"))
                if item and Path(item["path"]).is_file():
                    source_files.append({**existing, "name": item["name"], "path": item["path"]})
            for asset in references:
                for source in source_by_asset.get(str(asset).split(".")[0], []):
                    candidate_path = source.get("candidate_source_path")
                    if not candidate_path:
                        continue
                    candidate_path = self.expand_portable_path(candidate_path)
                    identifier = self.path_ids.get(str(candidate_path).casefold(), file_id(candidate_path))
                    if identifier in self.library:
                        item = self.library[identifier]
                        entry = {"file_id": identifier, "name": item["name"], "path": item["path"], "unreal_asset": asset,
                                 "source_matches_import_md5": source.get("source_matches_import_md5"),
                                 "gain_db": round(20 * math.log10(max(float(current.get("volume", 1)), 0.000001)), 3)}
                        if not any(existing["file_id"] == identifier for existing in source_files):
                            source_files.append(entry)
            if source_files:
                current["source_files"] = source_files
                current["source_evidence"] = audit.get("evidence", "Saved source import metadata")
                slot["current_assignment"] = current
            elif "source_files" in current:
                current["source_files"] = []
        return catalog

    def slots(self) -> list[dict]:
        slots = self.catalog().get("slots", [])
        if isinstance(slots, dict):
            return [{"id": key, **value} for key, value in slots.items()]
        return slots

    def persist_library(self) -> None:
        self.reindex_paths()
        roots = [self.portable_path(Path(root)) or root for root in self.roots]
        atomic_json(self.data_dir / "library.json", {"schema_version": 1, "roots": roots, "root_templates": roots,
                    "files": list(self.library.values()), "updated_at": now_iso()}, backup=True)

    def persist_profile(self) -> None:
        self.profile["updated_at"] = now_iso()
        atomic_json(self.working_path, self.profile, backup=True)

    def state(self) -> dict:
        with self.lock:
            catalog = self.catalog()
            packs: dict[str, int] = {}
            for item in self.library.values():
                packs[item.get("pack", "Другие")] = packs.get(item.get("pack", "Другие"), 0) + 1
            return {"catalog": catalog, "library": copy.deepcopy(list(self.library.values())),
                    "profile": copy.deepcopy(self.profile), "packs": [{"name": key, "count": value} for key, value in sorted(packs.items())],
                    "timing_issues": self.timing_issues(catalog=catalog),
                    "profile_names": sorted(path.stem for path in self.profile_dir.glob("*.json")),
                    "roots": self.roots[:], "capabilities": {"ffprobe": bool(self.ffprobe), "ffmpeg": bool(self.ffmpeg)},
                    "data_dir": str(self.data_dir), "app_dir": str(self.app_dir), "version": VERSION,
                    "project_write_enabled": False}

    def timing_issues(self, *, assignments: dict | None = None, catalog: dict | None = None) -> list[dict]:
        """Read-only warnings: a timing rule never silently revokes a user's choice."""
        catalog = catalog if catalog is not None else self.catalog()
        slots = catalog.get("slots", [])
        by_id = ({str(key): {"id": key, **value} for key, value in slots.items()} if isinstance(slots, dict)
                 else {str(slot.get("id")): slot for slot in slots})
        assignments = self.profile.get("assignments", {}) if assignments is None else assignments
        issues = []
        for slot_id, assignment in assignments.items():
            if not isinstance(assignment, dict):
                continue
            slot = by_id.get(str(slot_id))
            if slot is None:
                continue
            for key, role in (("clips", "clip"), ("alternatives", "alternative")):
                for index, clip in enumerate(assignment.get(key, [])):
                    clip = {"file_id": clip} if isinstance(clip, str) else clip
                    fit = timing_fit(slot, self.library.get(clip.get("file_id")), clip)
                    if not fit["fits"]:
                        issues.append({"slot_id": slot_id, "file_id": clip.get("file_id"), "role": role, "index": index,
                                       "status": assignment.get("status"), "effective_duration": fit["duration"],
                                       "source_duration": fit["source_duration"], "min_duration": fit["min_duration"],
                                       "max_duration": fit["max_duration"], "mode": fit["mode"], "known": fit["known"],
                                       "code": fit["code"], "reason": fit["reason"], "policy_reason": fit["policy_reason"]})
        return issues

    def probe(self, path: Path, root: Path | None = None, *, content_hash: str | None = None) -> dict:
        path = e_path(path, must_exist=True)
        if path.suffix.lower() not in AUDIO_EXTENSIONS or not path.is_file():
            raise ApiError("Неподдерживаемый аудиофайл: " + str(path), 415)
        info = path.stat()
        identifier = self.path_ids.get(str(path).casefold(), file_id(path))
        existing = self.library.get(identifier)
        if existing and existing.get("bytes") == info.st_size and existing.get("mtime_ns") == info.st_mtime_ns:
            return copy.deepcopy(existing)
        root = root or path.parent
        try:
            relative = str(path.relative_to(root)).replace("\\", "/")
        except ValueError:
            relative = path.name
        record = {"id": identifier, "name": path.name, "path": str(path), "relative_path": relative,
                  "pack": root.name, "bytes": info.st_size, "mtime_ns": info.st_mtime_ns,
                  "duration": None, "channels": None, "sample_rate": None, "format": path.suffix.lower().lstrip(".")}
        if template := self.portable_path(path):
            record["portable_path"] = template
        record["available"] = True
        parts = Path(relative).parts
        if len(parts) >= 3 and re.match(r"^\d{2}_", parts[0]):
            record["category"] = parts[0]
            record["pack"] = parts[1]
        elif len(parts) >= 2:
            record["pack"] = parts[0]
        if self.is_project_checkout and root.is_relative_to(self.project_root) and not root.is_relative_to(self.app_dir):
            record["pack"] = "MessControl current"
            record["category"] = root.name
        if record["pack"] in self.pack_metadata:
            record["source_pack"] = self.pack_metadata[record["pack"]]
        if content_hash:
            record["sha256"] = content_hash
        inventory = self.inventory.get(str(path).casefold())
        if inventory and inventory.get("bytes") == info.st_size and inventory.get("_inventory_mtime", 0) >= info.st_mtime:
            try:
                stream = next((stream for stream in inventory.get("streams", []) if stream.get("sample_rate")), {})
                record.update(duration=round(float(inventory["format"]["duration"]), 5),
                              channels=int(stream["channels"]), sample_rate=int(stream["sample_rate"]),
                              codec=stream.get("codec_name"), sha256=inventory.get("sha256"),
                              metadata_source="existing_audio_inventory")
                return record
            except (KeyError, TypeError, ValueError):
                pass
        if self.ffprobe:
            response = self.run_media([self.ffprobe, "-v", "error", "-show_entries", "format=duration:stream=codec_type,codec_name,sample_rate,channels,duration", "-of", "json", str(path)])
            if response.returncode == 0:
                try:
                    metadata = json.loads(response.stdout)
                    audio = next((stream for stream in metadata.get("streams", []) if stream.get("codec_type") == "audio"), {})
                    duration = metadata.get("format", {}).get("duration") or audio.get("duration")
                    record.update(duration=round(float(duration), 5) if duration is not None else None,
                                  channels=int(audio["channels"]) if audio.get("channels") else None,
                                  sample_rate=int(audio["sample_rate"]) if audio.get("sample_rate") else None,
                                  codec=audio.get("codec_name"))
                except (ValueError, TypeError):
                    record["metadata_error"] = "ffprobe вернул неполные метаданные."
            else:
                record["metadata_error"] = response.stderr.strip()[-400:]
        elif path.suffix.lower() in {".wav", ".wave"}:
            import wave
            try:
                with wave.open(str(path), "rb") as sound:
                    record.update(duration=round(sound.getnframes() / sound.getframerate(), 5), channels=sound.getnchannels(), sample_rate=sound.getframerate())
            except (OSError, wave.Error) as error:
                record["metadata_error"] = str(error)
        return record

    def scan(self, value: str) -> dict:
        root = e_path(value, must_exist=True)
        if not root.is_dir():
            raise ApiError("Для сканирования выберите папку.")
        protected = [os.environ.get(key) for key in ("SystemRoot", "ProgramFiles", "ProgramFiles(x86)", "ProgramData")]
        if root == Path(root.anchor) or any(root == Path(value).resolve() for value in protected if value):
            raise ApiError("Выберите папку с аудио, а не системную папку или корень диска.")
        if not self.scan_lock.acquire(blocking=False):
            raise ApiError("Другое сканирование уже выполняется.", 409)
        try:
            inventory_path = root.parent / "Catalog" / "AudioInventory.json"
            if inventory_path.exists():
                inventory_mtime = inventory_path.stat().st_mtime
                for entry in read_json(inventory_path, []):
                    candidate = (root.parent / entry.get("path", "")).resolve()
                    self.inventory[str(candidate).casefold()] = {**entry, "_inventory_mtime": inventory_mtime}
            pack_path = root.parent / "Catalog" / "Library.json"
            if pack_path.exists():
                pack_catalog = read_json(pack_path, {})
                self.pack_metadata.update({item["slug"]: item for item in pack_catalog.get("items", []) if item.get("slug")})
            paths = []
            errors = []
            for folder, directories, filenames in os.walk(root, followlinks=False):
                directories[:] = [name for name in directories if not Path(folder, name).is_symlink()]
                for name in filenames:
                    path = Path(folder, name)
                    if path.suffix.lower() in AUDIO_EXTENSIONS:
                        paths.append(path)
            records = []
            with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
                futures = {pool.submit(self.probe, path, root): path for path in paths}
                for future in concurrent.futures.as_completed(futures):
                    try:
                        records.append(future.result())
                    except (ApiError, OSError) as error:
                        errors.append({"path": str(futures[future]), "error": str(error)})
            records.sort(key=lambda item: item["relative_path"].casefold())
            with self.lock:
                self.library.update({item["id"]: item for item in records})
                if str(root) not in self.roots:
                    self.roots.append(str(root))
                self.persist_library()
            return {"ok": True, "count": len(records), "errors": errors, "files": records}
        finally:
            self.scan_lock.release()

    def upload(self, filename: str, payload: bytes) -> dict:
        filename = PureWindowsPath(filename.replace("/", "\\")).name
        filename = re.sub(r'[<>:"/\\|?*\x00-\x1f]', "_", filename).strip(" .")[:180]
        if not filename or Path(filename).suffix.lower() not in AUDIO_EXTENSIONS:
            raise ApiError("Перетащите WAV, OGG, MP3, FLAC, AIFF, M4A или другой аудиофайл.", 415)
        if not payload:
            raise ApiError("Файл пуст.")
        digest = hashlib.sha256(payload).hexdigest()
        destination = self.upload_dir / (digest[:16] + "_" + filename)
        with self.lock:
            if not destination.exists():
                temporary = destination.with_name(destination.name + ".tmp")
                temporary.write_bytes(payload)
                os.replace(temporary, destination)
            record = self.probe(destination, self.upload_dir, content_hash=digest)
            record["name"] = filename
            record["pack"] = "Загруженные"
            record["sha256"] = digest
            self.library[record["id"]] = record
            self.persist_library()
        return {"ok": True, "files": [record]}

    def validate_assignment(self, slot_id: str, value: object, *, imported_library: dict | None = None) -> dict:
        if not isinstance(value, dict):
            raise ApiError("Назначение должно быть JSON-объектом.")
        status = value.get("status", "suggested")
        if status not in {"suggested", "approved", "rejected", "empty"}:
            raise ApiError("Неизвестный статус назначения.")
        clips = value.get("clips", [])
        if not isinstance(clips, list) or len(clips) > 100:
            raise ApiError("В назначении должно быть не более 100 звуков.")
        normalized = []
        for clip in clips:
            if not isinstance(clip, dict):
                raise ApiError("Некорректная запись звука.")
            identifier = str(clip.get("file_id", ""))
            item = self.library.get(identifier) or (imported_library or {}).get(identifier)
            if not item:
                raise ApiError(f"Звук {identifier} отсутствует в библиотеке. Сначала добавьте исходную папку.")
            duration = item.get("duration") or 86400
            start = number(clip.get("trim_start", 0) or 0, "Начало фрагмента", 0, duration)
            raw_end = clip.get("trim_end")
            end = None if raw_end is None or raw_end == "" else number(raw_end, "Конец фрагмента", 0, duration + 0.01)
            if end is not None and end <= start:
                raise ApiError("Конец фрагмента должен быть позже начала.")
            if duration and start >= duration:
                raise ApiError("Начало фрагмента находится за концом аудио.")
            normalized.append({**clip, "file_id": identifier, "gain_db": number(clip.get("gain_db", 0) or 0, "Усиление", -60, 24),
                               "trim_start": start, "trim_end": end})
        if status == "approved" and not normalized:
            raise ApiError("Нельзя одобрить слот без звука.")
        result = {**value, "clips": normalized, "status": status, "note": str(value.get("note", ""))[:12000],
                  "reason": str(value.get("reason", ""))[:12000]}
        if "alternatives" in value:
            alternatives = value["alternatives"]
            if not isinstance(alternatives, list):
                raise ApiError("Альтернативы должны быть списком звуков.")
            alternatives = [{"file_id": clip} if isinstance(clip, str) else clip for clip in alternatives]
            result["alternatives"] = self.validate_assignment(slot_id, {"clips": alternatives, "status": "suggested"}, imported_library=imported_library)["clips"]
        if value.get("confidence") is not None:
            result["confidence"] = number(value["confidence"], "Уверенность", 0, 1)
        return result

    def assign(self, slot_id: str, assignment: object) -> dict:
        with self.lock:
            known_slots = {str(item.get("id")) for item in self.slots()}
            if known_slots and slot_id not in known_slots:
                raise ApiError("Такого слота нет в каталоге.", 404)
            if not slot_id or len(slot_id) > 250:
                raise ApiError("Некорректный идентификатор слота.")
            normalized = self.validate_assignment(slot_id, assignment)
            self.profile["assignments"][slot_id] = normalized
            self.persist_profile()
            return {"ok": True, "assignment": copy.deepcopy(normalized), "updated_at": self.profile["updated_at"],
                    "timing_issues": self.timing_issues(assignments={slot_id: normalized})}

    def save_profile(self, name: str) -> dict:
        name = safe_name(name)
        with self.lock:
            self.profile["name"] = name
            self.profile["catalog_version"] = self.catalog().get("version", 1)
            self.profile["source_snapshot"] = self.catalog().get("source_snapshot", self.catalog().get("sources", {}))
            self.persist_profile()
            destination = self.profile_dir / (name + ".json")
            atomic_json(destination, self.export_profile(), backup=True)
            return {"ok": True, "path": str(destination), "profile": copy.deepcopy(self.profile)}

    def load_profile(self, name: str) -> dict:
        name = safe_name(name)
        path = self.profile_dir / (name + ".json")
        if not path.is_file():
            raise ApiError("Профиль не найден.", 404)
        return self.import_profile(read_json(path, {}))

    def save_export(self, name: str | None = None) -> dict:
        with self.lock:
            name = safe_name(name or self.profile.get("name", "audio-profile"))
            exported = self.export_profile()
            destination = self.data_dir / "exports" / (name + ".json")
            atomic_json(destination, exported, backup=True)
            return {"ok": True, "path": str(destination),
                    "approved_count": len(exported["import_plan"]["approved"]),
                    "proposed_count": len(exported["import_plan"]["proposed"])}

    def import_profile(self, payload: dict) -> dict:
        if not isinstance(payload, dict):
            raise ApiError("Профиль должен содержать JSON-объект.")
        profile = payload.get("profile", payload)
        if not isinstance(profile, dict) or not isinstance(profile.get("assignments"), dict):
            raise ApiError("В профиле отсутствует объект assignments.")
        if profile.get("schema_version", 1) != PROFILE_VERSION:
            raise ApiError("Эта версия формата профиля пока не поддерживается.")
        exported_files = payload.get("files", profile.get("files", []))
        if isinstance(exported_files, dict):
            exported_files = list(exported_files.values())
        remap = {}
        additions = {}
        with self.lock:
            by_path = {str(Path(item["path"]).resolve()).casefold(): item for item in self.library.values()}
            by_hash = {item.get("sha256"): item for item in self.library.values() if item.get("sha256")}
            for item in exported_files:
                if not isinstance(item, dict) or not item.get("id"):
                    continue
                matching = self.library.get(item["id"]) or by_hash.get(item.get("sha256"))
                if not matching and item.get("path"):
                    path = e_path(item["path"])
                    matching = by_path.get(str(path).casefold())
                    if not matching and path.is_file():
                        matching = self.probe(path)
                        additions[matching["id"]] = matching
                if matching:
                    remap[item["id"]] = matching["id"]
            candidate = copy.deepcopy(profile)
            assignments = {}
            for slot_id, assignment in candidate["assignments"].items():
                if isinstance(assignment, dict):
                    for key in ("clips", "alternatives"):
                        if not isinstance(assignment.get(key, []), list):
                            raise ApiError("Некорректный список звуков в профиле.")
                        assignment[key] = [{"file_id": clip} if isinstance(clip, str) else clip for clip in assignment.get(key, [])]
                        for clip in assignment[key]:
                            if not isinstance(clip, dict):
                                raise ApiError("Некорректная запись звука в профиле.")
                            clip["file_id"] = remap.get(clip.get("file_id"), clip.get("file_id"))
                assignments[slot_id] = self.validate_assignment(slot_id, assignment, imported_library=additions)
            candidate["assignments"] = assignments
            candidate["schema_version"] = PROFILE_VERSION
            candidate.pop("files", None)
            candidate.pop("import_plan", None)
            self.library.update(additions)
            if additions:
                self.persist_library()
            self.profile = candidate
            self.persist_profile()
            return {"ok": True, "profile": copy.deepcopy(self.profile), "remapped_files": remap}

    def export_profile(self) -> dict:
        with self.lock:
            catalog = self.catalog()
            assignments = self.profile.get("assignments", {})
            timing_issues = self.timing_issues(catalog=catalog)
            used = {clip["file_id"] if isinstance(clip, dict) else clip for assignment in assignments.values() for key in ("clips", "alternatives") for clip in assignment.get(key, [])}
            files = []
            for identifier in sorted(used):
                if identifier not in self.library:
                    continue
                item = copy.deepcopy(self.library[identifier])
                path = e_path(item["path"])
                item["exists"] = path.is_file()
                if item["exists"]:
                    stat = path.stat()
                    if not item.get("sha256") or item.get("bytes") != stat.st_size or item.get("mtime_ns") != stat.st_mtime_ns:
                        item["sha256"] = file_hash(path)
                files.append(item)
            slots = {str(slot.get("id")): slot for slot in self.slots()}
            approved, proposed = [], []
            for slot_id, assignment in assignments.items():
                plan = {"slot_id": slot_id, "slot": copy.deepcopy(slots.get(slot_id, {"id": slot_id})), "assignment": copy.deepcopy(assignment),
                        "timing_warnings": [copy.deepcopy(issue) for issue in timing_issues if issue["slot_id"] == slot_id]}
                if assignment.get("status") == "approved":
                    approved.append(plan)
                elif assignment.get("status") == "suggested":
                    proposed.append(plan)
            return {"schema_version": PROFILE_VERSION, "exported_at": now_iso(), "tool": "MessControl Audio Desk",
                    "profile": copy.deepcopy(self.profile), "catalog": catalog, "files": files,
                    "timing_issues": timing_issues,
                    "import_plan": {"mode": "review_only", "applied_to_unreal": False, "approved": approved, "proposed": proposed,
                                    "timing_issues": copy.deepcopy(timing_issues),
                                    "instruction": "Apply only approved entries after checking source hashes, catalog targets, current game source, and timing_warnings. Timing warnings preserve manual choices; review them before integration. Proposed entries require user approval."}}

    def suggest(self) -> dict:
        """Conservative filename/context scoring. Never claims to have listened."""
        def tokens(value: object) -> set[str]:
            if isinstance(value, (dict, list)):
                value = json.dumps(value, ensure_ascii=False)
            return set(re.findall(r"[a-zа-яё0-9]+", str(value).lower()))
        changed = 0
        with self.lock:
            for slot in self.slots():
                slot_id = str(slot.get("id", ""))
                previous = self.profile["assignments"].get(slot_id, {})
                if previous.get("status") in {"approved", "rejected"} or previous.get("clips") or previous.get("selection_method") == "curated_gap":
                    continue
                search = slot.get("search_keywords", {})
                positive = search.get("positive", [])
                negative = search.get("negative", [])
                wanted = tokens(positive or slot.get("search_terms", slot.get("keywords", [])))
                if not wanted:
                    wanted = tokens(" ".join(str(slot.get(key, "")) for key in ("id", "name", "title", "description")))
                kind = str(slot.get("kind", slot.get("type", "sfx"))).lower()
                results = []
                for item in self.library.values():
                    basename = Path(item["name"]).stem.lower()
                    name_words = tokens(basename)
                    context_words = tokens(item.get("relative_path", ""))
                    # Filename evidence dominates category words such as UI/Footsteps.
                    overlap = wanted & name_words
                    phrase_matches = [term for term in positive if str(term).lower() in basename]
                    if not overlap and not phrase_matches:
                        continue
                    # Whole-file metadata is authoritative here. A multi-take SFX must
                    # be explicitly trimmed by the reviewer, never arbitrarily cropped.
                    fit = timing_fit(slot, item)
                    if not fit["fits"]:
                        continue
                    excluded = any((set(tokens(term)) <= name_words if len(tokens(term)) > 1 else bool(tokens(term) & name_words)) for term in negative)
                    if excluded:
                        continue
                    if kind == "music" and name_words & {"chime", "sting", "jingle", "ui", "sfx"}:
                        continue
                    if slot.get("group") == "piano":
                        octave = slot_id.rsplit(".", 1)[-1].lower()
                        if octave not in basename or not name_words & {"piano", "note", "toothpiano"}:
                            continue
                    score = len(overlap) * 4.0 + len(phrase_matches) * 3 + len(wanted & context_words) * 0.25 + fit["score"] * 3.0
                    if bool(slot.get("loop")) and name_words & {"loop", "seamless", "ambient", "ambience"}:
                        score += 2
                    results.append((score, item))
                if not results:
                    continue
                results.sort(key=lambda pair: (-pair[0], pair[1]["name"]))
                best_score, item = results[0]
                if best_score <= 1:
                    continue
                self.profile["assignments"][slot_id] = {"status": "suggested", "clips": [{"file_id": item["id"], "gain_db": -6 if kind == "music" else 0, "trim_start": 0, "trim_end": None}],
                    "alternatives": [{"file_id": candidate["id"], "gain_db": -6 if kind == "music" else 0, "trim_start": 0, "trim_end": None} for _, candidate in results[1:6]],
                    "note": "", "reason": "Кандидат по имени файла, контексту слота и длительности. Требует прослушивания и одобрения.",
                    "confidence": round(min(0.8, 0.25 + best_score / 40), 2), "selection_method": "filename_and_metadata"}
                changed += 1
            if changed:
                self.persist_profile()
            return {"ok": True, "count": changed, "profile": copy.deepcopy(self.profile)}

    def get_file(self, identifier: str) -> tuple[dict, Path]:
        with self.lock:
            record = self.library.get(identifier)
            if not record:
                raise ApiError("Аудиофайл не найден в библиотеке.", 404)
            path = e_path(record["path"], must_exist=True)
            return copy.deepcopy(record), path

    def media_lock(self, key: str) -> threading.Lock:
        with self.lock:
            return self.media_locks.setdefault(key, threading.Lock())

    def cache_key(self, identifier: str, path: Path) -> str:
        stat = path.stat()
        return hashlib.sha256(f"{identifier}:{stat.st_size}:{stat.st_mtime_ns}".encode()).hexdigest()[:32]

    def preview(self, identifier: str) -> Path:
        _, path = self.get_file(identifier)
        if not self.ffmpeg:
            raise ApiError("Для предпрослушивания этого формата нужен ffmpeg в PATH.", 503)
        key = self.cache_key(identifier, path)
        destination = self.cache_dir / (key + ".wav")
        with self.media_lock("preview:" + key):
            if destination.exists():
                return destination
            temporary = self.cache_dir / (key + ".tmp.wav")
            result = self.run_media([self.ffmpeg, "-v", "error", "-nostdin", "-y", "-i", str(path), "-vn", "-ac", "2", "-ar", "44100", "-c:a", "pcm_s16le", str(temporary)], timeout=180)
            if result.returncode:
                temporary.unlink(missing_ok=True)
                raise ApiError("Не удалось декодировать аудио: " + result.stderr.strip()[-500:], 422)
            os.replace(temporary, destination)
            self.prune_cache(exclude=destination)
        return destination

    def prune_cache(self, *, exclude: Path | None = None) -> None:
        paths = sorted((path for path in self.cache_dir.glob("*.wav") if path != exclude), key=lambda item: item.stat().st_mtime)
        total = sum(path.stat().st_size for path in paths) + (exclude.stat().st_size if exclude and exclude.exists() else 0)
        for path in paths:
            if total <= 2 * 1024 ** 3:
                break
            try:
                size = path.stat().st_size
                path.unlink()
                total -= size
            except OSError:
                pass

    def waveform(self, identifier: str) -> dict:
        item, path = self.get_file(identifier)
        if not self.ffmpeg:
            return {"peaks": [], "duration": item.get("duration"), "unavailable": "ffmpeg не найден"}
        key = self.cache_key(identifier, path)
        destination = self.cache_dir / (key + ".waveform.json")
        with self.media_lock("waveform:" + key):
            if destination.exists():
                return read_json(destination, {})
            # Low sample rate keeps long music/ambience analysis bounded in RAM.
            duration = item.get("duration") or 60
            sample_rate = max(50, min(2000, int(2_000_000 / max(duration, 1))))
            result = self.run_media([self.ffmpeg, "-v", "error", "-nostdin", "-i", str(path), "-vn", "-ac", "1", "-ar", str(sample_rate), "-f", "s16le", "-c:a", "pcm_s16le", "pipe:1"], timeout=120, binary=True)
            if result.returncode:
                raise ApiError("Не удалось построить волну аудио.", 422)
            raw = result.stdout
            values = [value[0] / 32768 for value in struct.iter_unpack("<h", raw[:len(raw) // 2 * 2])]
            bucket_count = min(600, len(values))
            peaks = []
            for index in range(bucket_count):
                start = index * len(values) // bucket_count
                end = max(start + 1, (index + 1) * len(values) // bucket_count)
                peaks.append(round(max(abs(value) for value in values[start:end]), 5))
            peak = max((abs(value) for value in values), default=0)
            rms = math.sqrt(sum(value * value for value in values) / max(1, len(values)))
            response = {"peaks": peaks, "duration": item.get("duration"), "peak_db": round(20 * math.log10(max(peak, 1e-8)), 2),
                        "rms_db": round(20 * math.log10(max(rms, 1e-8)), 2), "analysis": "mono waveform preview; low sample rate; not true-peak/loudness measurement"}
            atomic_json(destination, response)
            return response


class DeskServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True

    def __init__(self, address: tuple[str, int], desk: AudioDesk):
        self.desk = desk
        super().__init__(address, DeskHandler)


class DeskHandler(BaseHTTPRequestHandler):
    server_version = "AudioDesk/1.0"

    @property
    def desk(self) -> AudioDesk:
        return self.server.desk

    def log_message(self, format: str, *args) -> None:
        # No audio paths or profile bodies in request logs.
        if "/api/audio/" not in self.path and "/api/waveform/" not in self.path:
            super().log_message(format, *args)

    def end_headers(self) -> None:
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Referrer-Policy", "no-referrer")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Security-Policy", "default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; img-src 'self' data: blob:; media-src 'self' blob:; connect-src 'self'; object-src 'none'; frame-ancestors 'none'")
        super().end_headers()

    def check_request(self) -> None:
        host = self.headers.get("Host", "")
        allowed = {f"127.0.0.1:{self.server.server_port}", f"localhost:{self.server.server_port}"}
        if host.casefold() not in allowed:
            raise ApiError("Разрешено только локальное подключение.", 403)
        origin = self.headers.get("Origin")
        if origin and origin not in {"http://" + host for host in allowed}:
            raise ApiError("Запрос с другого сайта запрещён.", 403)

    def json_response(self, payload: object, status: int = 200, *, download: str | None = None) -> None:
        encoded = json.dumps(payload, ensure_ascii=False, allow_nan=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(encoded)))
        if download:
            self.send_header("Content-Disposition", "attachment; filename=audio-profile.json; filename*=UTF-8''" + quote(download))
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(encoded)

    def read_body(self, limit: int) -> bytes:
        if self.headers.get("Transfer-Encoding"):
            raise ApiError("Используйте Content-Length.", 411)
        try:
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError as error:
            raise ApiError("Некорректный Content-Length.") from error
        if length < 0 or length > limit:
            raise ApiError("Запрос слишком большой.", 413)
        if not length:
            return b""
        self.connection.settimeout(120)
        result = self.rfile.read(length)
        if len(result) != length:
            raise ApiError("Передача файла прервана.")
        return result

    def read_object(self) -> dict:
        try:
            value = json.loads(self.read_body(MAX_JSON).decode("utf-8-sig"))
        except (ValueError, UnicodeError) as error:
            raise ApiError("Не удалось прочитать JSON.") from error
        if not isinstance(value, dict):
            raise ApiError("Ожидался JSON-объект.")
        return value

    def do_HEAD(self) -> None:
        self.do_GET()

    def do_GET(self) -> None:
        try:
            self.check_request()
            path = unquote(urlsplit(self.path).path)
            if path == "/api/health":
                self.json_response(self.desk.health())
            elif path == "/api/state":
                self.json_response(self.desk.state())
            elif path == "/api/profile/export":
                self.json_response(self.desk.export_profile(), download=safe_name(self.desk.profile.get("name", "audio-profile")) + ".json")
            elif path.startswith("/api/audio/"):
                _, source = self.desk.get_file(path.removeprefix("/api/audio/"))
                self.stream_file(source)
            elif path.startswith("/api/preview/"):
                source = self.desk.preview(path.removeprefix("/api/preview/"))
                self.stream_file(source)
            elif path.startswith("/api/waveform/"):
                self.json_response(self.desk.waveform(path.removeprefix("/api/waveform/")))
            elif path.startswith("/api/"):
                raise ApiError("API не найден.", 404)
            else:
                requested = "index.html" if path in {"", "/"} else path.lstrip("/")
                web_root = self.desk.resource_file("web").resolve()
                source = (web_root / requested).resolve()
                if not source.is_relative_to(web_root) or not source.is_file():
                    raise ApiError("Файл не найден.", 404)
                self.stream_file(source)
        except ApiError as error:
            self.json_response({"error": str(error)}, error.status)
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass
        except Exception as error:
            self.json_response({"error": "Внутренняя ошибка: " + str(error)}, 500)

    def do_POST(self) -> None:
        try:
            self.check_request()
            path = urlsplit(self.path).path
            if path == "/api/upload":
                result = self.desk.upload(unquote(self.headers.get("X-Filename", "audio.wav")), self.read_body(MAX_UPLOAD))
            else:
                payload = self.read_object()
                if path == "/api/assign":
                    result = self.desk.assign(str(payload.get("slot_id", "")), payload.get("assignment"))
                elif path == "/api/profile/save":
                    result = self.desk.save_profile(payload.get("name", self.desk.profile.get("name")))
                elif path == "/api/profile/load":
                    result = self.desk.load_profile(payload.get("name"))
                elif path == "/api/profile/import":
                    result = self.desk.import_profile(payload)
                elif path == "/api/profile/export":
                    result = self.desk.save_export(payload.get("name"))
                elif path == "/api/library/scan":
                    result = self.desk.scan(str(payload.get("path", "")))
                elif path == "/api/suggest":
                    result = self.desk.suggest()
                else:
                    raise ApiError("API не найден.", 404)
            self.json_response(result)
        except ApiError as error:
            self.json_response({"error": str(error)}, error.status)
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass
        except Exception as error:
            self.json_response({"error": "Внутренняя ошибка: " + str(error)}, 500)

    def stream_file(self, path: Path) -> None:
        size = path.stat().st_size
        start, end = 0, size - 1
        partial = False
        header = self.headers.get("Range")
        if header:
            match = re.fullmatch(r"bytes=(\d*)-(\d*)", header.strip())
            if not match or not size or not any(match.groups()):
                return self.range_error(size)
            first, last = match.groups()
            if first:
                start = int(first)
                end = min(int(last), size - 1) if last else size - 1
            else:
                count = int(last)
                if count <= 0:
                    return self.range_error(size)
                start = max(0, size - count)
            if start > end or start >= size:
                return self.range_error(size)
            partial = True
        self.send_response(206 if partial else 200)
        mime = mimetypes.guess_type(str(path))[0] or "application/octet-stream"
        overrides = {".js": "application/javascript; charset=utf-8", ".wav": "audio/wav", ".aif": "audio/aiff", ".aiff": "audio/aiff", ".opus": "audio/ogg"}
        self.send_header("Content-Type", overrides.get(path.suffix.lower(), mime))
        self.send_header("Content-Length", str(max(0, end - start + 1)))
        self.send_header("Accept-Ranges", "bytes")
        if partial:
            self.send_header("Content-Range", f"bytes {start}-{end}/{size}")
        self.end_headers()
        if self.command == "HEAD":
            return
        with path.open("rb") as stream:
            stream.seek(start)
            remaining = end - start + 1
            while remaining > 0:
                chunk = stream.read(min(256 * 1024, remaining))
                if not chunk:
                    break
                self.wfile.write(chunk)
                remaining -= len(chunk)

    def range_error(self, size: int) -> None:
        self.send_response(416)
        self.send_header("Content-Range", f"bytes */{size}")
        self.send_header("Content-Length", "0")
        self.end_headers()


def main(argv: list[str] | None = None) -> int:
    # Frozen Python ignores PYTHONIOENCODING. Launcher logs must also accept
    # Unicode installation and profile paths on Windows with an ANSI locale.
    for stream in (sys.stdout, sys.stderr):
        if stream is not None and hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="replace")
    parser = argparse.ArgumentParser(description="Local audio audition and approval desk. No Unreal/project writes.")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--app-dir", type=Path, default=APP_DIR, help="Portable installation root containing library and data")
    parser.add_argument("--data-dir", type=Path, help="User data folder; defaults to <app-dir>/data")
    parser.add_argument("--open-browser", action="store_true", help="Open the local application after startup")
    parser.add_argument("--scan", action="append", default=[], metavar="AUDIO_FOLDER")
    parser.add_argument("--suggest", action="store_true", help="Seed empty slots using filename and metadata context")
    parser.add_argument("--prepare", action="store_true", help="Scan/suggest and exit without starting server")
    parser.add_argument("--inspect", action="store_true", help="Print library/profile counts and exit")
    args = parser.parse_args(argv)
    app_root = args.app_dir.resolve()
    desk = AudioDesk(args.data_dir or app_root / "data", app_dir=app_root)
    for path in args.scan:
        result = desk.scan(path)
        print(json.dumps({"scanned": path, "count": result["count"], "errors": result["errors"]}, ensure_ascii=False), flush=True)
    if args.suggest:
        print(json.dumps({"suggested": desk.suggest()["count"]}, ensure_ascii=False), flush=True)
    if args.inspect or args.prepare:
        print(json.dumps({"library_count": len(desk.library), "assignment_count": len(desk.profile["assignments"]), "data_dir": str(desk.data_dir)}, ensure_ascii=False), flush=True)
        return 0
    if not 1 <= args.port <= 65535:
        parser.error("port must be between 1 and 65535")
    server = DeskServer(("127.0.0.1", args.port), desk)
    print(f"Audio Desk: http://127.0.0.1:{args.port}/", flush=True)
    print(f"Data: {desk.data_dir}\nStop: Ctrl+C. Project writes are disabled.", flush=True)
    if args.open_browser:
        threading.Timer(0.15, lambda: webbrowser.open(f"http://127.0.0.1:{args.port}/", new=2)).start()
    try:
        server.serve_forever(poll_interval=0.3)
    except KeyboardInterrupt:
        print("\nAudio Desk stopped. Profile saved.")
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except ApiError as error:
        print(str(error), file=sys.stderr)
        raise SystemExit(1)
