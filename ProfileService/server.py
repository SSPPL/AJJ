#!/usr/bin/env python3
"""Local account profile service for the AJB PC Port listen server tests.

Standard library only, no third party runtime dependency.

Endpoints
    POST /login         {"account_id": "...", "password": "..."} -> {"token": "...", "expires_at": "..."}
    GET  /profile?token=<token>                                  -> public profile JSON
    POST /save-profile  {"token": "...", "profile": {...}}       -> {"saved": true, "profile": {...}}
    GET  /health                                                 -> {"status": "ok"}

The password is never returned by any endpoint. Authentication is intentionally
permissive for this local test service: a token is issued for any account that
exists in accounts.json without verifying the password. The wire shape, the
validation rules and the error handling match the documented contract, while the
service stays a local fixture rather than a production auth boundary.

Run:
    py ProfileService\\server.py --host 0.0.0.0 --port 8080
"""

from __future__ import annotations

import argparse
import json
import os
import secrets
import sys
import threading
from datetime import datetime, timedelta, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

SERVICE_DIR = os.path.dirname(os.path.abspath(__file__))
ACCOUNTS_PATH = os.path.join(SERVICE_DIR, "accounts.json")
PROFILES_DIR = os.path.join(SERVICE_DIR, "profiles")

# Short lived, per the contract. Overridable with AJB_PROFILE_TOKEN_TTL so the
# expired-token path can be exercised in a test without waiting five minutes.
TOKEN_TTL_SECONDS = int(os.environ.get("AJB_PROFILE_TOKEN_TTL", "300"))
MAX_BODY_BYTES = 64 * 1024

# The exact public field set. Anything outside this list is rejected by
# /save-profile instead of being silently stored.
PROFILE_TEXT_FIELDS = ("account_id", "name", "title", "game_server_user_id")
PROFILE_INT_RANGES = {
    "icon_id": (0, 4096),
    "level": (0, 9999),
}
PROFILE_CUSTOM_DATA_FIELDS = ("chara_skin_id", "stand_skin_id", "kill_count")

EMOTE_FIELDS = ("emote_id", "voice_id", "emote_name", "voice_name")

_lock = threading.Lock()
# token -> (account_id, expires_at)
_tokens: dict[str, tuple[str, datetime]] = {}


class ServiceError(Exception):
    """Raised for every expected failure, mapped to a status code and a message."""

    def __init__(self, status: int, message: str) -> None:
        super().__init__(message)
        self.status = status
        self.message = message


def load_json_file(path: str) -> dict:
    with open(path, "r", encoding="utf-8") as handle:
        return json.load(handle)


def load_accounts() -> dict:
    try:
        accounts = load_json_file(ACCOUNTS_PATH)
    except FileNotFoundError as exc:
        raise ServiceError(500, "accounts.json is missing") from exc
    except json.JSONDecodeError as exc:
        raise ServiceError(500, "accounts.json is not valid JSON") from exc

    if not isinstance(accounts, dict):
        raise ServiceError(500, "accounts.json must be an object of account_id -> record")

    return accounts


def account_ids() -> list[str]:
    return sorted(load_accounts().keys())


def profile_path(account_id: str) -> str:
    # The account id is used as a file name, so it must never contain a path
    # separator or a parent directory hop.
    if not account_id or "/" in account_id or "\\" in account_id or account_id in (".", ".."):
        raise ServiceError(400, "illegal account id")

    return os.path.join(PROFILES_DIR, f"{account_id}.json")


def load_profile(account_id: str) -> dict:
    path = profile_path(account_id)

    try:
        profile = load_json_file(path)
    except FileNotFoundError as exc:
        raise ServiceError(404, f"no profile exists for account {account_id}") from exc
    except json.JSONDecodeError as exc:
        raise ServiceError(500, f"the profile for {account_id} is not valid JSON") from exc

    if not isinstance(profile, dict):
        raise ServiceError(500, f"the profile for {account_id} must be an object")

    return profile


def issue_token(account_id: str) -> dict:
    token = secrets.token_urlsafe(24)
    expires_at = datetime.now(timezone.utc) + timedelta(seconds=TOKEN_TTL_SECONDS)

    with _lock:
        _tokens[token] = (account_id, expires_at)

    return {"token": token, "expires_at": expires_at.strftime("%Y-%m-%dT%H:%M:%SZ")}


def account_for_token(token: str) -> str:
    if not token:
        raise ServiceError(400, "a token is required")

    with _lock:
        entry = _tokens.get(token)

    if entry is None:
        raise ServiceError(401, "unknown token")

    account_id, expires_at = entry

    if datetime.now(timezone.utc) >= expires_at:
        with _lock:
            _tokens.pop(token, None)
        raise ServiceError(401, "expired token")

    return account_id


def validate_profile(profile: dict, expected_account_id: str | None) -> dict:
    if not isinstance(profile, dict):
        raise ServiceError(400, "profile must be an object")

    unknown = set(profile.keys()) - set(PROFILE_TEXT_FIELDS) - set(PROFILE_INT_RANGES) - {"custom_data"}
    if unknown:
        raise ServiceError(400, f"illegal profile field(s): {', '.join(sorted(unknown))}")

    account_id = profile.get("account_id")
    if not isinstance(account_id, str) or not account_id:
        raise ServiceError(400, "account_id is required")

    if expected_account_id is not None and account_id != expected_account_id:
        raise ServiceError(400, "account_id does not match the token")

    name = profile.get("name")
    if not isinstance(name, str) or not name.strip():
        raise ServiceError(400, "name is required")

    title = profile.get("title", "")
    if not isinstance(title, str):
        raise ServiceError(400, "title must be a string")

    user_id = profile.get("game_server_user_id", "")
    if not isinstance(user_id, str):
        raise ServiceError(400, "game_server_user_id must be a string")

    cleaned: dict = {"account_id": account_id, "name": name, "title": title, "game_server_user_id": user_id}

    for field, (low, high) in PROFILE_INT_RANGES.items():
        value = profile.get(field)
        if not isinstance(value, int) or isinstance(value, bool):
            raise ServiceError(400, f"{field} is required and must be an integer")
        if value < low or value > high:
            raise ServiceError(400, f"{field} is out of range ({low}..{high})")
        cleaned[field] = value

    custom_data = profile.get("custom_data", {})
    if not isinstance(custom_data, dict):
        raise ServiceError(400, "custom_data must be an object")

    unknown_custom = set(custom_data.keys()) - set(PROFILE_CUSTOM_DATA_FIELDS) - {"emotes"}
    if unknown_custom:
        raise ServiceError(400, f"illegal custom_data field(s): {', '.join(sorted(unknown_custom))}")

    cleaned_custom: dict = {}
    for field in PROFILE_CUSTOM_DATA_FIELDS:
        value = custom_data.get(field, 0)
        if not isinstance(value, int) or isinstance(value, bool):
            raise ServiceError(400, f"custom_data.{field} must be an integer")
        if value < 0 or value > 255:
            raise ServiceError(400, f"custom_data.{field} is out of range (0..255)")
        cleaned_custom[field] = value

    emotes = custom_data.get("emotes", [])
    if not isinstance(emotes, list):
        raise ServiceError(400, "custom_data.emotes must be an array")
    if len(emotes) > 64:
        raise ServiceError(400, "custom_data.emotes may hold at most 64 entries")

    cleaned_emotes: list[dict] = []
    for emote in emotes:
        if not isinstance(emote, dict):
            raise ServiceError(400, "every emote must be an object")

        unknown_emote = set(emote.keys()) - set(EMOTE_FIELDS)
        if unknown_emote:
            raise ServiceError(400, f"illegal emote field(s): {', '.join(sorted(unknown_emote))}")

        cleaned_emote: dict = {}
        for field in ("emote_id", "voice_id"):
            value = emote.get(field, 0)
            if not isinstance(value, int) or isinstance(value, bool):
                raise ServiceError(400, f"emote.{field} must be an integer")
            if value < 0 or value > 255:
                raise ServiceError(400, f"emote.{field} is out of range (0..255)")
            cleaned_emote[field] = value

        for field in ("emote_name", "voice_name"):
            value = emote.get(field, "")
            if not isinstance(value, str):
                raise ServiceError(400, f"emote.{field} must be a string")
            cleaned_emote[field] = value

        cleaned_emotes.append(cleaned_emote)

    cleaned_custom["emotes"] = cleaned_emotes
    cleaned["custom_data"] = cleaned_custom

    return cleaned


def public_profile(profile: dict) -> dict:
    """Strips every private field. The password can never leave the service."""
    return {key: value for key, value in profile.items() if not key.startswith("_")}


class ProfileRequestHandler(BaseHTTPRequestHandler):
    server_version = "AJBProfileService/1.0"
    protocol_version = "HTTP/1.1"

    # -- helpers ---------------------------------------------------------

    def send_json(self, status: int, payload: dict) -> None:
        body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def send_error_json(self, status: int, message: str) -> None:
        self.send_json(status, {"error": message})

    def read_json_body(self) -> dict:
        try:
            length = int(self.headers.get("Content-Length") or 0)
        except ValueError as exc:
            raise ServiceError(400, "Content-Length is not a number") from exc

        if length <= 0:
            raise ServiceError(400, "a JSON body is required")

        if length > MAX_BODY_BYTES:
            raise ServiceError(413, "the request body is too large")

        raw = self.rfile.read(length)

        try:
            return json.loads(raw.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise ServiceError(400, "the request body is not valid UTF-8 JSON") from exc

    def log_message(self, fmt: str, *args) -> None:
        sys.stderr.write("[profile-service] %s - %s\n" % (self.address_string(), fmt % args))

    # -- dispatch --------------------------------------------------------

    def do_GET(self) -> None:  # noqa: N802 - name is fixed by the base class
        try:
            parsed = urlparse(self.path)
            query = parse_qs(parsed.query)

            if parsed.path == "/health":
                self.send_json(200, {"status": "ok", "accounts": account_ids()})
                return

            if parsed.path == "/profile":
                token = (query.get("token") or [""])[0]
                account_id = account_for_token(token)
                self.send_json(200, public_profile(load_profile(account_id)))
                return

            raise ServiceError(404, f"unknown path {parsed.path}; use /profile or /health")
        except ServiceError as error:
            self.send_error_json(error.status, error.message)
        except Exception as error:  # pragma: no cover - defensive, never crash a request
            self.send_error_json(500, f"unexpected error: {error}")

    def do_POST(self) -> None:  # noqa: N802 - name is fixed by the base class
        try:
            parsed = urlparse(self.path)

            if parsed.path == "/login":
                self.handle_login()
                return

            if parsed.path == "/save-profile":
                self.handle_save_profile()
                return

            raise ServiceError(404, f"unknown path {parsed.path}; use /login or /save-profile")
        except ServiceError as error:
            self.send_error_json(error.status, error.message)
        except Exception as error:  # pragma: no cover - defensive, never crash a request
            self.send_error_json(500, f"unexpected error: {error}")

    # -- endpoints -------------------------------------------------------

    def handle_login(self) -> None:
        body = self.read_json_body()

        account_id = body.get("account_id")
        if not isinstance(account_id, str) or not account_id:
            raise ServiceError(400, "account_id is required")

        accounts = load_accounts()
        if account_id not in accounts:
            raise ServiceError(401, f"unknown account {account_id}")

        # Local fixture: the password is accepted but never checked and never returned.
        # The account must still own a profile so the join can be resolved afterwards.
        load_profile(account_id)

        token_info = issue_token(account_id)
        token_info["account_id"] = account_id

        self.send_json(200, token_info)

    def handle_save_profile(self) -> None:
        body = self.read_json_body()

        token = body.get("token")
        if not isinstance(token, str) or not token:
            raise ServiceError(400, "a token is required")

        account_id = account_for_token(token)

        profile = body.get("profile")
        if not isinstance(profile, dict):
            raise ServiceError(400, "profile is required and must be an object")

        # The token owns the account, a save can never write into another account's file.
        cleaned = validate_profile(profile, account_id)

        path = profile_path(cleaned["account_id"])
        with open(path, "w", encoding="utf-8") as handle:
            json.dump(cleaned, handle, ensure_ascii=False, indent=2)
            handle.write("\n")

        self.send_json(200, {"saved": True, "profile": cleaned})

    def do_PUT(self) -> None:  # noqa: N802
        self.send_error_json(405, "method not allowed; use GET or POST")

    def do_DELETE(self) -> None:  # noqa: N802
        self.send_error_json(405, "method not allowed; use GET or POST")


def parse_arguments(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Local AJB account profile service")
    parser.add_argument("--host", default="127.0.0.1", help="bind address, default 127.0.0.1")
    parser.add_argument("--port", type=int, default=8080, help="bind port, default 8080")
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    arguments = parse_arguments(argv)

    if not os.path.isdir(PROFILES_DIR):
        print(f"[profile-service] profiles directory is missing: {PROFILES_DIR}", file=sys.stderr)
        return 1

    server = ThreadingHTTPServer((arguments.host, arguments.port), ProfileRequestHandler)
    print(f"[profile-service] listening on http://{arguments.host}:{arguments.port}")
    print(f"[profile-service] accounts: {', '.join(account_ids()) or '(none)'}")

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n[profile-service] shutting down")
    finally:
        server.server_close()

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
