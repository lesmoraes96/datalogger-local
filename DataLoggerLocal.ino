#include <Wire.h>
#include <RTClib.h>
#include <LiquidCrystal_I2C.h>
#include "DHT.h"
#include <SPI.h>
#include <SD.h>
#include <Ethernet_Generic.h>
#include <utility/w5100.h>
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
#define ETH_CS    5
#define ETH_MOSI  23
#define ETH_MISO  19
#define ETH_SCK   18

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
SPIClass spiETH(HSPI);
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
EthernetClient clientModbus;
EthernetServer serverModbus(502);
ModbusTCPServer modbusTCPServer;
PCF8591 pcf(0x48);
WiFiClientSecure clientHttps;
WiFiClient clientHttpHost;
WiFiServer serverHttp(80);
const char* ssid = "VIVOFIBRA-3F5A";
const char* password = "97d1f23f5a";
const char* apiGatewayHost = "91xrkdweb2.execute-api.sa-east-1.amazonaws.com";
const int httpsPort = 443;

// === Utilitário para evitar conflito SPI ===
void selecionarDispositivoSPI(int csAtivo) {
  digitalWrite(SD_CS, HIGH);
  digitalWrite(ETH_CS, HIGH);
  digitalWrite(csAtivo, LOW);
}

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
  selecionarDispositivoSPI(SD_CS);
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

void inicializarEthernet() {
  spiETH.begin(ETH_SCK, ETH_MISO, ETH_MOSI, ETH_CS);
  Ethernet.init(ETH_CS);
  if (Ethernet.begin(mac, &spiETH) == 0) {
    lcd.clear();
    lcd.print("ERRO ETHERNET");
    while (1);
  }
  Serial.print("Ethernet IP: ");
  Serial.println(Ethernet.localIP());
  digitalWrite(ETH_CS, HIGH);
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
  serverHttp.begin();
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
  pinMode(ETH_CS, OUTPUT);
  digitalWrite(ETH_CS, HIGH);

  inicializarLCD();
  inicializarRTC();
  inicializarADC();
  inicializarSD();
  inicializarEthernet();
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
  integrarDadosServerHttp();
  delay(2000);
}

// === Leitura sensores ===
void lerSensor() {
  temperatura = dht.readTemperature();
  umidade = dht.readHumidity();
  if (isnan(temperatura) || isnan(umidade)) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Erro sensor DHT");
    lcd.setCursor(0, 1);
    lcd.print("                ");
    Serial.println("Erro na leitura do DHT11!");
  }
}

void lerPressao() {
  int leitura = pcf.read(0);  // Canal AIN0 retorna 0–255 (8 bits)
  
  // Converte para tensão (3.3V / 255)
  float tensao = leitura * (3.3 / 255.0);

  // Cálculo para sensor MPXV7002DP (saída 0.5V a 4.5V = -2 a +2 kPa)
  // A tensão em 2.5V corresponde a 0 Pa (pressão diferencial nula)
  // Fator de escala: 2 kPa / 2.0V = 1 kPa/V = 1000 Pa/V
  pressao = (tensao - 2.5) * 1000.0;
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
    snprintf(linha2, sizeof(linha2), "P:%dPa [%s]", (int)pressao, portaFechada ? "OK" : "PORTA");
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

  selecionarDispositivoSPI(SD_CS);
  arquivo = SD.open("/log.csv", FILE_APPEND);
  if (arquivo) {
    arquivo.println(linha);
    arquivo.flush();
    arquivo.close();
    Serial.println("Salvo no CSV:");
    Serial.println(linha);
  } else {
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
  json += "\"pressao\":" + String(pressao) + ",";
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
  Serial.println("Dados enviados via HTTP");
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
  clientModbus = serverModbus.available();
  if (clientModbus) {
    modbusTCPServer.accept(clientModbus);
    modbusTCPServer.poll();
    modbusTCPServer.holdingRegisterWrite(0, (int)(temperatura * 10));
    modbusTCPServer.holdingRegisterWrite(1, (int)(umidade * 10));
    modbusTCPServer.holdingRegisterWrite(2, (int)(pressao));
    modbusTCPServer.holdingRegisterWrite(3, portaFechada ? 1 : 0);
    modbusTCPServer.holdingRegisterWrite(4, alarmeAtivo ? 1 : 0);
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
  }
}

// === Rota para dados JSON ===
void integrarDadosServerHttp() {
  clientHttpHost = serverHttp.available();
  if (clientHttpHost) {
    Serial.println("Cliente conectado");

    // Espera por dados do cliente
    while (clientHttpHost.connected() && !clientHttpHost.available()) {
      delay(1);
    }

    String req = clientHttpHost.readStringUntil('\r');
    clientHttpHost.readStringUntil('\n');
    Serial.println("Requisição: " + req);

    if (req.indexOf("GET /dados") >= 0) {
      String json = "{";
      json += "\"temperatura\":" + String(temperatura, 1) + ",";
      json += "\"umidade\":" + String(umidade, 1) + ",";
      json += "\"pressao\":" + String(pressao, 0) + ",";
      json += "\"portaFechada\":" + String(portaFechada ? "true" : "false") + ",";
      json += "\"alarmeAtivo\":" + String(alarmeAtivo ? "true" : "false");
      json += "}";

      clientHttpHost.println("HTTP/1.1 200 OK");
      clientHttpHost.println("Content-Type: application/json");
      clientHttpHost.println("Connection: close");
      clientHttpHost.println();
      clientHttpHost.println(json);

      Serial.println("JSON enviado:");
      Serial.println(json);
    } else {
      clientHttpHost.println("HTTP/1.1 404 Not Found");
      clientHttpHost.println("Content-Type: text/plain");
      clientHttpHost.println("Connection: close");
      clientHttpHost.println();
      clientHttpHost.println("Rota não encontrada.");
    }

    delay(1);
    clientHttpHost.stop();
    Serial.println("Cliente desconectado\n");
  }
}