/*
 * TO-DO: 
 *      implement web toggle for display ON/OFF - done (not nice, but done)
 *      check MQTT functionality (not sending?) 
 *      refactor for readability
 *      implement saving of initial vars to "EEPROM" - done
 *          -> initial counter value (kWh) - done
 *      implement load of initial vars to "EEPROM" - done
 *          -> initial counter value (kWh) - done
 *      refactor webpage (design)
 */

// Library for the ESP wdt functions
#include <ESP.h>;
// Library for the ESP8266 to get the WiFi functionality
#include <ESP8266WiFi.h>
// Library to represent a MQTT Broker
#include <PubSubClient.h>

#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Libraries for the ntp functionality
#include <NTPClient.h>
#include <WiFiUdp.h>

// Libraries for the webserver functionality
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>

// Library for value persistence
#include <EEPROM.h>

// WiFi Settings
const char* host = "HOSTNAME";
const char* update_path = "/firmware";
const char* update_username = "ADMIN_USER";
const char* update_password = "ADMIN_PASS";
const char* SSID = "SSID";
const char* PSK = "PSK";

// MQTT Settings
const char* MQTT_BROKER = "MQTT_BROKER_IP";

// ESP,MQTT,HTTP base functionality
WiFiClient espClient;
PubSubClient client(espClient);
ESP8266WebServer server(80);
ESP8266HTTPUpdateServer httpUpdater;

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#define IR_LED 12
#define STATE_LED 14
#define IR_TRANS A0

#define LOWER_LIMIT 100
#define UPPER_LIMIT 300

// display values
bool toggleDisplay = true; //set to false, when web toggle is implemented

// versioning
String version = "0.9.0";

// time values
const long utcOffsetInSeconds = 3600;
char daysOfTheWeek[7][12] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", utcOffsetInSeconds);
byte minuteLastReset;
byte hourLastReset;
byte dayLastReset;

// delay without blocking
unsigned long previousMillis = 0;
const long interval = 50;

// measurement vars
signed long initialCounterValue = 0;
signed long currentCounterValue = 0;
float energyTenMin = 0;
float energyHour = 0;
float energyDay = 0;
signed short repPerKWh = 1; //set to 1 to avoid division by zero trap..
signed long rep = 0;
int measurementValueOff = 0;
int measurementValueOn = 0;

// state machines
typedef enum { DAY_RESET, HOUR_RESET, MINUTE_RESET, TIME_IDLE} sm_t;
sm_t timeStateMachine = TIME_IDLE;

typedef enum { MEASURING, MEASURING_DONE, MEAS_IDLE} sm_M;
sm_M measStateMachine = MEAS_IDLE;

// web presence
#import "html/header.h"
#import "html/index.h"
#import "html/footer.h"

void handleRoot() {
  String head = HEADER_page;
  String main = MAIN_page;
  String foot = FOOTER_page;
  String beginTable = "";
  String endTable = "";
  // add split of timeDateItem
  String state1 = "<p>Aktueller Z&auml;hlerstand: " + String(currentCounterValue) + " kWh</p>";
  String state2 = "<p>Energieverbrauch aktuelle 10 Minuten: " + String(energyTenMin) + " kW</p>";
  String state3 = "<p>Energieverbrauch aktuelle Stunde: " + String(energyHour) + " kW</p>";
  String state4 = "<p>Energieverbrauch heute: " + String(energyDay) + " kWh</p>";
  String state7 = "<p>WiFi Status: " + String(WiFi.status()) + "</p>";
  String state6 = "<p>IP-Adresse: " + String(WiFi.localIP()[0]) + "." + String(WiFi.localIP()[1]) + "." + String(WiFi.localIP()[2]) + "." + String(WiFi.localIP()[3]) + "</p>";
  String state5 = "<p>Aktuelle Uhrzeit: " + timeClient.getFormattedTime() + "</p>";
  String state8 = "<p>Hostname: " + String(host) + "</p>";
  String state9 = "<p>Display ist an (0/1): " + String(toggleDisplay);
  String state10 = "<p>Softwareversion: " + version;
  server.send(200, "text/html", head + main + state9 + state1 + state2 + state3 + state4 + state5 + state6 + state7 + state8 + state10 + foot);
}

void handleInput() {
  initialCounterValue = server.arg("setInitialCounter").toInt();
  repPerKWh = server.arg("setRepPerKWh").toInt();
  //Serial.print("Initialer Zählerstand: ");
  //Serial.println(initialCounterValue);
  //Serial.print("Umdrehungen pro kWh: ");
  //Serial.println(repPerKWh);
  saveToEEPROMOnWebCommit();
  String s = "<a href='/'> Go Back </a>";
  server.send(200, "text/html", s);
}

void handleToggleDisplay(){
    toggleDisplay = !toggleDisplay;
    String s1 = "displayOnState: " + String(toggleDisplay);
    String s2 = "<a href='/'> Go Back </a>";
    server.send(200, "text/html", s1 + s2);
}

void handleNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";

  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }

  server.send(404, "text/plain", message);
}

void setup_wifi() {
  delay(10);
  display.clearDisplay();
  display.setCursor(0, 0);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(SSID);
  display.println("Connecting to: ");
  display.println(SSID);
  display.display();

  WiFi.begin(SSID, PSK);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    display.print(".");
    display.display();
  }
  display.println();

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Connected!");
  display.println("IP adress: ");
  display.println(WiFi.localIP());
  display.display();
}

void reconnect() {
  while (!client.connected()) {
    display.clearDisplay();
    display.setCursor(0, 0);
    Serial.print("Reconnecting...");
    display.println("Reconnecting to MQTT Broker...");
    display.display();
    if (!client.connect("ESP8266Client")) {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" retrying in 5 seconds");
      delay(5000);
    }
  }
}

void saveToEEPROMOnWebCommit() {
  EEPROM.begin(1024);
  EEPROM.put(0, repPerKWh);
  EEPROM.commit();
  EEPROM.end();
  Serial.println("Saved repPerKWh to EEPROM");
  Serial.print("repPerKWh: ");
  Serial.println(repPerKWh);
}

void saveToEEPROMOnChange() {
    EEPROM.begin(1024);
    EEPROM.put(0, repPerKWh);
    EEPROM.put(0+sizeof(repPerKWh), currentCounterValue);
    EEPROM.put(0+sizeof(repPerKWh)+sizeof(currentCounterValue), energyTenMin);
    EEPROM.put(0+sizeof(repPerKWh)+sizeof(currentCounterValue)+sizeof(energyTenMin), energyHour);
    EEPROM.put(0+sizeof(repPerKWh)+sizeof(currentCounterValue)+sizeof(energyTenMin)+sizeof(energyHour), energyDay);
    EEPROM.put(0+sizeof(repPerKWh)+sizeof(currentCounterValue)+sizeof(energyTenMin)+sizeof(energyHour)+sizeof(energyDay), rep);
    EEPROM.commit();
    EEPROM.end();
    Serial.println("Saved to EEPROM due to a value change");
}

void loadFromEEPROMOnBoot() {
    EEPROM.begin(1024);
    EEPROM.get(0, repPerKWh);
    EEPROM.get(0+sizeof(repPerKWh), currentCounterValue);
    EEPROM.get(0+sizeof(repPerKWh)+sizeof(currentCounterValue), energyTenMin);
    EEPROM.get(0+sizeof(repPerKWh)+sizeof(currentCounterValue)+sizeof(energyTenMin), energyHour);
    EEPROM.get(0+sizeof(repPerKWh)+sizeof(currentCounterValue)+sizeof(energyTenMin)+sizeof(energyHour), energyDay);
    EEPROM.get(0+sizeof(repPerKWh)+sizeof(currentCounterValue)+sizeof(energyTenMin)+sizeof(energyHour)+sizeof(energyDay), rep);
    EEPROM.end();
    Serial.println("Recovered values from EEPROM");
    Serial.print("repPerKWh: ");
    Serial.println(repPerKWh);
    Serial.print("currentCounterValue: ");
    Serial.println(currentCounterValue);
    Serial.print("energyTenMin: ");
    Serial.println(energyTenMin);
    Serial.print("energyHour: ");
    Serial.println(energyHour);
    Serial.print("energyDay: ");
    Serial.println(energyDay);
    Serial.print("rep: ");
    Serial.println(rep);
}

void handleReps(int analogValue) {
  if ( analogValue >= LOWER_LIMIT ) {
    rep += 1;
    currentCounterValue = rep / repPerKWh;
    energyTenMin += 1.0 / repPerKWh;
    energyHour += 1.0 / repPerKWh;
    energyDay += 1.0 / repPerKWh;
    client.publish("/ferraris/energyTenMin", String(energyTenMin).c_str());
    client.publish("/ferraris/energyHour", String(energyHour).c_str());
    client.publish("/ferraris/energyDay", String(energyDay).c_str());
    client.publish("/ferraris/counterValue", String(currentCounterValue).c_str());
    saveToEEPROMOnChange();
  }
}

int getMeasurementDiff(){
    // maybe implement RTOS so this might be a separate task without blocking the rest
    // for the 50ms of the delay().. actual: not neccessary
    digitalWrite(IR_LED, HIGH);
    delay(50);
    int measurementValueOn = analogRead(IR_TRANS);
    digitalWrite(IR_LED, LOW);
    delay(50);
    int measurementValueOff = analogRead(IR_TRANS);
    int measurementValueDiff = measurementValueOn - measurementValueOff;
    //turn below on for debug measurements when installing the repCounter
    //Serial.print("Messdifferenz: ");
    //Serial.println(measurementValueDiff);
    return measurementValueDiff;
}

void smMeasurement(int mVDiff) {
  switch (measStateMachine) {
    case MEAS_IDLE:
      if (mVDiff >= LOWER_LIMIT) {
        measStateMachine = MEASURING;
        break;
      } else {
        measStateMachine = MEAS_IDLE;
        break;
      }
      break;

    case MEASURING:
      handleReps(mVDiff);
      // turn LED on to signalize a rep
      digitalWrite(STATE_LED, HIGH);
      measStateMachine = MEASURING_DONE;
      break;

    case MEASURING_DONE:
      if (mVDiff >= LOWER_LIMIT) {
        // measurement condition is still true
        measStateMachine = MEASURING_DONE;
        break;
      } else if (mVDiff <= LOWER_LIMIT) {
        // turn LED off
        digitalWrite(STATE_LED, LOW);
        measStateMachine = MEAS_IDLE;
        break;
      }
  }
}

void smTimeBasedReset() {
  switch (timeStateMachine) {
    case TIME_IDLE:

      timeStateMachine = MINUTE_RESET;
      break;

    case MINUTE_RESET:
      if (minuteLastReset != floor(timeClient.getMinutes() / 10)) {
        energyTenMin = 0;
        //Serial.println("Resetted energyTenMin");
        client.publish("/ferraris/energyTenMin", String(energyTenMin).c_str());
        minuteLastReset = floor(timeClient.getMinutes() / 10);
      }
      timeStateMachine = HOUR_RESET;
      break;

    case HOUR_RESET:
      if (hourLastReset != timeClient.getHours()) {
        energyHour = 0;
        //Serial.println("Resetted energyHour");
        client.publish("/ferraris/energyHour", String(energyHour).c_str());
        hourLastReset = timeClient.getHours();
      }
      timeStateMachine = DAY_RESET;
      break;
    case DAY_RESET:
      if (dayLastReset != timeClient.getDay()) {
        energyDay = 0;
        //Serial.println("Resetted energyDay");
        client.publish("/ferraris/energyDay", String(energyDay).c_str());
        dayLastReset = timeClient.getDay();
      }
      timeStateMachine = TIME_IDLE;
      break;

    default:
      break;
  }
}

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3C for cheap 128x64 0.96" china displays
    Serial.println(F("Display allocation failed"));
    for (;;); // Don't proceed, loop forever
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.cp437(true);
  display.println("Booting....");
  display.display();
  setup_wifi();
  client.setServer(MQTT_BROKER, 1883);
  delay(2000);
  display.clearDisplay();
  timeClient.begin();
  pinMode(IR_LED, OUTPUT);
  pinMode(STATE_LED, OUTPUT);
  pinMode(IR_TRANS, INPUT);
  /*use mdns for host name resolution*/
  if (!MDNS.begin(host)) {
    Serial.println("Error setting up MDNS responder!");
    while (1) {
      delay(1000);
    }
  }
  // do this only once to inflate EEPROM sections..
  // saveToEEPROMOnChange();
  if (repPerKWh == 1) {
    loadFromEEPROMOnBoot();
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("Recovered repPerKWh");
    display.print("on boot: ");
    display.print(repPerKWh);
    display.println(" kWh");
    display.display();
    delay(1000);
  }
  httpUpdater.setup(&server, update_path, update_username, update_password);
  server.on("/", handleRoot);
  server.on("/get", handleInput);
  server.on("/toggleDisplay", handleToggleDisplay);
  server.onNotFound(handleNotFound);
  server.begin();

  MDNS.addService("http", "tcp", 80);
  timeClient.begin();
  timeClient.forceUpdate();
  hourLastReset = timeClient.getHours();
  minuteLastReset = floor(timeClient.getMinutes() / 10);
  dayLastReset = timeClient.getDay();
  
}

void loop() {
  timeClient.update();
  server.handleClient();
  MDNS.update();
  client.loop();
  smMeasurement(getMeasurementDiff());
  smTimeBasedReset();
  // debug purposes to find upper & lower limit when installed.
  //Serial.print(" Analogwert: ");
  //Serial.print(buffer);
  //Serial.print(" UPPER_LIMIT: ");
  //Serial.print(UPPER_LIMIT);
  //Serial.print(" LOWER_LIMIT: ");
  //Serial.println(LOWER_LIMIT);

  // webbased switch to toggle display
  // oleds will burndown fast...
  if (toggleDisplay) {
    display.clearDisplay();
    display.setCursor(0, 0);
    //display.print("Analogwert: ");
    //display.println(buffer);
    display.println("Zaehlerstand: ");
    display.print(currentCounterValue);
    display.println(" kWh");
    display.println("Verbrauch:");
    display.print("(10min) : ");
    display.print(energyTenMin);
    display.println(" kWh");
    display.print("(1h)    : ");
    display.print(energyHour);
    display.println(" kWh");
    display.print("(1d)    : ");
    display.print(energyDay);
    display.println(" kWh");
    display.display();
  } else {
    display.clearDisplay();
    display.display();
  }
}
