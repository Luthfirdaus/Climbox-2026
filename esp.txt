#include <WiFi.h>
#include <HTTPClient.h>

#define WIFI_SSID "Climbox2026"
#define WIFI_PASSWORD "menujupangandaran"

// GANTI DENGAN URL WEB APP GOOGLE APPS SCRIPT
const String GOOGLE_SCRIPT_URL = "PASTE_URL_DISINI";

// Serial port untuk terima data dari Arduino
// Ganti ke Serial2 jika pakai UART2 (RX=16, TX=17)
#define ARDUINO_SERIAL Serial

const long SERIAL_BAUD = 9600;

void connectWiFi()
{
    Serial.print("Menghubungkan WiFi");

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }

    Serial.println();
    Serial.println("WiFi Connected!");
    Serial.print("IP : ");
    Serial.println(WiFi.localIP());
}

String getValue(String data, String key)
{
    int idx = data.indexOf(key);
    if (idx < 0) return "";
    int start = idx + key.length();
    while (start < (int)data.length() && data.charAt(start) == ' ') start++;
    int end = start;
    while (end < (int)data.length() && data.charAt(end) != ',' && data.charAt(end) != '\n' && data.charAt(end) != '\r') end++;
    return data.substring(start, end);
}

void setup()
{
    Serial.begin(115200);
    ARDUINO_SERIAL.begin(SERIAL_BAUD);

    connectWiFi();
}

void loop()
{
    if (ARDUINO_SERIAL.available())
    {
        String data = ARDUINO_SERIAL.readStringUntil('\n');
        data.trim();
        if (data.length() == 0) return;

        Serial.print("Data diterima: ");
        Serial.println(data);

        String wind_direction_str = getValue(data, "wind_direction:");
        String wind_speed_str = getValue(data, "wind_speed:");
        String rainfall_str = getValue(data, "rainfall:");
        String tss_str = getValue(data, "TSS:");
        String tds_str = getValue(data, "TDS:");
        String water_temp_str = getValue(data, "water temp:");
        String ph_str = getValue(data, "pH:");
        String do_str = getValue(data, "DO:");
        String ec_str = getValue(data, "EC:");
        String pump_laut = getValue(data, "pompaAirLaut:");
        String pump_bilas = getValue(data, "pompaBilas:");
        String lat_str = getValue(data, "Latitude:");
        String lon_str = getValue(data, "Longitude:");
        String air_temp_str = getValue(data, "air temp:");
        String air_hum_str = getValue(data, "air humidity:");

        if (WiFi.status() == WL_CONNECTED)
        {

            HTTPClient http;

            String url = GOOGLE_SCRIPT_URL;
            url += "?device=alat_1";
            url += "&wind_direction_str=" + wind_direction_str;
            url += "&wind_speed_str=" + wind_speed_str;
            url += "&rainfall_str=" + rainfall_str;
            url += "&tss_str=" + tss_str;
            url += "&tds_str=" + tds_str;
            url += "&water_temp_str=" + water_temp_str;
            url += "&air_temp_str=" + air_temp_str;
            url += "&air_hum_str=" + air_hum_str;
            url += "&ph_str=" + ph_str;
            url += "&do_str=" + do_str;
            url += "&ec_str=" + ec_str;
            url += "&pump_laut=" + pump_laut;
            url += "&pump_bilas=" + pump_bilas;
            url += "&lat_str=" + lat_str;
            url += "&lon_str=" + lon_str;

            Serial.println("-----------------------------------");
            Serial.println("Mengirim Data...");
            Serial.println(url);

            http.begin(url);

            http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

            int httpCode = http.GET();

            Serial.print("HTTP Code : ");
            Serial.println(httpCode);

            if (httpCode > 0)
            {
                Serial.print("Response : ");
                Serial.println(http.getString());
            }
            else
            {
                Serial.println("Gagal mengirim data.");
            }

            http.end();
        }
        else
        {
            Serial.println("WiFi Disconnect");

            connectWiFi();
        }
    }

    delay(100);
}
