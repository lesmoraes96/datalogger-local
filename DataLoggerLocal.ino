#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <RTClib.h>
#include "DHT.h"
#include <LiquidCrystal_I2C.h>
#include <Ethernet.h>

// Configurações
#define DHTPIN 4
#define DHTTYPE DHT11
#define SD_CS 5
#define LED_VERDE 14
#define LED_VERMELHO 27
#define REED_PIN 15
#define W5500_CS 25


// Limites de segurança
const float TEMP_MIN = 18.0;
const float TEMP_MAX = 26.0;
const float UMID_MIN = 50.0;
const float UMID_MAX = 80.0;
const int PRESS_MIN = 10;
const int PRESS_MAX = 20;

// Objetos
LiquidCrystal_I2C lcd(0x27, 16, 2);
DHT dht(DHTPIN, DHTTYPE);
RTC_DS3231 rtc;
File arquivo;
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
EthernetClient client;

// Variáveis de controle
unsigned long lastSwitch = 0;
int telaAtual = 0;
unsigned long ultimaGravacao = 0;
const unsigned long intervaloGravacao = 10000;
unsigned long ultimaAtualizacaoPressao = 0;
int pressaoSimulada = 0;

// === Inicializações ===
void inicializarLCD() {
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Inicializando");
  delay(2000);
  lcd.clear();
}

void inicializarRTC() {
  if (!rtc.begin()) {
    lcd.print("ERRO RTC");
    while (1);
  }
  rtc.adjust(DateTime(F(__DATE__), F(__TIME__))); // Usar 1x se necessário
}

void inicializarSD() {
  if (!SD.begin(SD_CS)) {
    lcd.print("ERRO SD");
    while (1);
  }
  arquivo = SD.open("/log.csv", FILE_WRITE);
  if (arquivo && arquivo.size() == 0) {
    arquivo.println("Data,Hora,Temperatura,Umidade,Pressao");
    arquivo.close();
  }
}

void inicializarEthernet() {
  Ethernet.init(W5500_CS);

  if (Ethernet.begin(mac) == 0) {
    lcd.clear();
    lcd.print("FALHA ETHERNET");
    while (true);
  }
}

// === Funções de exibição ===
void exibirDataHora(DateTime now) {
  char data[17], hora[17];
  snprintf(data, sizeof(data), "%02d/%02d/%04d      ", now.day(), now.month(), now.year());
  snprintf(hora, sizeof(hora), "%02d:%02d:%02d      ", now.hour(), now.minute(), now.second());

  lcd.setCursor(0, 0); lcd.print(data);
  lcd.setCursor(0, 1); lcd.print(hora);
}

void exibirSensores(float t, float h, int pressao, bool portaFechada) {
  if (!isnan(t) && !isnan(h)) {
    char linha1[17], linha2[17];
    snprintf(linha1, sizeof(linha1), "T:%.1fC U:%d%%", t, (int)h);
    snprintf(linha2, sizeof(linha2), "P:%dPa %s     ", pressaoSimulada, portaFechada ? "OK" : "PORTA!");

    lcd.setCursor(0, 0); lcd.print(linha1);
    lcd.setCursor(0, 1); lcd.print(linha2);
  } else {
    lcd.setCursor(0, 0); lcd.print("Erro sensor DHT");
    lcd.setCursor(0, 1); lcd.print("                ");
  }
}

// === Função de gravação ===
void gravarDados(DateTime now, float t, float h, int pressao) {
  if (isnan(t) || isnan(h)) return;

  char linha[64];
  snprintf(linha, sizeof(linha), "%02d/%02d/%04d,%02d:%02d:%02d,%.1f,%d,%d",
           now.day(), now.month(), now.year(),
           now.hour(), now.minute(), now.second(),
           t, (int)h, pressao);

  arquivo = SD.open("/log.csv", FILE_WRITE);
  if (arquivo) {
    arquivo.println(linha);
    arquivo.close();
    Serial.println("Salvo:");
    Serial.println(linha);
  } else {
    Serial.println("Erro ao escrever no SD");
  }
}

//Funcao de alarme
void verificarLimites(float t, float h, int p) {
  bool dentroLimites = true;

  if (t < TEMP_MIN || t > TEMP_MAX) dentroLimites = false;
  if (h < UMID_MIN || h > UMID_MAX) dentroLimites = false;
  if (p < PRESS_MIN || p > PRESS_MAX) dentroLimites = false;

  if (dentroLimites) {
    digitalWrite(LED_VERDE, HIGH);
    digitalWrite(LED_VERMELHO, LOW);
  } else {
    digitalWrite(LED_VERDE, LOW);
    digitalWrite(LED_VERMELHO, HIGH);
  }
}

// === Setup e Loop Principal ===
void setup() {
  Wire.begin(21, 22);
  Serial.begin(115200);
  dht.begin();

  pinMode(LED_VERDE, OUTPUT);
  pinMode(LED_VERMELHO, OUTPUT);
  pinMode(REED_PIN, INPUT_PULLUP);

  inicializarLCD();
  inicializarRTC();
  // inicializarSD();
}

void loop() {
  unsigned long agora = millis();
  DateTime now = rtc.now();
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  bool portaFechada = digitalRead(REED_PIN) == LOW;

  // Atualiza pressão simulada a cada 8 segundos
  if (agora - ultimaAtualizacaoPressao > 8000) {
    pressaoSimulada = random(5, 31);
    ultimaAtualizacaoPressao = agora;
  }

  verificarLimites(t, h, pressaoSimulada);

  // Alterna entre telas a cada 5 segundos
  if (agora - lastSwitch > 5000) {
    lastSwitch = agora;
    telaAtual = 1 - telaAtual;
  }

  if (telaAtual == 0) exibirDataHora(now);
  else exibirSensores(t, h, pressaoSimulada, portaFechada);

  // Grava dados a cada 10 segundos
  if (agora - ultimaGravacao > intervaloGravacao) {
    ultimaGravacao = agora;
    // gravarDados(now, t, h, pressaoSimulada);
  }

  delay(300);
}
