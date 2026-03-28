# FastHTTPOTA AGENTS.md

## Project Summary

FastHTTPOTA is a minimal Arduino library for ESP32 that downloads and applies OTA firmware updates over HTTP/HTTPS. It's designed as a companion to FastBLEOTA, sharing the same callback-based API design.

## Design Philosophy

- **Single Responsibility**: Only handles download and apply. No version checking, no scheduling.
- **Caller-Controlled**: The calling code decides when to update and provides the URL.
- **Callback-Based**: Events are communicated via virtual callbacks (matching FastBLEOTA pattern).
- **Blocking API**: `update()` blocks until complete, restart, or error.

## Directory Layout

```
FastHTTPOTA/
├── src/
│   ├── FastHTTPOTA.h         # Public API and callback interface
│   └── FastHTTPOTA.cpp       # ESP32 implementation using HTTPUpdate.h
├── examples/
│   └── basic/
│       └── basic.ino         # Simple usage example
├── library.json              # PlatformIO metadata
├── library.properties        # Arduino IDE metadata
├── README.md                 # Documentation
└── AGENTS.md                 # This file
```

## Key Patterns

### Callback Interface

```cpp
class FastHTTPOTACallbacks {
  virtual void onStart(size_t totalSize);
  virtual void onProgress(size_t received, size_t total, float percent);
  virtual void onComplete();
  virtual void onError(fho_error_t error, const char* message);
  virtual void onAbort();
};
```

### Usage Pattern

```cpp
// External code handles version checking
if (newVersionAvailable) {
  FastHTTPOTA.update(firmwareUrl);  // Blocking, restarts on success
}
```

### Error Handling

All errors are reported via `onError` callback and stored in `_lastError`. Check `getLastError()` or `getLastErrorString()` after a failed `update()` call.

## Integration with LaundryIQ

In LaundryIQ v2 firmware, the OTA check logic lives in the application code:

```cpp
// liq_ota.cpp (in LaundryIQ firmware, not this library)
void checkForUpdates() {
  HTTPClient http;
  http.begin("https://api.laundryiq.app/api/v1/device/ota/check");
  // ... add headers, GET, parse response ...
  
  if (updateAvailable) {
    // FastHTTPOTA just does the download
    FastHTTPOTA.update(downloadUrl);
  }
}
```

## Comparison with FastBLEOTA

| Aspect | FastBLEOTA | FastHTTPOTA |
|--------|------------|-------------|
| Transport | BLE | HTTP/HTTPS |
| Control Flow | Push (client sends data) | Pull (device downloads) |
| Progress | Via BLE characteristic | Via callback |
| CRC | Built-in CRC32 | Relies on HTTPS/TCP |
| Scheduling | None | None |

## Implementation Notes

- `onStart()` fires on the first `onProgress` tick so `totalSize` is the actual `Content-Length`.
- `abort()` only works before `httpUpdate.update()` starts; it cannot interrupt a running transfer.
- HTTPS uses `setInsecure()` (no cert validation). Use `setCACert()` for production.
- `HTTP_UPDATE_NO_UPDATES` maps to `FHO_ERROR_NO_UPDATES` (not a hard error — server has same version).
- HTTPUpdate error codes are mapped to `fho_error_t` in `mapHTTPUpdateError()`.
- `httpUpdate.rebootOnUpdate(false)` is set so restart happens in our code (after `onComplete()`).

## PlatformIO Dependency

Always reference via GitHub URL (not the PlatformIO registry):
```ini
lib_deps =
  https://github.com/LeeorNahum/FastHTTPOTA.git#main
```

## Sync Policy

Update this file when:
- Adding new callback methods
- Changing error codes
- Adding platform support
- Modifying the public API
