#include <Wire.h>
#include <RTClib.h>
#include <LiquidCrystal_I2C.h>
#include "DHT.h"
#include <SPI.h>
#include <SD.h>
#include <ArduinoModbus.h>
#include <PCF8591.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>


// === Pins and configurations ===
#define DHT_PIN 4
#define DHT_TYPE DHT11
#define GREEN_ALARM_PIN 14
#define RED_ALARM_PIN 27
#define REED_PIN 15
#define SD_MISO 33
#define SD_MOSI 26
#define SD_SCK  25
#define SD_CS   17


// === Setpoints ===
float TEMP_MIN = 20.0;
float TEMP_MAX = 30.0;
float HUMID_MIN = 60.0;
float HUMID_MAX = 70.0;
float PRESS_MIN = 500.0;
float PRESS_MAX = 700.0;


// === Global variables ===
DHT dht(DHT_PIN, DHT_TYPE);
SPIClass spiSD(VSPI);
RTC_DS1307 rtc;
LiquidCrystal_I2C lcd(0x27, 16, 2);
File dataFile;
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
float temperature = 0.0;
float humidity = 0.0;
float pressure = 0.0;
unsigned long lastScreenSwitchTime = 0;
bool showTimeScreen = true;
bool doorClosed = true;
bool alarmActive = false;
WiFiClient modbusClient;
bool modbusClientConnected = false;
WiFiServer modbusServer(502);
ModbusTCPServer modbusTCPServer;
PCF8591 pcf(0x48);
WiFiClientSecure httpsClient;
WiFiClient httpHostClient;
WebServer httpServer(80);
String dashboardUrl = "https://lesmoraes.grafana.net/public-dashboards/e4933e3c55714b679a5978b967fe1f29";
String chatbotUrl = "https://notebooklm.google.com/notebook/4674b017-dcee-4ea8-82d2-3efdd1b10fd5";
const char* ssid = "REPLACE_WITH_YOUR_WIFI_SSID";
const char* password = "REPLACE_WITH_YOUR_PASSWORD";
const char* apiGatewayHost = "91xrkdweb2.execute-api.sa-east-1.amazonaws.com";
const int httpsPort = 443;


// === Module initialization ===
void initializeLCD() {
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Initializing...");
  delay(2000);
  lcd.clear();
  delay(500);
}


void initializeRTC() {
  if (!rtc.begin()) {
    lcd.print("RTC ERROR");
    saveHttpLog("RTC initialization error", "ERROR");
    while (1);
  }
  rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
}


void initializeSD() {
  spiSD.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  bool status = SD.begin(SD_CS, spiSD);
  if (!status) {
    lcd.clear();
    lcd.print("SD ERROR");
    saveHttpLog("SD initialization error", "ERROR");
    while (1);
  }
  if (!SD.exists("/data_log.csv")) {
    dataFile = SD.open("/data_log.csv", FILE_WRITE);
    if (dataFile) {
      dataFile.println("DateTime,Temperature,Humidity,Pressure,DoorClosed,AlarmActive");
      dataFile.flush();
      dataFile.close();
    } else {
      Serial.println("Error creating log file");
      saveHttpLog("Error creating log file", "ERROR");
    }
  }
  digitalWrite(SD_CS, HIGH);
}


void initializeWiFi() {
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.print("WiFi connected. IP: ");
  Serial.println(WiFi.localIP());
}


void initializeModbusServer() {
  modbusServer.begin();
  if (!modbusTCPServer.begin()) {
    Serial.println("Failed to start Modbus TCP server");
    saveHttpLog("Failed to start Modbus TCP server", "ERROR");
    lcd.print("MODBUS ERROR");
    while (1);
  }
  modbusTCPServer.configureHoldingRegisters(0, 20);
  Serial.println("Modbus TCP server active");
}


void initializeADC() {
  if (!pcf.begin()) {
    lcd.clear();
    lcd.print("ADC ERROR");
    saveHttpLog("ADC initialization failed", "ERROR");
    while (1);
  }
}


void initializeHttpServer() {
  httpServer.on("/", handleRoot);
  httpServer.on("/setpoints", HTTP_POST, handleSetpoints);
  httpServer.on("/data", HTTP_GET, handleJsonData);
  httpServer.begin();
  Serial.println("HTTP server started");
}


// ======= ROOT HANDLER (HTTP SERVER HMI) =======
void handleRoot() {
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
  html += "<title>Clean Room HMI</title></head><body>";
  html += "<h2>📊 Current Measurements</h2>";
  html += "<p><b>Temperature:</b> " + String(temperature, 1) + " °C</p>";
  html += "<p><b>Humidity:</b> " + String(humidity, 1) + " %</p>";
  html += "<p><b>Pressure:</b> " + String(pressure, 2) + " Pa</p>";
  html += "<p><b>Door:</b> " + String(doorClosed ? "Closed" : "Open") + "</p>";
  html += "<p><b>Alarm:</b> " + String(alarmActive ? "Active" : "Inactive") + "</p>";

  html += "<h2>⚙️ Current Setpoints</h2>";
  html += "<p>TEMP: " + String(TEMP_MIN) + " - " + String(TEMP_MAX) + " °C</p>";
  html += "<p>HUMID: " + String(HUMID_MIN) + " - " + String(HUMID_MAX) + " %</p>";
  html += "<p>PRESS: " + String(PRESS_MIN) + " - " + String(PRESS_MAX) + " Pa</p>";

  // Form to update setpoints
  html += "<h2>✏️ Update Setpoints</h2>";
  html += "<form method='POST' action='/setpoints'>";
  html += "TEMP_MIN: <input type='text' name='temp_min' value='" + String(TEMP_MIN) + "'> °C<br>";
  html += "TEMP_MAX: <input type='text' name='temp_max' value='" + String(TEMP_MAX) + "'> °C<br>";
  html += "HUMID_MIN: <input type='text' name='humid_min' value='" + String(HUMID_MIN) + "'> %<br>";
  html += "HUMID_MAX: <input type='text' name='humid_max' value='" + String(HUMID_MAX) + "'> %<br>";
  html += "PRESS_MIN: <input type='text' name='press_min' value='" + String(PRESS_MIN) + "'> Pa<br>";
  html += "PRESS_MAX: <input type='text' name='press_max' value='" + String(PRESS_MAX) + "'> Pa<br>";
  html += "<input type='submit' value='Save'>";
  html += "</form>";

  // QR Code + links
  html += "<h2>📱 Dashboard</h2>";
  html += "<img src='https://api.qrserver.com/v1/create-qr-code/?size=150x150&data=" 
          + dashboardUrl + "' alt='QR Code'><br>";
  html += "<a href='" + dashboardUrl + "' target='_blank'>Clean Room Monitor</a>";

  // AI Chatbot
  html += "<h2>🤖 Chatbot</h2>";
  html += "<a href='" + chatbotUrl + "' target='_blank'>Access Chatbot</a>";

  html += "</body></html>";

  httpServer.send(200, "text/html; charset=UTF-8", html);
  saveHttpLog("HMI page accessed successfully", "INFO");
}


// ======= SETPOINTS POST HANDLER =======
void handleSetpoints() {
  if (httpServer.hasArg("temp_min")) TEMP_MIN = httpServer.arg("temp_min").toFloat();
  if (httpServer.hasArg("temp_max")) TEMP_MAX = httpServer.arg("temp_max").toFloat();
  if (httpServer.hasArg("humid_min")) HUMID_MIN = httpServer.arg("humid_min").toFloat();
  if (httpServer.hasArg("humid_max")) HUMID_MAX = httpServer.arg("humid_max").toFloat();
  if (httpServer.hasArg("press_min")) PRESS_MIN = httpServer.arg("press_min").toFloat();
  if (httpServer.hasArg("press_max")) PRESS_MAX = httpServer.arg("press_max").toFloat();

  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'></head><body>";
  html += "<h3>✅ Setpoints updated!</h3>";
  html += "<a href='/'>Back</a>";
  html += "</body></html>";

  httpServer.send(200, "text/html; charset=UTF-8", html);
  saveSetpointsHttp();
}


// ======= JSON DATA HANDLER =======
void handleJsonData() {
  String json = "{";
  json += "\"temperature\":" + String(temperature, 1) + ",";
  json += "\"humidity\":" + String(humidity, 1) + ",";
  json += "\"pressure\":" + String(pressure, 2) + ",";
  json += "\"doorClosed\":" + String(doorClosed ? 1 : 0) + ",";
  json += "\"alarmActive\":" + String(alarmActive ? 1 : 0) + ",";
  json += "\"TEMP_MIN\":" + String(TEMP_MIN) + ",";
  json += "\"TEMP_MAX\":" + String(TEMP_MAX) + ",";
  json += "\"HUMID_MIN\":" + String(HUMID_MIN) + ",";
  json += "\"HUMID_MAX\":" + String(HUMID_MAX) + ",";
  json += "\"PRESS_MIN\":" + String(PRESS_MIN) + ",";
  json += "\"PRESS_MAX\":" + String(PRESS_MAX);
  json += "}";

  httpServer.send(200, "application/json", json);
  saveHttpLog("Data sent via HTTP", "INFO");
}


// === Main Setup ===
void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);
  dht.begin();
  pcf.begin();

  pinMode(GREEN_ALARM_PIN, OUTPUT);
  pinMode(RED_ALARM_PIN, OUTPUT);
  pinMode(REED_PIN, INPUT_PULLUP);

  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);

  initializeLCD();
  initializeWiFi();
  initializeRTC();
  initializeADC();
  initializeSD();
  initializeModbusServer();
  initializeHttpServer();

  Serial.println("System started successfully");
  saveHttpLog("System started successfully", "INFO");
}


// === Main Loop ===
void loop() {
  readSensors();
  readPressure();
  checkDoor();
  checkAlarms();
  switchScreen();
  recordData();
  integrateScadaData();
  httpServer.handleClient();
  delay(2000);
}


// === Sensor Reading ===
void readSensors() {
  temperature = dht.readTemperature();
  humidity = dht.readHumidity();
  if (isnan(temperature) || isnan(humidity)) {
    Serial.println("DHT11 read error!");
    saveHttpLog("DHT11 read error!", "ERROR");
  }
}


void readPressure() {
  int reading = pcf.read(0);  // Channel AIN0 returns 0-255 (8 bits)

  if (reading < 0 || reading > 255) {
    Serial.println("ADC (PCF8591) read error!");
    saveHttpLog("ADC (PCF8591) read error!", "ERROR");
    return;
  }
  
  // Convert to voltage (3.3V / 255)
  float voltage = reading * (3.3 / 255.0);

  // Calculation for MPXV7002DP sensor (0.5V to 4.5V = -2 to +2 kPa)
  // 2.5V corresponds to 0 Pa (zero differential pressure)
  // Scale factor: 2 kPa / 2.0V = 1 kPa/V = 1000 Pa/V
  pressure = (voltage - 2.5) * 1000.0;

  // Limit to 2 decimal places
  pressure = round(pressure * 100.0) / 100.0;
}


// === Check Alarm Status ===
void checkAlarms() {
  bool tempAlarm = (temperature < TEMP_MIN || temperature > TEMP_MAX);
  bool humidAlarm = (humidity < HUMID_MIN || humidity > HUMID_MAX);
  bool pressAlarm = (pressure < PRESS_MIN || pressure > PRESS_MAX);
  alarmActive = tempAlarm || humidAlarm || pressAlarm;
  digitalWrite(GREEN_ALARM_PIN, !alarmActive);
  digitalWrite(RED_ALARM_PIN, alarmActive);
}


// === Screen Switching ===
void switchScreen() {
  unsigned long now = millis();
  if (now - lastScreenSwitchTime >= 5000) {
    showTimeScreen = !showTimeScreen;
    lastScreenSwitchTime = now;
    lcd.clear();
  }
  if (showTimeScreen) {
    DateTime nowTime = rtc.now();
    char dateStr[17], timeStr[17];
    snprintf(dateStr, sizeof(dateStr), "%02d/%02d/%04d      ", nowTime.day(), nowTime.month(), nowTime.year());
    snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d      ", nowTime.hour(), nowTime.minute(), nowTime.second());
    lcd.setCursor(0, 0); lcd.print(dateStr);
    lcd.setCursor(0, 1); lcd.print(timeStr);
  } else {
    char line1[17], line2[17];
    snprintf(line1, sizeof(line1), "T:%.1fC H:%d%%", temperature, (int)humidity);
    snprintf(line2, sizeof(line2), "P:%dPa [%s]", (int)pressure, doorClosed ? "OK   " : "DOOR");
    lcd.setCursor(0, 0); lcd.print(line1);
    lcd.setCursor(0, 1); lcd.print(line2);
  }
}


// === Check Door Status ===
void checkDoor() {
  doorClosed = digitalRead(REED_PIN) == LOW; // LOW = magnet present = door closed
}


// === Save Measurements to SD Card ===
void saveMeasurementsCsv() {
  DateTime now = rtc.now();
  char line[128];
  snprintf(line, sizeof(line), "%02d/%02d/%04d %02d:%02d:%02d,%.1f,%d,%.0f,%s,%s",
           now.day(), now.month(), now.year(),
           now.hour(), now.minute(), now.second(),
           temperature, (int)humidity, pressure,
           doorClosed ? "true" : "false",
           alarmActive ? "true" : "false");

  dataFile = SD.open("/data_log.csv", FILE_APPEND);
  if (dataFile) {
    dataFile.println(line);
    dataFile.flush();
    dataFile.close();
    saveHttpLog("Measurements saved to CSV", "INFO");
    Serial.println("Measurements written to CSV:");
    Serial.println(line);
  } else {
    saveHttpLog("SD write error", "ERROR");
    Serial.println("SD write error");
  }
  digitalWrite(SD_CS, HIGH);
}


// === Save Measurements to HTTP Endpoint ===
void saveMeasurementsHttp() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected. HTTP data send failed");
    return;
  }

  WiFiClientSecure client;
  client.setInsecure();

  if (!client.connect(apiGatewayHost, httpsPort)) {
    Serial.println("HTTPS connection failed");
    return;
  }

  DateTime now = rtc.now();
  String json = "{";
  json += "\"datetime\":\"" + String(now.year()) + "-" + String(now.month()) + "-" + String(now.day()) +
          " " + String(now.hour()) + ":" + String(now.minute()) + ":" + String(now.second()) + "\",";
  json += "\"temperature\":" + String(temperature, 1) + ",";
  json += "\"humidity\":" + String(humidity, 1) + ",";
  json += "\"pressure\":" + String(pressure, 2) + ",";
  json += "\"door_state\":" + String(doorClosed ? 1 : 0) + ",";
  json += "\"alarm_state\":" + String(alarmActive ? 1 : 0);
  json += "}";

  client.println("POST /PROD/measurements HTTP/1.1");
  client.println("Host: 91xrkdweb2.execute-api.sa-east-1.amazonaws.com");
  client.println("Content-Type: application/json");
  client.print("Content-Length: "); client.println(json.length());
  client.println("Connection: close");
  client.println();
  client.println(json);

  while (client.connected()) {
    while (client.available()) {
      String line = client.readStringUntil('\n');
      Serial.println(line);
    }
  }
  client.stop();
  Serial.println("Measurements sent via HTTP");
  saveHttpLog("Measurements sent via HTTP", "INFO");
}


// === Record Measurement Data ===
void recordData() {
  static unsigned long lastRecording = 0;
  const unsigned long recordingInterval = 300000;  // 5 minutes
  unsigned long now = millis();
  if (now - lastRecording >= recordingInterval) {
    lastRecording = now;
    saveMeasurementsCsv();
    saveMeasurementsHttp();
  }
}


// === Integrate Data with SCADA ===
void integrateScadaData() {
  // If no active client, check for new connections
  if (!modbusClientConnected) {
    WiFiClient newClient = modbusServer.available();
    if (newClient) {
      modbusClient = newClient;
      modbusTCPServer.accept(modbusClient);
      modbusClientConnected = true;
      Serial.println("Modbus client connected");
      saveHttpLog("Modbus client connected", "INFO");
    }
  } 
  // If client exists, maintain poll and check connection
  else {
    if (modbusClient.connected()) {
      modbusTCPServer.poll();

      // Update sensor registers
      modbusTCPServer.holdingRegisterWrite(0, (int)(temperature * 10));
      modbusTCPServer.holdingRegisterWrite(1, (int)(humidity * 10));

      // Pressure in 32 bits (2 registers)
      int32_t pressureInt = (int32_t)(pressure * 100); 
      uint16_t pressureLow  = (uint16_t)(pressureInt & 0xFFFF);
      uint16_t pressureHigh = (uint16_t)((pressureInt >> 16) & 0xFFFF);
      modbusTCPServer.holdingRegisterWrite(2, pressureLow);
      modbusTCPServer.holdingRegisterWrite(3, pressureHigh);

      modbusTCPServer.holdingRegisterWrite(4, doorClosed ? 1 : 0);
      modbusTCPServer.holdingRegisterWrite(5, alarmActive ? 1 : 0);

      // Read setpoints only if valid Modbus data received
      bool setpointsReceived = false;
      for (int i = 10; i <= 15; i++) {
        if (modbusTCPServer.holdingRegisterRead(i) != 0) {
          setpointsReceived = true;
          break;
        }
      }

      // Read updated setpoints from client
      if (setpointsReceived) {
        TEMP_MIN     = modbusTCPServer.holdingRegisterRead(10) / 10.0;
        TEMP_MAX     = modbusTCPServer.holdingRegisterRead(11) / 10.0;
        HUMID_MIN    = modbusTCPServer.holdingRegisterRead(12) / 10.0;
        HUMID_MAX    = modbusTCPServer.holdingRegisterRead(13) / 10.0;
        PRESS_MIN    = modbusTCPServer.holdingRegisterRead(14);
        PRESS_MAX    = modbusTCPServer.holdingRegisterRead(15);

        Serial.println("Data integrated with SCADA");
        Serial.print("TEMP_MIN: "); Serial.println(TEMP_MIN);
        Serial.print("TEMP_MAX: "); Serial.println(TEMP_MAX);
        Serial.print("HUMID_MIN: "); Serial.println(HUMID_MIN);
        Serial.print("HUMID_MAX: "); Serial.println(HUMID_MAX);
        Serial.print("PRESS_MIN: "); Serial.println(PRESS_MIN);
        Serial.print("PRESS_MAX: "); Serial.println(PRESS_MAX);

        saveHttpLog("Data integrated with SCADA", "INFO");
        saveSetpointsHttp();
      }
    } else {
      // Client disconnected, wait for new connection
      modbusClientConnected = false;
      Serial.println("Modbus client disconnected");
      saveHttpLog("Modbus client disconnected", "INFO");
    }
  }
}


// === Save Setpoints to HTTP Endpoint ===
void saveSetpointsHttp() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected. HTTP setpoints send failed");
    return;
  }

  WiFiClientSecure client;
  client.setInsecure();

  if (!client.connect(apiGatewayHost, httpsPort)) {
    Serial.println("HTTPS connection failed");
    return;
  }

  DateTime now = rtc.now();
  String json = "{";
  json += "\"datetime\":\"" + String(now.year()) + "-" + String(now.month()) + "-" + String(now.day()) +
          " " + String(now.hour()) + ":" + String(now.minute()) + ":" + String(now.second()) + "\",";

  json += "\"temp_min\":" + String(TEMP_MIN, 1) + ",";
  json += "\"temp_max\":" + String(TEMP_MAX, 1) + ",";
  json += "\"humid_min\":" + String(HUMID_MIN, 1) + ",";
  json += "\"humid_max\":" + String(HUMID_MAX, 1) + ",";
  json += "\"press_min\":" + String(PRESS_MIN, 2) + ",";
  json += "\"press_max\":" + String(PRESS_MAX, 2) + ",";
  json += "\"active\":true";
  json += "}";

  client.println("POST /PROD/setpoints HTTP/1.1");
  client.println("Host: 91xrkdweb2.execute-api.sa-east-1.amazonaws.com");
  client.println("Content-Type: application/json");
  client.print("Content-Length: "); client.println(json.length());
  client.println("Connection: close");
  client.println();
  client.println(json);

  while (client.connected()) {
    while (client.available()) {
      String line = client.readStringUntil('\n');
      Serial.println(line);
    }
  }
  client.stop();
  Serial.println("Setpoints sent via HTTP");
  saveHttpLog("Setpoints sent via HTTP", "INFO");
}


// === Save Logs to HTTP Endpoint ===
void saveHttpLog(const char* message, const char* level) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected. HTTP log send failed");
    return;
  }

  WiFiClientSecure client;
  client.setInsecure();

  if (!client.connect(apiGatewayHost, httpsPort)) {
    Serial.println("HTTPS connection failed for logs");
    return;
  }

  // Current date/time
  DateTime now = rtc.now();

  // Build log JSON
  char json[256];
  int n = snprintf(json, sizeof(json),
    "{\"datetime\":\"%04d-%02d-%02d %02d:%02d:%02d\","
    "\"log_level\":\"%s\",\"message\":\"%s\"}",
    now.year(), now.month(), now.day(),
    now.hour(), now.minute(), now.second(),
    level, message
  );

  if (n < 0 || n >= (int)sizeof(json)) {
    Serial.println("JSON overflow building log, aborting send");
    return;
  }

  // Build HTTP request
  client.print("POST /PROD/logs HTTP/1.1\r\n");
  client.print("Host: "); client.print(apiGatewayHost); client.print("\r\n");
  client.print("Content-Type: application/json\r\n");
  client.print("Content-Length: "); client.print((int)strlen(json)); client.print("\r\n");
  client.print("Connection: close\r\n\r\n");
  client.print(json);

  // Read response with timeout
  unsigned long start = millis();
  while (client.connected() && (millis() - start) < 5000) {
    while (client.available()) {
      String line = client.readStringUntil('\n');
      Serial.println(line);
      start = millis();
    }
    delay(1);
  }
  client.stop();

  Serial.print("Log sent via HTTP: ");
  Serial.println(message);
}
