#include <SPI.h>
#include <MFRC522.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>    
#include <ESP8266WebServer.h>     
#include <ArduinoJson.h>  
#include <WiFiClientSecure.h>

// Pin Definitions
#define SS_PIN D8
#define RST_PIN D0
#define BUZZER D4
#define LED_PIN D2

const char* ADMIN_USERNAME = "admin"; 
const char* ADMIN_PASSWORD = "admin";

// Network Configuration
const char *ssid = "LastStretchWiFi";
const char *password = "fW3zk6TVQV";
const char* device_token = "43bce14101765a43";
const char* URL = "https://rfidatttendance.site/data/getdata.php";

// OTA Configuration  
const char* OTA_UPDATE_URL = "https://rfidatttendance.site/firmware/";
const char* FIRMWARE_VERSION = "1.0.4"; //initial patch version
const char* BUILD_TIMESTAMP = __TIMESTAMP__;
const unsigned long VERSION_CHECK_INTERVAL = 5000;
const unsigned long OTA_CHECK_INTERVAL = 21600000;  

// Timing Constants
const unsigned long CARD_TIMEOUT = 3000;    
const unsigned long WIFI_ATTEMPT_DELAY = 500;
const unsigned long READER_RESET_INTERVAL = 10000;
const unsigned long MAX_WIFI_ATTEMPTS = 20;
const unsigned long WATCHDOG_INTERVAL = 1000;  
const unsigned long OPERATION_TIMEOUT = 3000;  

// Enhanced Watchdog Tracking Structure
struct WatchdogDiagnostics {
  unsigned long totalResets;
  unsigned long lastResetTime;
  unsigned long longestOperationTime;
  unsigned long criticalOperationThreshold;
  unsigned long consecutiveFailures;
  bool systemStable;
};

// Global watchdog diagnostics
WatchdogDiagnostics watchdogStats = {
  0,  // totalResets
  0,  // lastResetTime
  5000,  // longestOperationTime (5 seconds)
  10000,  // criticalOperationThreshold (10 seconds)
  0,  // consecutiveFailures
  true  // systemStable
};

// RFID Reader Instance
MFRC522 mfrc522(SS_PIN, RST_PIN);

// Web Server Instance
ESP8266WebServer server(80);

// Global Variables
String lastCardUID = "";
unsigned long lastCardRead = 0;
unsigned long lastWatchdogFeed = 0;
unsigned long operationStartTime = 0;
unsigned long lastOTACheck = 0;
bool isLongOperation = false;
WiFiClient wifiClient;
WiFiClientSecure secureClient;

// Add this function to handle authentication
bool handleAuthentication() {
  if (!server.authenticate(ADMIN_USERNAME, ADMIN_PASSWORD)) {
    server.requestAuthentication();
    return false;
  }
  return true;
}

// Add OTA update status callback
void updateCallback(int progress, int total) {
  static int lastProgress = 0;
  if (progress - lastProgress >= 10 || progress == total) {
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    
    if (progress != total) {
      digitalWrite(BUZZER, LOW);
      delay(50);
      digitalWrite(BUZZER, HIGH);
    }
    
    ESP.wdtFeed();
  }
}

void setupOTA() {
  ESPhttpUpdate.onStart([]() {
    digitalWrite(LED_PIN, HIGH);
    digitalWrite(BUZZER, LOW);
    delay(50);
    digitalWrite(BUZZER, HIGH);
    delay(300);
    digitalWrite(BUZZER, LOW);
    delay(50);
    digitalWrite(BUZZER, HIGH);
  });
  
  ESPhttpUpdate.onEnd([]() {
    digitalWrite(LED_PIN, LOW);
    digitalWrite(BUZZER, LOW);
    delay(500);
    digitalWrite(BUZZER, HIGH);
  });
  
  ESPhttpUpdate.onProgress(updateCallback);
  
  ESPhttpUpdate.onError([](int error) {
    for (int i = 0; i < 3; i++) {
      digitalWrite(BUZZER, LOW);
      delay(100);
      digitalWrite(BUZZER, HIGH);
      delay(100);
    }
    errorIndicator();
  });

  WiFiClient client;
  client.setTimeout(30000);
  
  ESPhttpUpdate.setLedPin(LED_PIN, LOW);
}

void setupWebInterface() {
  // Root page with version info and interactive buttons
  server.on("/", HTTP_GET, []() {
    if (!handleAuthentication()) {
      return;
    }
    
    String html = F("<html><head>");
    html += F("<meta name='viewport' content='width=device-width, initial-scale=1'>");
    html += F("<style>");
    html += F("body { font-family: Arial, sans-serif; max-width: 800px; margin: 20px auto; padding: 20px; }");
    html += F(".card { background: #f0f0f0; padding: 15px; border-radius: 5px; margin: 10px 0; }");
    html += F(".button { background: #007bff; color: white; border: none; padding: 10px 20px; margin-right: 10px; border-radius: 5px; cursor: pointer; }");
    html += F(".button:disabled { background: #cccccc; cursor: not-allowed; }");
    html += F(".button.verify { background: #28a745; }");
    html += F("#status { margin-top: 20px; padding: 10px; border: 1px solid #ddd; border-radius: 5px; }");
    html += F(".error { color: #dc3545; }");
    html += F(".success { color: #28a745; }");
    html += F(".info { color: #007bff; }");
    html += F(".header-bar { display: flex; justify-content: space-between; align-items: center; margin-bottom: 20px; }");
    html += F(".user-info { display: flex; align-items: center; gap: 10px; }");
    html += F(".logout-btn { background: #dc3545; }");
    html += F(".progress-bar { width: 100%; height: 20px; background-color: #f0f0f0; border-radius: 10px; margin: 10px 0; overflow: hidden; }");
    html += F(".progress-fill { width: 0%; height: 100%; background-color: #007bff; transition: width 0.3s ease; }");
    html += F(".progress-text { text-align: center; margin-top: 5px; }");
    html += F(".update-messages { margin-top: 10px; }");
    html += F("</style>");
    html += F("</head><body>");
    
    html += F("<div class='header-bar'>");
    html += F("<h1>RFID Attendance System</h1>");
    html += F("<div class='user-info'>");
    html += F("<span>Logged in as: ");
    html += ADMIN_USERNAME;
    html += F("</span>");
    html += F("<button onclick='logout()' class='button logout-btn'>Logout</button>");
    html += F("</div>");
    html += F("</div>");
    
    html += F("<div class='card'>");
    html += F("<p><strong>Firmware Version:</strong> ");
    html += String(FIRMWARE_VERSION);
    html += F("</p><p><strong>Build Time:</strong> ");
    html += String(BUILD_TIMESTAMP);
    html += F("</p><p><strong>Device Token:</strong> ");
    html += String(device_token);
    html += F("</p><p><strong>Device IP:</strong> ");
    html += WiFi.localIP().toString();
    html += F("</p></div>");
    
    html += F("<div style='margin: 20px 0;'>");
    html += F("<button onclick='checkUpdate()' id='updateBtn' class='button'>Check for Updates</button>");
    html += F("<button onclick='verifyVersion()' id='verifyBtn' class='button verify'>Verify Version</button>");
    html += F("</div>");
    
    html += F("<div id='status'></div>");
    
    html += F("<script>");
    
    html += F("function logout() {");
    html += F("  fetch('/logout').then(() => {");
    html += F("    window.location.reload();");
    html += F("  });");
    html += F("}");
    
    html += F("function checkUpdate() {");
    html += F("  const statusDiv = document.getElementById('status');");
    html += F("  const updateBtn = document.getElementById('updateBtn');");
    html += F("  updateBtn.disabled = true;");

    html += F("  statusDiv.innerHTML = `");
    html += F("    <div class='info'>Checking for updates...</div>");
    html += F("    <div class='progress-bar'><div class='progress-fill'></div></div>");
    html += F("    <div class='progress-text'>0%</div>");
    html += F("    <div class='update-messages'></div>");
    html += F("  `;");

    html += F("  const progressBar = statusDiv.querySelector('.progress-fill');");
    html += F("  const progressText = statusDiv.querySelector('.progress-text');");
    html += F("  const messageDiv = statusDiv.querySelector('.update-messages');");

    html += F("  const eventSource = new EventSource('/check-update');");

    html += F("  eventSource.onmessage = function(event) {");
    html += F("    const data = event.data;");
    
    html += F("    if (data.includes('Progress:')) {");
    html += F("      const progress = parseInt(data.match(/Progress: (\\d+)%/)[1]);");
    html += F("      progressBar.style.width = progress + '%';");
    html += F("      progressText.textContent = progress + '%';");
    html += F("    } else if (data.includes('Error:')) {");
    html += F("      messageDiv.innerHTML += `<div class='error'>${data}</div>`;");
    html += F("      eventSource.close();");
    html += F("      updateBtn.disabled = false;");
    html += F("    } else if (data.includes('successful') || data.includes('Rebooting')) {");
    html += F("      messageDiv.innerHTML += `<div class='success'>${data}</div>`;");
    html += F("      eventSource.close();");
    html += F("      setTimeout(() => {");
    html += F("        messageDiv.innerHTML += '<div class=\"info\">Device is rebooting. This page will refresh in 30 seconds...</div>';");
    html += F("        setTimeout(() => { window.location.reload(); }, 30000);");
    html += F("      }, 2000);");
    html += F("    } else if (data.includes('Device is up to date')) {");
    html += F("      progressBar.style.width = '100%';");
    html += F("      progressText.textContent = '100%';");
    html += F("      progressBar.style.backgroundColor = '#28a745';");
    html += F("      messageDiv.innerHTML = `<div class='success'>✓ Your device is currently on the latest version (${FIRMWARE_VERSION})</div>`;");
    html += F("      eventSource.close();");
    html += F("      updateBtn.disabled = false;");
    html += F("    } else if (data !== '') {");
    html += F("      messageDiv.innerHTML += `<div class='info'>${data}</div>`;");
    html += F("    }");
    html += F("  };");

    html += F("  eventSource.onerror = function() {");
    html += F("    messageDiv.innerHTML += '<div class=\"error\">Connection lost. Please refresh the page.</div>';");
    html += F("    eventSource.close();");
    html += F("    updateBtn.disabled = false;");
    html += F("  };");
    html += F("}");
    
    html += F("function verifyVersion() {");
    html += F("  const statusDiv = document.getElementById('status');");
    html += F("  const verifyBtn = document.getElementById('verifyBtn');");
    html += F("  verifyBtn.disabled = true;");
    html += F("  statusDiv.className = 'info';");
    html += F("  statusDiv.innerHTML = 'Verifying version...';");
    
    html += F("  fetch('/verify')");
    html += F("  .then(response => {");
    html += F("    if (!response.ok) {");
    html += F("      throw new Error('Version verification failed');");
    html += F("    }");
    html += F("    return response.text();");
    html += F("  })");
    html += F("  .then(text => {");
    html += F("    statusDiv.className = 'success';");
    html += F("    statusDiv.innerHTML = text;");
    html += F("  })");
    html += F("  .catch(error => {");
    html += F("    statusDiv.className = 'error';");
    html += F("    statusDiv.innerHTML = 'Error: ' + error.message;");
    html += F("  })");
    html += F("  .finally(() => {");
    html += F("    setTimeout(() => { verifyBtn.disabled = false; }, 2000);");
    html += F("  });");
    html += F("}");
    
    html += F("</script>");
    html += F("</body></html>");
    
    server.send(200, "text/html", html);
  });

  // Rest of the endpoints remain unchanged
  server.on("/version", HTTP_GET, []() {
    if (!handleAuthentication()) return;
    DynamicJsonDocument doc(256);
    doc["version"] = FIRMWARE_VERSION;
    doc["buildTime"] = BUILD_TIMESTAMP;
    doc["deviceToken"] = device_token;
    doc["deviceIP"] = WiFi.localIP().toString();
    doc["wifiSSID"] = ssid;
    doc["wifiStrength"] = WiFi.RSSI();
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
  });

  server.on("/verify", HTTP_GET, []() {
    if (!handleAuthentication()) return;
    int versionNum = String(FIRMWARE_VERSION).substring(4).toInt();
    for (int i = 0; i < versionNum; i++) {
      digitalWrite(LED_PIN, HIGH);
      digitalWrite(BUZZER, LOW);
      delay(200);
      digitalWrite(LED_PIN, LOW);
      digitalWrite(BUZZER, HIGH);
      delay(300);
    }
    String response = "Version " + String(FIRMWARE_VERSION) + " verified - Blinked " + String(versionNum) + " times";
    server.send(200, "text/plain", response);
  });

  server.on("/check-update", HTTP_GET, []() {
    if (!handleAuthentication()) return;
    
    // Set headers for SSE
    server.sendHeader("Cache-Control", "no-cache");
    server.sendHeader("Content-Type", "text/event-stream");
    server.sendHeader("Connection", "keep-alive");
    server.sendHeader("Access-Control-Allow-Origin", "*");
    
    // Send initial message
    server.send(200, "text/event-stream", "data: Update check initiated\n\n");
    
    // Start the update process
    checkForUpdates();
  });

  server.on("/logout", HTTP_GET, []() {
    server.send(401, "text/plain", "Logged out");
  });

  server.onNotFound([]() {
    if (!handleAuthentication()) return;
    String message = F("Page Not Found\n\n");
    message += F("URI: ");
    message += server.uri();
    message += F("\nMethod: ");
    message += (server.method() == HTTP_GET ? F("GET") : F("POST"));
    message += F("\nArguments: ");
    message += String(server.args());
    message += F("\n");
    for (uint8_t i = 0; i < server.args(); i++) {
      message += F(" ") + server.argName(i) + F(": ") + server.arg(i) + F("\n");
    }
    server.send(404, "text/plain", message);
  });

  server.begin();
  Serial.println(F("Secure web server started"));
  Serial.print(F("Access the web interface at http://"));
  Serial.println(WiFi.localIP());
}

void checkForUpdates() {
  if (!WiFi.isConnected()) {
    Serial.println(F("[UPDATE] WiFi not connected. Skipping update check."));
    return;
  }
  
  isLongOperation = true;
  operationStartTime = millis();
  
  // Initialize secure client with more explicit settings
  WiFiClientSecure client;
  client.setInsecure();  
  client.setTimeout(30000);  // 30 second timeout
  client.flush();  // Clear any pending data
  
  Serial.println(F("\n[UPDATE] Starting update check..."));
  
  // Robust DNS resolution with retry
  IPAddress serverIP;
  int dnsRetries = 3;
  while (!WiFi.hostByName("rfidatttendance.site", serverIP) && dnsRetries > 0) {
    Serial.println(F("[UPDATE] DNS resolution retry..."));
    delay(1000);
    dnsRetries--;
  }
  
  if (dnsRetries == 0) {
    Serial.println(F("[UPDATE] DNS resolution failed after retries!"));
    errorIndicator();
    isLongOperation = false;
    return;
  }
  
  Serial.print(F("[UPDATE] Resolved IP: "));
  Serial.println(serverIP);
  
  HTTPClient https;
  https.setTimeout(10000);
  
  String metaUrl = String(F("https://rfidatttendance.site/firmware/firmware.bin.meta"));
  Serial.println("[UPDATE] Metadata URL: " + metaUrl);
  
  // Explicit connection close and reopen
  https.end();
  delay(100);
  
  if (!https.begin(client, metaUrl)) {
    Serial.println(F("[UPDATE] Failed to begin HTTPS connection for metadata"));
    errorIndicator();
    isLongOperation = false;
    return;
  }
  
  // Enhanced headers
  https.addHeader(F("Content-Type"), F("application/json"));
  https.addHeader(F("User-Agent"), F("ESP8266HTTPClient"));
  https.addHeader(F("Connection"), F("keep-alive"));
  https.addHeader(F("Cache-Control"), F("no-cache"));
  
  Serial.println(F("[UPDATE] Requesting metadata..."));
  
  // Add retry mechanism for metadata fetch
  int retries = 3;
  int httpCode;
  
  while (retries > 0) {
    httpCode = https.GET();
    if (httpCode == HTTP_CODE_OK) break;
    
    Serial.printf("[UPDATE] Metadata attempt failed, retries left: %d\n", retries - 1);
    delay(1000);
    retries--;
  }
  
  Serial.printf("[UPDATE] Metadata HTTP Code: %d\n", httpCode);
  
  if (httpCode == HTTP_CODE_OK) {
    String metaData = https.getString();
    https.end();
    
    Serial.println("[UPDATE] Metadata received: " + metaData);
    
    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, metaData);
    
    if (!error) {
      const char* serverVersion = doc["version"];
      const char* firmwareHash = doc["hash"];
      unsigned long firmwareSize = doc["size"];
      
      Serial.printf("[UPDATE] Server Version: %s, Current Version: %s\n", serverVersion, FIRMWARE_VERSION);
      Serial.printf("[UPDATE] Firmware Size: %lu bytes\n", firmwareSize);
      
      if (String(serverVersion) != String(FIRMWARE_VERSION)) {
        Serial.println(F("[UPDATE] Update needed. Preparing..."));
        
        // Send early response to browser
        server.send(200, "text/plain", "Update found - starting installation...");
        
        // Reset client before update
        client.flush();
        client.stop();
        delay(100);
        
        // Initialize new client for update
        WiFiClientSecure updateClient;
        updateClient.setInsecure();
        updateClient.setTimeout(60000);  // Longer timeout for firmware download
        
        ESPhttpUpdate.setLedPin(LED_PIN, LOW);
        
        ESPhttpUpdate.onStart([]() {
          Serial.println(F("[UPDATE] Update Start"));
          digitalWrite(LED_PIN, HIGH);
          digitalWrite(BUZZER, LOW);
          delay(200);
          digitalWrite(BUZZER, HIGH);
        });
        
        ESPhttpUpdate.onEnd([]() {
          Serial.println(F("[UPDATE] Update End"));
          digitalWrite(LED_PIN, LOW);
          successIndicator();
        });
        
        ESPhttpUpdate.onProgress([](int cur, int total) {
          static int lastProgress = -1;
          int progress = (cur * 100) / total;
          
          if (progress - lastProgress >= 10 || progress == 100) {
            Serial.printf("[UPDATE] Progress: %d%%\n", progress);
            lastProgress = progress;
            
            digitalWrite(LED_PIN, !digitalRead(LED_PIN));
            digitalWrite(BUZZER, LOW);
            delay(50);
            digitalWrite(BUZZER, HIGH);
            
            ESP.wdtFeed();
          }
        });
        
        ESPhttpUpdate.onError([](int error) {
          Serial.printf("[UPDATE] Error[%d]: ", error);
          String errorMsg;
          
          switch(error) {
            case HTTP_UE_TOO_LESS_SPACE: errorMsg = "Not enough space"; break;
            case HTTP_UE_SERVER_NOT_REPORT_SIZE: errorMsg = "Server did not report size"; break;
            case HTTP_UE_SERVER_FILE_NOT_FOUND: errorMsg = "Firmware file not found"; break;
            case HTTP_UE_SERVER_FORBIDDEN: errorMsg = "Forbidden"; break;
            case HTTP_UE_SERVER_WRONG_HTTP_CODE: errorMsg = "Wrong HTTP code"; break;
            case HTTP_UE_SERVER_FAULTY_MD5: errorMsg = "Wrong MD5"; break;
            case HTTP_UE_BIN_VERIFY_HEADER_FAILED: errorMsg = "Verify bin header failed"; break;
            case HTTP_UE_BIN_FOR_WRONG_FLASH: errorMsg = "New binary not for this flash"; break;
            default: errorMsg = "Connection failed";  // Changed from "Unknown error"
          }
          
          Serial.println(errorMsg);
          errorIndicator();
        });
        
        Serial.println(F("[UPDATE] Starting firmware download..."));
        
        // Add retry mechanism for firmware update
        int updateRetries = 3;
        t_httpUpdate_return ret;
        
        while (updateRetries > 0) {
          ret = ESPhttpUpdate.update(updateClient, "https://rfidatttendance.site/firmware/firmware.bin");
          
          if (ret != HTTP_UPDATE_FAILED) break;
          
          Serial.printf("[UPDATE] Update attempt failed, retries left: %d\n", updateRetries - 1);
          delay(1000);
          updateRetries--;
        }
        
        switch (ret) {
          case HTTP_UPDATE_FAILED:
            Serial.printf("[UPDATE] Update failed! Error (%d): %s\n", 
                        ESPhttpUpdate.getLastError(),
                        ESPhttpUpdate.getLastErrorString().c_str());
            errorIndicator();
            break;
            
          case HTTP_UPDATE_NO_UPDATES:
            Serial.println(F("[UPDATE] No updates available"));
            break;
            
          case HTTP_UPDATE_OK:
            Serial.println(F("[UPDATE] Update successful! Rebooting..."));
            successIndicator();
            delay(1000);
            ESP.restart();
            break;
        }
      } else {
        Serial.println(F("[UPDATE] Device is already on the latest version"));
        server.send(200, "text/plain", "Device is up to date");
      }
    } else {
      Serial.printf("[UPDATE] JSON Parse Error: %s\n", error.c_str());
      errorIndicator();
      server.send(200, "text/plain", "Update check failed - JSON parse error");
    }
  } else {
    Serial.printf("[UPDATE] Failed to get metadata. HTTP Code: %d\n", httpCode);
    Serial.println("[UPDATE] Error: " + https.errorToString(httpCode));
    errorIndicator();
    server.send(200, "text/plain", "Update check failed - HTTP error");
  }
  
  https.end();
  client.stop();
  isLongOperation = false;
}

bool initializeRFIDReader() {
  mfrc522.PCD_Init();
  delay(100);
  
  mfrc522.PCD_SetAntennaGain(MFRC522::RxGain_48dB);
  delay(50);
  
  byte version = mfrc522.PCD_ReadRegister(mfrc522.VersionReg);
  if (version == 0x00 || version == 0xFF) {
    return false;
  }
  
  return true;
}

bool handleCardRead(String &cardUID) {
  if (cardUID == lastCardUID && (millis() - lastCardRead <= CARD_TIMEOUT)) {
    return false;
  }
  
  bool sendResult = SendCardID(cardUID);
  return sendResult;
}


bool SendCardID(const String &Card_uid) {
  if (!WiFi.isConnected()) {
    errorBeep();
    return false;
  }

  IPAddress serverIP;
  if (!WiFi.hostByName("rfidatttendance.site", serverIP)) {
    errorBeep();
    return false;
  }
  
  isLongOperation = true;
  operationStartTime = millis();
  
  HTTPClient https;
  
  https.setTimeout(10000);
  
  secureClient.setInsecure();
  secureClient.setTimeout(10000);
  
  String getData = "?card_uid=" + Card_uid + "&device_token=" + device_token;
  String Link = "https://rfidatttendance.site/data/getdata.php" + getData;
  
  bool beginStatus = https.begin(secureClient, Link);
  if (!beginStatus) {
    errorBeep();
    https.end();
    isLongOperation = false;
    return false;
  }
  
  https.addHeader("Content-Type", "application/x-www-form-urlencoded");
  https.addHeader("User-Agent", "ESP8266HTTPClient");
  
  int httpCode = https.GET();
  
  if (httpCode > 0) {
    if (httpCode == HTTP_CODE_OK) {
      String payload = https.getString();
      payload.trim();
      
      if (payload.length() > 0 && 
          (payload.substring(0, 6) == "login:" || 
           payload.substring(0, 7) == "logout:" || 
           payload == "succesful" ||
           payload != "Not found!")) {
        
        successIndicator();
        https.end();
        isLongOperation = false;
        return true;
      } else {
        errorBeep();
        https.end();
        isLongOperation = false;
        return false;
      }
    } else {
      errorBeep();
      https.end();
      isLongOperation = false;
      return false;
    }
  } else {
    errorBeep();
    https.end();
    isLongOperation = false;
    return false;
  }
}

bool connectToWiFi() {
  WiFi.mode(WIFI_OFF);
  delay(1000);
  WiFi.mode(WIFI_STA);
  
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < MAX_WIFI_ATTEMPTS) {
    delay(WIFI_ATTEMPT_DELAY);
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    successBeep();
    return true;
  }
  
  errorBeep();
  return false;
}

bool isValidResponse(const String &payload) {
  return payload.substring(0, 6) == "login:" || 
         payload.substring(0, 7) == "logout:" || 
         payload == "succesful" ||
         payload.length() > 0;
}

// Enhanced Watchdog Logging Function
void logWatchdogEvent(const String& eventType, const String& details = "") {
  Serial.print("[WATCHDOG] ");
  Serial.print(eventType);
  if (details.length() > 0) {
    Serial.print(": ");
    Serial.print(details);
  }
  Serial.println();
}

void handleWatchdog() {
  unsigned long currentMillis = millis();
  
  // Standard watchdog feeding
  if (currentMillis - lastWatchdogFeed >= WATCHDOG_INTERVAL) {
    // Check if operation is taking too long
    if (!isLongOperation || (currentMillis - operationStartTime < OPERATION_TIMEOUT)) {
      ESP.wdtFeed();
      lastWatchdogFeed = currentMillis;
      
      // Reset consecutive failures if operation completes successfully
      watchdogStats.consecutiveFailures = 0;
      watchdogStats.systemStable = true;
    }
  }
  
  // Advanced operation timeout detection
  if (isLongOperation) {
    unsigned long currentOperationDuration = currentMillis - operationStartTime;
    
    // Track longest operation
    if (currentOperationDuration > watchdogStats.longestOperationTime) {
      watchdogStats.longestOperationTime = currentOperationDuration;
      logWatchdogEvent("LONG_OPERATION", "Duration: " + String(currentOperationDuration) + "ms");
    }
    
    // Critical operation threshold
    if (currentOperationDuration >= watchdogStats.criticalOperationThreshold) {
      watchdogStats.consecutiveFailures++;
      watchdogStats.systemStable = false;
      
      logWatchdogEvent("CRITICAL_TIMEOUT", 
        "Operation exceeded " + 
        String(watchdogStats.criticalOperationThreshold) + 
        "ms. Consecutive failures: " + 
        String(watchdogStats.consecutiveFailures)
      );
    }
  }
  
  // Intelligent restart mechanism
  if (isLongOperation && (currentMillis - operationStartTime >= OPERATION_TIMEOUT)) {
    watchdogStats.totalResets++;
    watchdogStats.lastResetTime = currentMillis;
    
    logWatchdogEvent("SYSTEM_RESTART", 
      "Total Resets: " + String(watchdogStats.totalResets) + 
      ", Consecutive Failures: " + String(watchdogStats.consecutiveFailures)
    );
    
    // Adaptive restart strategy
    if (watchdogStats.consecutiveFailures > 3) {
      // More aggressive recovery for persistent issues
      criticalErrorIndicator();
      delay(1000);
      ESP.restart();
    } else {
      // Soft reset for occasional timeouts
      errorIndicator();
      ESP.reset();
    }
  }
}

// Sound and Light Indicators
void startupBeep() {
  digitalWrite(BUZZER, LOW);
  delay(100);
  digitalWrite(BUZZER, HIGH);
  delay(50);
  digitalWrite(BUZZER, LOW);
  delay(50);
  digitalWrite(BUZZER, HIGH);
}

void successBeep() {
  digitalWrite(BUZZER, LOW);
  delay(200);
  digitalWrite(BUZZER, HIGH);
  delay(100);
  digitalWrite(BUZZER, LOW);
  delay(100);
  digitalWrite(BUZZER, HIGH);
}

void errorBeep() {
  // Emphasis on buzzer for error scenarios
  for (int i = 0; i < 5; i++) { // Increased number of beeps
    digitalWrite(BUZZER, LOW);
    delay(100);
    digitalWrite(BUZZER, HIGH);
    delay(100);
  }
}

void successIndicator() {
  digitalWrite(LED_PIN, HIGH);  // LED only lights for successful scan
  
  digitalWrite(BUZZER, LOW);
  delay(300);
  digitalWrite(BUZZER, HIGH);
  delay(100);
  digitalWrite(BUZZER, LOW);
  delay(100);
  digitalWrite(BUZZER, HIGH);
  
  delay(500);
  digitalWrite(LED_PIN, LOW);
}

void errorIndicator() {
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_PIN, HIGH);
    digitalWrite(BUZZER, LOW);
    delay(50);
    digitalWrite(LED_PIN, LOW);
    digitalWrite(BUZZER, HIGH);
    delay(50);
  }
  
  digitalWrite(LED_PIN, HIGH);
  digitalWrite(BUZZER, LOW);
  delay(200);
  digitalWrite(LED_PIN, LOW);
  digitalWrite(BUZZER, HIGH);
}

void criticalErrorIndicator() {
  for (int i = 0; i < 5; i++) {
    digitalWrite(LED_PIN, HIGH);
    digitalWrite(BUZZER, LOW);
    delay(200);
    digitalWrite(LED_PIN, LOW);
    digitalWrite(BUZZER, HIGH);
    delay(200);
  }
}

void startupIndicator() {
  digitalWrite(LED_PIN, HIGH);
  delay(100);
  digitalWrite(BUZZER, LOW);
  delay(100);
  digitalWrite(LED_PIN, LOW);
  digitalWrite(BUZZER, HIGH);
  delay(50);
  digitalWrite(LED_PIN, HIGH);
  digitalWrite(BUZZER, LOW);
  delay(50);
  digitalWrite(LED_PIN, LOW);
  digitalWrite(BUZZER, HIGH);
  delay(50);
  digitalWrite(LED_PIN, HIGH);
  delay(150);
  digitalWrite(LED_PIN, LOW);
}

void readyIndicator() {
  for (int i = 0; i < 2; i++) {
    digitalWrite(LED_PIN, HIGH);
    digitalWrite(BUZZER, LOW);
    delay(200);
    digitalWrite(LED_PIN, LOW);
    digitalWrite(BUZZER, HIGH);
    delay(100);
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println(F("\n=== RFID Attendance System Starting ==="));
  Serial.println("Firmware Version: " + String(FIRMWARE_VERSION));
  Serial.println("Build Time: " + String(BUILD_TIMESTAMP));
  
  // Initialize pins and indicators
  pinMode(BUZZER, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(BUZZER, HIGH);
  digitalWrite(LED_PIN, LOW);
  
  startupBeep();
  startupIndicator();
  
  // Initialize SPI and RFID
  SPI.begin();
  delay(50);
  
  if (!initializeRFIDReader()) {
    Serial.println(F("RFID Reader initialization failed! System halted."));
    while (true) {
      criticalErrorIndicator();
      delay(2000);
    }
  }
  
  // Connect to WiFi
  if (!connectToWiFi()) {
    Serial.println(F("Initial WiFi connection failed! Continuing with retries..."));
    errorIndicator();
  }

  // Show version indicator during boot
  unsigned long startTime = millis();
  bool versionShown = false;
  
  while (millis() - startTime < VERSION_CHECK_INTERVAL) {
    if (!versionShown) {
      int versionNum = String(FIRMWARE_VERSION).substring(4).toInt();
      
      // Blink and beep pattern based on version number
      for (int i = 0; i < versionNum; i++) {
        digitalWrite(LED_PIN, HIGH);
        digitalWrite(BUZZER, LOW);
        delay(200);
        digitalWrite(LED_PIN, LOW);
        digitalWrite(BUZZER, HIGH);
        delay(300);
      }
      
      versionShown = true;
    }
    yield(); // Allow ESP8266 to handle system tasks
  }

  // Setup OTA and Web Interface
  setupOTA();
  setupWebInterface();

  readyIndicator();
  Serial.println(F("System ready!"));
  Serial.println(F("============================"));
}

void loop() {
  static unsigned long lastReset = 0;
  static unsigned long lastUpdateCheck = 0;
  const unsigned long UPDATE_CHECK_INTERVAL = 3600000; // Check every hour
  
  handleWatchdog();
  server.handleClient();  // Handle web interface requests
  
  // Check for updates periodically
  if (millis() - lastOTACheck >= OTA_CHECK_INTERVAL) {
    checkForUpdates();
    lastOTACheck = millis();
  }

  // Check for updates periodically
  if (millis() - lastUpdateCheck >= UPDATE_CHECK_INTERVAL) {
    Serial.println("Performing periodic update check...");
    checkForUpdates();
    lastUpdateCheck = millis();
  }
  
  // WiFi maintenance
  if (!WiFi.isConnected()) {
    Serial.println(F("WiFi connection lost. Reconnecting..."));
    isLongOperation = true;
    operationStartTime = millis();
    connectToWiFi();
    isLongOperation = false;
  }
  
  // Periodic RFID reader reset
  if (millis() - lastReset > READER_RESET_INTERVAL) {
    isLongOperation = true;
    operationStartTime = millis();
    
    mfrc522.PCD_Reset();
    mfrc522.PCD_Init();
    mfrc522.PCD_SetAntennaGain(MFRC522::RxGain_48dB);
    lastReset = millis();
    
    isLongOperation = false;
  }
  
  // Clear old card data
  if (millis() - lastCardRead > CARD_TIMEOUT) {
    lastCardUID = "";
  }
  
  // Check for new cards
  if (!mfrc522.PICC_IsNewCardPresent()) {
    delay(50);
    return;
  }

  // Select and read card
  if (!mfrc522.PICC_ReadCardSerial()) {
    return;
  }
  
  isLongOperation = true;
  operationStartTime = millis();
  
  String currentCardUID = "";
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    if (mfrc522.uid.uidByte[i] < 0x10) currentCardUID += "0";
    currentCardUID += String(mfrc522.uid.uidByte[i], HEX);
  }
  
  if (handleCardRead(currentCardUID)) {
    lastCardUID = currentCardUID;
    lastCardRead = millis();
  }
  
  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
  
  isLongOperation = false;
  delay(50);
}