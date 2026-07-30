#include <WiFi.h>
#include <HTTPClient.h>

// ================== KONFIGURASI WIFI & GOOGLE SHEETS ==================
const char* ssid = "Climbox2026";
const char* password = "menujupangandaran";

// Ganti dengan URL Web App Google Apps Script Anda
const char* serverUrl = "https://script.google.com/macros/s/AKfycbyfPLkQT73NwgRaQm7FYQL3zbQ84DaxArihJmIf6898VHK1dLKCGSuCgo18smDIU_SDDw/exec";

// ================== VARIABEL PENAMPUNG DATA ==================
String humidity = "0";
String air_temperature = "0";
String water_temperature = "0";
String TSS = "0";
String TDS = "0";
String DO = "0";
String pH_val = "0";
String EC_val = "0";
String pompa_laut = "OFF";
String pompa_bilas = "OFF";
String latitude = "0";
String longitude = "0";
String rtc_time = "00:00:00";
String rtc_date = "01";

void setup() {
  // Serial Monitor untuk debug
  Serial.begin(115200);

  // Inisialisasi Serial2 pada ESP32 menggunakan RXD2 (GPIO 16) dan TXD4 (GPIO 17)
  // RXD2 dihubungkan ke TX Arduino (Pin 32)
  Serial2.begin(9600, SERIAL_8N1, 2, 4);

  // Koneksi WiFi
  WiFi.begin(ssid, password);
  Serial.print("Menghubungkan ke WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Terhubung!");
  Serial.print("Alamat IP: ");
  Serial.println(WiFi.localIP());
}

void loop() {
  // Cek apakah ada data masuk dari Arduino
  while (Serial2.available()) {
    String incomingData = Serial2.readStringUntil('\n');
    incomingData.trim(); // Bersihkan spasi dan newline

    // Hanyaproses jika baris tersebut mengandung teks "humidity:"
    if (incomingData.indexOf("humidity:") != -1) {
      Serial.println("\n--- Data Valid Diterima dari Arduino ---");
      Serial.println(incomingData);

      // Parsing data string
      parseData(incomingData);

      // Kirim data ke Google Sheets
      sendToGoogleSheets();
      
      break; // Keluar dari while setelah data valid diproses
    } else if (incomingData.length() > 0) {
      // Cetak jika ada data teks lain tapi bukan data sensor
      Serial.println("Pesan lain dari Arduino: " + incomingData);
    }
  }
}

// Fungsi untuk memecah string data dari Arduino berdasarkan key-value
// Di dalam fungsi parseData(String data)
void parseData(String data) {
  humidity = getValue(data, "humidity: ", ",");
  air_temperature = getValue(data, "air_temperature: ", ",");
  water_temperature = getValue(data, "water_temperature: ", ",");
  TSS = getValue(data, "TSS: ", ",");
  TDS = getValue(data, "TDS: ", ",");
  DO = getValue(data, "DO: ", ",");
  pH_val = getValue(data, "pH: ", ",");     // <--- Tambahan parsing pH
  EC_val = getValue(data, "EC: ", ",");     // <--- Tambahan parsing EC
  pompa_laut = getValue(data, "pompa_laut: ", ",");
  pompa_bilas = getValue(data, "pompa_bilas: ", ",");
  latitude = getValue(data, "latitude: ", ",");
  longitude = getValue(data, "longitude: ", ",");
}

// Helper untuk mengambil nilai di antara kunci dan pemisah
String getValue(String data, String key, String terminator) {
  int startIndex = data.indexOf(key);
  if (startIndex == -1) return "0";
  
  startIndex += key.length();
  int endIndex;
  
  if (terminator == "") {
    endIndex = data.length();
  } else {
    endIndex = data.indexOf(terminator, startIndex);
    if (endIndex == -1) endIndex = data.length();
  }
  
  String val = data.substring(startIndex, endIndex);
  val.trim();
  return val;
}

// Fungsi untuk mengirim data ke Google Sheets
void sendToGoogleSheets() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;

    String url = String(serverUrl) + 
                 "?device=alat_1" +
                 "&arah_angin=" + urlEncode("-") +
                 "&kecepatan_angin=" + urlEncode("-") +
                 "&curah_hujan=" + urlEncode("-") +
                 "&tss=" + urlEncode(TSS) +
                 "&tds=" + urlEncode(TDS) +
                 "&suhu_air=" + urlEncode(water_temperature) +
                 "&suhu_udara=" + urlEncode(air_temperature) +
                 "&hum=" + urlEncode(humidity) +
                 "&pH=" + urlEncode(pH_val) +      // <--- Masukkan pH_val di sini
                 "&do=" + urlEncode(DO) +
                 "&ec=" + urlEncode(EC_val) +      // <--- Masukkan EC_val di sini
                 "&pompa_laut=" + urlEncode(pompa_laut) +
                 "&pompa_bilas=" + urlEncode(pompa_bilas) +
                 "&lat=" + urlEncode(latitude) +
                 "&lng=" + urlEncode(longitude);

    Serial.println("Mengirim URL: " + url);

    http.begin(url);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    
    int httpResponseCode = http.GET();

    if (httpResponseCode > 0) {
      Serial.print("HTTP Response code: ");
      Serial.println(httpResponseCode);
      String payload = http.getString();
      Serial.println("Response: " + payload);
    } else {
      Serial.print("Error code: ");
      Serial.println(httpResponseCode);
    }
    http.end();
  } else {
    Serial.println("WiFi Terputus!");
    WiFi.reconnect();
  }
}

// Fungsi pembantu untuk mengonversi teks agar aman dibaca URL
String urlEncode(String str) {
  String encodedString = "";
  char c;
  char code0;
  char code1;
  for (int i = 0; i < str.length(); i++) {
    c = str.charAt(i);
    if (isalnum(c)) {
      encodedString += c;
    } else {
      code1 = (c & 0xf) + '0';
      if ((c & 0xf) > 9) {
        code1 = (c & 0xf) - 10 + 'A';
      }
      c = (c >> 4) & 0xf;
      code0 = c + '0';
      if (c > 9) {
        code0 = c - 10 + 'A';
      }
      encodedString += '%';
      encodedString += code0;
      encodedString += code1;
    }
  }
  return encodedString;
}