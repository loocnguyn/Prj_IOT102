#include <SPI.h>
#include <MFRC522.h>
#include <Servo.h>
#include <EEPROM.h>

/************ PIN ************/
#define SS_PIN     10
#define RST_PIN     9
#define SERVO_PIN   6
#define BUTTON_PIN  3
#define BUZZER_PIN  7

/************ CONFIG ************/
#define MAX_CARDS 5
#define DOOR_OPEN_TIME 2000   // ms

/************ OBJECT ************/
MFRC522 rfid(SS_PIN, RST_PIN);
Servo doorServo;

/************ SYSTEM ************/
byte masterUID[4] = {0x02, 0xED, 0xCC, 0x01};
bool adminMode = false;
bool sentWait = false;

/************ SERIAL STATUS ************/
void sendStatus(const char* msg) {
  Serial.println(msg);
}

/************ BUZZER ************/
void beep(int t) {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(t);
  digitalWrite(BUZZER_PIN, LOW);
}

/************ GAS ALARM (3s) ************/
void gasAlarm3s() {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(3000);
  digitalWrite(BUZZER_PIN, LOW);
}

/************ CARD COUNT ************/
int countCards() {
  int count = 0;
  for (int i = 0; i < EEPROM.length(); i += 4) {
    if (EEPROM.read(i) != 0xFF) count++;
  }
  return count;
}

/************ RFID FUNCTIONS ************/
bool compareUID(byte *a, byte *b) {
  for (byte i = 0; i < 4; i++)
    if (a[i] != b[i]) return false;
  return true;
}

bool isValidCard(byte *uid) {
  for (int i = 0; i < EEPROM.length(); i += 4) {
    bool match = true;
    for (byte j = 0; j < 4; j++) {
      if (EEPROM.read(i + j) != uid[j]) {
        match = false;
        break;
      }
    }
    if (match) return true;
  }
  return false;
}

bool saveCard(byte *uid) {
  if (countCards() >= MAX_CARDS) return false;

  for (int i = 0; i < EEPROM.length(); i += 4) {
    if (EEPROM.read(i) == 0xFF) {
      for (byte j = 0; j < 4; j++)
        EEPROM.write(i + j, uid[j]);
      return true;
    }
  }
  return false;
}

void removeCard(byte *uid) {
  for (int i = 0; i < EEPROM.length(); i += 4) {
    bool match = true;
    for (byte j = 0; j < 4; j++) {
      if (EEPROM.read(i + j) != uid[j]) {
        match = false;
        break;
      }
    }
    if (match) {
      for (byte j = 0; j < 4; j++)
        EEPROM.write(i + j, 0xFF);
      return;
    }
  }
}

void resetAll() {
  for (int i = 0; i < EEPROM.length(); i++)
    EEPROM.write(i, 0xFF);
}

/************ DOOR ************/
void openDoor() {
  doorServo.write(90);
  beep(80);
  delay(DOOR_OPEN_TIME);
  doorServo.write(0);
}

/************ SETUP ************/
void setup() {
  Serial.begin(9600);

  SPI.begin();
  rfid.PCD_Init();

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);

  doorServo.attach(SERVO_PIN);
  doorServo.write(0);

  sendStatus("SYSTEM:READY");
}

/************ LOOP ************/
void loop() {

  /* ===== COMMAND FROM ESP32 ===== */
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();

    if (cmd == "GAS_ALARM_3S") {
      sendStatus("ALARM:GAS");
      gasAlarm3s();
    }
  }

  /* ===== RFID WAIT ===== */
  if (!rfid.PICC_IsNewCardPresent()) {
    if (!sentWait) {
      sendStatus("RFID:WAIT");
      sentWait = true;
    }
    return;
  }

  if (!rfid.PICC_ReadCardSerial()) return;
  sentWait = false;

  byte *uid = rfid.uid.uidByte;

  /* ===== MASTER CARD ===== */
  if (compareUID(uid, masterUID)) {
    sendStatus("RFID:MASTER");
    adminMode = true;
    openDoor();
    sendStatus("ADMIN:ENTER");
  }

  /* ===== NORMAL CARD ===== */
  else {
    if (isValidCard(uid)) {
      sendStatus("RFID:ACCESS_GRANTED");
      openDoor();
    } else {
      sendStatus("RFID:ACCESS_DENIED");
      beep(600);
    }
  }

  /* ===== ADMIN MODE ===== */
  if (adminMode) {
    unsigned long start = millis();
    int count = 0;

    while (millis() - start < 5000) {
      if (digitalRead(BUTTON_PIN) == LOW) {
        count++;
        delay(400);
      }
    }

    if (count == 1) {
      if (countCards() < MAX_CARDS) {
        sendStatus("ADMIN:ADD_CARD");
        while (!rfid.PICC_IsNewCardPresent());
        rfid.PICC_ReadCardSerial();
        saveCard(rfid.uid.uidByte);
        beep(150);
      }
    }
    else if (count == 2) {
      sendStatus("ADMIN:REMOVE_CARD");
      while (!rfid.PICC_IsNewCardPresent());
      rfid.PICC_ReadCardSerial();
      removeCard(rfid.uid.uidByte);
      beep(300);
    }
    else if (count >= 3) {
      sendStatus("ADMIN:RESET_ALL");
      resetAll();
      beep(800);
    }

    adminMode = false;
  }

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}
//Heloo testing commit changes
