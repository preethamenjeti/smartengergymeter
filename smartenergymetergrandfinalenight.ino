#include <WiFi.h>
#include <HTTPClient.h>
#include <LiquidCrystal_I2C.h>

// ===== LCD Setup =====
LiquidCrystal_I2C lcd(0x27, 16, 2); // I2C address 0x27, 16 columns, 2 rows

// ===== WiFi Credentials =====
const char* ssid = "POCO X Pro 5G";
const char* password = "1234567890";

// Twilio Credentials
const char* TWILIO_ACCOUNT_SID = "acc sid";
const char* TWILIO_AUTH_TOKEN = "auth token";
const char* FROM_NUMBER = "given twilio number";
const char* TO_NUMBER   = "verified number";

// ===== Sensor Pins =====
#define VOLTAGE_PIN 35  // ZMPT101B
#define CURRENT_PIN 34  // ACS712
#define sampleCount 200

// ===== Calibration Variables =====
float voltageOffset = 0;
float currentOffset = 0;
float voltageScale = 0.23;         // Adjust based on calibration
float currentScale = 1.0 / 115.0;  // For ACS712-20A

// ===== Energy and Alert Logic =====
float energy = 0.0;
unsigned long overPowerStart = 0;
bool alertSent = false;

void setup() {
  Serial.begin(115200);
  analogReadResolution(12);
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Connecting...");

  // Connect to Wi-Fi
  WiFi.begin(ssid, password);
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 20) {
    delay(500);
    Serial.print(".");
    tries++;
  }

  lcd.clear();
  if (WiFi.status() == WL_CONNECTED) {
    lcd.setCursor(0, 0);
    lcd.print("WiFi Connected");
    Serial.println("\n✅ WiFi Connected");
  } else {
    lcd.setCursor(0, 0);
    lcd.print("WiFi Failed");
    Serial.println("\n❌ WiFi Failed");
  }

  // Auto-calibrate sensors
  voltageOffset = calibrateSensor(VOLTAGE_PIN, "Voltage");
  currentOffset = calibrateSensor(CURRENT_PIN, "Current");
}

void loop() {
  int rawAvg = 0;
  for (int j = 0; j < sampleCount; j++) {
    rawAvg += analogRead(VOLTAGE_PIN);
    delayMicroseconds(100);
  }
  rawAvg /= sampleCount;

  if (rawAvg < 1200 || rawAvg > 3000) {
    Serial.println("⚠️ No AC supply. Skipping...");
    delay(1000);
    return;
  }

  float voltageSum = 0, currentSum = 0;
  for (int j = 0; j < sampleCount; j++) {
    float v = analogRead(VOLTAGE_PIN) - voltageOffset;
    float i = analogRead(CURRENT_PIN) - currentOffset;
    voltageSum += v * v;
    currentSum += i * i;
    delayMicroseconds(150);
  }

  float Vrms = sqrt(voltageSum / sampleCount) * voltageScale;
  float Irms = sqrt(currentSum / sampleCount) * currentScale;
  float power = Vrms * Irms;

  if (Vrms < 20.0) {
    Vrms = Irms = power = 0.00;
  }

  if (power > 1.0) {
    energy += power / 3600000.0;
  }

  // ----------- Serial Print ----------
  Serial.println("======== Smart Energy Monitor ========");
  Serial.print("Voltage   : "); Serial.print(Vrms, 2); Serial.println(" V");
  Serial.print("Current   : "); Serial.print(Irms, 2); Serial.println(" A");
  Serial.print("Power     : "); Serial.print(power, 2); Serial.println(" W");
  Serial.print("Energy    : "); Serial.print(energy, 5); Serial.println(" kWh");
  Serial.println("======================================\n");

  // ----------- LCD Output ----------
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("V:"); lcd.print(Vrms, 0);
  lcd.print(" I:"); lcd.print(Irms, 2);

  lcd.setCursor(0, 1);
  lcd.print("P:"); lcd.print(power, 0);
  lcd.print("W E:"); lcd.print(energy, 2);

  // ----------- SMS Alert Logic ----------
  if (power > 100.0) {
    if (overPowerStart == 0) {
      overPowerStart = millis();
    } else if ((millis() - overPowerStart > 5000) && !alertSent) {
      sendSMS(power);
      alertSent = true;
    }
  } else {
    overPowerStart = 0;
    alertSent = false;
  }

  delay(1000);
}

// ----------- Sensor Calibration ----------
float calibrateSensor(int pin, const char* label) {
  long total = 0;
  for (int i = 0; i < 1000; i++) {
    total += analogRead(pin);
    delay(1);
  }
  float offset = total / 1000.0;
  Serial.print(label); Serial.print(" Offset = "); Serial.println(offset);
  return offset;
}

// ----------- Twilio SMS Sender ----------
void sendSMS(float power) {
  if ((WiFi.status() == WL_CONNECTED)) {
    HTTPClient http;
    String msgBody = "Smart Meter Alert: Power = " + String(power, 1) + "W";

    String url = "https://api.twilio.com/2010-04-01/Accounts/" + String(TWILIO_ACCOUNT_SID) + "/Messages.json";
    String postData = "To=" + String(TO_NUMBER) +
                      "&From=" + String(FROM_NUMBER) +
                      "&Body=" + msgBody;

    http.begin(url);
    http.setAuthorization(TWILIO_ACCOUNT_SID, TWILIO_AUTH_TOKEN);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");

    int httpResponseCode = http.POST(postData);
    if (httpResponseCode > 0) {
      Serial.println("✅ SMS Sent Successfully!");
    } else {
      Serial.print("❌ SMS Failed. Error code: ");
      Serial.println(httpResponseCode);
    }
    http.end();
  }
}
