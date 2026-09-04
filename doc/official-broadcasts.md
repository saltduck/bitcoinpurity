# Bitcoin Purity Official Broadcasts

This document describes the signed `broadcasts.json` manifest used by the
bitcoin-qt GUI official notice checker and the operational workflow for
publishing it.

## Overview

The GUI periodically fetches:

```text
https://downloads.bitcoinpurity.org/broadcasts.json
```

The manifest is signed with the same ECDSA scheme as official datadir package
manifests (`contrib/official-packages/sign-manifest.py`). The client verifies the
signature before showing any notice.

User flow:

1. Fetch and verify the signed manifest.
2. Filter out notices the user has already read and notices that have expired.
3. Show each remaining notice as plain text in a dialog.
4. After the user acknowledges a notice, persist its `id` so it is not shown again.

Users can disable official notices in **Settings → Display → Show official notices**
(default: enabled). The CLI flag `-nocheckforbroadcasts` also disables checks.

## Manifest schema (`schema: 1`)

```json
{
  "schema": 1,
  "notices": [
    {
      "id": "maintenance-2026-09",
      "title": "Scheduled maintenance",
      "body": "CDN maintenance is planned this weekend.",
      "published_at": "2026-09-01T00:00:00Z",
      "expires_at": "2026-12-01T00:00:00Z"
    }
  ],
  "signature": "<base64>"
}
```

### Fields

| Field | Required | Description |
|---|---|---|
| `id` | yes | Stable unique identifier used for read/dismiss tracking |
| `body` | yes | Plain-text notice body shown to the user |
| `title` | no | Dialog window title (defaults to “Official Notice”) |
| `published_at` | no | ISO-8601 UTC timestamp |
| `expires_at` | no | ISO-8601 UTC timestamp; after this time the notice is ignored |

Notice bodies must be plain text. The GUI renders them with `Qt::PlainText` and
does not interpret HTML.

## Publishing workflow

### 1. Author the unsigned manifest

Create a `broadcasts.json` file with `schema: 1` and a `notices` array. Keep
each `id` stable once published so clients that already dismissed it do not
prompt again.

### 2. Sign the manifest

```bash
python3 contrib/official-packages/sign-manifest.py \
  --sign broadcasts.json \
  --key manifest-sign.pem
```

Use the same signing key as official package / release manifests.

### 3. Upload to the CDN

Publish the signed file to:

```text
https://downloads.bitcoinpurity.org/broadcasts.json
```

## Client configuration

| Option | Default | Description |
|---|---|---|
| `-checkforbroadcasts` | `1` | Enable automatic GUI notice checks (`-nocheckforbroadcasts` disables) |
| `-broadcastsmanifest=<url>` | CDN default | Override manifest URL (testing) |
| Settings → Display → Show official notices | on | GUI toggle persisted in Qt `QSettings` |

Automatic checks run at most once every day after a short startup delay. Users
can also choose **Help → Check Official Notices…**.

If `broadcasts.json` is missing on the CDN (HTTP 404), the client treats that as
“no notices” and does not report an error.

Read notice IDs are stored under the Qt settings group `official_broadcasts`.

## Security model

1. Manifest must be fetched from `downloads.bitcoinpurity.org` (or a local
   override URL for testing).
2. Manifest signature is verified with the embedded secp256k1 public key
   (same key as official packages / software updates).
3. Notice bodies are displayed as plain text only.
