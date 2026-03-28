/**
 * FastHTTPOTA - Implementation using ESP32's HTTPUpdate library.
 *
 * HTTPUpdate handles:
 *   - HTTP/HTTPS connection and redirect following
 *   - Streaming firmware bytes directly to the OTA flash partition
 *   - MD5 verification of the received binary
 *   - Calling ESP.restart() on success
 *
 * This wrapper adds:
 *   - A FastBLEOTA-compatible callback interface
 *   - Proper error code mapping from HTTPUpdate's internal codes
 *   - Optional Authorization header for authenticated firmware servers
 *   - Pre-transfer abort support
 */

#include "FastHTTPOTA.h"

#if defined(ESP32)

#include <WiFi.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>

// Global singleton
FastHTTPOTAClass FastHTTPOTA;

// Static pointers shared with the HTTPUpdate progress callback.
// HTTPUpdate does not pass user data through its progress callback, so we
// use file-scope statics. This means FastHTTPOTA is not re-entrant, but
// OTA is inherently a single-operation-at-a-time affair.
static FastHTTPOTACallbacks* s_callbacks = nullptr;
static bool s_onStartFired = false;

// HTTPUpdate progress callback — fires with current/total byte counts.
// We fire onStart() the first time we see a non-zero total, so callers get
// the actual Content-Length rather than 0.
static void httpUpdateProgress(int current, int total) {
  if (!s_callbacks) return;

  if (!s_onStartFired) {
    s_callbacks->onStart((size_t)(total > 0 ? total : 0));
    s_onStartFired = true;
  }

  if (total > 0) {
    float percent = ((float)current / (float)total) * 100.0f;
    s_callbacks->onProgress((size_t)current, (size_t)total, percent);
  } else {
    // Server did not send Content-Length; report bytes received with 0 total.
    s_callbacks->onProgress((size_t)current, 0, 0.0f);
  }
}

// ─────────────────────────────────────────────────────────────────────────────

FastHTTPOTAClass::FastHTTPOTAClass()
  : _callbacks(nullptr)
  , _updating(false)
  , _aborted(false)
  , _lastError(FHO_ERROR_NONE)
{}

void FastHTTPOTAClass::setCallbacks(FastHTTPOTACallbacks* callbacks) {
  _callbacks = callbacks;
  s_callbacks = callbacks;
}

void FastHTTPOTAClass::setError(fho_error_t error) {
  _lastError = error;
  if (_callbacks && error != FHO_ERROR_NONE) {
    _callbacks->onError(error, errorToString(error));
  }
}

fho_error_t FastHTTPOTAClass::getLastError() {
  return _lastError;
}

const char* FastHTTPOTAClass::getLastErrorString() {
  return errorToString(_lastError);
}

bool FastHTTPOTAClass::isUpdating() {
  return _updating;
}

void FastHTTPOTAClass::abort() {
  _aborted = true;
}

// Map HTTPUpdate's internal error codes to our fho_error_t enum.
// HTTPUpdate error values are defined in HTTPUpdate.h as HTTPUpdateError.
fho_error_t FastHTTPOTAClass::mapHTTPUpdateError(int e) {
  // Values from ESP32 Arduino HTTPUpdate.h:
  //   HTTP_UE_TOO_LESS_SPACE        = -1
  //   HTTP_UE_SERVER_NOT_REPORT_SIZE= -2
  //   HTTP_UE_SERVER_FILE_NOT_FOUND = -3
  //   HTTP_UE_SERVER_FORBIDDEN      = -4
  //   HTTP_UE_SERVER_WRONG_HTTP_CODE= -5
  //   HTTP_UE_SERVER_FAULTY_MD5     = -6
  //   HTTP_UE_BIN_VERIFY_HEADER_FAILED = -7
  //   HTTP_UE_BIN_FOR_WRONG_FLASH   = -8
  //   HTTP_UE_NO_PARTITION          = -9
  switch (e) {
    case -1: return FHO_ERROR_NO_SPACE;
    case -2: return FHO_ERROR_UPDATE_FAILED;      // server didn't report size
    case -3: return FHO_ERROR_SERVER_ERROR;       // 404
    case -4: return FHO_ERROR_SERVER_ERROR;       // 403
    case -5: return FHO_ERROR_SERVER_ERROR;       // wrong HTTP code
    case -6: return FHO_ERROR_WRITE_FAILED;       // MD5 mismatch
    case -7: return FHO_ERROR_HEADER_INVALID;     // bad firmware header
    case -8: return FHO_ERROR_HEADER_INVALID;     // wrong flash target
    case -9: return FHO_ERROR_NO_PARTITION;       // no OTA partition
    default: return FHO_ERROR_UPDATE_FAILED;
  }
}

bool FastHTTPOTAClass::update(const char* url, const char* auth) {
  if (_updating) {
    return false;
  }

  _updating = true;
  _lastError = FHO_ERROR_NONE;
  s_onStartFired = false;

  // Pre-transfer abort check — caller may have called abort() before update().
  if (_aborted) {
    _aborted = false;
    _updating = false;
    if (_callbacks) _callbacks->onAbort();
    setError(FHO_ERROR_ABORTED);
    return false;
  }

  if (WiFi.status() != WL_CONNECTED) {
    _updating = false;
    setError(FHO_ERROR_WIFI_NOT_CONNECTED);
    return false;
  }

  Serial.printf("[FHO] Updating from: %s\n", url);

  // Register progress callback.
  httpUpdate.onProgress(httpUpdateProgress);

  // Optional: set Authorization header.
  // HTTPUpdate supports setAuthorization() only for basic auth.
  // For Bearer tokens we use rebootOnUpdate to stay in control of the restart.
  httpUpdate.rebootOnUpdate(false);

  bool isHttps = strncmp(url, "https://", 8) == 0;
  HTTPUpdateResult result;

  if (isHttps) {
    WiFiClientSecure client;
    // Using setInsecure() skips certificate validation.
    // TODO: Replace with client.setCACert(rootCA) for production security.
    client.setInsecure();

    if (auth) {
      httpUpdate.setAuthorization(auth);
    }

    result = httpUpdate.update(client, url);
  } else {
    WiFiClient client;

    if (auth) {
      httpUpdate.setAuthorization(auth);
    }

    result = httpUpdate.update(client, url);
  }

  _updating = false;
  _aborted = false;

  switch (result) {
    case HTTP_UPDATE_OK:
      Serial.println("[FHO] Update successful, restarting.");
      if (_callbacks) _callbacks->onComplete();
      delay(100);
      ESP.restart();
      return true;  // Never reached

    case HTTP_UPDATE_NO_UPDATES:
      // Server indicated no new firmware (e.g. HTTP 304 or same version).
      // This is not an error — caller can treat it as a no-op.
      Serial.println("[FHO] No update available.");
      setError(FHO_ERROR_NO_UPDATES);
      return false;

    case HTTP_UPDATE_FAILED:
    default: {
      int httpErr = httpUpdate.getLastError();
      fho_error_t mapped = mapHTTPUpdateError(httpErr);
      Serial.printf("[FHO] Update failed (%d): %s\n",
                    httpErr, httpUpdate.getLastErrorString().c_str());
      setError(mapped);
      return false;
    }
  }
}

const char* FastHTTPOTAClass::errorToString(fho_error_t error) {
  switch (error) {
    case FHO_ERROR_NONE:               return "No error";
    case FHO_ERROR_WIFI_NOT_CONNECTED: return "WiFi not connected";
    case FHO_ERROR_ABORTED:            return "Update aborted";
    case FHO_ERROR_NO_UPDATES:         return "No update available";
    case FHO_ERROR_SERVER_ERROR:       return "Server error (4xx/5xx or file not found)";
    case FHO_ERROR_NO_SPACE:           return "Not enough flash space";
    case FHO_ERROR_NO_PARTITION:       return "No OTA partition found";
    case FHO_ERROR_HEADER_INVALID:     return "Firmware header validation failed";
    case FHO_ERROR_WRITE_FAILED:       return "Flash write failed (MD5 mismatch)";
    case FHO_ERROR_UPDATE_FAILED:      return "Update failed";
    case FHO_ERROR_NOT_SUPPORTED:      return "Platform not supported";
    default:                           return "Unknown error";
  }
}

#else

// ─── Non-ESP32 stub ───────────────────────────────────────────────────────────
FastHTTPOTAClass FastHTTPOTA;

FastHTTPOTAClass::FastHTTPOTAClass()
  : _callbacks(nullptr), _updating(false), _aborted(false)
  , _lastError(FHO_ERROR_NOT_SUPPORTED)
{}
void FastHTTPOTAClass::setCallbacks(FastHTTPOTACallbacks* callbacks) {
  _callbacks = callbacks;
}
bool FastHTTPOTAClass::update(const char* url, const char* auth) {
  setError(FHO_ERROR_NOT_SUPPORTED);
  return false;
}
void FastHTTPOTAClass::abort() {}
bool FastHTTPOTAClass::isUpdating() { return false; }
fho_error_t FastHTTPOTAClass::getLastError() { return _lastError; }
const char* FastHTTPOTAClass::getLastErrorString() { return "Platform not supported"; }
void FastHTTPOTAClass::setError(fho_error_t error) {
  _lastError = error;
  if (_callbacks && error != FHO_ERROR_NONE) {
    _callbacks->onError(error, errorToString(error));
  }
}
const char* FastHTTPOTAClass::errorToString(fho_error_t error) {
  return "Platform not supported";
}

#endif
