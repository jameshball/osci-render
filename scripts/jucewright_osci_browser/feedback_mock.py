from __future__ import annotations

import json
import struct
import threading
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse


class FeedbackMockServer:
    def __init__(self, artifact_dir: Path):
        self.artifact_dir = artifact_dir
        self.upload_descriptors: dict[str, dict] = {}
        self.uploads: dict[str, bytes] = {}
        self.submission: dict | None = None
        self.errors: list[str] = []
        self.server = ThreadingHTTPServer(("127.0.0.1", 0), self._handler())
        self.thread = threading.Thread(target=self.server.serve_forever, name="feedback-mock", daemon=True)

    @property
    def base_url(self) -> str:
        return f"http://127.0.0.1:{self.server.server_port}"

    def start(self) -> None:
        self.thread.start()

    def stop(self) -> None:
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=5)
        sanitized_submission = None
        if self.submission is not None:
            sanitized_submission = dict(self.submission)
            if sanitized_submission.get("license_token"):
                sanitized_submission["license_token"] = "<redacted>"
        payload = {
            "descriptors": list(self.upload_descriptors.values()),
            "uploadSizes": {key: len(value) for key, value in self.uploads.items()},
            "submission": sanitized_submission,
            "errors": self.errors,
        }
        (self.artifact_dir / "feedback-mock.json").write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
        for index, (upload_id, body) in enumerate(self.uploads.items(), start=1):
            descriptor = self.upload_descriptors.get(upload_id, {})
            if descriptor.get("kind") != "screenshot":
                continue
            filename = Path(str(descriptor.get("filename") or f"screenshot-{index}.png")).name
            (self.artifact_dir / f"submitted-{index}-{filename}").write_bytes(body)

    def assert_valid_submission(self) -> str:
        if self.errors:
            raise AssertionError("; ".join(self.errors))
        if self.submission is None:
            raise AssertionError("feedback submission was not received")
        required = {
            "kind", "title", "description", "contact_email", "product_version",
            "platform", "os_name", "os_version", "architecture",
        }
        missing = sorted(required - self.submission.keys())
        if missing:
            raise AssertionError(f"submission missing required fields: {missing}")
        if self.submission.get("title") != "Automation feedback report":
            raise AssertionError("feedback title did not round-trip")
        if not self.submission.get("osci_render_log"):
            raise AssertionError("diagnostic log was not supplied")
        if not isinstance(self.submission.get("client_context"), dict):
            raise AssertionError("client context was not supplied")
        platform = str(self.submission.get("platform") or "")
        architecture = self.submission.get("architecture")
        if platform.endswith("-arm64") and architecture != "arm64":
            raise AssertionError("ARM64 build reported the wrong architecture")

        submitted_ids = self.submission.get("upload_ids") or []
        descriptors = [self.upload_descriptors.get(value) for value in submitted_ids]
        kinds = [descriptor.get("kind") for descriptor in descriptors if descriptor]
        if kinds.count("project") != 1:
            raise AssertionError("exactly one project snapshot was not uploaded")
        if kinds.count("screenshot") < 2:
            raise AssertionError("automatic and user screenshots were not both uploaded")
        for upload_id, descriptor in self.upload_descriptors.items():
            body = self.uploads.get(upload_id)
            if body is None:
                raise AssertionError(f"upload {upload_id} was not received")
            if descriptor["kind"] == "screenshot" and not body.startswith(b"\x89PNG\r\n\x1a\n"):
                raise AssertionError("screenshot was not normalized to PNG")
            if descriptor["kind"] == "project":
                if len(body) < 9 or body[:4] != struct.pack("<I", 0x21324356):
                    raise AssertionError("project did not use JUCE binary XML")
                xml_length = struct.unpack("<I", body[4:8])[0]
                project_xml = body[8:8 + xml_length].decode("utf-8")
                if "projectFilePath" in project_xml:
                    raise AssertionError("project snapshot leaked projectFilePath")
        return "feedback mock received complete sanitized submission"

    def _handler(self):
        owner = self

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, _format: str, *_args) -> None:
                return

            def _json_body(self) -> dict:
                size = int(self.headers.get("Content-Length", "0"))
                return json.loads(self.rfile.read(size).decode("utf-8"))

            def _send(self, status: int, payload: dict) -> None:
                data = json.dumps(payload).encode("utf-8")
                self.send_response(status)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(data)))
                self.end_headers()
                self.wfile.write(data)

            def do_POST(self) -> None:
                path = urlparse(self.path).path
                try:
                    body = self._json_body()
                    if path.endswith("/feedback/uploads"):
                        uploads = []
                        for descriptor in body.get("files", []):
                            upload_id = str(uuid.uuid4())
                            owner.upload_descriptors[upload_id] = descriptor
                            uploads.append({
                                "id": upload_id,
                                "kind": descriptor["kind"],
                                "url": f"{owner.base_url}/upload/{upload_id}?signature=feedback-test",
                                "method": "PUT",
                                "headers": {"Content-Type": descriptor["content_type"]},
                            })
                        self._send(201, {"success": True, "data": {"uploads": uploads}})
                        return
                    if path.endswith("/feedback"):
                        owner.submission = body
                        self._send(201, {"success": True, "data": {
                            "reference": "FB-AUTOMATION",
                            "verified_customer": True,
                            "created_at": "2026-07-11T12:00:00Z",
                        }})
                        return
                    self._send(404, {"success": False, "error": "not found"})
                except Exception as exc:
                    owner.errors.append(str(exc))
                    self._send(500, {"success": False, "error": "mock failure"})

            def do_PUT(self) -> None:
                parsed = urlparse(self.path)
                upload_id = parsed.path.rsplit("/", 1)[-1]
                descriptor = owner.upload_descriptors.get(upload_id)
                if descriptor is None:
                    self._send(404, {"success": False, "error": "unknown upload"})
                    return
                if parsed.query != "signature=feedback-test":
                    owner.errors.append("signed upload query was not preserved")
                    self._send(400, {"success": False, "error": "invalid upload signature"})
                    return
                size = int(self.headers.get("Content-Length", "0"))
                owner.uploads[upload_id] = self.rfile.read(size)
                if self.headers.get("Content-Type") != descriptor["content_type"]:
                    owner.errors.append(f"content type mismatch for {upload_id}")
                self.send_response(200)
                self.send_header("Content-Length", "0")
                self.end_headers()

        return Handler
