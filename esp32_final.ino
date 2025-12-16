/************ BLYNK DEFINE ************/
#define BLYNK_TEMPLATE_ID   "TMPL6vWozczrS"
#define BLYNK_TEMPLATE_NAME "PT1"
#define BLYNK_AUTH_TOKEN    "bZtaQ-BJ_gDg7F888cRh5uWUxGC_X3eg"
#define BLYNK_PRINT Serial

/************ LIBRARY ************/
#include <WiFi.h>
#include <WebServer.h>
#include <EEPROM.h>
#include <BlynkSimpleEsp32.h>
#include <WidgetTerminal.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>

/************ PIN ************/
#define LED_PIN   26
#define PIR_PIN   27
#define LDR_PIN   33

#define DHT_PIN   4
#define DHT_TYPE  DHT11
#define RAIN_PIN  25

#define MQ2_PIN   32
#define GAS_THRESHOLD 230

#define RX_PIN 16
#define TX_PIN 17

#define BTN_PIN 0
#define HOLD_TIME 5000

/************ OBJECT ************/
WebServer webServer(80);
HardwareSerial ArduinoSerial(1);
BlynkTimer timer;
WidgetTerminal terminal(V4);

LiquidCrystal_I2C lcd(0x27, 16, 2);
DHT dht(DHT_PIN, DHT_TYPE);

/************ WIFI ************/
String ssid_saved = "";
String pass_saved = "";
int wifiMode = 0;
unsigned long lastPress = 0;

/************ LED LOGIC ************/
bool manualLight = false;     // bật tay từ Blynk
bool lightOn = false;         // trạng thái LED hiện tại
bool lastLedState = false;    // trạng thái LED trước đó
unsigned long lightTime = 0;

/************ RFID DISPLAY ************/
bool rfidActive = false;
unsigned long rfidTime = 0;
#define RFID_DISPLAY_TIME 3000

/************ LCD PAGE ************/
unsigned long lcdTime = 0;
bool lcdPage = false;
#define LCD_INTERVAL 3000

/************ HTML ************/
const char html[] PROGMEM = R"html(
<!DOCTYPE html><html>
<body>
<h2>WiFi Config</h2>
<form action="/save">
SSID:<input name="ssid"><br>
PASS:<input name="pass"><br>
<input type="submit">
</form>
</body></html>
)html";

/************ BLYNK SWITCH V6 (MANUAL LED) ************/
BLYNK_WRITE(V6) {
  manualLight = param.asInt();

  if (manualLight) {
    digitalWrite(LED_PIN, HIGH);
    lightOn = true;
  } else {
    digitalWrite(LED_PIN, LOW);
    lightOn = false;
  }
}

/************ SEND LED STATUS TO TERMINAL ************/
void sendLedStatusToTerminal() {
  if (lightOn != lastLedState) {
    terminal.println(lightOn ? "LED1: ON" : "LED1: OFF");
    terminal.flush();
    lastLedState = lightOn;
  }
}

/************ SHOW RFID ************/
void showRFID(const char* l1, const char* l2="") {
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print(l1);
  lcd.setCursor(0,1);
  lcd.print(l2);
  rfidActive = true;
  rfidTime = millis();
}

/************ READ FROM UNO ************/
void readArduino() {
  while (ArduinoSerial.available()) {
    String msg = ArduinoSerial.readStringUntil('\n');
    msg.trim();
    if (!msg.length()) return;

    if (
      msg == "RFID:ACCESS_GRANTED" ||
      msg == "RFID:ACCESS_DENIED"  ||
      msg == "RFID:MASTER"         ||
      msg == "ADMIN:ADD_CARD"      ||
      msg == "ADMIN:REMOVE_CARD"   ||
      msg == "ADMIN:RESET_ALL"     ||
      //GAS 6.Nhận cảnh báo GAS từ UNO
      msg == "ALARM:GAS"
    ) {
      terminal.println(msg);
      terminal.flush();
    }

    if (msg == "RFID:WAIT") showRFID("Scan RFID...");
    else if (msg == "RFID:ACCESS_GRANTED") showRFID("Access Granted","Door Open");
    else if (msg == "RFID:ACCESS_DENIED") showRFID("Access Denied","Try Again");
    else if (msg == "RFID:MASTER") showRFID("Master Card","Admin Mode");
  }
}

/************ AUTO LIGHT (PIR + LDR) ************/
void handleAutoLight() {
  if (manualLight) return;

  bool pir = digitalRead(PIR_PIN);
  int ldr = analogRead(LDR_PIN);
  bool isDark = ldr < 2000;

  if (pir && isDark && !lightOn) {
    digitalWrite(LED_PIN, HIGH);
    lightOn = true;
    lightTime = millis();
  }

  if (lightOn && millis() - lightTime > 5000) {
    digitalWrite(LED_PIN, LOW);
    lightOn = false;
  }
}

/************ SENSOR + LCD ************/
void sendData() {
  if (rfidActive) return;

  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (isnan(t) || isnan(h)) return;

  bool rain = digitalRead(RAIN_PIN) == LOW;
  //GAS 1.Đọc cảm biến Gas
  int gasValue = analogRead(MQ2_PIN);
  //GAS 2. So sánh với ngưỡng
  bool gasAlarm = gasValue > GAS_THRESHOLD;

  handleAutoLight();

  if (millis() - lcdTime > LCD_INTERVAL) {
    lcdPage = !lcdPage;
    lcd.clear();
    lcdTime = millis();
  }

  if (!lcdPage) {
    lcd.setCursor(0,0);
    lcd.printf("Temp:%.1fC   ", t);
    lcd.setCursor(0,1);
    lcd.printf("Humi:%d%%    ", (int)h);
  } else {
    lcd.setCursor(0,0);
    //GAS 3.Hiển thị Gas lên LCD
    lcd.printf("Gas:%d %s ", gasValue, gasAlarm ? "AL" : "OK");
    lcd.setCursor(0,1);
    lcd.printf("Weather:%s ", rain ? "RAIN" : "DRY");
  }

  if (Blynk.connected()) {
    Blynk.virtualWrite(V1, t);
    Blynk.virtualWrite(V2, h);
    Blynk.virtualWrite(V3, rain ? "Rain" : "Dry");
    //GAS 3.Gửi dữ liệu gas lên Blynk
    Blynk.virtualWrite(V5, gasValue);
  }
  //GAS 4.Gửi cảnh báo gas sang Arduino UNO
  static bool gasSent = false;
  if (gasAlarm && !gasSent) {
    ArduinoSerial.println("GAS_ALARM_3S");
    gasSent = true;
  }
  //GAS 5. Khi Gas trở lại bình thường thì chỉnh về bình thưởng để có thể báo lại lần sau nếu GAS tăng tiếp
  if (!gasAlarm) gasSent = false;

  //  GỬI TRẠNG THÁI LED
  sendLedStatusToTerminal();
}

/************ RESET BUTTON ************/
void checkButton() {
  if (digitalRead(BTN_PIN) == LOW) {
    if (millis() - lastPress > HOLD_TIME) {
      EEPROM.begin(200);
      for (int i=0;i<200;i++) EEPROM.write(i,0);
      EEPROM.commit();
      ESP.restart();
    }
  } else lastPress = millis();
}

/************ WEB ************/
void setupWeb() {
  webServer.on("/", [](){ webServer.send(200,"text/html",html); });
  webServer.on("/save", [](){
    EEPROM.writeString(0, webServer.arg("ssid"));
    EEPROM.writeString(40, webServer.arg("pass"));
    EEPROM.commit();
    webServer.send(200,"text/plain","Saved! Restart");
  });
  webServer.begin();
}

/************ SETUP ************/
void setup() {
  Serial.begin(115200);
  ArduinoSerial.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);

  pinMode(LED_PIN, OUTPUT);
  pinMode(PIR_PIN, INPUT);
  pinMode(LDR_PIN, INPUT);
  pinMode(RAIN_PIN, INPUT_PULLUP);
  pinMode(BTN_PIN, INPUT_PULLUP);

  Wire.begin(21,22);
  lcd.init();
  lcd.backlight();

  dht.begin();
  EEPROM.begin(200);

  char ss[32], pw[64];
  EEPROM.readString(0, ss, sizeof(ss));
  EEPROM.readString(40, pw, sizeof(pw));
  ssid_saved = ss;
  pass_saved = pw;

  if (ssid_saved.length()) {
    WiFi.begin(ssid_saved.c_str(), pass_saved.c_str());
    Blynk.config(BLYNK_AUTH_TOKEN);
    Blynk.connect();
    wifiMode = 1;
  } else {
    WiFi.softAP("ESP32_CONFIG");
    setupWeb();
    wifiMode = 0;
  }

  timer.setInterval(2000, sendData);
  timer.setInterval(100, readArduino);
}

/************ LOOP ************/
void loop() {
  checkButton();
  if (wifiMode == 0) webServer.handleClient();
  if (wifiMode == 1) Blynk.run();
  timer.run();

  if (rfidActive && millis() - rfidTime > RFID_DISPLAY_TIME) {
    rfidActive = false;
    lcd.clear();
  }
}

