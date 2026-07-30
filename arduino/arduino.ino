#include <SPI.h>
#include <SoftwareSerial.h>
#include <Wire.h>
#include <DS3231.h>
#include <LiquidCrystal_I2C.h>
#include <TinyGPS++.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "DFRobot_PH.h"
#include <EEPROM.h>
#include <ModbusMaster.h>

// ================== PIN BUTTON MANUAL (ANOMALI) ==================
const int btnManual1 = 23;
const int btnManual2 = 25;
const int btnManual3 = 28;
const int btnManual4 = 38;
const int btnManual5 = 42;

// ================== MODE SISTEM ==================
enum Mode { MODE_DISPLAY, SETTING };
Mode currentMode = MODE_DISPLAY;

// ================== RTC & LCD ==================
DS3231 rtc;
LiquidCrystal_I2C lcd(0x27, 20, 4);  

// ================== INTERVAL WAKTU & SIKLUS ==================
unsigned long previousMillis = 0;
const long interval = 1000;

// Timer Utama Otomatis (Setiap 5 Menit = 300.000 ms)
unsigned long lastAutoTriggerMs = 0;
const unsigned long AUTO_INTERVAL_MS = 300000; 

// Status Siklus Milidetik
bool isCycleActive = false;
unsigned long cycleStartMs = 0;
bool sensorHasRead = false;
bool dataHasBeenSent = false;

// ================== Serial Ports ==================
SoftwareSerial mySerial(32, 30); // TX di pin 32 ke ESP32 (RXD 2)
SoftwareSerial rs485Serial(10, 9);

// ================== GPS ==================
static const uint32_t GPSBaud = 9600;
TinyGPSPlus gps;
bool gpsEverFixed = false;
double gpsLat = 0.0, gpsLng = 0.0;

// ================== RTC strings ==================
String jam, menit, detik, tanggal;

// ================== LCD slide control ==================
unsigned long prevLcdMillis = 0;
const unsigned long lcdMinInterval = 100;
bool lcdForce = false;
uint8_t currentSlide = 0; 
uint8_t prevSlide = 255;

// ================== Sensor pins ==================
#define TURB_PIN A0
#define TDS_PIN A1
#define PRESS_PIN A2
#define ONE_WIRE_PIN 2
#define PH_PIN A4
#define DO_PIN PRESS_PIN

OneWire ds(ONE_WIRE_PIN);
DallasTemperature ds18(&ds);

// ================== TSS/NTU calibration ==================
const float VREF_ADC = 4.94f;
float V_NTU0 = 3.80;
float V_NTU1000 = 3.00;
float V_NTU3000 = 2.60;

// ================== DO mg/L ==================
#define ADC_COUNTS 1023.0
#define VREF_MV 5000.0
const float DO_VZERO_mV = 35.0f;
const float DO_VAIR_mV = 1145.0f;
float g_do_mgL = NAN;

// ================== TDS calibrated ==================
const float VREF_TDS_MEAS = 4.914f;
const float TDS_FACTOR = 0.5f;
const float CAL_FACTOR = 1.1002f;
#define TDS_SCOUNT 30
int tdsBuf[TDS_SCOUNT], tdsBufTmp[TDS_SCOUNT];
int tdsIdx = 0;

// ================== RS485 (CWT-TH) ==================
ModbusMaster rs485;
float g_airTempC = NAN;
float g_airRH = NAN;
unsigned long lastTHReadMs = 0;
const unsigned long TH_READ_INTERVAL = 500;

// ================== Global sensor cache ==================
float g_tempC = NAN;
float g_tss_NTU = NAN;
float g_tdsPPM = NAN;
float g_pH = NAN;

// ================== PUMPS / RELAY ==================
#define POMPA_AIR_LAUT_PIN 39   // Relay 1
#define POMPA_AIR_BILAS_PIN 41  // Relay 2

DFRobot_PH ph;

// ================== Prototypes ==================
void updateLCD();
void bacaRTC();
void processGPS();
void readTH_RS485();
float readVoltageAvg(byte pin, int samples = 20);
float readDS18x20C();
int medianFilter(int *src, int n);
float DOsaturationAtTemp(float tC);
void triggerNewCycle();

void setup() {
  Serial.begin(9600);

  // Tombol Manual Anomali
  pinMode(btnManual1, INPUT_PULLUP);
  pinMode(btnManual2, INPUT_PULLUP);
  pinMode(btnManual3, INPUT_PULLUP);
  pinMode(btnManual4, INPUT_PULLUP);
  pinMode(btnManual5, INPUT_PULLUP);

  Wire.begin();
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Weather Station");
  lcd.setCursor(0, 1);
  lcd.print("Sistem Siap...");

  mySerial.begin(9600);
  Serial2.begin(GPSBaud); 

  rs485Serial.begin(4800);
  rs485.begin(1, rs485Serial);
  rs485Serial.listen();

  ds18.begin();
  ph.begin();

  pinMode(POMPA_AIR_LAUT_PIN, OUTPUT);
  pinMode(POMPA_AIR_BILAS_PIN, OUTPUT);

  // Kondisi Awal: Semua Relay Mati (HIGH)
  digitalWrite(POMPA_AIR_LAUT_PIN, HIGH);   
  digitalWrite(POMPA_AIR_BILAS_PIN, HIGH);  

  lastAutoTriggerMs = millis();

  delay(400);
  lcd.clear();
  
  // Langsung jalankan siklus pertama otomatis saat di-colok
  Serial.println(">>> SISTEM DINYALAKAN: Memulai Siklus Pertama Otomatis! <<<");
  triggerNewCycle();
}

void loop() {
  unsigned long currentMillis = millis();

  rs485Serial.listen();
  readTH_RS485();
  processGPS();

  // ================== 1. CEK TOMBOL MANUAL & TIMER OTOMATIS ==================
  if (!isCycleActive) {
    // Cek Tombol Manual (Pin 23, 25, 28, 38, 42)
    bool buttonPressed = (digitalRead(btnManual1) == LOW || digitalRead(btnManual2) == LOW || 
                          digitalRead(btnManual3) == LOW || digitalRead(btnManual4) == LOW || 
                          digitalRead(btnManual5) == LOW);

    if (buttonPressed) {
      delay(30); // Debounce ringan
      if (digitalRead(btnManual1) == LOW || digitalRead(btnManual2) == LOW || 
          digitalRead(btnManual3) == LOW || digitalRead(btnManual4) == LOW || 
          digitalRead(btnManual5) == LOW) {
        
        Serial.println("\n>>> TOMBOL MANUAL DITEKAN: Memulai Siklus! <<<");
        isCycleActive = true;
        cycleStartMs = currentMillis; // CATAT WAKTU MULAI DENGAN PRESISI
        sensorHasRead = false;
        dataHasBeenSent = false;
      }
    }
    // Cek Timer Otomatis 5 Menit
    else if (currentMillis - lastAutoTriggerMs >= AUTO_INTERVAL_MS) {
      lastAutoTriggerMs = currentMillis;
      Serial.println("\n>>> TIMER 5 MENIT: Memulai Siklus Otomatis! <<<");
      isCycleActive = true;
      cycleStartMs = currentMillis; // CATAT WAKTU MULAI DENGAN PRESISI
      sensorHasRead = false;
      dataHasBeenSent = false;
    }
  }

  // ================== 2. EKSEKUSI SIKLUS BERDASARKAN MILIDETIK ==================
  if (isCycleActive) {
    unsigned long elapsedMs = currentMillis - cycleStartMs;

    // --- TAHAP 1 (0 - 10 Detik): Pompa Laut (Relay 1) Nyala ---
    if (elapsedMs < 10000UL) {
      digitalWrite(POMPA_AIR_LAUT_PIN, LOW);   // Relay 1 Nyala
      digitalWrite(POMPA_AIR_BILAS_PIN, HIGH); // Relay 2 Mati

      // --- DETIK KE-5 (di rentang 5000 - 5500 ms): Semua Sensor Membaca ---
      if (elapsedMs >= 5000UL && elapsedMs < 5500UL && !sensorHasRead) {
        float tempC_new = readDS18x20C();
        float vTurb_new = readVoltageAvg(TURB_PIN);
        float tssNTU;
        if (vTurb_new >= V_NTU0) tssNTU = 0.0;
        else if (vTurb_new <= V_NTU3000) tssNTU = 3000.0;
        else if (vTurb_new > V_NTU1000) {
          tssNTU = ((V_NTU0 - vTurb_new) / (V_NTU0 - V_NTU1000)) * 1000.0;
        } else {
          tssNTU = 1000.0 + ((V_NTU1000 - vTurb_new) / (V_NTU1000 - V_NTU3000)) * 2000.0;
        }
        g_tss_NTU = tssNTU;

        int med = medianFilter(tdsBuf, TDS_SCOUNT);
        float vAvg = med * (VREF_TDS_MEAS / 1023.0f);
        float tempUsedC = (g_tempC <= -999) ? 25.0f : g_tempC;
        float comp = 1.0f + 0.02f * (tempUsedC - 25.0f);
        float vComp = vAvg / comp;
        float tdsRaw = (133.42f * vComp * vComp * vComp - 255.86f * vComp * vComp + 857.39f * vComp) * TDS_FACTOR;
        g_tdsPPM = tdsRaw * CAL_FACTOR;

        if (isnan(g_tempC) || fabs(g_tempC - tempC_new) > 0.2f) {
          g_tempC = tempC_new;
        }

        long doSum = 0;
        for (int i = 0; i < 20; i++) {
          doSum += analogRead(DO_PIN);
          delay(1);
        }
        uint16_t doRaw = doSum / 20;
        float do_mV = doRaw * (VREF_MV / ADC_COUNTS);
        float doSat = DOsaturationAtTemp(tempUsedC);
        float frac = (do_mV - DO_VZERO_mV) / (DO_VAIR_mV - DO_VZERO_mV);
        if (frac < 0) frac = 0;
        if (frac > 1.2f) frac = 1.2f;
        g_do_mgL = frac * doSat;

        sensorHasRead = true;
        Serial.println("[DETIK 5] Sensor Selesai Membaca!");
      }

      // Sampel TDS buffer berkala
      static unsigned long tdsSampleMs = millis();
      if (millis() - tdsSampleMs > 40) {
        tdsSampleMs = millis();
        tdsBuf[tdsIdx] = analogRead(TDS_PIN);
        tdsIdx = (tdsIdx + 1) % TDS_SCOUNT;
      }

    } 
    // --- TAHAP 2 (10 - 12 Detik): Pompa Laut Mati & Kirim Data ---
    else if (elapsedMs >= 10000UL && elapsedMs < 12000UL) {
      digitalWrite(POMPA_AIR_LAUT_PIN, HIGH); // Relay 1 Mati
      digitalWrite(POMPA_AIR_BILAS_PIN, HIGH);

      // Kirim Data di rentang detik ke-12
      if (!dataHasBeenSent) {
        bacaRTC();
        String latStr = gpsEverFixed ? String(gpsLat, 6) : "searching";
        String lngStr = gpsEverFixed ? String(gpsLng, 6) : "searching";

        String humSend     = (!isnan(g_airRH) && g_airRH > 0) ? String(g_airRH, 1) : "tidak mengukur";
        String airTempSend = (!isnan(g_airTempC) && g_airTempC > -50) ? String(g_airTempC, 1) : "tidak mengukur";
        String wtempSend   = (!isnan(g_tempC) && g_tempC > -900) ? String(g_tempC, 2) : "tidak mengukur";
        String tssSend     = String(g_tss_NTU, 1);
        String tdsSend     = (!isnan(g_tdsPPM)) ? String(g_tdsPPM, 0) : "tidak mengukur";
        String doSend      = (!isnan(g_do_mgL)) ? String(g_do_mgL, 2) : "tidak mengukur";

        String dataToSend = String("humidity: ") + humSend
                          + ", air_temperature: " + airTempSend
                          + ", water_temperature: " + wtempSend
                          + ", TSS: " + tssSend
                          + ", TDS: " + tdsSend
                          + ", DO: " + doSend
                          + ", pompa_laut: ON"
                          + ", pompa_bilas: OFF"
                          + ", latitude: " + latStr 
                          + ", longitude: " + lngStr
                          + ", rtc_time: " + jam + ":" + menit + ":" + detik
                          + ", rtc_date: " + tanggal;

        mySerial.println(dataToSend);
        Serial.println("[DETIK 12] Kirim Data ke ESP32: " + dataToSend);
        dataHasBeenSent = true;
      }
    }
    // --- TAHAP 3 (12 - 17 Detik): Jeda Tambahan 5 Detik ---
    else if (elapsedMs >= 12000UL && elapsedMs < 17000UL) {
      digitalWrite(POMPA_AIR_LAUT_PIN, HIGH);
      digitalWrite(POMPA_AIR_BILAS_PIN, HIGH);
    }
    // --- TAHAP 4 (17 - 27 Detik): Pompa Bilas (Relay 2) Nyala 10 Detik ---
    else if (elapsedMs >= 17000UL && elapsedMs < 27000UL) {
      digitalWrite(POMPA_AIR_LAUT_PIN, HIGH);
      digitalWrite(POMPA_AIR_BILAS_PIN, LOW); // Relay 2 Nyala
    }
    // --- SETELAH DETIK 27: Selesai ---
    else if (elapsedMs >= 27000UL) {
      digitalWrite(POMPA_AIR_LAUT_PIN, HIGH);
      digitalWrite(POMPA_AIR_BILAS_PIN, HIGH);
      isCycleActive = false;
      Serial.println("[DETIK 27] SIKLUS SELESAI (Kembali Siaga)\n");
    }
  }

  // ================== 3. UPDATE LCD & TOMBOL NAVIGASI ==================
  if (digitalRead(38) == LOW) { // btnUp
    delay(50);
    if (digitalRead(38) == LOW) {
      currentSlide = (currentSlide + 1) % 2;
      lcdForce = true;
    }
  } else if (digitalRead(42) == LOW) { // btnDown
    delay(50);
    if (digitalRead(42) == LOW) {
      currentSlide = (currentSlide - 1 + 2) % 2;
      lcdForce = true;
    }
  }

  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;
    bacaRTC();
    lcdForce = true;
  }

  if (lcdForce) {
    if (millis() - prevLcdMillis >= lcdMinInterval) {
      updateLCD();
      lcdForce = false;
    }
  }

  delay(20);
}

void triggerNewCycle() {
  // Hapus atau abaikan status aktif sebelumnya, langsung paksa mulai siklus baru dari nol
  isCycleActive = true;
  cycleStartMs = millis();
  sensorHasRead = false;
  dataHasBeenSent = false;
  
  // Langsung aktifkan Relay 1 di detik pertama
  digitalWrite(POMPA_AIR_LAUT_PIN, LOW);
  digitalWrite(POMPA_AIR_BILAS_PIN, HIGH);
}

void updateLCD() {
  prevLcdMillis = millis();
  if (currentSlide != prevSlide) {
    lcd.clear();
    prevSlide = currentSlide;
  }

  if (currentSlide == 0) {
    lcd.setCursor(0, 0);
    lcd.print("Sistem: ");
    lcd.print(isCycleActive ? "BERJALAN" : "IDLE    ");
    lcd.setCursor(0, 1);
    lcd.print("RTC: " + jam + ":" + menit + ":" + detik);
  } else if (currentSlide == 1) {
    lcd.setCursor(0, 0);
    lcd.print(gpsEverFixed ? "GPS: Fixed" : "GPS: Searching");
  }
}

void bacaRTC() {
  bool h12, pm;
  int jamInt = rtc.getHour(h12, pm);
  int menitInt = rtc.getMinute();
  int detikInt = rtc.getSecond();
  int tglInt = rtc.getDate();

  jam = (jamInt < 10) ? "0" + String(jamInt) : String(jamInt);
  menit = (menitInt < 10) ? "0" + String(menitInt) : String(menitInt);
  detik = (detikInt < 10) ? "0" + String(detikInt) : String(detikInt);
  tanggal = (tglInt < 10) ? "0" + String(tglInt) : String(tglInt);
}

void processGPS() {
  while (Serial2.available() > 0) {
    char c = (char)Serial2.read();
    gps.encode(c);
  }
  if (gps.location.isUpdated() && gps.location.isValid()) {
    gpsLat = gps.location.lat();
    gpsLng = gps.location.lng();
    gpsEverFixed = true;
  }
}

float readVoltageAvg(byte pin, int samples) {
  long sum = 0;
  for (int i = 0; i < samples; i++) {
    sum += analogRead(pin);
    delay(1);
  }
  return (sum / (float)samples) * VREF_ADC / 1023.0;
}

float readDS18x20C() {
  ds18.requestTemperatures();
  float t = ds18.getTempCByIndex(0);
  if (t > -40 && t < 85) return t;
  return -1000;
}

void readTH_RS485() {
  if (millis() - lastTHReadMs < TH_READ_INTERVAL) return;
  lastTHReadMs = millis();
  uint8_t res = rs485.readHoldingRegisters(0x0000, 2);
  if (res == rs485.ku8MBSuccess) {
    g_airRH = rs485.getResponseBuffer(0) / 10.0;
    g_airTempC = (int16_t)rs485.getResponseBuffer(1) / 10.0;
  }
}

int medianFilter(int *src, int n) {
  for (int i = 0; i < n; i++) tdsBufTmp[i] = src[i];
  for (int j = 0; j < n - 1; j++)
    for (int i = 0; i < n - 1 - j; i++)
      if (tdsBufTmp[i] > tdsBufTmp[i + 1]) {
        int t = tdsBufTmp[i];
        tdsBufTmp[i] = tdsBufTmp[i + 1];
        tdsBufTmp[i + 1] = t;
      }
  if (n & 1) return tdsBufTmp[(n - 1) / 2];
  return (tdsBufTmp[n / 2] + tdsBufTmp[n / 2 - 1]) / 2;
}

float DOsaturationAtTemp(float tC) {
  struct P { float t; float d; };
  const P tab[] = { { 0, 14.6 }, { 10, 11.0 }, { 20, 9.1 }, { 25, 8.26 }, { 30, 7.6 } };
  if (tC <= tab[0].t) return tab[0].d;
  if (tC >= tab[4].t) return tab[4].d;
  for (int i = 0; i < 4; i++) {
    if (tC <= tab[i + 1].t) {
      float t1 = tab[i].t, d1 = tab[i].d;
      float t2 = tab[i + 1].t, d2 = tab[i + 1].d;
      return d1 + (d2 - d1) * (tC - t1) / (t2 - t1);
    }
  }
  return 8.26f;
}