"""Project-only, loopback review board. Python standard library; no external services."""
from __future__ import annotations

import argparse
import copy
from datetime import datetime, timezone
from email.utils import formatdate
import json
import logging
import math
import os
from pathlib import Path
import re
import tempfile
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import unquote, urlsplit
from uuid import uuid4

HERE = Path(__file__).resolve().parent
MEDIA_ROOT = Path("E:/CodexWork/MessControl")
DEFAULT_DATA = MEDIA_ROOT / "ReviewBoard/reviews.json"
DEFAULT_LOG = MEDIA_ROOT / "ReviewBoard/server.log"
TEMP_ROOT = Path("E:/CodexTemp/MessControlReviewBoard")
EXTENSIONS = {".mp4": "video/mp4", ".png": "image/png", ".jpg": "image/jpeg", ".webp": "image/webp", ".gif": "image/gif"}
ID = re.compile(r"^[a-zA-Z0-9_-]{1,80}$")
STATUSES = {"accepted", "rework", "rejected", "pending"}
STATIC_FILES = {"/": ("index.html", "text/html; charset=utf-8"),
                "/index.html": ("index.html", "text/html; charset=utf-8"),
                "/app.js": ("app.js", "text/javascript; charset=utf-8"),
                "/styles.css": ("styles.css", "text/css; charset=utf-8")}
LOG = logging.getLogger("washnsmash.review")


def utc_now():
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def finite_json(text):
    def reject(value):
        raise ValueError("Недопустимое число JSON: " + value)
    return json.loads(text, parse_constant=reject)


def in_work_root(path):
    resolved = Path(path).resolve()
    if not Path(path).is_absolute() or not resolved.is_relative_to(MEDIA_ROOT.resolve()):
        raise ValueError("Путь должен находиться внутри E:/CodexWork/MessControl")
    return resolved


def media_path(value):
    if not isinstance(value, str):
        raise ValueError("Некорректный путь manifest")
    path = in_work_root(value)
    if path.suffix.lower() not in EXTENSIONS:
        raise ValueError("Manifest разрешает только mp4/png/jpg/webp/gif")
    return path


def load_seed(path):
    if path.stat().st_size > 2 * 1024 * 1024:
        raise ValueError("Manifest слишком большой")
    seed = finite_json(path.read_text(encoding="utf-8-sig"))
    if not isinstance(seed, dict) or seed.get("schemaVersion") != 1:
        raise ValueError("Неизвестная версия manifest")
    if not isinstance(seed.get("project"), dict):
        raise ValueError("Нет проекта в manifest")
    manifest = seed.get("media", [])
    tasks = seed.get("tasks", [])
    tags = seed.get("allowedTags", [])
    if not isinstance(manifest, list) or not isinstance(tasks, list) or not isinstance(tags, list):
        raise ValueError("Некорректный manifest")
    if not all(isinstance(tag, str) and 0 < len(tag) <= 60 for tag in tags):
        raise ValueError("Некорректные теги")
    media = {}
    for entry in manifest:
        if not isinstance(entry, dict) or not isinstance(entry.get("id"), str) or not ID.fullmatch(entry["id"]):
            raise ValueError("Некорректный media ID")
        if entry["id"] in media:
            raise ValueError("Повтор media ID")
        resolved = media_path(entry.get("path"))
        expected_kind = "video" if resolved.suffix.lower() == ".mp4" else "image"
        if entry.get("kind") != expected_kind:
            raise ValueError("Тип media не совпадает с расширением")
        media[entry["id"]] = entry
    task_ids = set()
    for task in tasks:
        if not isinstance(task, dict) or not isinstance(task.get("id"), str) or not ID.fullmatch(task["id"]):
            raise ValueError("Некорректный task ID")
        if task["id"] in task_ids:
            raise ValueError("Повтор task ID")
        task_ids.add(task["id"])
        versions = task.get("versions")
        if not isinstance(versions, list) or not versions:
            raise ValueError("Задаче нужна версия")
        seen = set()
        for version in versions:
            version_id = version.get("id") if isinstance(version, dict) else None
            if not isinstance(version_id, str) or not ID.fullmatch(version_id) or version_id in seen:
                raise ValueError("Некорректный version ID")
            seen.add(version_id)
            ids = version.get("mediaIds", [])
            if not isinstance(ids, list) or not all(isinstance(item, str) and item in media for item in ids):
                raise ValueError("Неизвестный media ID у версии")
    return seed, media


class FeedbackStore:
    def __init__(self, path):
        self.path = in_work_root(path)
        if self.path.suffix.lower() != ".json":
            raise ValueError("Хранилище должно быть JSON")
        self.lock = threading.Lock()
        self.path.parent.mkdir(parents=True, exist_ok=True)
        if self.path.exists():
            if self.path.stat().st_size > 32 * 1024 * 1024:
                raise ValueError("Хранилище слишком большое; исходный файл сохранён")
            value = finite_json(self.path.read_text(encoding="utf-8-sig"))
            if not isinstance(value, dict) or value.get("schemaVersion") != 1 or not isinstance(value.get("feedback"), list):
                raise ValueError("Неизвестное хранилище; исходный файл сохранён")
            if not all(isinstance(item, dict) and item.get("author") == "user" for item in value["feedback"]):
                raise ValueError("Некорректная история; исходный файл сохранён")
            self.entries = value["feedback"]
        else:
            self.entries = []

    def snapshot(self):
        with self.lock:
            return copy.deepcopy(self.entries)

    def append(self, entry):
        with self.lock:
            entries = self.entries + [entry]
            payload = json.dumps({"schemaVersion": 1, "feedback": entries}, ensure_ascii=False, indent=2, allow_nan=False)
            temporary = None
            try:
                with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", dir=self.path.parent,
                                                 prefix=self.path.name + ".", suffix=".tmp", delete=False) as stream:
                    temporary = Path(stream.name)
                    stream.write(payload)
                    stream.flush()
                    os.fsync(stream.fileno())
                os.replace(temporary, self.path)
                self.entries = entries
            finally:
                if temporary is not None and temporary.exists():
                    temporary.unlink()


def public_state(seed, manifest, feedback):
    result = {"project": copy.deepcopy(seed["project"]), "tasks": copy.deepcopy(seed["tasks"]),
              "feedback": feedback, "allowedTags": copy.deepcopy(seed["allowedTags"])}
    for task in result["tasks"]:
        for version in task["versions"]:
            version["media"] = []
            for media_id in version.pop("mediaIds", []):
                entry = manifest[media_id]
                path = media_path(entry["path"])
                version["media"].append({"id": media_id, "kind": entry["kind"],
                    "label": str(entry.get("label", media_id)), "url": "/media/" + media_id,
                    "available": path.is_file()})
    return result


def validate_feedback(body, seed):
    allowed = {"taskId", "versionId", "score", "status", "comment", "tags", "timecodeSeconds"}
    if not isinstance(body, dict) or set(body) - allowed:
        raise ValueError("Неизвестные поля отзыва")
    task = next((item for item in seed["tasks"] if item["id"] == body.get("taskId")), None)
    version = next((item for item in task["versions"] if item["id"] == body.get("versionId")), None) if task else None
    if version is None:
        raise ValueError("Неизвестная задача или версия")
    score = body.get("score")
    if type(score) is not int or not 0 <= score <= 10:
        raise ValueError("Выберите вашу оценку от0 до10")
    status = body.get("status")
    if not isinstance(status, str) or status not in STATUSES:
        raise ValueError("Выберите статус отзыва")
    comment = body.get("comment", "")
    if not isinstance(comment, str) or len(comment) > 5000:
        raise ValueError("Комментарий должен быть не длиннее5000 символов")
    tags = body.get("tags", [])
    if not isinstance(tags, list) or len(tags) > 12 or not all(isinstance(tag, str) and tag in seed["allowedTags"] for tag in tags):
        raise ValueError("Неизвестный тег")
    timestamp = body.get("timecodeSeconds")
    if timestamp is not None and (type(timestamp) not in (int, float) or not math.isfinite(timestamp) or not 0 <= timestamp <= 86400):
        raise ValueError("Некорректный таймкод")
    return {"id": uuid4().hex, "taskId": task["id"], "versionId": version["id"], "score": score,
            "status": status, "comment": comment.strip(), "tags": list(dict.fromkeys(tags)),
            "timecodeSeconds": timestamp, "createdAt": utc_now(), "author": "user"}


class ReviewServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True

    def __init__(self, seed_path, data_path, port):
        self.seed_path = Path(seed_path).resolve()
        load_seed(self.seed_path)
        self.store = FeedbackStore(data_path)
        super().__init__(("127.0.0.1", port), ReviewHandler)


class ReviewHandler(BaseHTTPRequestHandler):
    server_version = "WashnSmashReview/1"
    sys_version = ""
    protocol_version = "HTTP/1.1"

    def log_message(self, format, *args):
        LOG.info("%s %s", self.client_address[0], format % args)

    def end_headers(self):
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("X-Frame-Options", "DENY")
        self.send_header("Referrer-Policy", "no-referrer")
        self.send_header("Content-Security-Policy", "default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self'; media-src 'self'; connect-src 'self'; object-src 'none'; frame-ancestors 'none'; base-uri 'none'; form-action 'self'")
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def json_reply(self, status, value, head=False):
        payload = json.dumps(value, ensure_ascii=False, allow_nan=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        if not head:
            self.wfile.write(payload)

    def route(self):
        expected = "127.0.0.1:" + str(self.server.server_port)
        if self.headers.get("Host") != expected:
            self.json_reply(403, {"error": "Используйте локальный адрес127.0.0.1"}, self.command == "HEAD")
            return None
        parts = urlsplit(self.path)
        if parts.scheme or parts.netloc:
            self.json_reply(400, {"error": "Некорректный адрес"}, self.command == "HEAD")
            return None
        return unquote(parts.path)

    def do_HEAD(self):
        self.do_GET(head=True)

    def do_GET(self, head=False):
        route = self.route()
        if route is None:
            return
        try:
            if route in STATIC_FILES:
                name, content_type = STATIC_FILES[route]
                payload = (HERE / name).read_bytes()
                self.send_response(200)
                self.send_header("Content-Type", content_type)
                self.send_header("Content-Length", str(len(payload)))
                self.end_headers()
                if not head:
                    self.wfile.write(payload)
            elif route == "/api/state":
                seed, manifest = load_seed(self.server.seed_path)
                self.json_reply(200, public_state(seed, manifest, self.server.store.snapshot()), head)
            elif route.startswith("/media/") and ID.fullmatch(route[7:]):
                _, manifest = load_seed(self.server.seed_path)
                entry = manifest.get(route[7:])
                if entry is None:
                    self.json_reply(404, {"error": "Медиа нет в manifest"}, head)
                else:
                    self.serve_media(media_path(entry["path"]), head)
            else:
                self.json_reply(404, {"error": "Страница не найдена"}, head)
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass
        except Exception:
            LOG.exception("Read request failed")
            self.json_reply(500, {"error": "Не удалось прочитать состояние. Проверьте локальный журнал."}, head)

    def serve_media(self, path, head):
        if not path.is_file():
            self.json_reply(404, {"error": "Файл ещё не добавлен в локальную папку"}, head)
            return
        stat = path.stat()
        size = stat.st_size
        start, end, status = 0, size - 1, 200
        requested = self.headers.get("Range")
        if requested:
            match = re.fullmatch(r"bytes=(\d*)-(\d*)", requested.strip())
            try:
                if match is None or not any(match.groups()) or size == 0:
                    raise ValueError()
                first, last = match.groups()
                if first:
                    start = int(first)
                    end = min(int(last), size - 1) if last else size - 1
                else:
                    length = int(last)
                    if length <= 0:
                        raise ValueError()
                    start, end = max(0, size - length), size - 1
                if start >= size or start < 0 or end < start:
                    raise ValueError()
                status = 206
            except ValueError:
                self.send_response(416)
                self.send_header("Content-Range", "bytes */" + str(size))
                self.send_header("Content-Length", "0")
                self.end_headers()
                return
        self.send_response(status)
        self.send_header("Content-Type", EXTENSIONS[path.suffix.lower()])
        self.send_header("Accept-Ranges", "bytes")
        self.send_header("Content-Length", str(max(0, end - start + 1)))
        self.send_header("Last-Modified", formatdate(stat.st_mtime, usegmt=True))
        if status == 206:
            self.send_header("Content-Range", f"bytes {start}-{end}/{size}")
        self.end_headers()
        if head:
            return
        with path.open("rb") as stream:
            stream.seek(start)
            remaining = end - start + 1
            while remaining > 0:
                chunk = stream.read(min(64 * 1024, remaining))
                if not chunk:
                    break
                self.wfile.write(chunk)
                remaining -= len(chunk)

    def do_POST(self):
        route = self.route()
        if route is None:
            return
        self.close_connection = True
        if route != "/api/feedback":
            self.json_reply(404, {"error": "Нет такого действия"})
            return
        expected = "http://127.0.0.1:" + str(self.server.server_port)
        if self.headers.get("Origin") not in (None, expected):
            self.json_reply(403, {"error": "Разрешена только локальная форма"})
            return
        if self.headers.get("Content-Type", "").split(";", 1)[0].strip().lower() != "application/json":
            self.json_reply(415, {"error": "Ожидается JSON"})
            return
        try:
            length = int(self.headers.get("Content-Length", "-1"))
            if not 0 < length <= 64 * 1024 or self.headers.get("Transfer-Encoding"):
                self.json_reply(413, {"error": "Некорректный размер отзыва"})
                return
            body = finite_json(self.rfile.read(length).decode("utf-8"))
            seed, _ = load_seed(self.server.seed_path)
            entry = validate_feedback(body, seed)
            self.server.store.append(entry)
            self.json_reply(201, {"feedback": entry})
        except (ValueError, UnicodeError) as error:
            self.json_reply(400, {"error": str(error)})
        except Exception:
            LOG.exception("Feedback could not be saved")
            self.json_reply(500, {"error": "Отзыв не сохранён. Проверьте локальный журнал."})

    def do_OPTIONS(self):
        self.json_reply(405, {"error": "Cross-origin доступ отключён"})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--data", type=Path, default=DEFAULT_DATA)
    parser.add_argument("--log", type=Path, default=DEFAULT_LOG)
    args = parser.parse_args()
    if not 1 <= args.port <= 65535:
        parser.error("port must be1..65535")
    TEMP_ROOT.mkdir(parents=True, exist_ok=True)
    os.environ["TEMP"] = os.environ["TMP"] = str(TEMP_ROOT)
    log_path = in_work_root(args.log)
    log_path.parent.mkdir(parents=True, exist_ok=True)
    logging.basicConfig(filename=log_path, encoding="utf-8", level=logging.INFO,
                        format="%(asctime)s %(levelname)s %(message)s")
    server = ReviewServer(HERE / "seed.json", args.data, args.port)
    print(f"Wash'n'Smash review: http://127.0.0.1:{server.server_port}")
    print(f"Feedback: {server.store.path}\nLog: {log_path}\nCtrl+C to stop.")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
