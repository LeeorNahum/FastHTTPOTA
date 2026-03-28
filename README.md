# FastHTTPOTA

Fast and simple HTTP Over-The-Air firmware updates for ESP32.

![Version](https://img.shields.io/badge/version-1.1.0-blue)
![Platform](https://img.shields.io/badge/platform-ESP32-green)

## Features

- **Simple API**: Just call `update(url)` when you have a firmware URL
- **Callback Interface**: React to OTA events (start, progress, complete, error, abort)
- **HTTPS Support**: Secure firmware downloads via `WiFiClientSecure`
- **Auth Header**: Optional Bearer token support for authenticated firmware servers
- **Progress Notifications**: Real-time download progress with actual `Content-Length`
- **Abort Support**: Cancel updates before the HTTP transfer begins
- **Minimal Dependencies**: Uses ESP32's built-in `HTTPUpdate` library

## Quick Start

### Installation

**PlatformIO (recommended — use GitHub link, not the registry):**

```ini
lib_deps =
  https://github.com/LeeorNahum/FastHTTPOTA.git#main
```

### Basic Usage

```cpp
#include <WiFi.h>
#include <FastHTTPOTA.h>

class MyOTACallbacks : public FastHTTPOTACallbacks {
public:
  void onStart(size_t totalSize) override {
    Serial.printf("[OTA] Starting: %u bytes\n", totalSize);
  }
  void onProgress(size_t received, size_t total, float percent) override {
    Serial.printf("[OTA] %.1f%% (%u / %u bytes)\n", percent, received, total);
  }
  void onComplete() override {
    Serial.println("[OTA] Done! Restarting...");
  }
  void onError(fho_error_t err, const char* msg) override {
    Serial.printf("[OTA] Error %d: %s\n", (int)err, msg);
  }
};

void setup() {
  Serial.begin(115200);

  WiFi.begin("SSID", "password");
  while (WiFi.status() != WL_CONNECTED) delay(500);

  FastHTTPOTA.setCallbacks(new MyOTACallbacks());
}

void checkAndApplyUpdate(const char* firmwareUrl) {
  // Optionally pass a Bearer token as the second argument:
  //   FastHTTPOTA.update(firmwareUrl, "liq_yourapikey");
  bool ok = FastHTTPOTA.update(firmwareUrl);

  if (!ok) {
    fho_error_t err = FastHTTPOTA.getLastError();
    if (err == FHO_ERROR_NO_UPDATES) {
      Serial.println("[OTA] Already up to date.");
    } else {
      Serial.printf("[OTA] Failed: %s\n", FastHTTPOTA.getLastErrorString());
    }
  }
  // On success, ESP.restart() was called — this line is never reached.
}
```

## API Reference

### `FastHTTPOTA` (global singleton)

| Method | Description |
|--------|-------------|
| `setCallbacks(callbacks*)` | Set callback handler. Caller owns the pointer. |
| `update(url, auth=nullptr)` | Download and apply firmware. Blocking. Device restarts on success. |
| `abort()` | Request abort. Only effective before the HTTP transfer starts. |
| `isUpdating()` | Returns `true` while `update()` is executing. |
| `getLastError()` | Returns the last `fho_error_t` code. |
| `getLastErrorString()` | Returns a human-readable string for the last error. |

### Callbacks (`FastHTTPOTACallbacks`)

All methods have empty default implementations — override only what you need.

| Callback | When | Parameters |
|----------|------|------------|
| `onStart(totalSize)` | Once, when download begins | `totalSize`: bytes (0 if server omits Content-Length) |
| `onProgress(received, total, percent)` | Periodically during download | `total`/`percent` are 0 if Content-Length was absent |
| `onComplete()` | After successful flash, before restart | — |
| `onError(error, message)` | On any failure | `error`: code, `message`: string |
| `onAbort()` | When abort() was called before download started | — |

> **Important:** Callbacks run in the caller's task context. Keep them fast — no blocking I/O.

### Error Codes

| Value | Name | Description |
|-------|------|-------------|
| 0 | `FHO_ERROR_NONE` | No error |
| 1 | `FHO_ERROR_WIFI_NOT_CONNECTED` | WiFi not connected before calling `update()` |
| 2 | `FHO_ERROR_ABORTED` | Aborted via `abort()` before download started |
| 3 | `FHO_ERROR_NO_UPDATES` | Server has no new firmware (HTTP 304 or same version) |
| 4 | `FHO_ERROR_SERVER_ERROR` | Server returned 4xx/5xx or file not found |
| 5 | `FHO_ERROR_NO_SPACE` | Not enough flash space for this firmware |
| 6 | `FHO_ERROR_NO_PARTITION` | No OTA partition found in partition table |
| 7 | `FHO_ERROR_HEADER_INVALID` | Firmware binary header validation failed |
| 8 | `FHO_ERROR_WRITE_FAILED` | Flash write error or MD5 mismatch |
| 9 | `FHO_ERROR_UPDATE_FAILED` | Generic HTTPUpdate failure (check Serial for details) |
| 10 | `FHO_ERROR_NOT_SUPPORTED` | Non-ESP32 platform (stub) |

## Design Notes

### onStart size

`onStart(totalSize)` is called at the first `onProgress` tick, so `totalSize` reflects the actual `Content-Length` from the server. If the server omits `Content-Length`, `totalSize` is 0 and progress percentages will be 0.

### Abort behavior

`abort()` sets a flag that is checked **before** `httpUpdate.update()` is called. Since `HTTPUpdate` is synchronous and blocking, an abort requested after the download has begun cannot interrupt the transfer. Plan your abort logic accordingly (e.g. call `abort()` from a separate FreeRTOS task before calling `update()`).

### HTTPS / TLS

HTTPS downloads use `WiFiClientSecure` with `setInsecure()` (no certificate pinning). This protects against passive eavesdropping but not against a malicious server. For production, replace with `client.setCACert(rootCA)` pinned to your firmware server's CA.

### Authorization

Pass a Bearer token as the second argument to `update()`:

```cpp
FastHTTPOTA.update("https://api.example.com/firmware.bin", "mySecretToken");
```

The token is sent as `Authorization: mySecretToken`. Prefix `Bearer ` yourself if needed:

```cpp
String auth = "Bearer " + apiKey;
FastHTTPOTA.update(url, auth.c_str());
```

## ESP32 Partition Table

ESP32 OTA requires a partition table with two app slots (`ota_0`, `ota_1`) and an `otadata` partition. The default Arduino-ESP32 partition tables already include these, so no change is usually needed.

| Board type | Common default partition | Approx app size |
|------------|--------------------------|-----------------|
| 8MB+ boards (most ESP32-S3) | `default_8MB.csv` | ~3.2MB |
| 4MB boards | `default.csv` | ~1.25MB |

If your firmware exceeds the default app slot size:

```ini
; 4MB flash, larger app slot:
board_build.partitions = min_spiffs.csv

; 16MB flash:
board_build.partitions = default_16MB.csv
```

## How It Works

FastHTTPOTA wraps ESP32's built-in `HTTPUpdate` library (`HTTPUpdate.h`), which:
1. Opens an HTTP/HTTPS connection to the firmware URL
2. Streams the response body directly to the OTA flash partition
3. Verifies the download via MD5
4. Calls `ESP.restart()` on success

This keeps the implementation minimal (~100 lines) while leveraging ESP32's battle-tested OTA infrastructure.

## Requirements

- ESP32 (any variant: S2, S3, C3, C6, etc.)
- Arduino framework (PlatformIO or Arduino IDE)
- OTA-capable partition table
- WiFi connected before calling `update()`

## Credits

- Inspired by [FastBLEOTA](https://github.com/LeeorNahum/FastBLEOTA)
- Uses ESP32 Arduino `HTTPUpdate.h`

## License

MIT
