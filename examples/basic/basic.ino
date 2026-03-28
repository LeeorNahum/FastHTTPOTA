/**
 * FastHTTPOTA Basic Example
 * 
 * Demonstrates how to use FastHTTPOTA to download and apply
 * firmware updates from a URL.
 */

#include <WiFi.h>
#include <FastHTTPOTA.h>

// WiFi credentials
const char* ssid = "YOUR_SSID";
const char* password = "YOUR_PASSWORD";

// Example firmware URL (replace with your own)
const char* firmwareUrl = "https://example.com/firmware.bin";

// Custom callbacks for OTA events
class MyOTACallbacks : public FastHTTPOTACallbacks {
public:
  void onStart(size_t size) override {
    Serial.printf("[OTA] Update started: %u bytes\n", size);
  }
  
  void onProgress(size_t received, size_t total, float percent) override {
    Serial.printf("[OTA] Progress: %.1f%% (%u / %u)\n", percent, received, total);
  }
  
  void onComplete() override {
    Serial.println("[OTA] Update complete! Restarting...");
  }
  
  void onError(fho_error_t err, const char* msg) override {
    Serial.printf("[OTA] Error: %s (code %d)\n", msg, err);
  }
  
  void onAbort() override {
    Serial.println("[OTA] Update aborted");
  }
};

void setup() {
  Serial.begin(115200);
  Serial.println("\n\nFastHTTPOTA Basic Example\n");
  
  // Connect to WiFi
  Serial.printf("Connecting to %s...\n", ssid);
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.printf("\nConnected! IP: %s\n", WiFi.localIP().toString().c_str());
  
  // Set up OTA callbacks
  FastHTTPOTA.setCallbacks(new MyOTACallbacks());
  
  // In a real application, you would:
  // 1. Check your server for available updates
  // 2. Get the firmware URL if an update is available
  // 3. Call FastHTTPOTA.update(url)
  
  Serial.println("\nType 'update' in Serial to start OTA update");
}

void loop() {
  // Check for serial command
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    
    if (cmd == "update") {
      Serial.printf("\nStarting OTA update from:\n%s\n\n", firmwareUrl);
      
      // This is a blocking call - device will restart on success
      bool started = FastHTTPOTA.update(firmwareUrl);
      
      if (!started) {
        Serial.printf("Failed to start update: %s\n", 
                      FastHTTPOTA.getLastErrorString());
      }
    }
    else if (cmd == "help") {
      Serial.println("Commands:");
      Serial.println("  update - Start OTA update");
      Serial.println("  help   - Show this help");
    }
  }
  
  delay(10);
}
