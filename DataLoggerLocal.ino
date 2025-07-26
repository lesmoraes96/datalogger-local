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
EthernetServer server(502);
ModbusTCPServer modbusTCPServer;
PCF8591 pcf(0x48);

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

void inicializarModbus() {
  server.begin();
  if (!modbusTCPServer.begin()) {
    Serial.println("Falha ao iniciar Modbus TCP");
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

// === Setup principal ===
void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);
  dht.begin();

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
  inicializarModbus();

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

// === Gravar dados no SD Card ===
void gravarDados() {
  static unsigned long ultimaGravacao = 0;
  const unsigned long intervaloGravacao = 30000;
  unsigned long agora = millis();
  if (agora - ultimaGravacao >= intervaloGravacao) {
    ultimaGravacao = agora;
    DateTime now = rtc.now();
    if (isnan(temperatura) || isnan(umidade)) return;
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
      Serial.println("Salvo:");
      Serial.println(linha);
      } else {
      Serial.println("Erro ao escrever no SD");
    }
    digitalWrite(SD_CS, HIGH);
  }
}

void integrarDadosScada() {
  EthernetClient client = server.available();
  if (client) {
    modbusTCPServer.accept(client);
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
