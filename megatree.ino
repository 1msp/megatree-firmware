#include <FastLED.h>
#include <Arduino.h>
#include <array>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <cmath>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncWebSocket.h>
#include <DNSServer.h>
#include <esp_wifi.h>
#include "esp_sleep.h"
#include "esp_adc_cal.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <AsyncJson.h>
#include <ESPmDNS.h> // Include the mDNS library

// NVS related
// Preferences for storing configs
Preferences preferences; // Declare a Preferences object

// Define Firmware Version
const char* firmwareVersion = "0.1.0";

// Define Firmware Update URL
const char* firmwareUrl = "http://iot.videntify.ai/firmware/mt/update.bin"; 
const char* firmwareMetadata = "http://iot.videntify.ai/firmware/mt/update.json";
bool isFirmwareUpdating = false;
bool startOTAUpdate = false;

// Define LED parameters
#define LED_TYPE    WS2813
#define COLOR_ORDER GRB
#define BRIGHTNESS  128
#define MIN_BRIGHTNESS 40
#define LEDS_PER_COL 14

#define LED_LF_PIN 25
#define NUM_LF_LEDS 14
CRGB lfLeds[NUM_LF_LEDS];

#define LED_RF_PIN 26
#define NUM_RF_LEDS 14
CRGB rfLeds[NUM_RF_LEDS];

#define LED_RR_PIN 32
#define NUM_RR_LEDS 14
CRGB rrLeds[NUM_RR_LEDS];

#define LED_LR_PIN 33
#define NUM_LR_LEDS 14
CRGB lrLeds[NUM_LR_LEDS];

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Define the GPIO pin where the analog sensor is connected
const int analogPin = 34; // Change as per your setup

// Resistor values in the voltage divider
const float R1 = 470000; // Replace with the actual R1 value
const float R2 = 1000000; // Replace with the actual R2 value

// Variables for managing timing
unsigned long previousMillisLF = 0;
unsigned long previousMillisRF = 0;
unsigned long previousMillisRR = 0;
unsigned long previousMillisLR = 0;
unsigned long previousMillisDisplayVoltage = 0;
unsigned long previousMillisDNS = 0;
unsigned long previousMillisIsConnectingWifi = 0;
unsigned long preloadMillisEnd = millis() + 5000;

// Factory reset
const int buttonPin = 15; // GPIO pin where the button is connected
int lastButtonState = HIGH; // Variable to store the last button state
unsigned long buttonHoldTime = 5000; // Required button hold time in milliseconds (5 seconds)
bool factoryResetTriggered = false;

enum ColorState { COLOR_WHITE, YELLOW, BLUE, RED };

unsigned long lastUpdate = 0;
ColorState colorState = COLOR_WHITE;

// Variables for logo LED behavior
int logoBrightness = 20;
bool increasingBrightness = true;
CRGB logoColor = CRGB::White;

bool initializingReadings = true;
const int numReadings = 20; // Size of the rolling window

// Use std::array instead of a raw array
std::array<int, numReadings> readings;

int readIndex = 0; // The index of the current reading
float total = 0;   // The running total
float average = 0; // The average

// Wi-Fi Credentials
String apSSID = "";         // Default SSID for AP mode
String apPassword = "";     // Default Password for AP mode

//External Wifi
String wifiSSID = "";
String wifiPassword = "";

bool isConnectingToWiFi = false;
bool wifiConnectionSuccess = false;
AsyncWebServerRequest *pendingRequest = nullptr;
unsigned long wifiStartAttemptTime = 0;
const unsigned long wifiTimeout = 10000; // 10-second timeout
String cachedJsonString;

#define MAX_CLIENTS 4  // ESP32 supports up to 10 but I have not tested it yet
#define WIFI_CHANNEL 6 // 2.4GHz channel 6

IPAddress localIP(4, 3, 2, 1);       // The IP address the web server
IPAddress gatewayIP(4, 3, 2, 1);     // Gateway IP address
IPAddress subnetMask(255, 255, 255, 0);

const String localIPURL = "http://4.3.2.1"; // String version of the local IP with http

DNSServer dnsServer;
AsyncWebServer server(80);
AsyncWebSocket ws("/ws"); // Create a WebSocket object
AsyncWebSocket wsScan("/ws/scanwifi"); // WebSocket for scanning networks

unsigned long lastScanTime = 0;         // Last scan initiation time
unsigned long wifiScanCount = 0;
const unsigned long scanInterval = 60000;     // Interval for scanning (10 
#define MAX_WIFI_SCANS 5
bool isScanning = false;               // Tracks ongoing scan

// Variables for Wi-Fi network selection
char selectedSSID[32];
char selectedPassword[64];
bool connectToWiFi = false;
bool hasInternet = false;
bool isPreloading = true;

const char index_html[] PROGMEM = R"=====(
<!DOCTYPE html>
<html>
  <head>
    <title>Megatree</title>
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <style>
      * {
        margin: 0;
        padding: 0;
        box-sizing: border-box;
      }
      html, body {
        height: 100%;
        width: 100%;
        font-family: Arial, sans-serif;
        background-color: #f7f7f7;
        display: flex;
        flex-direction: column;
        align-items: center;
        justify-content: center;
      }
      h1 {
        font-size: 11vw;
        max-width: 95%;
        color: #4a90e2;
        margin-bottom: 20px;
        text-align: center;
        white-space: nowrap;
        letter-spacing: 2px;
      }
      .container {
        width: 90%;
        background: white;
        border-radius: 10px;
        box-shadow: 0 4px 6px rgba(0, 0, 0, 0.1);
        padding: 20px;
        text-align: center;
      }
      .status-section {
        margin-bottom: 20px;
        padding: 10px;
        background: #f0f0f0;
        border-radius: 10px;
        text-align: left;
        width: 100%;
      }
      .infoDisplay {
        display: flex;
        justify-content: space-between;
        align-items: center;
        font-size: .7rem;
        margin: 5px 0;
        padding: 5px 0;
        text-transform: uppercase;
      }
      #ssidDisplay {
        font-size: 1.2rem;
        font-weight: bold;
        margin: 10px 0;
        color: #4CAF50;
        text-align: left;
      }
      .config-section {
        width: 100%;
        display: flex;
        flex-direction: column;
        align-items: flex-start;
      }
      label {
        font-size: 1rem;
        font-weight: bold;
        margin-bottom: 5px;
        margin-top: 15px;
      }
      select, input {
        width: 100%;
        padding: 10px;
        font-size: 1rem;
        border: 1px solid #ccc;
        border-radius: 5px;
        box-sizing: border-box;
      }
      button {
        font-size: 1rem;
        padding: 10px 20px;
        margin-top: 10px;
        border: none;
        border-radius: 5px;
        cursor: pointer;
        transition: background-color 0.3s;
        width: 100%;
      }
      #joinWifiButton {
        background-color: #007BFF !important;
        color: white !important;
        display: none;
        width: 90% !important;
        margin-bottom: 20px !important;
        text-align: center !important;
      }
      #updateFirmwareButton {
        background-color: #FFA500;
        color: black;
        display: block;
        border-radius: 0 !important;
      }
      button:disabled {
        background-color: #ccc;
        cursor: not-allowed;
      }
      .button-container {
        display: flex;
        flex-direction: column;
        justify-content: center; /* Centers vertically */
        align-items: center; /* Centers horizontally */
        width: 90%;
        background: #f0f0f0; /* Matches the container background */
        margin-top: 20px;
        text-align: center; 
        border: 1px solid black;
        visibility: hidden;
      }
    .update-info {
        font-size: 15px !important;
        font-weight: normal;
        color: black; /* A similar blue as the title for emphasis */
        display: block;
        width: 80%; /* Ensures it spans a consistent width */
        text-align: left; /* Ensures text within update-info is left-aligned */
        margin-top: 10px;
        line-height: 180%;
    }
    /* Overlay covering the whole page */
    .overlay {
      position: fixed;
      top: 0;
      left: 0;
      width: 100%;
      height: 100%;
      background-color: rgba(255, 255, 255, 0.7);
      z-index: 9999;
      display: flex;
      align-items: center;
      justify-content: center;
    }

    /* Spinner styling */
    .spinner {
      border: 8px solid #f3f3f3;
      border-top: 8px solid #3498db;
      border-radius: 50%;
      width: 120px;
      height: 120px;
      animation: spin 1s linear infinite;
    }

    @keyframes spin {
      0% { transform: rotate(0deg); }
      100% { transform: rotate(360deg); }
    }
    </style>
    <script>
      function setBrightness() {
        var xhr = new XMLHttpRequest();
        var brightness = document.getElementById("brightnessSlider").value;
        xhr.open("GET", "/setBrightness?value=" + brightness, true);
        xhr.send();
      }

      function setTheme() {
        var xhr = new XMLHttpRequest();
        var theme = document.getElementById("themeSelect").value;
        xhr.open("GET", "/setTheme?theme=" + theme, true);
        xhr.send();
      }

      function getBrightness() {
        var xhr = new XMLHttpRequest();
        xhr.open("GET", "/getBrightness", true);
        xhr.onreadystatechange = function () {
          if (xhr.readyState === 4 && xhr.status === 200) {
            var brightness = xhr.responseText;
            document.getElementById("brightnessSlider").value = brightness;
          }
        };
        xhr.send();
      }

      function getTheme() {
        var xhr = new XMLHttpRequest();
        xhr.open("GET", "/getTheme", true);
        xhr.onreadystatechange = function () {
          if (xhr.readyState === 4 && xhr.status === 200) {
            var theme = xhr.responseText;
            document.getElementById("themeSelect").value = theme;
          }
        };
        xhr.send();
      }

      function checkWifiStatus() {
        fetch('/wifiStatus')
          .then(response => response.json())
          .then(data => {
            const ssidDisplay = document.getElementById('ssidDisplay');
            if (data.status === 'not connected') {
              document.getElementById('joinWifiButton').style.display = 'block';
              document.getElementById('updateFirmwareButton').style.display = 'none';
              document.getElementById('updateInfo').style.display = 'none';
              ssidDisplay.innerText = '';
            } else if (data.status === 'connected') {
              document.getElementById('joinWifiButton').style.display = 'none';
              if (data.ssid) {
                ssidDisplay.innerText = `Connected: ${data.ssid}`;
              }
              checkUpdateAvailability();
            }
          })
          .catch(error => {
            console.error('Error fetching Wi-Fi status:', error);
          });
      }

      function checkUpdateAvailability() {
        fetch('/isUpdateAvailable')
          .then(response => response.json())
          .then(data => {
            const buttonContainer = document.getElementById('buttonContainer');
            const updateFirmwareButton = document.getElementById('updateFirmwareButton');
            const updateInfo = document.getElementById('updateInfo');

            if (data.status === true) {
              updateFirmwareButton.disabled = false;
              updateInfo.innerHTML = `New Firmware Available: <strong>${data.version}</strong><br> Your version: <strong>${data.localVersion}</strong><br><p><br><strong>What's new:</strong> ${data.release_notes}`;
              buttonContainer.style.visibility = 'visible';  
            } else {
              //updateFirmwareButton.style.display = 'none';
              //updateInfo.style.display = 'none';
              buttonContainer.style.visibility = 'hidden';
            }
          })
          .catch(error => {
            console.error('Error checking update availability:', error);
            document.getElementById('updateFirmwareButton').style.display = 'none';
            document.getElementById('updateInfo').style.display = 'none';
          });
      }

      function joinWifi() {
        window.location.href = '/wifi';
      }

      function triggerFirmwareUpdate() {
        var button = document.getElementById('updateFirmwareButton');
        button.disabled = true;
        button.innerText = 'Updating... Please wait';

        fetch('/triggerUpdate')
          .then(response => {
            if (response.ok) {
              //document.body.style.pointerEvents = 'none';
              //document.body.style.opacity = '0.5';
              document.getElementById('overlay').style.display = 'flex';
              setTimeout(function () {
                window.location.href = window.location.href + '?x=' + new Date().getTime();
              }, 30000);
            } else {
              document.getElementById('overlay').style.display = 'none';
              console.error('Failed to trigger firmware update');
              button.innerText = 'Failed. Try Again';
              button.disabled = false;
            }
          })
          .catch(error => {
            console.error('Error triggering firmware update:', error);
            button.innerText = 'Error. Try Again';
            button.disabled = false;
          });
      }

      window.onload = function () {
        getBrightness();
        getTheme();
        checkWifiStatus();
      };

      var socket = new WebSocket('ws://' + window.location.hostname + '/ws');
      socket.onmessage = function (event) {
        var data = JSON.parse(event.data);
        document.getElementById('batteryPercentage').textContent = data.percentage + '%';
        document.getElementById('averageVoltage').textContent = data.voltage.toFixed(2) + 'V';
        document.getElementById('runtime').textContent = data.runtime;
        document.getElementById('firmwareInfo').textContent = data.firmwareInfo;
      };
    </script>
  </head>
  <body>
    <h1>Megatree</h1>
    <div class="container">
      <div class="status-section">
        <p id="ssidDisplay"></p>
        <div class="infoDisplay">
          <div>Battery Capacity:</div>
          <div id="batteryPercentage">--%</div>
        </div>
        <div class="infoDisplay">
          <div>Voltage:</div>
          <div id="averageVoltage">--V</div>
        </div>
        <div class="infoDisplay">
          <div>Runtime:</div>
          <div id="runtime">--</div>
        </div>
         <div class="infoDisplay">
          <div>Firmware:</div>
          <div id="firmwareInfo">--</div>
        </div>
      </div>
      <div class="config-section">
        <label for="brightnessSlider">Brightness</label>
        <input type="range" id="brightnessSlider" min="40" max="255" onchange="setBrightness()">
        <label for="themeSelect">Theme</label>
        <select id="themeSelect" onchange="setTheme()">
          <option value="1" selected>Christmas Flash</option>
          <option value="2">Breathing White</option>
          <option value="3">Long Beach Pride</option>
          <option value="4">Lights Out</option>
        </select>
      </div>
    </div>
    <div id="buttonContainer" class="button-container">
        <div id="updateInfo" class="update-info"></div>
        <button id="updateFirmwareButton" onclick="triggerFirmwareUpdate()" disabled>Update (recommended)</button>
    </div>
    <button id="joinWifiButton" onclick="joinWifi()">Join Wi-Fi</button>
    <div id="overlay" class="overlay" style="display: none;">
      <div class="spinner"></div>
    </div>
  </body>
</html>
)=====";

const char wifi_html[] PROGMEM = R"=====(
<!DOCTYPE html>
<html>
  <head>
    <title>Wi-Fi Setup</title>
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <style>
     * {
        margin: 0;
        padding: 0;
        box-sizing: border-box;
      }
      html, body {
        height: 100%;
        width: 100%;
        font-family: Arial, sans-serif;
        background-color: #f7f7f7;
        display: flex;
        flex-direction: column;
        align-items: center;
        justify-content: center;
      }
      h1 {
       font-size: 11vw; /* Dynamically adjust font size to 10% of viewport width */
        max-width: 95%; /* Ensure it doesn't stretch beyond 90% of the container */
        color: #4a90e2;
        margin-bottom: 20px;
        text-align: center;
        white-space: nowrap; /* Prevent wrapping */
        letter-spacing: 2px;
      }
      .container {
        width: 90%;
        padding: 20px;
        background: white;
        border-radius: 10px;
        box-shadow: 0 4px 6px rgba(0, 0, 0, 0.1);
        text-align: center;
      }
      .form-group {
        text-align: left;
        margin-bottom: 15px;
      }
      label {
        font-size: 1rem;
        font-weight: bold;
        margin-bottom: 5px;
        display: block;
      }
      select, input {
        width: 100%;
        padding: 10px;
        font-size: 1rem;
        border: 1px solid #ccc;
        border-radius: 5px;
        box-sizing: border-box;
      }
      button {
        width: 100%;
        padding: 10px 20px;
        font-size: 1rem;
        background-color: #4a90e2;
        color: white;
        border: none;
        border-radius: 5px;
        cursor: pointer;
        transition: background-color 0.3s;
        margin-top: 10px;
      }
      button:disabled {
        background-color: #c7c7c7;
        cursor: not-allowed;
      }
      button:hover:not(:disabled) {
        background-color: #357abd;
      }
      #status {
        font-size: 1.2rem;
        margin-top: 15px;
        padding: 10px;
      }
      #status.success {
        color: green;
      }
      #status.error {
        color: red;
      }
      #status.info {
        color: black;
        font-size: 16px; 
        margin-bottom: 15px;
        text-align: left;
      }
      #instructions {
        font-size: 1rem;
        margin-top: 15px;
        color: #333;
        display: none;
      }
      a {
        font-size: 1rem;
        color: #4a90e2;
        text-decoration: none;
        font-weight: bold;
      }
      a:hover {
        text-decoration: underline;
      }
    </style>
  </head>
  <body>
    <h1>Wi-Fi Setup</h1>
    <div class="container">
      <p id="status" class="info"></p>
      <div class="form-group">
        <label for="ssid">Network:</label>
        <select id="ssid"></select>
      </div>
      <div class="form-group">
        <label for="password">Password:</label>
        <input type="text" id="password" placeholder="Enter your Wi-Fi password">
      </div>
      <button id="connect" onclick="connect()">Connect</button>
      <p id="instructions">
        Success! Open your browser and navigate to <a href='http://mt.local' target="_blank">http://mt.local</a> to manage your megatree.
      </p>
    </div>
    <script>
      var scanSocket = null;

      function connectWebSocket() {
        scanSocket = new WebSocket('ws://' + window.location.hostname + '/ws/scanwifi');

        scanSocket.onopen = function () {
          console.log('WebSocket connection established for scanning');
          updateStatus('Scanning for networks...', 'info');
        };

        scanSocket.onmessage = function (event) {
          console.log('Received data:', event.data);
          var data = JSON.parse(event.data);

          if (data.networks && data.networks.length > 0) {
            let ssidSelect = document.getElementById('ssid');
            ssidSelect.innerHTML = ''; // Clear existing options
            data.networks.forEach(network => {
              let option = document.createElement('option');
              option.value = network.ssid;
              option.text = `${network.ssid} (${network.rssi} dBm)`;
              ssidSelect.appendChild(option);
            });
            updateStatus('Select wifi network and enter the password.', 'info');
          } else {
            updateStatus('No networks found. Please try again.', 'error');
          }
        };

        scanSocket.onerror = function (error) {
          console.error('WebSocket error:', error);
          updateStatus('Error scanning networks. Please refresh the page.', 'error');
        };

        scanSocket.onclose = function () {
          console.log('WebSocket connection closed');
        };
      }

      function connect() {
        document.getElementById('connect').disabled = true;
        let ssid = document.getElementById('ssid').value;
        let password = document.getElementById('password').value;

        fetch('/connectToWiFi', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ ssid, password })
        })
          .then(response => response.json())
          .then(data => {
            if (data.status === 'success') {
              updateStatus('Connected successfully!', 'success');
              document.getElementById('instructions').style.display = 'block';
              setTimeout(function () {
                window.location.href = 'http://mt.local/';
              }, 10000);
            } else if (data.status === 'timeout') {
              updateStatus('Connection timed out. Please try again.', 'error');
              document.getElementById('connect').disabled = false;
            } else {
              updateStatus('Unexpected response.', 'error');
              document.getElementById('connect').disabled = false;
            }
          })
          .catch(error => {
            console.error('Error connecting to Wi-Fi:', error);
            updateStatus('Error connecting to Wi-Fi.', 'error');
            document.getElementById('connect').disabled = false;
          });
      }

      function updateStatus(message, statusClass) {
        let statusElement = document.getElementById('status');
        statusElement.className = statusClass;
        statusElement.innerText = message;
      }

      window.onload = connectWebSocket;
    </script>
  </body>
</html>
)=====";

bool userOverrideBrightness = false;
int selectedTheme = 1; //hardcoded default, otherwise pulled from NVS

// Global variables to keep track of the breathing effect
unsigned long lastBreath = 0; // Last update of brightness
int brightness = 128;
int minBrightnessLocal = 30;      // Current brightness level
bool increasing = true;      // Flag to track increasing/decreasing brightness
float phase = 0;


// Function to close all active WebSocket connections
void closeAllWebSockets(AsyncWebSocket& ws) {
    // Manually close each client connection
  ws.cleanupClients();  // Clean up any clients marked for deletion
  ws.enable(false);     // Disable new connections

  // Iterate over all connected clients
  for (size_t i = 0; i < ws.count(); i++) {
    AsyncWebSocketClient* client = ws.client(i);
    if (client && client->status() == WS_CONNECTED) {
      client->close(1000, "Server shutting down");
    }
  }

  Serial.println("WebSocket server has been shut down.");
}

void closeAllWebSockets() {
    delay(500);  // Wait for clients to receive the shutdown message (optional)
    closeAllWebSockets(ws);
    Serial.println("All ws WebSocket connections closed.");

    delay(500);  // Wait for clients to receive the shutdown message (optional)
   closeAllWebSockets(wsScan);
    Serial.println("All wsScan WebSocket connections closed.");
    delay(500);
}

void closeAllResources() {
  Serial.println("\nClosing All Resources");
  Serial.println("\nturning off leds");
  turnOffAllLeds();
  selectedTheme = 4;
  delay(1000);
  Serial.println("\nshutting down http server");
  server.end();
  Serial.println("\nshutting down websockets");
  closeAllWebSockets();
  delay(500);
  Serial.println("\nshutting down dns server");
  dnsServer.stop();
  delay(500);
  //WiFi.disconnect();      // Disconnect from network
  //WiFi.mode(WIFI_OFF);     // Turn off the Wi-Fi radio
}

void getAPSettings() {
  // Read stored Wi-Fi credentials
  preferences.begin("ap", false); // Open in read-only mode
  apSSID = preferences.getString("ssid", "");
  apPassword = preferences.getString("password", "");
  

  if(apSSID.isEmpty()) {
    apSSID = generateRandomWAPName();
    preferences.putString("ssid", apSSID);
  }

  if(apPassword.isEmpty()) {
    apPassword = generateRandomPassword();
    preferences.putString("password", apPassword);
  }

  preferences.end();
}

void setup() {
  Serial.setTxBufferSize(1024);
  Serial.begin(115200);
  // Wait for the Serial object to become available.
  while (!Serial);

  // Factory reset GPIO 15
  pinMode(buttonPin, INPUT_PULLUP); // Set the button pin as input with an internal pull-up resistor
  lastButtonState = digitalRead(buttonPin); //

  // Initialize all the readings to 0
  readings.fill(0);

  // Display setup
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.display();

  // Print a welcome message to the Serial port.
  Serial.println("\n\MT Firmware version: " + String(firmwareVersion) +  " compiled " __DATE__ " " __TIME__ " by VIDENTIFY");  // __DATE__ is provided by the platformio ide
  Serial.printf("Model: %s", ESP.getChipModel());
  Serial.printf("%s-%d\n\r", ESP.getChipModel(), ESP.getChipRevision());
  
  // Initialize Wi-Fi in AP+STA mode
  WiFi.mode(WIFI_MODE_APSTA);

  // Register Wi-Fi event handler
  WiFi.onEvent(WiFiEvent); // Register the event handler here

  // Read stored Wi-Fi credentials
  preferences.begin("wifi", true); // Open in read-only mode
  wifiSSID = preferences.getString("ssid", "");
  wifiPassword = preferences.getString("password", "");
  preferences.end();

  Serial.println("\nSSID: " + wifiSSID);
  //Serial.println("\storedPassword: " + wifiPassword);

  //Get AP Settings
  getAPSettings();

  if (wifiSSID != "") {
    //connectToWiFi = true;
    connectToSelectedWifiAsync(wifiSSID, wifiPassword);
    //connectToSelectedWiFi(wifiSSID, wifiPassword); // Attempt to connect using stored credentials
  } else {
    // No stored credentials, start AP mode
    startSoftAccessPoint(apSSID.c_str(), apPassword.c_str(), localIP, gatewayIP);
  }

  // Initialize mDNS
  if (!MDNS.begin("mt")) {
    Serial.println("Error setting up mDNS responder!");
  } else {
    Serial.println("mDNS responder started");
    // Add service to advertise
    MDNS.addService("http", "tcp", 80);
  }

  setUpDNSServer(dnsServer, localIP);
  setUpWebserver(server, localIP);

  // Initialize WebSocket for /
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  // ws for wifi scan
  wsScan.onEvent(onWsScanEvent);
  server.addHandler(&wsScan);
  
  server.begin();

  Serial.print("\n");
  Serial.print("Startup Time:"); // Should be somewhere between 270-350 for Generic ESP32 (D0WDQ6 chip, can have a higher startup time on first boot)
  Serial.println(millis());
  Serial.print("\n");

  delay(2000);

  // analogSetAttenuation(ADC_0db);
  analogReadResolution(12); // Set ADC resolution
  // Initialize the analogPin as an input
  pinMode(analogPin, INPUT); // Battery voltage input

  //get selected theme
  selectedTheme = get_nvs_selected_theme();
  Serial.println("\nSelected Theme: " + String(selectedTheme));

  //set up leds
  const int brightness = get_nvs_brightness();
  Serial.println("\nLED brightness: " + String(brightness));
  FastLED.setBrightness(brightness);
  FastLED.setMaxRefreshRate(0);

  FastLED.addLeds<LED_TYPE, LED_LF_PIN, COLOR_ORDER>(lfLeds, NUM_LF_LEDS);
  FastLED.addLeds<LED_TYPE, LED_RF_PIN, COLOR_ORDER>(rfLeds, NUM_RF_LEDS);
  FastLED.addLeds<LED_TYPE, LED_RR_PIN, COLOR_ORDER>(rrLeds, NUM_RR_LEDS);
  FastLED.addLeds<LED_TYPE, LED_LR_PIN, COLOR_ORDER>(lrLeds, NUM_LR_LEDS);

  Serial.println("LED Setup Done.");
  //preload the leds to overcome low voltage
  delay(500);
  led_preload();
  delay(500);

  //Check if connected to external wifi
  bool wifiEnabled = externalWifiEnabled();
  Serial.println("\nConnected to external wifi: " + String(wifiEnabled ? "true" : "false"));

  Serial.println("\nRandom WAP: " + generateRandomWAPName());
  Serial.println("\nRandom Pass: " + generateRandomPassword());
}

void WiFiEvent(arduino_event_id_t event) {
  switch(event) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.print("Connected to Wi-Fi network. IP Address: ");
      Serial.println(WiFi.localIP());
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      Serial.println("Disconnected from Wi-Fi network");
      break;
    default:
      break;
  }
}

void handleHttpBrightnessChange(AsyncWebServerRequest *request) {
  if (request->hasParam("value")) {
    userOverrideBrightness = true;
    String brightnessValue = request->getParam("value")->value();
    Serial.println("Setting brightness: " + brightnessValue);
    int newBrightness = brightnessValue.toInt();
    set_led_brightness(newBrightness);
    request->send(200, "text/plain", "Brightness set to " + brightnessValue);
  }
  else {
    request->send(400, "text/plain", "Brightness value not provided");
  }
}

void handleSetTheme(AsyncWebServerRequest *request) {
  Serial.println("Setting theme now!!!");
  if (request->hasParam("theme")) {
    String theme = request->getParam("theme")->value();
    Serial.println("Theme set to: " + theme);
    selectedTheme = theme.toInt();

    //persist to NVS
    preferences.begin("theme", false); 
    preferences.putString("id", theme);
    preferences.end();

    request->send(200, "text/plain", "theme set to " + theme);
  }
  else {
    request->send(400, "text/plain", "theme value not provided");
  }
}

void handleHttpGetTheme(AsyncWebServerRequest *request) {
  Serial.println("Retrieving theme now!!!");
  int lTheme = get_nvs_selected_theme();

  Serial.println("Current theme: " + String(lTheme));

  // Send the theme as the response
  request->send(200, "text/plain", String(lTheme));
}

int get_nvs_selected_theme() {
  // Open NVS in read-only mode
  preferences.begin("theme", true); 
  String theme = preferences.getString("id", "1"); // Default to "1" if no value exists
  preferences.end();

  return (int)theme.toInt();
}

void handleHttpGetBrightness(AsyncWebServerRequest *request) {
  Serial.println("Retrieving brightness now!!!");
  int brightness = get_nvs_brightness();

  Serial.println("Current brightness: " + String(brightness));

  // Send the theme as the response
  request->send(200, "text/plain", String(brightness));
}

int get_nvs_brightness() {
  // Open NVS in read-only mode
  preferences.begin("led", true); 
  int iNVSBrightness = preferences.getInt("brightness", 128); // Default to "1" if no value exists
  preferences.end();

  return iNVSBrightness;
}

void set_nvs_brightness(int newBrightness) {
  //persist to NVS
  preferences.begin("led", false); 
  preferences.putInt("brightness", newBrightness);
  preferences.end();
}

void set_led_brightness(int newBrightness) {
  int iConstrainedBrightness = constrain(newBrightness, MIN_BRIGHTNESS, 255); // Ensure within range
  FastLED.setBrightness(iConstrainedBrightness);
  set_nvs_brightness(iConstrainedBrightness);
}

void setUpDNSServer(DNSServer &dnsServer, const IPAddress &localIP) {
  // Define the DNS interval in milliseconds between processing DNS requests
  #define DNS_INTERVAL 30

  // Set the TTL for DNS response and start the DNS server
  dnsServer.setTTL(3600);
  dnsServer.start(53, "*", localIP);
  //dnsServer.start(53, "cp3domain.com", localIP);
}

void startSoftAccessPoint(const char *ssid, const char *password, const IPAddress &localIP, const IPAddress &gatewayIP) {
  // Define the maximum number of clients that can connect to the server
  #define MAX_CLIENTS 4
  // Define the WiFi channel to be used (channel 6 in this case)
  #define WIFI_CHANNEL 6

  // Set the WiFi mode to access point and station
  WiFi.mode(WIFI_MODE_APSTA);

  // Define the subnet mask for the WiFi network
  const IPAddress subnetMask(255, 255, 255, 0);

  // Configure the soft access point with a specific IP and subnet mask
  WiFi.softAPConfig(localIP, gatewayIP, subnetMask);

  // Start the soft access point with the given ssid, password, channel, max number of clients
  WiFi.softAP(ssid, password, WIFI_CHANNEL, 0, MAX_CLIENTS);

  // Disable AMPDU RX on the ESP32 WiFi to fix a bug on Android
  esp_wifi_stop();
  esp_wifi_deinit();
  wifi_init_config_t my_config = WIFI_INIT_CONFIG_DEFAULT();
  my_config.ampdu_rx_enable = false;
  esp_wifi_init(&my_config);
  esp_wifi_start();
  vTaskDelay(100 / portTICK_PERIOD_MS);  // Add a small delay
}

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    Serial.println("WebSocket client connected for main page");
  }
  else if (type == WS_EVT_DISCONNECT) {
    Serial.println("WebSocket client disconnected for main page");
  }
}

void startWiFiScan() {
    WiFi.scanNetworks(true); // Start async scan
    isScanning = true;       // Indicate that a scan is in progress
    Serial.println("Background Wi-Fi scan initiated...");
}

void onWsScanEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_CONNECT) {
        Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());

        // Send cached scan results immediately when a client connects
        if (!cachedJsonString.isEmpty()) {
            client->text(cachedJsonString); // Send the cached JSON string
            Serial.printf("Sent cached Wi-Fi networks to client #%u\n", client->id());
        } else {
            client->text("{\"networks\":[]}"); // Send an empty result if no cache exists
            Serial.printf("No cached results available for client #%u\n", client->id());
        }
    } else if (type == WS_EVT_DISCONNECT) {
        Serial.printf("WebSocket client #%u disconnected\n", client->id());
    } else if (type == WS_EVT_ERROR) {
        Serial.printf("WebSocket client #%u error: %s\n", client->id(), (char *)arg);
    } else if (type == WS_EVT_DATA) {
        AwsFrameInfo *info = (AwsFrameInfo *)arg;
        String message = "";

        // Accumulate the message data
        for (size_t i = 0; i < len; i++) {
            message += (char)data[i];
        }

        Serial.printf("Message received from client #%u: %s\n", client->id(), message.c_str());

        // Handle incoming message
        if (message == "startScan") {
            if (!WiFi.isConnected()) {
                startWiFiScan(); // Start a new scan
                client->text("{\"status\":\"scan started\"}");
            } else {
                client->text("{\"status\":\"already connected\"}");
            }
        }
    }
}

void setUpWebserver(AsyncWebServer &server, const IPAddress &localIP) {
  //======================== Webserver ========================
  // Required
  server.on("/connecttest.txt", [](AsyncWebServerRequest *request) { request->redirect("http://logout.net"); }); // windows 11 captive portal workaround
  server.on("/wpad.dat", [](AsyncWebServerRequest *request) { request->send(404); });                            // 404 response to stop repetitive requests

  // OTA Trigger Endpoint
  server.on("/triggerUpdate", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "OTA update initiated...");
    Serial.println("[OTA] Update triggered via web endpoint.");

    startOTAUpdate = true;
  });

  server.on("/isUpdateAvailable", HTTP_GET, handleIsUpdateAvailable);

  // Background responses for captive portal
  server.on("/generate_204", [](AsyncWebServerRequest *request) { request->redirect(localIPURL); });         // android captive portal redirect
  server.on("/redirect", [](AsyncWebServerRequest *request) { request->redirect(localIPURL); });             // microsoft redirect
  server.on("/hotspot-detect.html", [](AsyncWebServerRequest *request) { request->redirect(localIPURL); });  // apple call home
  server.on("/canonical.html", [](AsyncWebServerRequest *request) { request->redirect(localIPURL); });       // firefox captive portal call home
  server.on("/success.txt", [](AsyncWebServerRequest *request) { request->send(200); });                     // firefox captive portal call home
  server.on("/ncsi.txt", [](AsyncWebServerRequest *request) { request->redirect(localIPURL); });             // windows call home

  server.on("/getBrightness", HTTP_GET, handleHttpGetBrightness);
  server.on("/setBrightness", HTTP_GET, handleHttpBrightnessChange);

  server.on("/setTheme", HTTP_GET, handleSetTheme);
  server.on("/getTheme", HTTP_GET, handleHttpGetTheme);

  // Wi-Fi Selection Page
  server.on("/wifi", HTTP_GET, [](AsyncWebServerRequest *request) {
    AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", wifi_html);
    response->addHeader("Cache-Control", "no-cache");
    request->send(response);
  });

  // Wi-Fi Status Endpoint
  server.on("/wifiStatus", HTTP_GET, [](AsyncWebServerRequest *request) {
    DynamicJsonDocument jsonDoc(256); // Allocate sufficient memory for the JSON document
    jsonDoc["status"] = externalWifiEnabled() ? "connected" : "not connected";
    jsonDoc["ssid"] = WiFi.SSID(); // Optionally include the SSID if connected

    String response;
    serializeJson(jsonDoc, response); // Serialize JSON object into a string
    request->send(200, "application/json", response);
  });

  // Scan Networks
  server.on("/scanNetworks", HTTP_GET, [](AsyncWebServerRequest *request) {
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) {
      // Scan is in progress
      request->send(202, "application/json", "[]"); // Send an empty array with 202 Accepted status
    } else if (n == WIFI_SCAN_FAILED || n == 0) {
      // Scan has not been started yet or failed, so start it
      WiFi.scanNetworks(true); // Start async scan
      request->send(202, "application/json", "[]"); // Send an empty array with 202 Accepted status
    } else {
      // Scan completed
      String networks = "[";
      for (int i = 0; i < n; ++i) {
        networks += "\"" + WiFi.SSID(i) + "\"";
        if (i < n - 1) networks += ",";
      }
      networks += "]";
      WiFi.scanDelete(); // Delete scan result to free memory
      request->send(200, "application/json", networks);
    }
  });

  // Connect to Wi-Fi
  server.addHandler(new AsyncCallbackJsonWebHandler("/connectToWiFi", [](AsyncWebServerRequest *request, JsonVariant &json) {
      JsonObject jsonObj = json.as<JsonObject>();
      if (jsonObj.containsKey("ssid") && jsonObj.containsKey("password")) {
          wifiSSID = String(jsonObj["ssid"]);
          wifiPassword = String(jsonObj["password"]);
          //connectToWiFi = true;

          connectToSelectedWifiAsync(wifiSSID, wifiPassword);

          // Save credentials to preferences
          preferences.begin("wifi", false);
          preferences.putString("ssid", wifiSSID);
          preferences.putString("password", wifiPassword);
          preferences.end();

          // Store the request to send the status later
          pendingRequest = request;
      } else {
          request->send(400, "text/plain", "Invalid JSON");
      }
  }));

  // Return 404 to webpage icon
  server.on("/favicon.ico", [](AsyncWebServerRequest *request) { request->send(404); }); // webpage icon

  // Serve Basic HTML Page
  server.on("/", HTTP_ANY, [](AsyncWebServerRequest *request) {
    AsyncWebServerResponse *response = request->beginResponse(200, "text/html", index_html);
    // Remove or modify the caching headers to prevent caching
    // Comment out or remove the existing Cache-Control header
    // response->addHeader("Cache-Control", "public,max-age=31536000");

    // Add headers to prevent caching
    response->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    response->addHeader("Pragma", "no-cache");
    response->addHeader("Expires", "Wed, 11 Jan 1984 05:00:00 GMT");

    request->send(response);
    Serial.println("Served / landing page");
  });

  // The catch-all
  server.onNotFound([](AsyncWebServerRequest *request) {
    request->redirect(localIPURL);
    Serial.print("onnotfound ");
    Serial.print(request->host());  // This gives some insight into whatever was being requested on the serial monitor
    Serial.print(" ");
    Serial.print(request->url());
    Serial.print(" sent redirect to " + localIPURL + "\n");
  });
}

void handleWiFiScanning() {
    // Check if enough time has passed since the last scan
    if ((lastScanTime == 0 || millis() - lastScanTime >= scanInterval) && wifiScanCount < MAX_WIFI_SCANS) {
        lastScanTime = millis();
        Serial.println("Scanning WiFi Networks - " + String(++wifiScanCount) + " / " + String(MAX_WIFI_SCANS));
        WiFi.scanNetworks(true, false, true, 250); // Last parameter is the active scan time (in ms)
        //WiFi.scanNetworks(true); // Start an asynchronous scan
    }

    // Check the status of the scan
    int scanResult = WiFi.scanComplete();
    if (scanResult == WIFI_SCAN_RUNNING) {
        return; // Scan is still in progress, do nothing
    }

    if (scanResult != WIFI_SCAN_FAILED && scanResult != WIFI_SCAN_RUNNING) {
        processScanResults(); // Handle the completed scan
    }
}

void processScanResults() {
    int scanResult = WiFi.scanComplete();

    if (scanResult > 0) {
        Serial.printf("\nScan completed with %d networks found:\n", scanResult);

        // Create JSON document
        DynamicJsonDocument jsonDoc(2048); // Adjust capacity if needed
        JsonArray networks = jsonDoc.createNestedArray("networks");

        for (int i = 0; i < scanResult; ++i) {
            String ssid = sanitizeSSID(WiFi.SSID(i).substring(0, 32)); // Sanitize and limit SSID length
            int rssi = WiFi.RSSI(i);

            if (ssid.length() == 0) {
                Serial.printf("SSID: <Hidden>, RSSI: %d\n", rssi);
                continue; // Skip hidden SSIDs
            }

            JsonObject network = networks.createNestedObject();
            network["ssid"] = ssid;
            network["rssi"] = rssi;

            Serial.printf("\tSSID: %s, RSSI: %d\n", ssid.c_str(), rssi);
        }

        serializeJson(jsonDoc, cachedJsonString); // Cache the results
        ws.textAll(cachedJsonString); // Broadcast to WebSocket clients

        Serial.println("Scan results sent to clients.\n");
    } else if (scanResult == 0) {
        cachedJsonString = "{\"networks\":[]}";
        ws.textAll(cachedJsonString);
        Serial.println("No networks found.");
    } else {
        cachedJsonString = "{\"error\":\"Scan failed\"}";
        ws.textAll(cachedJsonString);
        Serial.println("Wi-Fi scan failed.");
    }

    WiFi.scanDelete(); // Clear scan results
}

String sanitizeSSID(String ssid) {
    String sanitized;
    for (char c : ssid) {
        if (isPrintable(c)) {
            sanitized += c; // Keep printable characters
        } else {
            sanitized += '?'; // Replace non-printable characters with a placeholder
        }
    }
    return sanitized;
}

void loop() {
  unsigned long currentMillis = millis();

  if(startOTAUpdate) {
    startOTAUpdate = false;
    isFirmwareUpdating = true;
    delay(500);
    closeAllResources();
    delay(500);
    
    Serial.println("\nxTaskCreate for otaUpdateTask");
    xTaskCreate(
        otaUpdateTask,    // Task function
        "OTA Update Task",// Name of task
        8192,             // Stack size in words
        (void*)firmwareUrl,// Parameter passed to the task
        1,                // Priority of the task
        NULL);            // Task handle
  }

  if(isFirmwareUpdating) {
    delay(30);
    return;
  }

  if(!isPreloading) {
    switch(selectedTheme) {
      case 1:
        themeChristmasFlash(currentMillis);
        break;

      case 2:
        themeBreathingWhite(currentMillis);
        break;

      case 3:
        themePride(currentMillis);
        break;

      case 4:
        turnOffAllLeds();
        break;

      default:
        break;
    }
  }  

  if((currentMillis - preloadMillisEnd >= 3000) && isPreloading == true) {
    isPreloading = false;
    preloadMillisEnd = 0;
  }
  // Log battery voltage and adjust LED brightness
  if(currentMillis - previousMillisDisplayVoltage >= 2000) {
    logBatteryVoltage();
    previousMillisDisplayVoltage = currentMillis;
  }

  // Check for factory reset
  if(isFactoryResetHeld(buttonPin, buttonHoldTime)) { 
    factoryReset();
  }

  if (isConnectingToWiFi && (currentMillis - previousMillisIsConnectingWifi >= 2000)) {
    previousMillisIsConnectingWifi = currentMillis;
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("Wi-Fi connected. IP Address: " + WiFi.localIP().toString());
      isConnectingToWiFi = false;
      wifiConnectionSuccess = true;

      // Send success response
      if (pendingRequest != nullptr) {
        pendingRequest->send(200, "application/json",
          "{\"status\": \"success\", \"ip\": \"" + WiFi.localIP().toString() + "\"}");
        pendingRequest = nullptr; // Clear the request
        delay(6000);
      }
      
      WiFi.softAPdisconnect(true); // Disconnect clients and stop the AP
      WiFi.mode(WIFI_MODE_STA);
      Serial.println("Access Point disabled.");

      // Initialize mDNS
      if (!MDNS.begin("mt")) {
        Serial.println("Error setting up mDNS responder!");
      } else {
        Serial.println("mDNS responder started");
        // Add service to advertise
        MDNS.addService("http", "tcp", 80);
      }
    } 
    else if (millis() - wifiStartAttemptTime > wifiTimeout) {
      Serial.println("Wi-Fi connection timed out.");
      isConnectingToWiFi = false;

      // Send timeout response
      if (pendingRequest != nullptr) {
        pendingRequest->send(500, "application/json", "{\"status\": \"timeout\"}");
        pendingRequest = nullptr; // Clear the request
      }
    }
  }

  if (WiFi.status() != WL_CONNECTED) {
      handleWiFiScanning();
  }

  dnsServer.processNextRequest();  // Process DNS requests
  delay(30);
}

void factoryReset() {
  Serial.println("Factory Reset Trigger");
  //selectedTheme = 4; //turn lights off as indicator
  delay(1000);
  turnOffAllLeds();
  delay(1000);

  // Clear stored Wi-Fi credentials
  preferences.begin("wifi", false);
  preferences.clear();
  preferences.end();

  // Clear LED settings
  preferences.begin("led", false); 
  preferences.clear();
  preferences.end();

  //Clear theme settings
  preferences.begin("theme", false); 
  preferences.clear();
  preferences.end();
  
  delay(1000);
  ESP.restart(); // Restart the ESP32 to apply changes
}

void connectToSelectedWifiAsync(String SSID, String Password) {
  WiFi.mode(WIFI_MODE_APSTA);
  WiFi.begin(SSID, Password);

  isConnectingToWiFi = true;
  wifiConnectionSuccess = false; // Reset the connection status
  wifiStartAttemptTime = millis();
}

void connectToSelectedWiFi(String SSID, String Password) {
  connectToWiFi = false;
  WiFi.mode(WIFI_MODE_APSTA);
  WiFi.begin(SSID, Password);
  unsigned long startAttemptTime = millis();

  // Attempt connection
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
    delay(750);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected to Wi-Fi!");
    Serial.println(WiFi.localIP());
    // No need to initialize mDNS here; it's handled by the event
    
  } else {
    Serial.println("\nFailed to connect to Wi-Fi.");
    // Restart AP
    startSoftAccessPoint(apSSID.c_str(), apPassword.c_str(), localIP, gatewayIP);
  }
}

void turnOffAllLeds() {
  for(int i = 0; i < LEDS_PER_COL; i++) {
    lfLeds[i] = CRGB::Black; // Set each LED to black (off)
    rfLeds[i] = CRGB::Black; // Set each LED to black (off)
    lrLeds[i] = CRGB::Black; // Set each LED to black (off)
    rrLeds[i] = CRGB::Black; // Set each LED to black (off)
  }
  FastLED.show(); // Update the LED strip
}

void led_preload() {
  preloadMillisEnd = millis();
  FastLED.setBrightness(50);
  for(int i = 0; i < LEDS_PER_COL; i++) {
    lfLeds[i] = CRGB::Black; // Set each LED to black (off)
    rfLeds[i] = CRGB::DarkRed; // Set each LED to black (off)
    lrLeds[i] = CRGB::Black; // Set each LED to black (off)
    rrLeds[i] = CRGB::DarkGreen; // Set each LED to black (off)
  }
  FastLED.show(); // Update the LED strip
}

void themeChristmasFlash(unsigned long currentMillis) {
  // Calculate the next time for twinkle effects
  unsigned long nextTwinkleLF = previousMillisLF + random(500,800);
  unsigned long nextTwinkleRF = previousMillisRF + random(500,800);
  unsigned long nextTwinkleRR = previousMillisRR + random(500,800);
  unsigned long nextTwinkleLR = previousMillisLR + random(500,800);

  // Twinkle effects for each LED array
  if (currentMillis - previousMillisLF >= nextTwinkleLF - previousMillisLF) {
    previousMillisLF = currentMillis;
    twinkleEffect(lfLeds, NUM_LF_LEDS);
    FastLED.show();
  }
  if (currentMillis - previousMillisRF >= nextTwinkleRF - previousMillisRF) {
    previousMillisRF = currentMillis;
    twinkleEffect(rfLeds, NUM_RF_LEDS);
    FastLED.show();
  }
  if (currentMillis - previousMillisRR >= nextTwinkleRR - previousMillisRR) {
    previousMillisRR = currentMillis;
    twinkleEffect(rrLeds, NUM_RR_LEDS);
    FastLED.show();
  }
  if (currentMillis - previousMillisLR >= nextTwinkleLR - previousMillisLR) {
    previousMillisLR = currentMillis;
    twinkleEffect(lrLeds, NUM_LR_LEDS);
    FastLED.show();
  }

  FastLED.show();
}

void twinkleEffect(CRGB leds[], int numLeds) {
  for (int i = 0; i < numLeds; i++) {
    if (random(10) < 3) {
      leds[i] = CHSV(0, 0, random(MIN_BRIGHTNESS, BRIGHTNESS));
    } else {
      const unsigned randomColor = random(6);
      switch(randomColor) {
        case 0:
          leds[i] = CRGB::Red;
          break;
        case 1:
          leds[i] = CRGB::Red;
          break;
        case 2:
          leds[i] = CRGB::Yellow;
          break;
        case 3:
          leds[i] = CRGB::Blue;
          break;
        case 4:
          leds[i] = CRGB::Black;
          break;
        case 5:
          leds[i] = CRGB::Green;
          break;
      }

      leds[i].subtractFromRGB(random(MIN_BRIGHTNESS, BRIGHTNESS));
    }
  }
}

void themeBreathingWhite(unsigned long currentMillis) {
  // Update the brightness more frequently for a smoother transition
  if (currentMillis - lastBreath > 10) { // Updating every 10 milliseconds
    lastBreath = currentMillis;

    // Increment the phase at a slow rate for a smooth breathing effect
    phase += 0.01; // Smaller increment for slower breathing

    // Ensure phase stays within the 0 to 2*PI range
    if (phase > 2 * PI) {
      phase -= 2 * PI;
    }

    // Calculate the range of brightness variation
    int brightnessRange = 255 - minBrightnessLocal;

    // Calculate brightness using the sinusoidal function, scaled and offset
    int brightnessValue = minBrightnessLocal + (sin(phase) + 1) * (brightnessRange / 2.0);

    // Set the color and brightness for each LED
    for (int i = 0; i < NUM_LF_LEDS; i++) {
      lfLeds[i] = CHSV(0, 0, brightnessValue);
    }
    for (int i = 0; i < NUM_RF_LEDS; i++) {
      rfLeds[i] = CHSV(0, 0, brightnessValue);
    }
    for (int i = 0; i < NUM_RR_LEDS; i++) {
      rrLeds[i] = CHSV(0, 0, brightnessValue);
    }
    for (int i = 0; i < NUM_LR_LEDS; i++) {
      lrLeds[i] = CHSV(0, 0, brightnessValue);
    }

    FastLED.show();
  }
}

// Global variables for the pride theme
unsigned long lastPrideUpdate = 0;
const long updateInterval = 5; // Time between updates in milliseconds
float hueValue = 0.0; // Hue value, as a float for smoother transitions

void themePride(unsigned long currentMillis) {
  // Check if it's time to update
  if (currentMillis - lastPrideUpdate >= updateInterval) {
    lastPrideUpdate = currentMillis;

    // Slowly increment the hue value
    hueValue += 0.3;
    if (hueValue >= 255.0) {
      hueValue -= 255.0; // Wrap around at 255
    }

    // Update each LED array with subdivided groups
    updateStripWithGroups(lfLeds, NUM_LF_LEDS, 7);
    updateStripWithGroups(rfLeds, NUM_RF_LEDS, 7);
    updateStripWithGroups(rrLeds, NUM_RR_LEDS, 7);
    updateStripWithGroups(lrLeds, NUM_LR_LEDS, 7);

    FastLED.show();
  }
}

// Function to update a strip with subdivided groups
void updateStripWithGroups(CRGB leds[], int numLeds, int numGroups) {
  int groupLength = numLeds / numGroups;

  for (int group = 0; group < numGroups; group++) {
    int hueOffset = group * (255 / numGroups); // Offset for each group

    for (int i = 0; i < groupLength; i++) {
      int ledIndex = group * groupLength + i;
      leds[ledIndex] = CHSV((int)hueValue + hueOffset, 255, 255);
    }
  }
}

void adjustLEDBrightness(float batteryVoltage) {
  const float minVoltage = 2.9;
  const float maxVoltage = 4.1;
  const uint8_t minBrightnessValue = MIN_BRIGHTNESS;
  const uint8_t maxBrightnessValue = BRIGHTNESS;

  if (batteryVoltage <= minVoltage) {
    FastLED.setBrightness(minBrightnessValue);
    Serial.print("set brightness (min): ");
    Serial.println(minBrightnessValue);
  } /*else if (batteryVoltage >= maxVoltage) {
    FastLED.setBrightness(maxBrightnessValue);
    Serial.print("set brightness (max): ");
    Serial.println(maxBrightnessValue);
  } else {
    uint8_t brightnessCalc = map(batteryVoltage * 100, minVoltage * 100, maxVoltage * 100, minBrightnessValue, maxBrightnessValue);
    FastLED.setBrightness(brightnessCalc);
    Serial.print("map brightness: ");
    Serial.println(brightnessCalc);
  }*/
}

void logBatteryVoltage() {
  // Subtract the last reading
  total -= readings[readIndex];
  if(initializingReadings && readIndex == (numReadings - 1)) {
    initializingReadings = false;
  }

  // Read from the sensor
  const int currentAnalogRead = analogRead(analogPin);
  readings[readIndex] = currentAnalogRead;
  const float currentVoltage = currentAnalogRead * (3.3 / 4095.0);
  const float currentInputVoltage = currentVoltage * ((R1 + R2) / R2);

  // Add the reading to the total
  total += readings[readIndex];

  // Calculate the average
  unsigned samples = numReadings;
  if(initializingReadings) {
    samples = readIndex + 1;
  }
  average = total / samples;

  // Advance to the next position in the array
  readIndex = (readIndex + 1) % numReadings;

  // Convert the average reading to voltage
  float inputVoltage = ReadVoltageAccurate2(average); // Observation only, review likely has to be calibrated

  //Serial.print("Average Input Voltage: ");
  //Serial.print(inputVoltage);
  //Serial.println("V");

  float batteryPercentage = calculateBatteryPercentage(inputVoltage);
  displayBatteryInfo(static_cast<int>(floor(batteryPercentage)), inputVoltage, currentAnalogRead);
  wsSendBatteryInfo(inputVoltage, static_cast<int>(floor(batteryPercentage)));

  // Adjust LED brightness based on the current battery voltage
  if(!userOverrideBrightness) {
    adjustLEDBrightness(inputVoltage);  // Ensure averageVoltage is calculated and available here
  }
}

void wsSendBatteryInfo(float batteryVoltage, int batteryPercentage) {
  // Create a JSON document
    StaticJsonDocument<200> doc;  // Adjust the size according to your needs

    // Fill the document
    doc["voltage"] = batteryVoltage;
    doc["percentage"] = batteryPercentage;
    doc["runtime"] = getRuntime();  // Assuming getRuntime() returns a String or compatible type
    doc["firmwareInfo"] = firmwareVersion;

    // Serialize JSON document to String
    String dataToSend;
    serializeJson(doc, dataToSend);
    Serial.println("sending ws data: " + dataToSend);  // Uncomment if you want to see the output in serial monitor

    // Send data to all connected WebSocket clients
    ws.textAll(dataToSend);
}

float calculateBatteryPercentage(float averageVoltage) {
    // Define full charge and discharge voltage levels
    const float fullChargeVoltage = 4.14; // Adjust as per your battery specification
    const float dischargeVoltage = 2.9; // Adjust as per your battery specification

    // Map the voltage to a percentage
    float percentage = (averageVoltage - dischargeVoltage) / (fullChargeVoltage - dischargeVoltage);
    percentage = percentage * 100;

    // Clamp the percentage between 0 and 100
    if (percentage > 100) {
        percentage = 100;
    } else if (percentage < 0) {
        percentage = 0;
    }

    return percentage;
}

void displayBatteryInfo(int percent, float lastVoltage, int lastAnalogRead) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE); // Draw white text

  if(factoryResetTriggered) {
    display.setTextSize(3);
    display.setCursor(30,0);     // Start at top-left corner
    display.print("Resetting");
    display.println("");
  }
  else if(isFirmwareUpdating) {
    display.setTextSize(3);
    display.setCursor(30,0);     // Start at top-left corner
    display.print("Firmware Updating");
    display.println("");

    display.setCursor(45,0);     // Start at top-left corner
    display.print("Do Not Turn Off");
    display.println("");
  }
  else {
    display.setTextSize(1);
    display.setCursor(0,0);     // Start at top-left corner
    display.print("Charge:  ");
    display.setTextSize(2);
    display.print(percent); // Print the float value
    display.println("%"); // Print the percentage symbol and move to the next line

    // Show runtime
    String runtime = getRuntime();

    display.setTextSize(1);

    display.setCursor(0,25);
    display.print("Wifi: ");
    if((WiFi.status() == WL_CONNECTED)) {
      display.println(wifiSSID);

      display.setCursor(0,35);
      display.print("Url: ");
      display.println("http://mt.local"); // Print the Wi-Fi password
    }
    else {
      display.println(apSSID);
      display.setCursor(0,35);
      display.print("Pass: ");
      display.println(apPassword); // Print the Wi-Fi password
    }

    display.setCursor(0,45);
    display.print("brightness:       ");
    display.println(FastLED.getBrightness()); // Print the brightness

    display.setCursor(0,55);
    display.print("Voltage:         ");
    display.println(lastVoltage); // Print the voltage

    /*display.setCursor(0,55);
    display.print("runtime:     ");
    display.println(runtime);*/
  }

  display.display();
}

String getRuntime() {
  unsigned long millisValue = millis();
  unsigned long hours = millisValue / 3600000; // Convert to hours
  unsigned long remainder = millisValue % 3600000;
  unsigned long minutes = remainder / 60000; // Convert remainder to minutes
  remainder = remainder % 60000;
  unsigned long seconds = remainder / 1000; // Convert remainder to seconds

  String runtime = "";
  runtime += hours;
  runtime += ":";
  if (minutes < 10) runtime += "0";
  runtime += minutes;
  runtime += ":";
  if (seconds < 10) runtime += "0";
  runtime += seconds;

  return runtime;
}

double ReadVoltageAccurate2(float adc_read){
  float reading = adc_read; // Reference voltage is 3v3 so maximum reading is 3v3 = 4095 in range 0 to 4095
  if(reading < 1 || reading > 4095) return 0;
  float voltage_divider_offset = ((R1 + R2) / R2);
  float magicNumbers = -0.000000000000016 * pow(reading,4) + 0.000000000118171 * pow(reading,3)- 0.000000301211691 * pow(reading,2)+ 0.001109019271794 * reading + 0.034143524634089;
  return magicNumbers * voltage_divider_offset;
}

bool isFactoryResetHeld(int pin, unsigned long holdTime) {
  int currentButtonState = digitalRead(pin); // Read the current state of the button
  static unsigned long buttonPressTime = 0; // Static variable to store the time when the button was first pressed

  if (currentButtonState == LOW && lastButtonState == HIGH) { // Button press detected
    buttonPressTime = millis(); // Record the time when the button was pressed
    Serial.println("\nFactory Reset Button Pressed");
  } else if (currentButtonState == HIGH && lastButtonState == LOW) { // Button release detected
    buttonPressTime = 0; // Reset the press time
    Serial.println("\nFactory Reset Button Released");
  }

  lastButtonState = currentButtonState; // Update the last button state

  unsigned long totalTimeHeld = millis() - buttonPressTime;
  if (currentButtonState == LOW && totalTimeHeld >= holdTime) {
    //buttonPressTime = millis(); // Reset the press time to prevent repeated triggering
    factoryResetTriggered = true;
    return true;
  } else {
    //Serial.println("\nFactory reset - false");
    return false;
  }
}

void otaUpdateTask(void *parameter) {
  const char* url = (const char*)parameter;
  performOTAUpdate(url);
  vTaskDelete(NULL); // Delete this task when done
}

// OTA Update Function
void performOTAUpdate(const char* url) {
  WiFiClient client;
  HTTPClient http;

  Serial.println("[OTA] Connecting to update server...");
  http.begin(client, url); // Connect to the firmware binary URL

  int httpCode = http.GET(); // Initiate GET request
  if (httpCode == HTTP_CODE_OK) {
    int contentLength = http.getSize(); // Get content length
    bool canBegin = Update.begin(contentLength); // Start the update

    if (canBegin) {
      Serial.println("[OTA] Beginning firmware update...");

      // Create a buffer to hold chunks of data
      uint8_t buff[128] = { 0 };
      WiFiClient * stream = http.getStreamPtr();

      size_t written = 0;
      unsigned long lastProgressTime = millis();

      while(http.connected() && (written < contentLength || contentLength == -1)) {
        size_t sizeAvailable = stream->available();
        if(sizeAvailable) {
          int c = stream->readBytes(buff, ((sizeAvailable > sizeof(buff)) ? sizeof(buff) : sizeAvailable));
          written += Update.write(buff, c);

          // Reset watchdog timer
          delay(1); // Yield to allow other tasks to run

          // Optionally, print progress every second
          if (millis() - lastProgressTime > 1000) {
            Serial.printf("Progress: %d/%d bytes\n", written, contentLength);
            lastProgressTime = millis();
          }
        } else {
          // No data available, delay a bit
          delay(1);
        }
      }

      if (Update.end()) {
        Serial.println("\n[OTA] Update completed!");
        if (Update.isFinished()) {
          Serial.println("[OTA] Restarting device...");
          ESP.restart();
        } else {
          Serial.println("[OTA] Update not finished. Error.");
        }
      } else {
        Serial.printf("[OTA] Update error: %s\n", Update.errorString());
      }
    } else {
      Serial.println("[OTA] Not enough space for OTA.");
    }
  } else {
    Serial.printf("[OTA] HTTP request failed. Error code: %d\n", httpCode);
  }
  http.end();
}

// Function to generate a random alphanumeric password
String generateRandomPassword(int length) {
  const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
  String result;
  for (int i = 0; i < length; i++) {
    result += charset[random(0, sizeof(charset) - 1)];
  }
  return result;
}

bool externalWifiEnabled() {
  return (WiFi.status() == WL_CONNECTED);
}

// HTTP GET handler to check for updates
void handleIsUpdateAvailable(AsyncWebServerRequest* request) {
    Serial.println("[Update Check] Starting update check...");

    HTTPClient http;
    http.begin(firmwareMetadata);

    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("[Update Check] Failed to fetch update.json. HTTP code: %d\n", httpCode);
        request->send(500, "application/json", "{\"status\": false, \"error\": \"Failed to fetch update metadata.\"}");
        http.end();
        return;
    }

    String payload = http.getString();
    http.end();

    Serial.println("\nUpstream firmwareMetadata: " + payload);

    // Parse JSON
    DynamicJsonDocument jsonDoc(1024);
    DeserializationError error = deserializeJson(jsonDoc, payload);

    if (error) {
        Serial.printf("[Update Check] Failed to parse update.json: %s\n", error.c_str());
        request->send(500, "application/json", "{\"status\": false, \"error\": \"Failed to parse update metadata.\"}");
        return;
    }

    const char* upstreamVersion = jsonDoc["version"];
    const char* releaseNotes = jsonDoc["release_notes"];

    if (isUpdateNeeded(firmwareVersion, upstreamVersion)) {
        // Update available
        DynamicJsonDocument responseDoc(256);
        responseDoc["status"] = true;
        responseDoc["version"] = upstreamVersion;
        responseDoc["release_notes"] = releaseNotes;
        responseDoc["localVersion"] = firmwareVersion;

        String response;
        serializeJson(responseDoc, response);

        Serial.printf("[Update Check] Update available: version %s\n", upstreamVersion);
        request->send(200, "application/json", response);
    } else {
        // No update available
        DynamicJsonDocument responseDoc(64);
        responseDoc["status"] = false;

        String response;
        serializeJson(responseDoc, response);

        Serial.println("[Update Check] No update available.");
        request->send(200, "application/json", response);
    }
}

// Enhanced version comparison function
bool isUpdateNeeded(const char* localVersion, const char* upstreamVersion) {
    int currentMajor = 0, currentMinor = 0, currentPatch = 0;
    int newMajor = 0, newMinor = 0, newPatch = 0;

    // Parse versions
    int parsedCurrent = sscanf(localVersion, "%d.%d.%d", &currentMajor, &currentMinor, &currentPatch);
    int parsedNew = sscanf(upstreamVersion, "%d.%d.%d", &newMajor, &newMinor, &newPatch);

    if (parsedCurrent < 1 || parsedNew < 1) {
        Serial.println("[Version Comparison] Invalid version format.");
        return false;
    }

    if (newMajor > currentMajor) return true;
    if (newMajor < currentMajor) return false;

    if (newMinor > currentMinor) return true;
    if (newMinor < currentMinor) return false;

    if (parsedNew > 2 && newPatch > currentPatch) return true;

    return false;
}

// Function to generate a random 8-character password using 0-9 and A-F
String generateRandomPassword() {
  String password = "";
  char characters[] = "0123456789ABCDEF"; // Allowed characters in the password

  // Seed the random number generator for more randomness
  randomSeed(analogRead(0));

  // Generate an 8-character password
  for (int i = 0; i < 8; i++) {
    int index = random(16); // Random index from 0 to 15
    password += characters[index]; // Append character at the random index
  }

  return password;
}

// Function to generate a random WAP name with prefix "mt-" followed by a number from 1 to 99
String generateRandomWAPName() {
  // Seed the random number generator for more randomness
  randomSeed(analogRead(0));

  // Generate a random number from 1 to 99
  int randomNumber = random(1, 100);  // random(min, max) generates numbers from min to max-1

  // Construct the WAP name by concatenating the prefix and the random number
  String wapName = "mt-" + String(randomNumber);

  return wapName;
}