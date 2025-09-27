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

// === Pinos e configurações ===
#define DHTPIN 4
#define DHTTYPE DHT11
#define ALARME_VERDE 14
#define ALARME_VERMELHO 27
#define REED_PIN 15
#define SD_MISO 33
#define SD_MOSI 26
#define SD_SCK  25
#define SD_CS   17

// === Setpoints ===
float TEMP_MIN = 20.0;
float TEMP_MAX = 30.0;
float UMID_MIN = 60.0;
float UMID_MAX = 70.0;
float PRESSAO_MIN = 500.0;
float PRESSAO_MAX = 700.0;

// === Variáveis globais ===
DHT dht(DHTPIN, DHTTYPE);
SPIClass spiSD(VSPI);
RTC_DS1307 rtc;
LiquidCrystal_I2C lcd(0x27, 16, 2);
File arquivo;
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
float temperatura = 0.0;
float umidade = 0.0;
float pressao = 0.0;
unsigned long tempoUltimaTroca = 0;
bool mostrarTelaHora = true;
bool portaFechada = true;
bool alarmeAtivo = false;
WiFiClient clientModbus;
bool clientModbusConectado = false;
WiFiServer serverModbus(502);
ModbusTCPServer modbusTCPServer;
PCF8591 pcf(0x48);
WiFiClientSecure clientHttps;
WiFiClient clientHttpHost;
WebServer serverHttp(80);
String linkDashboard = "https://lesmoraes.grafana.net/public-dashboards/e4933e3c55714b679a5978b967fe1f29";
const char* ssid = "VIVOFIBRA-3F5A";
const char* password = "97d1f23f5a";
const char* apiGatewayHost = "91xrkdweb2.execute-api.sa-east-1.amazonaws.com";
const int httpsPort = 443;

// === Inicialização de módulos ===
void inicializarLCD() {
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Inicializando...");
  delay(2000);
  lcd.clear();
  delay(500);
}

void inicializarRTC() {
  if (!rtc.begin()) {
    lcd.print("ERRO RTC");
    while (1);
  }
  rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
}

void inicializarSD() {
  spiSD.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  bool status = SD.begin(SD_CS, spiSD);
  if (!status) {
    lcd.clear();
    lcd.print("ERRO SD");
    while (1);
  }
  if (!SD.exists("/log.csv")) {
    arquivo = SD.open("/log.csv", FILE_WRITE);
    if (arquivo) {
      arquivo.println("DataHora,Temperatura,Umidade,Pressao,PortaFechada,AlarmeAtivo");
      arquivo.flush();
      arquivo.close();
      } else {
      Serial.println("Erro ao criar log.csv");
    }
  }
  digitalWrite(SD_CS, HIGH);
}

void inicializarWifi() {
  Serial.print("Conectando ao WiFi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.print("WiFi conectado. IP: ");
  Serial.println(WiFi.localIP());
}

void inicializarServerModbus() {
  serverModbus.begin();
  if (!modbusTCPServer.begin()) {
    Serial.println("Falha ao iniciar servidor Modbus TCP");
    lcd.print("ERRO MODBUS");
    while (1);
  }
  modbusTCPServer.configureHoldingRegisters(0, 20);
  Serial.println("Servidor Modbus TCP ativo");
}

void inicializarADC() {
  if (!pcf.begin()) {
    lcd.clear();
    lcd.print("ERRO ADC");
    while (1);
  }
}

void inicializarServerHttp() {
  serverHttp.on("/", handleRoot);
  serverHttp.on("/setpoints", HTTP_POST, handleSetpoints);
  serverHttp.on("/dados", HTTP_GET, handleDadosJson);
  serverHttp.begin();
  Serial.println("Servidor HTTP iniciado");
}

// ======= HANDLE RAIZ (IHM SERVER HTTP) =======
void handleRoot() {
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
  html += "<title>IHM Clean Room</title></head><body>";
  html += "<h2>📊 Medições atuais</h2>";
  html += "<p><b>Temperatura:</b> " + String(temperatura, 1) + " °C</p>";
  html += "<p><b>Umidade:</b> " + String(umidade, 1) + " %</p>";
  html += "<p><b>Pressão:</b> " + String(pressao, 2) + " Pa</p>";
  html += "<p><b>Porta:</b> " + String(portaFechada ? "Fechada" : "Aberta") + "</p>";
  html += "<p><b>Alarme:</b> " + String(alarmeAtivo ? "Ativo" : "Inativo") + "</p>";

  html += "<h2>⚙️ Setpoints atuais</h2>";
  html += "<p>TEMP: " + String(TEMP_MIN) + " - " + String(TEMP_MAX) + " °C</p>";
  html += "<p>UMID: " + String(UMID_MIN) + " - " + String(UMID_MAX) + " %</p>";
  html += "<p>PRESSÃO: " + String(PRESSAO_MIN) + " - " + String(PRESSAO_MAX) + " Pa</p>";

  // Formulário para atualizar setpoints
  html += "<h2>✏️ Definir novos setpoints</h2>";
  html += "<form method='POST' action='/setpoints'>";
  html += "TEMP_MIN: <input type='text' name='temp_min' value='" + String(TEMP_MIN) + "'><br>";
  html += "TEMP_MAX: <input type='text' name='temp_max' value='" + String(TEMP_MAX) + "'><br>";
  html += "UMID_MIN: <input type='text' name='umid_min' value='" + String(UMID_MIN) + "'><br>";
  html += "UMID_MAX: <input type='text' name='umid_max' value='" + String(UMID_MAX) + "'><br>";
  html += "PRESSAO_MIN: <input type='text' name='pressao_min' value='" + String(PRESSAO_MIN) + "'><br>";
  html += "PRESSAO_MAX: <input type='text' name='pressao_max' value='" + String(PRESSAO_MAX) + "'><br>";
  html += "<input type='submit' value='Salvar'>";
  html += "</form>";

  // QR Code + link
  html += "<h2>📱 Dashboard</h2>";
  html += "<img src='https://api.qrserver.com/v1/create-qr-code/?size=150x150&data=" 
          + linkDashboard + "' alt='QR Code'><br>";
  html += "<a href='" + linkDashboard + "' target='_blank'>" + "Clean Room Monitor" + "</a>";

  html += "</body></html>";

  serverHttp.send(200, "text/html; charset=UTF-8", html);
  salvarLogsHttp("Pagina IHM acessada com sucesso", "INFO");
}

// ======= HANDLE POST SETPOINTS SERVER HTTP =======
void handleSetpoints() {
  if (serverHttp.hasArg("temp_min")) TEMP_MIN = serverHttp.arg("temp_min").toFloat();
  if (serverHttp.hasArg("temp_max")) TEMP_MAX = serverHttp.arg("temp_max").toFloat();
  if (serverHttp.hasArg("umid_min")) UMID_MIN = serverHttp.arg("umid_min").toFloat();
  if (serverHttp.hasArg("umid_max")) UMID_MAX = serverHttp.arg("umid_max").toFloat();
  if (serverHttp.hasArg("pressao_min")) PRESSAO_MIN = serverHttp.arg("pressao_min").toFloat();
  if (serverHttp.hasArg("pressao_max")) PRESSAO_MAX = serverHttp.arg("pressao_max").toFloat();

  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'></head><body>";
  html += "<h3>✅ Setpoints atualizados!</h3>";
  html += "<a href='/'>Voltar</a>";
  html += "</body></html>";

  serverHttp.send(200, "text/html; charset=UTF-8", html);
  salvarSetpointsHttp();
}


// ======= HANDLE GET JSON SERVER HTTP=======
void handleDadosJson() {
  String json = "{";
  json += "\"temperatura\":" + String(temperatura, 1) + ",";
  json += "\"umidade\":" + String(umidade, 1) + ",";
  json += "\"pressao\":" + String(pressao, 2) + ",";
  json += "\"portaFechada\":" + String(portaFechada ? 1 : 0) + ",";
  json += "\"alarmeAtivo\":" + String(alarmeAtivo ? 1 : 0) + ",";
  json += "\"TEMP_MIN\":" + String(TEMP_MIN) + ",";
  json += "\"TEMP_MAX\":" + String(TEMP_MAX) + ",";
  json += "\"UMID_MIN\":" + String(UMID_MIN) + ",";
  json += "\"UMID_MAX\":" + String(UMID_MAX) + ",";
  json += "\"PRESSAO_MIN\":" + String(PRESSAO_MIN) + ",";
  json += "\"PRESSAO_MAX\":" + String(PRESSAO_MAX);
  json += "}";
  
  serverHttp.send(200, "application/json", json);
  salvarLogsHttp("Dados enviados via HTTP", "INFO");
}

// === Setup principal ===
void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);
  dht.begin();
  pcf.begin();

  pinMode(ALARME_VERDE, OUTPUT);
  pinMode(ALARME_VERMELHO, OUTPUT);
  pinMode(REED_PIN, INPUT_PULLUP);

  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);

  inicializarLCD();
  inicializarRTC();
  inicializarADC();
  inicializarSD();
  inicializarWifi();
  inicializarServerModbus();
  inicializarServerHttp();

  Serial.println("Sistema iniciado.");
}

// === Loop principal ===
void loop() {
  lerSensor();
  lerPressao();
  verificarPorta();
  verificarAlarmes();
  alternarTela();
  gravarDados();
  integrarDadosScada();
  serverHttp.handleClient();
  delay(2000);
}

// === Leitura sensores ===
void lerSensor() {
  temperatura = dht.readTemperature();
  umidade = dht.readHumidity();
  if (isnan(temperatura) || isnan(umidade)) {
    Serial.println("Erro na leitura do DHT11!");
    salvarLogsHttp("Erro na leitura do DHT11!", "ERROR");
  }
}

void lerPressao() {
  int leitura = pcf.read(0);  // Canal AIN0 retorna 0–255 (8 bits)

  if (leitura < 0 || leitura > 255) {
  Serial.println("Erro na leitura do ADC (PCF8591)!");
  salvarLogsHttp("Erro na leitura do ADC (PCF8591)!", "ERROR");
  return;
}
  
  // Converte para tensão (3.3V / 255)
  float tensao = leitura * (3.3 / 255.0);

  // Cálculo para sensor MPXV7002DP (saída 0.5V a 4.5V = -2 a +2 kPa)
  // A tensão em 2.5V corresponde a 0 Pa (pressão diferencial nula)
  // Fator de escala: 2 kPa / 2.0V = 1 kPa/V = 1000 Pa/V
  pressao = (tensao - 2.5) * 1000.0;

  // Limita para 2 casas decimais
  pressao = round(pressao * 100.0) / 100.0;
}

// === Verificar estado alarmes ===
void verificarAlarmes() {
  bool alarmeTemp = (temperatura < TEMP_MIN || temperatura > TEMP_MAX);
  bool alarmeUmid = (umidade < UMID_MIN || umidade > UMID_MAX);
  bool alarmePressao = (pressao < PRESSAO_MIN || pressao > PRESSAO_MAX);
  alarmeAtivo = alarmeTemp || alarmeUmid || alarmePressao;
  digitalWrite(ALARME_VERDE, !alarmeAtivo);
  digitalWrite(ALARME_VERMELHO, alarmeAtivo);
}

// === Alternar valores na tela ===
void alternarTela() {
  unsigned long agora = millis();
  if (agora - tempoUltimaTroca >= 5000) {
    mostrarTelaHora = !mostrarTelaHora;
    tempoUltimaTroca = agora;
    lcd.clear();
  }
  if (mostrarTelaHora) {
    DateTime now = rtc.now();
    char data[17], hora[17];
    snprintf(data, sizeof(data), "%02d/%02d/%04d      ", now.day(), now.month(), now.year());
    snprintf(hora, sizeof(hora), "%02d:%02d:%02d      ", now.hour(), now.minute(), now.second());
    lcd.setCursor(0, 0); lcd.print(data);
    lcd.setCursor(0, 1); lcd.print(hora);
  } else {
    char linha1[17], linha2[17];
    snprintf(linha1, sizeof(linha1), "T:%.1fC U:%d%%", temperatura, (int)umidade);
    snprintf(linha2, sizeof(linha2), "P:%dPa [%s]", (int)pressao, portaFechada ? "OK   " : "PORTA");
    lcd.setCursor(0, 0); lcd.print(linha1);
    lcd.setCursor(0, 1); lcd.print(linha2);
  }
}

// === Verificar estado da porta ===
void verificarPorta() {
  portaFechada = digitalRead(REED_PIN) == LOW; // LOW = ímã presente = porta fechada
}

// === Salvar Medicoes Cartao SD ===
void salvarMedicoesCsv() {
  DateTime now = rtc.now();
  char linha[128];
  snprintf(linha, sizeof(linha), "%02d/%02d/%04d %02d:%02d:%02d,%.1f,%d,%.0f,%s,%s",
           now.day(), now.month(), now.year(),
           now.hour(), now.minute(), now.second(),
           temperatura, (int)umidade, pressao,
           portaFechada ? "true" : "false",
           alarmeAtivo ? "true" : "false");

  arquivo = SD.open("/log.csv", FILE_APPEND);
  if (arquivo) {
    arquivo.println(linha);
    arquivo.flush();
    arquivo.close();
    salvarLogsHttp("Medicoes salvas no CSV", "INFO");
    Serial.println("Medicoes escritas no CSV:");
    Serial.println(linha);
  } else {
    salvarLogsHttp("Erro ao escrever no SD", "ERROR");
    Serial.println("Erro ao escrever no SD");
  }
  digitalWrite(SD_CS, HIGH);
}

// === Salvar Medicoes no Endpoint HTTP ===
void salvarMedicoesHttp() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi não conectado. Falha ao enviar dados HTTP");
    return;
  }

  WiFiClientSecure client;
  client.setInsecure();

  if (!client.connect(apiGatewayHost, httpsPort)) {
    Serial.println("Falha ao conectar HTTPS");
    return;
  }

  DateTime now = rtc.now();
  String json = "{";
  json += "\"datahora\":\"" + String(now.year()) + "-" + String(now.month()) + "-" + String(now.day()) +
          " " + String(now.hour()) + ":" + String(now.minute()) + ":" + String(now.second()) + "\",";
  json += "\"temperatura\":" + String(temperatura, 1) + ",";
  json += "\"umidade\":" + String(umidade, 1) + ",";
  json += "\"pressao\":" + String(pressao, 2) + ",";
  json += "\"estado_porta\":" + String(portaFechada ? 1 : 0) + ",";
  json += "\"estado_alarme\":" + String(alarmeAtivo ? 1 : 0);
  json += "}";

  client.println("POST /PROD/medicoes HTTP/1.1");
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
  Serial.println("Medicoes enviados via HTTP");
  salvarLogsHttp("Medicoes enviados via HTTP", "INFO");
}

// === Gravar Dados de Medicoes ===
void gravarDados() {
  static unsigned long ultimaGravacao = 0;
  const unsigned long intervaloGravacao = 300000;
  unsigned long agora = millis();
  if (agora - ultimaGravacao >= intervaloGravacao) {
    ultimaGravacao = agora;
    salvarMedicoesCsv();
    salvarMedicoesHttp();
  }
}

// === Integrar Dados com Scada ===
void integrarDadosScada() {
  // Se não há cliente ativo, verifica se alguém quer conectar
  if (!clientModbusConectado) {
    WiFiClient novoCliente = serverModbus.available();
    if (novoCliente) {
      clientModbus = novoCliente;
      modbusTCPServer.accept(clientModbus);
      clientModbusConectado = true;
      Serial.println("Cliente Modbus conectado");
      salvarLogsHttp("Cliente Modbus conectado", "INFO");
    }
  } 
  // Se já há cliente, mantém poll e verifica conexão
  else {
    if (clientModbus.connected()) {
      modbusTCPServer.poll();

      // Atualiza registradores de sensores
      modbusTCPServer.holdingRegisterWrite(0, (int)(temperatura * 10));
      modbusTCPServer.holdingRegisterWrite(1, (int)(umidade * 10));

      // pressão em 32 bits (2 registradores)
      int32_t pressaoInt = (int32_t)(pressao * 100); 
      uint16_t pressaoLow  = (uint16_t)(pressaoInt & 0xFFFF);
      uint16_t pressaoHigh = (uint16_t)((pressaoInt >> 16) & 0xFFFF);
      modbusTCPServer.holdingRegisterWrite(2, pressaoLow);
      modbusTCPServer.holdingRegisterWrite(3, pressaoHigh);


      modbusTCPServer.holdingRegisterWrite(4, portaFechada ? 1 : 0);
      modbusTCPServer.holdingRegisterWrite(5, alarmeAtivo ? 1 : 0);

      // Lê setpoints atualizados pelo cliente
      TEMP_MIN     = modbusTCPServer.holdingRegisterRead(10) / 10.0;
      TEMP_MAX     = modbusTCPServer.holdingRegisterRead(11) / 10.0;
      UMID_MIN     = modbusTCPServer.holdingRegisterRead(12) / 10.0;
      UMID_MAX     = modbusTCPServer.holdingRegisterRead(13) / 10.0;
      PRESSAO_MIN  = modbusTCPServer.holdingRegisterRead(14);
      PRESSAO_MAX  = modbusTCPServer.holdingRegisterRead(15);
      Serial.println("Dados integrados com Scada");
      Serial.print("TEMP_MIN: "); Serial.println(TEMP_MIN);
      Serial.print("TEMP_MAX: "); Serial.println(TEMP_MAX);
      Serial.print("UMID_MIN: "); Serial.println(UMID_MIN);
      Serial.print("UMID_MAX: "); Serial.println(UMID_MAX);
      Serial.print("PRESSAO_MIN: "); Serial.println(PRESSAO_MIN);
      Serial.print("PRESSAO_MAX: "); Serial.println(PRESSAO_MAX);
      salvarLogsHttp("Dados integrados com Scada", "INFO");

      salvarSetpointsHttp();
    } else {
      // Cliente desconectou, espera nova conexão
      clientModbusConectado = false;
      Serial.println("Cliente Modbus desconectado");
      salvarLogsHttp("Cliente Modbus desconectado", "INFO");
    }
  }
}

// === Salvar Setpoints no Endpoint HTTP ===
void salvarSetpointsHttp() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi não conectado. Falha ao enviar setpoints HTTP");
    return;
  }

  WiFiClientSecure client;
  client.setInsecure();

  if (!client.connect(apiGatewayHost, httpsPort)) {
    Serial.println("Falha ao conectar HTTPS");
    return;
  }

  DateTime now = rtc.now();
  String json = "{";
  json += "\"datahora\":\"" + String(now.year()) + "-" + String(now.month()) + "-" + String(now.day()) +
          " " + String(now.hour()) + ":" + String(now.minute()) + ":" + String(now.second()) + "\",";

  json += "\"temp_min\":" + String(TEMP_MIN, 1) + ",";
  json += "\"temp_max\":" + String(TEMP_MAX, 1) + ",";
  json += "\"umid_min\":" + String(UMID_MIN, 1) + ",";
  json += "\"umid_max\":" + String(UMID_MAX, 1) + ",";
  json += "\"pressao_min\":" + String(PRESSAO_MIN, 2) + ",";
  json += "\"pressao_max\":" + String(PRESSAO_MAX, 2) + ",";
  json += "\"ativo\":true";
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
  Serial.println("Setpoints enviados via HTTP");
  salvarLogsHttp("Setpoints enviados via HTTP", "INFO");
}

// === Salvar Logs no Endpoint HTTP ===
void salvarLogsHttp(const char* mensagem, const char* nivel) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi não conectado. Falha ao enviar log HTTP");
    return;
  }

  WiFiClientSecure client;
  client.setInsecure();

  if (!client.connect(apiGatewayHost, httpsPort)) {
    Serial.println("Falha ao conectar HTTPS para logs");
    return;
  }

  // Data/hora atual
  DateTime now = rtc.now();

  // Monta JSON do log
  char json[256];
  int n = snprintf(json, sizeof(json),
    "{\"datahora\":\"%04d-%02d-%02d %02d:%02d:%02d\","
    "\"log_level\":\"%s\",\"mensagem\":\"%s\"}",
    now.year(), now.month(), now.day(),
    now.hour(), now.minute(), now.second(),
    nivel, mensagem
  );

  if (n < 0 || n >= (int)sizeof(json)) {
    Serial.println("JSON overflow ao montar log, abortando envio");
    return;
  }

  // Monta requisição HTTP
  client.print("POST /PROD/logs HTTP/1.1\r\n");
  client.print("Host: "); client.print(apiGatewayHost); client.print("\r\n");
  client.print("Content-Type: application/json\r\n");
  client.print("Content-Length: "); client.print((int)strlen(json)); client.print("\r\n");
  client.print("Connection: close\r\n\r\n");
  client.print(json);

  // Lê resposta com timeout
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

  Serial.print("Log enviado via HTTP\n");
  Serial.println(mensagem);
}