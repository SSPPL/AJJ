# AJB ProfileService

Local account profile fixture for the AJB PC Port listen server tests. It stands in for the
arcade account backend: it authenticates an account, hands out a short lived token and serves
the public profile for that account.

Standard library only. No third party runtime dependency.

```powershell
py ProfileService\server.py --host 0.0.0.0 --port 8080
```

`--host` defaults to `127.0.0.1` and `--port` to `8080`, which matches the C++
`HttpProfileProvider` default (`http://127.0.0.1:8080`).

## Layout

```
ProfileService/
  server.py             the service
  accounts.json         account_id -> account record (holds the password)
  profiles/<id>.json    the public profile for each account
  README.md
```

## Endpoints

| Method | Path | Purpose |
| --- | --- | --- |
| `POST` | `/login` | `{"account_id","password"}` -> `{"token","expires_at","account_id"}` |
| `GET` | `/profile?token=<token>` | public profile JSON |
| `POST` | `/save-profile` | `{"token","profile"}` -> `{"saved","profile"}` |
| `GET` | `/health` | liveness plus the known account ids |

### `POST /login`

```json
{ "account_id": "PCPORT-SESSION-001", "password": "test-password" }
```

```json
{ "token": "…", "expires_at": "2026-01-01T00:00:00Z", "account_id": "PCPORT-SESSION-001" }
```

### `GET /profile?token=<token>`

Returns the profile with the fixed schema. The password is never part of the response, the
public view strips every private `_`-prefixed field.

```json
{
  "account_id": "PCPORT-SESSION-001",
  "name": "Jotaro Kujo",
  "icon_id": 1,
  "level": 50,
  "title": "Yare Yare Daze",
  "game_server_user_id": "PCPORT-SESSION-001",
  "custom_data": {
    "chara_skin_id": 1,
    "stand_skin_id": 1,
    "kill_count": 0,
    "emotes": [
      { "emote_id": 1, "voice_id": 1, "emote_name": "YareYare", "voice_name": "YareYareVoice" }
    ]
  }
}
```

### `POST /save-profile`

```json
{ "token": "…", "profile": { "account_id": "PCPORT-SESSION-001", "name": "…" } }
```

The token owns the account, so a save can only ever write the profile it authenticated for. An
`account_id` that does not match the token, an unknown profile field, a missing required field
or an out of range value is rejected with a `400` and nothing is written.

## Error handling

| Situation | Status | Body |
| --- | --- | --- |
| Unknown account | `401` | `{"error": "unknown account …"}` |
| Missing `account_id` | `400` | `{"error": "account_id is required"}` |
| Malformed JSON body | `400` | `{"error": "the request body is not valid UTF-8 JSON"}` |
| Missing token | `400` | `{"error": "a token is required"}` |
| Unknown token | `401` | `{"error": "unknown token"}` |
| Expired token | `401` | `{"error": "expired token"}` |
| No profile for the account | `404` | `{"error": "no profile exists for account …"}` |
| Illegal profile field | `400` | `{"error": "illegal profile field(s): …"}` |
| Wrong method | `405` | `{"error": "method not allowed; use GET or POST"}` |
| Unknown path | `404` | `{"error": "unknown path …"}` |

Every failure is answered as JSON and never crashes the request handler.

## Testing

```powershell
# token, then profile
$t = (Invoke-RestMethod http://127.0.0.1:8080/login -Method Post -ContentType application/json `
      -Body '{"account_id":"PCPORT-SESSION-001","password":"test-password"}').token

Invoke-RestMethod "http://127.0.0.1:8080/profile?token=$t"
Invoke-RestMethod "http://127.0.0.1:8080/health"
```

The expired token path is exercised by issuing a token with a one second lifetime:

```powershell
$env:AJB_PROFILE_TOKEN_TTL = "1"
py ProfileService\server.py --host 127.0.0.1 --port 8080
```

## Authentication policy

This is a local fixture, not a production auth boundary. A token is issued for any account that
exists in `accounts.json` without verifying the password, because the arcade credentials are not
available locally. The wire shape, the validation rules and the error handling are honoured so
the C++ provider is exercised against the real contract; the authentication strength is not.
