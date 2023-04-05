#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include <stdio.h>
#include <ESP32Servo.h>
#include <LiquidCrystal.h>

LiquidCrystal lcd(2, 4, 16, 17, 14, 27);

#define trigPin 21
#define echoPin 19
#define pinP1 18
#define led1 23
#define led2 22

SemaphoreHandle_t semaphore;

Servo entryServo;  // create servo object to control a servo
Servo exitServo;   // create servo object to control a servo

const char* ssid = "WIFI_SSID";
const char* password = "WIFI_PASSWORD";

typedef struct ParkingSpot {
  bool status;
  String telegram_id;
  bool payment_status;
} ParkingSpot;

ParkingSpot parking[2] = {
  //{ true, "", false },
  { true, "", false },
  { true, "", false }
};

String lastIDin = "undefined", lastIDout = "undefined";  // last Telegram IDs in and out
const int pinSA = 35, pinSB = 32;                        // pin sensor IN A and B
const int pinSC = 33, pinSD = 25;                        // pin sensor IN C and D
bool entrySensorA = false, entrySensorB = false;
bool exitSensorC = false, exitSensorD = false;

#define BOTtoken "BOT_TOKEN_FROM_BOTFATHER"  //bot token get from botfather bot on telegram

WiFiClientSecure client;
UniversalTelegramBot bot(BOTtoken, client);

int botRequestDelay = 1000;
unsigned long lastTimeBotRan;

bool presenceP1 = false;
bool presenceP2 = false;

void handleNewMessages(int numNewMessages) {
  Serial.println("handleNewMessages");
  Serial.println(String(numNewMessages));

  for (int i = 0; i < numNewMessages; i++) {
    String chat_id = String(bot.messages[i].chat_id);
    String from_id = bot.messages[i].from_id;

    String text = bot.messages[i].text;
    Serial.println(text);

    String from_name = bot.messages[i].from_name;

    telegramFunctionalitiesHandle(text, chat_id, from_id, from_name);
  }
}

void telegramFunctionalitiesHandle(String text, String chat_id, String from_id, String from_name) {
  if (text == "/start") {
    String welcome = "Welcome, " + from_name + "!\n";
    welcome += "Use the following commands to control your outputs.\n\n";
    welcome += "/status to see the parking spots status\n";
    welcome += "/ticket to book a parking spot\n";
    welcome += "/pay to pay your booked parking spot\n";
    welcome += "/enter to open the entry gate (you must have already taken the ticket!)\n";
    welcome += "/exit to open the exit gate (you must have already paid the ticket!)\n";
    bot.sendMessage(chat_id, welcome, "");
  }
  if (text == "/status") {
    // print the status of all parking spots
    int available = 0;
    int busy = 0;
    String status = "Parking status: \n";
    for (int i = 0; i < (sizeof parking / sizeof parking[0]); i++) {
      if (parking[i].status) {
        available++;
      } else {
        busy++;
      }
    }
    if (available == 0) {
      status += "PARKING FULL!\n";
    } else {
      status += "Available spots: ";
      status += available;
      status += "\n";
      status += "Busy spots: ";
      status += busy;
      status += "\n";
    }
    bot.sendMessage(chat_id, status, "");
  }
  if (text == "/ticket") {
    if (availableSpot()) {
      if (!checkTicketAlreadyPresent(from_id)) {
        for (int i = 0; i < (sizeof parking / sizeof parking[0]); i++) {
          if (parking[i].status) {
            parking[i].telegram_id = from_id;
            bot.sendMessage(chat_id, "Ticket taken correctly!");
            break;
          }
        }
      } else {
        bot.sendMessage(chat_id, "You already have a ticket!");
      }
    } else {
      bot.sendMessage(chat_id, "Sorry, no available parking spot at moment! Try later.");
    }
  }
  if (text == "/pay") {
    if (checkTicketAlreadyPresent(from_id)) {
      for (int i = 0; i < (sizeof parking / sizeof parking[0]); i++) {
        if (parking[i].telegram_id == from_id) {
          parking[i].payment_status = true;
          bot.sendMessage(chat_id, "Payment successful! Once you reach the bar, use the /exit command");
          break;
        }
      }
    } else {
      bot.sendMessage(chat_id, "You have no ticket to pay!");
    }
  }
  if (text == "/enter") {
    bool canEnter = false;
    for (int i = 0; i < (sizeof parking / sizeof parking[0]); i++) {
      if (parking[i].telegram_id == from_id && parking[i].status == true) {
        lastIDin = from_id;
        canEnter = true;
        break;
      }
    }
    canEnter == true ? bot.sendMessage(chat_id, "Now you can enter! Welcome.")
                     : bot.sendMessage(chat_id, "Before enter, you have to take a ticket!");
  }
  if (text == "/exit") {
    bool canExit = false;
    for (int i = 0; i < (sizeof parking / sizeof parking[0]); i++) {
      if (parking[i].telegram_id == from_id && parking[i].payment_status == true) {
        lastIDout = from_id;
        canExit = true;
        break;
      }
    }
    canExit == true ? bot.sendMessage(chat_id, "Now you can exit! Goodbye.")
                    : bot.sendMessage(chat_id, "Before exit, you have to pay the ticket!");
  }

  if (text == "/print") {
    for (int i = 0; i < (sizeof parking / sizeof parking[0]); i++) {
      Serial.print("-------- P");
      Serial.print(i);
      Serial.println(" --------");
      Serial.println(parking[i].status);
      Serial.println(parking[i].telegram_id);
      Serial.println(parking[i].payment_status);
    }
  }
}

void presenceP2edit() {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  long duration = pulseIn(echoPin, HIGH);
  long distance = duration / 58;

  if (distance != 0) {
    if (distance < 5) presenceP2 = true;
    else presenceP2 = false;
  }
}

void presenceP1edit() {
  presenceP1 = !digitalRead(pinP1);
}

bool availableSpot() {
  for (int i = 0; i < (sizeof parking / sizeof parking[0]); i++) {
    if (parking[i].status) {
      return true;
    }
  }
  return false;
}

bool checkTicketAlreadyPresent(String telegramID) {
  for (int i = 0; i < (sizeof parking / sizeof parking[0]); i++) {
    if (parking[i].telegram_id == telegramID && parking[i].status == false) return true;
  }
  return false;
}

//-----------TASK Telegram Messages------------
void telegramMessages(void* parameter) {
  for (;;) {
    if (millis() > lastTimeBotRan + botRequestDelay) {
      int numNewMessages = bot.getUpdates(bot.last_message_received + 1);

      while (numNewMessages) {
        Serial.println("got response");
        if (xSemaphoreTake(semaphore, (TickType_t)5) == pdTRUE) {
          handleNewMessages(numNewMessages);
          xSemaphoreGive(semaphore);
        }
        numNewMessages = bot.getUpdates(bot.last_message_received + 1);
      }
      lastTimeBotRan = millis();
    }
    vTaskDelay(100);
  }
}

//-----------TASK Entry Parking------------
void entryGate(void* parameter) {
  for (;;) {
    if (xSemaphoreTake(semaphore, (TickType_t)5) == pdTRUE) {
      entrySensorA = !digitalRead(pinSA);
      entrySensorB = !digitalRead(pinSB);

      //valSA < 200 ? entrySensorA = true : entrySensorA = false;
      //valSB < 200 ? entrySensorB = true : entrySensorB = false;

      if (entrySensorA == true) {
        for (int i = 0; i < (sizeof parking / sizeof parking[0]); i++) {
          if (parking[i].telegram_id == lastIDin && parking[i].status == true) {
            // OPEN GATE
            Serial.print("OPEN GATE");
            entryServo.write(100);
            parking[i].status = false;
          }
        }
      }

      if (entrySensorB == true) {
        for (int i = 0; i < (sizeof parking / sizeof parking[0]); i++) {
          if (parking[i].telegram_id == lastIDin && parking[i].status == false) {
            // CLOSE GATE
            Serial.print("CLOSE GATE");
            entryServo.write(30);
            lastIDin = "undefined";
          }
        }
      }
      xSemaphoreGive(semaphore);
      vTaskDelay(100);
    }
  }
}

//-----------TASK Exit Parking------------
void exitGate(void* parameter) {
  for (;;) {
    if (xSemaphoreTake(semaphore, (TickType_t)5) == pdTRUE) {
      exitSensorC = !digitalRead(pinSC);
      exitSensorD = !digitalRead(pinSD);

      //valSC < 200 ? exitSensorC = true : exitSensorC = false;
      //valSD < 200 ? exitSensorD = true : exitSensorD = false;

      if (exitSensorC == true) {
        for (int i = 0; i < (sizeof parking / sizeof parking[0]); i++) {
          if (parking[i].telegram_id == lastIDout && parking[i].status == false) {
            // OPEN GATE
            Serial.print("OPEN GATE");
            exitServo.write(90);
            parking[i].status = true;
          }
        }
      }

      if (exitSensorD == true) {
        for (int i = 0; i < (sizeof parking / sizeof parking[0]); i++) {
          if (parking[i].telegram_id == lastIDout && parking[i].status == true) {
            // CLOSE GATE
            Serial.print("CLOSE GATE");
            exitServo.write(0);

            lastIDout = "undefined";

            //free parking spot
            parking[i].payment_status = false;
            parking[i].telegram_id = "";
          }
        }
      }
      xSemaphoreGive(semaphore);
      vTaskDelay(100);
    }
  }
}

//-----------TASK PARKING LED STATUS------------
void parkingStatusLED(void* parameter) {
  for (;;) {
    presenceP1edit();
    presenceP2edit();

    presenceP1 ? digitalWrite(led1, HIGH) : digitalWrite(led1, LOW);
    presenceP2 ? digitalWrite(led2, HIGH) : digitalWrite(led2, LOW);
    vTaskDelay(500);
  }
}

//-----------TASK PARKING LCD STATUS------------
void parkingStatusLCD(void* parameter) {
  for (;;) {
    int cont = 0;
    for (int i = 0; i < (sizeof parking / sizeof parking[0]); i++) {
      if (parking[i].status) {
        cont++;
      }
    }
    lcd.setCursor(0, 1);
    // print the number of free parking
    lcd.print(cont);
    vTaskDelay(1000);
  }
}

//-----------TASK Kepp WIFI Alive---------------
void keepWiFiAlive(void* parameter) {
  for (;;) {
    if (WiFi.status() == WL_CONNECTED) {
      vTaskDelay(10000);
      continue;
    } else {
      Serial.println("[WIFI] DISCONNECTED! Trying to reconnect...");
      WiFi.reconnect();

      delay(1000);

      if(WiFi.status() == WL_CONNECTED) Serial.println(WiFi.localIP());

      vTaskDelay(1000);
      continue;
    }
  }
}

void setup() {
  Serial.begin(115200);

  entryServo.attach(12);  // attaches the ENTRY servo on pin 12 to the servo object
  exitServo.attach(13);   // attaches the EXIT servo on pin 13 to the servo object

  // semaphore setup
  if (semaphore == NULL) {
    semaphore = xSemaphoreCreateMutex();
    if ((semaphore) != NULL)
      xSemaphoreGive((semaphore));
  }

  // parking spots status setup
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  pinMode(pinP1, INPUT);
  pinMode(led1, OUTPUT);
  pinMode(led2, OUTPUT);

  // parking gate setup
  pinMode(pinSA, INPUT);
  pinMode(pinSB, INPUT);
  pinMode(pinSC, INPUT);
  pinMode(pinSD, INPUT);

  lastIDin = "undefined";
  lastIDout = "undefined";

  // Connect to Wi-Fi WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  // Add root certificate for api.telegram.org
  client.setCACert(TELEGRAM_CERTIFICATE_ROOT);

  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi..");
  }
  // Print ESP32 Local IP Address
  Serial.println(WiFi.localIP());

  // Set contrast of LCD
  analogWrite(15, 75);
  // Set colunms and rows of LCD
  lcd.begin(16, 2);
  // Print a message on the display
  delay(1000);
  lcd.print("Available Parking:");
  delay(2000);

  xTaskCreate(parkingStatusLED, "ParkingStatusLEDTask", 1024, NULL, 1, NULL);
  xTaskCreate(parkingStatusLCD, "ParkingStatusLCDTask", 1024, NULL, 1, NULL);
  xTaskCreate(telegramMessages, "TelegramTask", 10000, NULL, 3, NULL);
  xTaskCreate(entryGate, "EntryGateTask", 10000, NULL, 2, NULL);
  xTaskCreate(exitGate, "ExitGateTask", 10000, NULL, 2, NULL);

  xTaskCreatePinnedToCore(keepWiFiAlive, "keepWiFiAlive", 5000, NULL, 1, NULL, ARDUINO_RUNNING_CORE);
}

void loop() {
}
