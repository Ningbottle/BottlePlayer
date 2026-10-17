"""Read-only real-DLL smoke probe, always using a new isolated profile."""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import time
import uuid


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dll", type=Path, required=True)
    parser.add_argument("--dependencies", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    dll_path = args.dll.resolve(strict=True)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    dependencies = (args.dependencies or dll_path.parent).resolve(strict=True)
    dll_directory = os.add_dll_directory(str(dependencies))
    native = ctypes.CDLL(str(dll_path))
    native.EchoInitializeWithPathsV2.argtypes = [ctypes.c_char_p]
    native.EchoInitializeWithPathsV2.restype = ctypes.c_int
    native.EchoHandleRequest.argtypes = [ctypes.c_char_p] * 5 + [ctypes.POINTER(ctypes.c_void_p)]
    native.EchoHandleRequest.restype = None
    native.EchoFreeString.argtypes = [ctypes.c_void_p]
    native.EchoFreeString.restype = None
    native.EchoShutdown.argtypes = []
    native.EchoShutdown.restype = ctypes.c_int
    native.EchoNativeBuildFingerprint.argtypes = []
    native.EchoNativeBuildFingerprint.restype = ctypes.c_char_p

    def request(path, query=None):
        pointer = ctypes.c_void_p()
        begin = time.monotonic()
        try:
            native.EchoHandleRequest(b"GET", path.encode(),
                json.dumps(query or {}, ensure_ascii=False).encode(),
                json.dumps({"x-echo-request-id": str(uuid.uuid4())}).encode(), b"", ctypes.byref(pointer))
            if not pointer.value:
                raise RuntimeError("Empty C ABI response")
            envelope = json.loads(ctypes.string_at(pointer.value))
            body = envelope.get("body", {})
            if isinstance(body, str):
                body = json.loads(body)
            if not isinstance(body, dict):
                raise RuntimeError("Invalid C ABI response body")
            return envelope.get("status", 0), body, round((time.monotonic() - begin) * 1000)
        finally:
            if pointer.value:
                native.EchoFreeString(pointer)

    report = {"dll_sha256": hashlib.sha256(dll_path.read_bytes()).hexdigest(), "checks": []}
    initialized = False
    try:
        if native.EchoInitializeWithPathsV2(str(output / "native").encode()) != 0:
            raise RuntimeError("Isolated native initialization failed")
        initialized = True
        report["fingerprint"] = native.EchoNativeBuildFingerprint().decode()
        status, body, elapsed = request("/health")
        report["checks"].append({"name": "native_health", "ok": status == 200 and body.get("status") == 1, "elapsed_ms": elapsed})
        status, body, elapsed = request("/search", {"keywords": "周杰伦", "type": "song", "page": 1, "pagesize": 5})
        data = body.get("data", {})
        rows = data.get("info", []) if isinstance(data, dict) else []
        report["checks"].append({"name": "real_public_search", "ok": status == 200 and body.get("status") == 1 and bool(rows), "http_status": status, "tracks": len(rows), "elapsed_ms": elapsed})
        # Returned media URLs, account fields and full response payloads are not saved.
        status, body, elapsed = request("/diagnostics/memory")
        report["checks"].append({"name": "native_memory", "ok": status == 200 and body.get("status") == 1, "elapsed_ms": elapsed})
        data = body.get("data", {})
        report["process_memory"] = {key: data.get(key) for key in ("working_set_bytes", "private_bytes")}
    except Exception as error:
        report["failure_type"] = type(error).__name__
    finally:
        if initialized:
            report["shutdown"] = native.EchoShutdown()
        dll_directory.close()
        report["ok"] = len(report["checks"]) == 3 and all(row["ok"] for row in report["checks"]) and report.get("shutdown") == 0
        (output / "report.json").write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
        print(json.dumps(report, ensure_ascii=False))
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
