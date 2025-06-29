#include <Wire.h>
#include <RTClib.h>
#include <LiquidCrystal_I2C.h>
#include "DHT.h"
#include <SPI.h>
#include <SD.h>
#include <Ethernet.h>

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

// === Variáveis globais ===
DHT dht(DHTPIN, DHTTYPE);
SPIClass spiSD(VSPI);
RTC_DS1307 rtc;
LiquidCrystal_I2C lcd(0x27, 16, 2);
File arquivo;
SPIClass spiETH(HSPI);
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
float temperatura = 0.0;
float umidade = 0.0;
unsigned long tempoUltimaTroca = 0;
bool mostrarTelaHora = true;
bool portaFechada = true;

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
  // Verifica se o arquivo já existe
  if (!SD.exists("/log.csv")) {
    // Arquivo não existe: criar com cabeçalho
    arquivo = SD.open("/log.csv", FILE_WRITE);
    if (arquivo) {
      arquivo.println("DataHora,Temperatura,Umidade,PortaFechada");
      arquivo.close();
    } else {
      Serial.println("Erro ao criar log.csv");
    }
  }
}

void inicializarEthernet() {
  spiETH.begin(ETH_SCK, ETH_MISO, ETH_MOSI, ETH_CS);  // Inicializa barramento HSPI
  Ethernet.init(ETH_CS);  // Define CS para a biblioteca

  if (Ethernet.begin(mac) == 0) {
    Serial.println("ERRO ETHERNET");
    while (true);
  }

  Serial.println("Ethernet conectada com sucesso. IP: ");
  Serial.println(Ethernet.localIP());
}

// === Setup principal ===
void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);
  Serial.println("Inicializando sistema...");
  dht.begin();
  pinMode(ALARME_VERDE, OUTPUT);
  pinMode(ALARME_VERMELHO, OUTPUT);
  pinMode(REED_PIN, INPUT_PULLUP);

  inicializarLCD();
  inicializarRTC();
  inicializarSD();
  inicializarEthernet();

  Serial.println("Sistema iniciado.");
}

// === Loop principal ===
void loop() {
  lerSensor();
  verificarPorta();  
  verificarAlarmes();
  alternarTela();
  gravarDados();
  delay(2000);
}

// === Leitura do DHT11 ===
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
    return;
  }
}

// === Verifica setpoints ===
void verificarAlarmes() {
  bool alarmeTemp = (temperatura < TEMP_MIN || temperatura > TEMP_MAX);
  bool alarmeUmid = (umidade < UMID_MIN || umidade > UMID_MAX);
  bool alarmeAtivo = alarmeTemp || alarmeUmid;

  digitalWrite(ALARME_VERDE, !alarmeAtivo);
  digitalWrite(ALARME_VERMELHO, alarmeAtivo);
}

// === Alterna entre telas a cada 5s ===
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
    snprintf(linha1, sizeof(linha1), "Temp: %.1f C", temperatura);
    snprintf(linha2, sizeof(linha2), "Umid: %d %% %s   ", (int)umidade, portaFechada ? "OK" : "PORTA");

    lcd.setCursor(0, 0); lcd.print(linha1);
    lcd.setCursor(0, 1); lcd.print(linha2);
  }
}

 void verificarPorta() {
  portaFechada = digitalRead(REED_PIN) == LOW; // LOW = ímã presente = porta fechada
}

void gravarDados() {
  unsigned long agora = millis();
  static unsigned long ultimaGravacao = 0;
  const unsigned long intervaloGravacao = 30000;

  if (agora - ultimaGravacao >= intervaloGravacao) {
    ultimaGravacao = agora;
    DateTime now = rtc.now();

    if (isnan(temperatura) || isnan(umidade)) return;

    char linha[64];
    snprintf(linha, sizeof(linha), "%02d/%02d/%04d %02d:%02d:%02d,%.1f,%d,%s",
             now.day(), now.month(), now.year(),
             now.hour(), now.minute(), now.second(),
             temperatura, (int)umidade, portaFechada ? "true" : "false");

    arquivo = SD.open("/log.csv", FILE_WRITE);
    if (arquivo) {
      arquivo.seek(arquivo.size());
      arquivo.println(linha);
      arquivo.close();
      Serial.println("Salvo:");
      Serial.println(linha);
    } else {
      Serial.println("Erro ao escrever no SD");
    }
  }
}

