/*
 * ============================================================
 *  PROYECTO: Monitoreo de Calidad de Aire, Temperatura y Sonido
 *  HARDWARE: ESP32 + DHT22 + MQ135 + KY038 + OLED I2C
 *  PLATAFORMA: ThingSpeak
 * ============================================================
 *
 *  MAPA DE PINES (VERIFICADO Y FUNCIONANDO):
 *  D13 -> DHT22 (Temperatura y Humedad)
 *  GPIO33 -> KY038 (Sensor de Sonido - salida analógica AO)
 *  GPIO34 -> MQ135 (Calidad del Aire)
 *  D32 -> SDA   (Pantalla OLED I2C)
 *  D25 -> SCL   (Pantalla OLED I2C)
 *
 *  ALIMENTACIÓN:
 *  OLED VCC    -> 3.3V del ESP32 (DIRECTO, no por protoboard)
 *  DHT22/MQ135/KY038 -> 5V protoboard (OK)
 *
 *  LIBRERIAS NECESARIAS:
 *  - DHT sensor library by Adafruit
 *  - Adafruit Unified Sensor
 *  - Adafruit SSD1306
 *  - Adafruit GFX Library
 *  - ThingSpeak by MathWorks
 *
 *  CANALES THINGSPEAK:
 *  Field 1 -> Temperatura (°C)
 *  Field 2 -> Humedad (%)
 *  Field 3 -> Calidad Aire MQ135
 *  Field 4 -> Nivel de Sonido KY038
 * ============================================================
 */

// ── Librerías ──────────────────────────────────────────────
#include <WiFi.h>
#include <Wire.h>
#include <DHT.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "ThingSpeak.h"

// ── Configuración WiFi ─────────────────────────────────────
const char* WIFI_SSID     = "Yuzo";           // <-- Cambia por tu red WiFi
const char* WIFI_PASSWORD = "aeiou987";        // <-- Cambia por tu contraseña

// ── Configuración ThingSpeak ───────────────────────────────
const unsigned long CHANNEL_ID  = 3411838;        // <-- Cambia por Channel ID
const char* WRITE_API_KEY       = "8WCTI81QQWJ9HMFC"; // <-- Cambia por Write API Key
const int   THINGSPEAK_DELAY_MS = 20000;          // Mínimo 15s entre envíos (free tier)

// ── Pines (VERIFICADOS) ────────────────────────────────────
#define PIN_DHT22   13
#define PIN_KY038   33
#define PIN_MQ135   34
#define PIN_SDA     32
#define PIN_SCL     25

// ── DHT22 ──────────────────────────────────────────────────
#define DHT_TYPE DHT22
DHT dht(PIN_DHT22, DHT_TYPE);

// ── OLED 128x64 ────────────────────────────────────────────
#define OLED_WIDTH   128
#define OLED_HEIGHT   64
#define OLED_ADDRESS 0x3C
Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);

// ── Variables globales ─────────────────────────────────────
WiFiClient wifiClient;

float temperatura  = 0.0;
float humedad      = 0.0;
int   valorMQ135   = 0;
int   valorKY038   = 0;
bool  wifiConectado = false;
float nivelSonidoSuavizado = 0.0;
bool  sonidoInicializado = false;

unsigned long ultimoEnvio      = 0;
unsigned long ultimaLectura    = 0;
unsigned long ultimoCambioPag  = 0;

const int LECTURA_DELAY_MS    = 2000;
const int CAMBIO_PAGINA_MS    = 4000;
const int VENTANA_SONIDO_MS   = 100;
const float FACTOR_SUAVIZADO_SONIDO = 0.30;

int paginaActual   = 0;
const int TOTAL_PAGINAS = 3;

// ──────────────────────────────────────────────────────────
//  SETUP
// ──────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n========================================");
  Serial.println("  Monitoreo Calidad de Aire - ESP32");
  Serial.println("========================================");

  // I2C con pines verificados
  Wire.begin(PIN_SDA, PIN_SCL);

  // OLED
  inicializarOLED();

  // Sensores
  dht.begin();
  pinMode(PIN_MQ135, INPUT);
  pinMode(PIN_KY038, INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_MQ135, ADC_11db);
  analogSetPinAttenuation(PIN_KY038, ADC_11db);
  Serial.println("[OK] Sensores inicializados");

  // WiFi + ThingSpeak
  conectarWiFi();
  ThingSpeak.begin(wifiClient);
  Serial.println("[OK] ThingSpeak inicializado");

  // Pantalla bienvenida
  mostrarBienvenida();
  delay(2500);
}

// ──────────────────────────────────────────────────────────
//  LOOP
// ──────────────────────────────────────────────────────────
void loop() {
  unsigned long ahora = millis();

  // Leer sensores cada 2 segundos
  if (ahora - ultimaLectura >= LECTURA_DELAY_MS) {
    ultimaLectura = ahora;
    leerSensores();
    imprimirSerial();
  }

  // Rotar páginas OLED cada 4 segundos
  if (ahora - ultimoCambioPag >= CAMBIO_PAGINA_MS) {
    ultimoCambioPag = ahora;
    paginaActual = (paginaActual + 1) % TOTAL_PAGINAS;
  }
  actualizarOLED();

  // Enviar a ThingSpeak cada 20 segundos
  if (ahora - ultimoEnvio >= THINGSPEAK_DELAY_MS) {
    ultimoEnvio = ahora;
    verificarWiFi();
    enviarThingSpeak();
  }

  delay(100);
}

// ──────────────────────────────────────────────────────────
//  SENSORES
// ──────────────────────────────────────────────────────────
int leerNivelSonidoPicoAPico() {
  unsigned long inicio = millis();
  int valorMinimo = 4095;
  int valorMaximo = 0;

  while (millis() - inicio < VENTANA_SONIDO_MS) {
    int lectura = analogRead(PIN_KY038);

    if (lectura < valorMinimo) {
      valorMinimo = lectura;
    }

    if (lectura > valorMaximo) {
      valorMaximo = lectura;
    }
  }

  return valorMaximo - valorMinimo;
}

void actualizarNivelSonido() {
  int nivelActual = leerNivelSonidoPicoAPico();

  if (!sonidoInicializado) {
    nivelSonidoSuavizado = nivelActual;
    sonidoInicializado = true;
  } else {
    nivelSonidoSuavizado =
      ((1.0 - FACTOR_SUAVIZADO_SONIDO) * nivelSonidoSuavizado) +
      (FACTOR_SUAVIZADO_SONIDO * nivelActual);
  }

  valorKY038 = (int)(nivelSonidoSuavizado + 0.5);
}

void leerSensores() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (!isnan(t) && !isnan(h)) {
    temperatura = t;
    humedad     = h;
  } else {
    Serial.println("[WARN] DHT22 sin lectura, reintentando...");
  }

  valorMQ135 = analogRead(PIN_MQ135);

  // KY038: nivel acustico relativo por amplitud pico a pico.
  actualizarNivelSonido();
}

// ──────────────────────────────────────────────────────────
//  OLED
// ──────────────────────────────────────────────────────────
void inicializarOLED() {
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println("[ERROR] OLED no encontrada - verifica VCC=3.3V, SDA=D32, SCL=D25");
    return;
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  Serial.println("[OK] OLED inicializada");
}

void mostrarBienvenida() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(5, 0);
  display.println("AIR QUALITY MONITOR");
  display.drawLine(0, 10, 128, 10, SSD1306_WHITE);
  display.setCursor(0, 16); display.println(" DHT22  Temp/Humedad");
  display.setCursor(0, 27); display.println(" MQ135  Calidad Aire");
  display.setCursor(0, 38); display.println(" KY038  Sonido");
  display.drawLine(0, 52, 128, 52, SSD1306_WHITE);
  display.setCursor(15, 55); display.println(">> ThingSpeak <<");
  display.display();
}

void actualizarOLED() {
  display.clearDisplay();
  switch (paginaActual) {
    case 0: paginaTemperatura(); break;
    case 1: paginaAireSonido();  break;
    case 2: paginaEstado();      break;
  }
  display.display();
}

// Página 1: Temperatura y Humedad
void paginaTemperatura() {
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("- TEMP & HUMEDAD -");
  display.drawLine(0, 10, 128, 10, SSD1306_WHITE);

  display.setTextSize(2);
  display.setCursor(0, 14);
  display.print(temperatura, 1);
  display.setTextSize(1);
  display.setCursor(90, 14); display.println("C");
  display.setCursor(90, 24); display.println("Temp");

  display.setTextSize(2);
  display.setCursor(0, 40);
  display.print(humedad, 1);
  display.setTextSize(1);
  display.setCursor(90, 40); display.println("%");
  display.setCursor(90, 50); display.println("Hum");
}

// Página 2: Aire y Sonido
void paginaAireSonido() {
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("- AIRE & SONIDO -");
  display.drawLine(0, 10, 128, 10, SSD1306_WHITE);

  display.setCursor(0, 13); display.println("MQ135 (Calidad Aire):");
  display.setTextSize(2);
  display.setCursor(10, 22); display.print(valorMQ135);
  display.setTextSize(1);
  display.setCursor(0, 40); display.print(clasificarAire(valorMQ135));

  display.drawLine(0, 50, 128, 50, SSD1306_WHITE);
  display.setCursor(0, 53);
  display.print("Son: "); display.print(valorKY038);
  display.print(" "); display.print(clasificarSonido(valorKY038));
}

// Página 3: Estado del sistema
void paginaEstado() {
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("- ESTADO SISTEMA -");
  display.drawLine(0, 10, 128, 10, SSD1306_WHITE);

  display.setCursor(0, 13);
  display.print("WiFi: ");
  display.println(wifiConectado ? "CONECTADO" : "SIN CONEXION");

  display.setCursor(0, 24);
  display.print("IP: ");
  display.println(wifiConectado ? WiFi.localIP().toString() : "---");

  display.setCursor(0, 35);
  display.print("SSID: ");
  display.println(WIFI_SSID);

  display.setCursor(0, 46);
  display.print("ThingSpeak: ");
  long seg = (THINGSPEAK_DELAY_MS - (millis() - ultimoEnvio)) / 1000;
  if (seg > 0 && seg < 999) {
    display.print(seg); display.print("s");
  } else {
    display.print("Enviando...");
  }

  display.setCursor(0, 56);
  display.print("Uptime: "); display.print(millis()/1000); display.print("s");
}

// ──────────────────────────────────────────────────────────
//  CLASIFICACIONES
// ──────────────────────────────────────────────────────────
String clasificarAire(int v) {
  if (v < 800)       return "LIMPIO";
  else if (v < 1500) return "ACEPTABLE";
  else if (v < 2500) return "MODERADO";
  else if (v < 3500) return "MALO";
  else               return "MUY MALO";
}

String clasificarSonido(int v) {
  if (v < 30)        return "BAJO";
  else if (v < 100)  return "MODERADO";
  else if (v < 250)  return "ALTO";
  else               return "MUY ALTO";
}

// ──────────────────────────────────────────────────────────
//  WIFI
// ──────────────────────────────────────────────────────────
void conectarWiFi() {
  Serial.print("[WiFi] Conectando a: ");
  Serial.println(WIFI_SSID);

  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 10); display.println("Conectando WiFi...");
  display.setCursor(0, 25); display.println(WIFI_SSID);
  display.display();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int intentos = 0;
  while (WiFi.status() != WL_CONNECTED && intentos < 30) {
    delay(500);
    Serial.print(".");
    intentos++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifiConectado = true;
    Serial.println("\n[OK] WiFi conectado! IP: " + WiFi.localIP().toString());
    display.clearDisplay();
    display.setCursor(0, 10); display.println("[OK] WiFi Conectado!");
    display.setCursor(0, 25); display.println(WiFi.localIP().toString());
    display.display();
    delay(1500);
  } else {
    wifiConectado = false;
    Serial.println("\n[WARN] Sin WiFi - modo offline activo");
    display.clearDisplay();
    display.setCursor(0, 10); display.println("[!] Sin WiFi");
    display.setCursor(0, 25); display.println("Modo offline activo");
    display.display();
    delay(1500);
  }
}

void verificarWiFi() {
  if (WiFi.status() != WL_CONNECTED) {
    wifiConectado = false;
    Serial.println("[WiFi] Reconectando...");
    WiFi.disconnect();
    delay(500);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    int intentos = 0;
    while (WiFi.status() != WL_CONNECTED && intentos < 15) {
      delay(500);
      intentos++;
    }
    wifiConectado = (WiFi.status() == WL_CONNECTED);
    if (wifiConectado) Serial.println("[OK] WiFi reconectado");
  } else {
    wifiConectado = true;
  }
}

// ──────────────────────────────────────────────────────────
//  THINGSPEAK
// ──────────────────────────────────────────────────────────
void enviarThingSpeak() {
  if (!wifiConectado) {
    Serial.println("[ThingSpeak] Sin WiFi - envio omitido");
    return;
  }

  ThingSpeak.setField(1, temperatura);
  ThingSpeak.setField(2, humedad);
  ThingSpeak.setField(3, valorMQ135);
  ThingSpeak.setField(4, valorKY038);

  int code = ThingSpeak.writeFields(CHANNEL_ID, WRITE_API_KEY);

  if (code == 200) {
    Serial.println("[OK] ThingSpeak - datos enviados correctamente");
  } else {
    Serial.print("[ERROR] ThingSpeak codigo: ");
    Serial.println(code);
  }
}

// ──────────────────────────────────────────────────────────
//  SERIAL
// ──────────────────────────────────────────────────────────
void imprimirSerial() {
  Serial.println("----------------------------------------");
  Serial.printf("Temperatura : %.1f C\n", temperatura);
  Serial.printf("Humedad     : %.1f %%\n", humedad);
  Serial.printf("MQ135       : %d  [%s]\n", valorMQ135, clasificarAire(valorMQ135).c_str());
  Serial.printf("KY038 p-p   : %d  [%s]\n", valorKY038, clasificarSonido(valorKY038).c_str());
  Serial.println("----------------------------------------");
}
