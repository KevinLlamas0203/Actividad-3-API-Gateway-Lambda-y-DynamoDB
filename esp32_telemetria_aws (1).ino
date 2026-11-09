#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <time.h>

// ---------- Configuración de red y AWS ----------
const char* WIFI_SSID     = "Starlink";
const char* WIFI_PASSWORD = "PON_AQUI_TU_CONTRASENA";

const char* API_URL = "https://ioqqrrgey1.execute-api.us-east-2.amazonaws.com/Etapa1/data";
const char* API_KEY = "";  // si activas "Clave de API obligatoria" en API Gateway, ponla aquí

const char* DEVICE_ID   = "AGT-ESP32-001";
const char* SHIPMENT_ID = "ENV-2026-001";

// El circuito no tiene GPS: estos valores son fijos
const float LATITUD       = 19.2365;
const float LONGITUD      = -104.7032;
const float VELOCIDAD_KMH = 0.0;

const unsigned long INTERVALO_ENVIO_MS = 10000;  // un envío cada 10 s

// ---------- Pantalla LCD ----------
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ---------- Sensor DHT11 ----------
#define DHTPIN 4
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// ---------- Sensor HC-SR04 ----------
#define TRIG_PIN 5
#define ECHO_PIN 18

unsigned long tiempoAnterior = 0;
unsigned long ultimoEnvio = 0;
bool mostrarDistancia = false;

// ---------- Funciones de red ----------
void conectarWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long inicio = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - inicio < 15000) {
    delay(250);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi conectado, IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("No se pudo conectar al WiFi");
  }
}

// Fecha y hora UTC en formato ISO 8601 (2026-11-03T18:30:00Z)
bool obtenerTimestamp(char* buffer, size_t tam) {
  struct tm t;
  if (!getLocalTime(&t, 2000)) return false;
  strftime(buffer, tam, "%Y-%m-%dT%H:%M:%SZ", &t);
  return true;
}

bool enviarDatos(float temp, float hum) {
  if (WiFi.status() != WL_CONNECTED) {
    conectarWiFi();
    if (WiFi.status() != WL_CONNECTED) return false;
  }

  char timestamp[25];
  if (!obtenerTimestamp(timestamp, sizeof(timestamp))) {
    Serial.println("Sin hora NTP todavía, no se envía");
    return false;
  }

  char json[256];
  snprintf(json, sizeof(json),
           "{\"device_id\":\"%s\",\"shipment_id\":\"%s\",\"timestamp\":\"%s\","
           "\"temperature_c\":%.1f,\"humidity_pct\":%.1f,"
           "\"latitude\":%.4f,\"longitude\":%.4f,\"speed_kmh\":%.1f}",
           DEVICE_ID, SHIPMENT_ID, timestamp,
           temp, hum, LATITUD, LONGITUD, VELOCIDAD_KMH);

  WiFiClientSecure client;
  client.setInsecure();  // no valida el certificado; ver nota de seguridad

  HTTPClient http;
  http.setTimeout(8000);
  if (!http.begin(client, API_URL)) {
    Serial.println("No se pudo iniciar la conexión HTTP");
    return false;
  }

  http.addHeader("Content-Type", "application/json");
  if (strlen(API_KEY) > 0) {
    http.addHeader("x-api-key", API_KEY);
  }

  int codigo = http.POST(json);
  String respuesta = http.getString();
  http.end();

  Serial.print("POST -> HTTP ");
  Serial.print(codigo);
  Serial.print(" | ");
  Serial.println(respuesta);

  return codigo >= 200 && codigo < 300;
}

void setup() {
  Serial.begin(115200);

  // LCD: SDA = D21, SCL = D22
  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();

  dht.begin();

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);

  lcd.setCursor(0, 0);
  lcd.print("Sistema iniciado");
  lcd.setCursor(0, 1);
  lcd.print("Conectando WiFi");

  conectarWiFi();

  // Hora UTC por NTP
  configTime(0, 0, "pool.ntp.org", "time.google.com");

  lcd.setCursor(0, 1);
  lcd.print(WiFi.status() == WL_CONNECTED ? "WiFi OK         " : "Sin WiFi        ");
  delay(2000);
  lcd.clear();
}

void loop() {
  float temp = dht.readTemperature();
  float humedad = dht.readHumidity();

  // Medir distancia con HC-SR04
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  unsigned long duracion = pulseIn(ECHO_PIN, HIGH, 30000);
  float distancia = duracion * 0.0343 / 2.0;

  // Alternar pantalla cada 2 segundos
  if (millis() - tiempoAnterior >= 2000) {
    tiempoAnterior = millis();
    mostrarDistancia = !mostrarDistancia;
    lcd.clear();
  }

  if (mostrarDistancia) {
    lcd.setCursor(0, 0);
    lcd.print("Distancia:");

    lcd.setCursor(0, 1);
    if (duracion == 0) {
      lcd.print("Sin eco        ");
    } else {
      lcd.print(distancia, 1);
      lcd.print(" cm           ");
    }
  } else {
    lcd.setCursor(0, 0);
    if (isnan(temp) || isnan(humedad)) {
      lcd.print("Error en DHT11 ");
      lcd.setCursor(0, 1);
      lcd.print("Revisar sensor ");
    } else {
      lcd.print("Temp:");
      lcd.print(temp, 1);
      lcd.print((char)223);
      lcd.print("C       ");

      lcd.setCursor(0, 1);
      lcd.print("Hum:");
      lcd.print(humedad, 0);
      lcd.print("%           ");
    }
  }

  // Envío a AWS cada INTERVALO_ENVIO_MS
  if (millis() - ultimoEnvio >= INTERVALO_ENVIO_MS) {
    ultimoEnvio = millis();
    if (isnan(temp) || isnan(humedad)) {
      Serial.println("DHT11 sin lectura, no se envía");
    } else {
      enviarDatos(temp, humedad);
    }
  }

  // Monitor serie
  Serial.print("Temperatura: ");
  Serial.print(temp);
  Serial.print(" C | Humedad: ");
  Serial.print(humedad);
  Serial.print(" % | Distancia: ");

  if (duracion == 0) {
    Serial.println("Sin eco");
  } else {
    Serial.print(distancia, 1);
    Serial.println(" cm");
  }

  delay(200);
}
