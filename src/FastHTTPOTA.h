/**
 * FastHTTPOTA - Fast and simple HTTP Over-The-Air firmware updates for ESP32
 *
 * Inspired by FastBLEOTA, this library provides a clean callback API for
 * downloading and applying OTA updates over HTTP or HTTPS using ESP32's
 * built-in HTTPUpdate library.
 *
 * Key differences from FastBLEOTA:
 * - Transport: HTTP/HTTPS pull (device GETs a URL) vs BLE push
 * - No built-in scheduling or version checking — caller supplies URL and timing
 * - No CRC/chunking — HTTP + TCP handle reliability; HTTPUpdate handles integrity
 *
 * Usage:
 *   #include <FastHTTPOTA.h>
 *
 *   class MyCallbacks : public FastHTTPOTACallbacks {
 *   public:
 *     void onStart(size_t totalSize) override {
 *       Serial.printf("[OTA] Starting: %u bytes\n", totalSize);
 *     }
 *     void onProgress(size_t received, size_t total, float percent) override {
 *       Serial.printf("[OTA] %.1f%%\n", percent);
 *     }
 *     void onComplete() override { Serial.println("[OTA] Done, restarting"); }
 *     void onError(fho_error_t err, const char* msg) override {
 *       Serial.printf("[OTA] Error: %s\n", msg);
 *     }
 *   };
 *
 *   // In setup():
 *   FastHTTPOTA.setCallbacks(new MyCallbacks());
 *
 *   // When you have a URL to download:
 *   FastHTTPOTA.update("https://example.com/firmware.bin");
 */

#ifndef FAST_HTTP_OTA_H
#define FAST_HTTP_OTA_H

#include <Arduino.h>

/**
 * Error codes.
 *
 * FHO_ERROR_NONE         - No error (success or not yet run)
 * FHO_ERROR_WIFI_NOT_CONNECTED - WiFi must be connected before calling update()
 * FHO_ERROR_ABORTED      - Aborted via abort() before download started
 * FHO_ERROR_NO_UPDATES   - Server responded HTTP_UPDATE_NO_UPDATES (304 / same version)
 * FHO_ERROR_SERVER_ERROR - Server returned 4xx/5xx or file not found
 * FHO_ERROR_NO_SPACE     - Not enough flash space for this firmware
 * FHO_ERROR_NO_PARTITION - No valid OTA partition found
 * FHO_ERROR_HEADER_INVALID - Firmware binary header validation failed
 * FHO_ERROR_WRITE_FAILED - Flash write error (MD5 mismatch or write failure)
 * FHO_ERROR_UPDATE_FAILED - Generic update failure (check Serial for details)
 * FHO_ERROR_NOT_SUPPORTED - Platform is not ESP32 (stub)
 */
enum fho_error_t {
  FHO_ERROR_NONE = 0,
  FHO_ERROR_WIFI_NOT_CONNECTED,
  FHO_ERROR_ABORTED,
  FHO_ERROR_NO_UPDATES,
  FHO_ERROR_SERVER_ERROR,
  FHO_ERROR_NO_SPACE,
  FHO_ERROR_NO_PARTITION,
  FHO_ERROR_HEADER_INVALID,
  FHO_ERROR_WRITE_FAILED,
  FHO_ERROR_UPDATE_FAILED,
  FHO_ERROR_NOT_SUPPORTED,
};

/**
 * Callback interface. All methods are optional — override only what you need.
 *
 * IMPORTANT: Callbacks are invoked from the calling task context. Keep them
 * fast (no blocking I/O) to avoid stalling the update stream.
 */
class FastHTTPOTACallbacks {
public:
  virtual ~FastHTTPOTACallbacks() {}

  /**
   * Called once when the download begins.
   * @param totalSize  Firmware size in bytes (0 if server did not send Content-Length)
   */
  virtual void onStart(size_t totalSize) {}

  /**
   * Called periodically during download.
   * @param received  Bytes received so far
   * @param total     Total bytes expected (0 if unknown)
   * @param percent   Progress 0.0–100.0 (0 if total is unknown)
   */
  virtual void onProgress(size_t received, size_t total, float percent) {}

  /**
   * Called when the update flashed successfully, just before ESP.restart().
   * Use this to save state or log the event. Device restarts after this returns.
   */
  virtual void onComplete() {}

  /**
   * Called on any error. update() returns false immediately after this.
   * @param error  Error code
   * @param message Human-readable description
   */
  virtual void onError(fho_error_t error, const char* message) {}

  /**
   * Called when abort() was invoked before the download started.
   * Not called if abort() is called after httpUpdate.update() has begun
   * (HTTPUpdate is blocking and cannot be interrupted mid-transfer).
   */
  virtual void onAbort() {}
};

class FastHTTPOTAClass {
public:
  FastHTTPOTAClass();

  /**
   * Set callback handler for OTA events. Must be set before calling update().
   * The caller owns the pointer; FastHTTPOTA does not free it.
   */
  void setCallbacks(FastHTTPOTACallbacks* callbacks);

  /**
   * Download and apply firmware from URL. Blocking call.
   *
   * Returns false immediately if:
   *   - WiFi is not connected (FHO_ERROR_WIFI_NOT_CONNECTED)
   *   - abort() was called before this (FHO_ERROR_ABORTED)
   *   - Server has no new firmware (FHO_ERROR_NO_UPDATES)
   *   - Any download or flash error
   *
   * Returns true only conceptually — on success, ESP.restart() is called
   * and this function never actually returns to the caller.
   *
   * @param url  Full URL to firmware binary (http:// or https://)
   * @param auth Optional Bearer token for Authorization header (e.g. "liq_abc123")
   *             Pass nullptr to skip authorization header.
   */
  bool update(const char* url, const char* auth = nullptr);

  /**
   * Request abort of a pending update.
   *
   * If called BEFORE update() starts the HTTP transfer, the update is cancelled
   * and onAbort() is called. If called AFTER httpUpdate.update() has begun,
   * the transfer cannot be interrupted (HTTPUpdate is synchronous).
   *
   * Call abort() from a separate task or ISR, not from within a callback.
   */
  void abort();

  /** @return true if update() is currently executing */
  bool isUpdating();

  /** @return Last error code (FHO_ERROR_NONE if no error or not yet run) */
  fho_error_t getLastError();

  /** @return Human-readable string for the last error */
  const char* getLastErrorString();

private:
  FastHTTPOTACallbacks* _callbacks;
  bool _updating;
  bool _aborted;
  fho_error_t _lastError;

  void setError(fho_error_t error);
  static const char* errorToString(fho_error_t error);

#if defined(ESP32)
  fho_error_t mapHTTPUpdateError(int httpUpdateError);
#endif
};

extern FastHTTPOTAClass FastHTTPOTA;

#endif // FAST_HTTP_OTA_H
