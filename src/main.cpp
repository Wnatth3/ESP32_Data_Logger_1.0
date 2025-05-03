/*
Log:

*/

#include <Arduino.h>
#include <LittleFS.h>
#include <FS.h>           //this needs to be first, or it all crashes and burns.../
#include <WiFiManager.h>  //https://github.com/tzapu/WiFiManager
#include <ArduinoJson.h>  //https://github.com/bblanchon/ArduinoJson
// WiFiManager
// PubSubClient
#include <PubSubClient.h>
// OTA
#include <ESPmDNS.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFiClient.h>
// NTP
#include <NTPClient.h>
#include <WiFiUdp.h>
//
#include <RTClib.h>
#include <SoftwareSerial.h>
#include <Wire.h>
#include <ezLED.h>
// Sensors
#include "bsec.h"                  // SD0 Pkn connect to GND // BME680 - BSEC Software library
#include <PMserial.h>              // PMSA003 - PMSerial - https://github.com/avaldebe/PMserial
#include "Adafruit_VEML7700.h"     // VEML7700 - Adafruit VEML7700
#include "MHZ19.h"                 // MH-Z19B - MH-Z19 - https://github.com/WifWaf/MH-Z19
#include <SensirionI2cScd4x.h>     // SCD4x - Sensirion I2C SCD4x
#include <SensirionI2CSgp41.h>     // SGP41 - Sensirion I2C SGP41
#include <SensirionI2cSht4x.h>     // SHT40 - Sensirion I2C SHT4x
#include <VOCGasIndexAlgorithm.h>  // SGP41 - Sensirian Gas Index Algorithm
#include <NOxGasIndexAlgorithm.h>  // SGP41 - Sensirian Gas Index Algorithm
#include <Adafruit_AHTX0.h>        // AHT21 - Adafruit AHTX0
#include <ScioSense_ENS160.h>      // ENS160 - ENS160 Adafruit Fork - https://github.com/adafruit/ENS160_driver
#include <DHT.h>                   // DHT22 - DHT Sensor library
#include <TickTwo.h>
#include <Button2.h>

//******************************** Configulation ****************************//
#define _DEBUG_  // Uncomment this line if you want to debug
// #define _20SecTest  // Uncomment this line if you want 20sec Sensors Test

#define FORMAT_LITTLEFS_IF_FAILED true

// -- Read Sensores every 5, 10, or 15 minutes -- //
#define _5Min  // Uncomment this line if you want to read sensors every 5 minutes
// #define _10Min  // Uncomment this line if you want to read sensors every 10 minutes
// #define _15Min  // Uncomment this line if you want to read sensors every 15 minutes
//******************************** Global Variables *************************//
#define deviceName "WeatherSt"

const char* filename = "/config.txt";  // Config file name

bool mqttParameter;

//----------------- esLED ---------------------//
#define led LED_BUILTIN
ezLED statusLed(led);

//----------------- Reset WiFi Button ---------//
// #define resetWifiPin 0
// EasyButton resetWifiBt(resetWifiPin);
#define resetWifiBtPin 0
Button2 resetWifiBt;

//----------------- WiFi Manager --------------//

// default custom static IP
char static_ip[16]  = "192.168.0.191";
char static_gw[16]  = "192.168.0.1";
char static_sn[16]  = "255.255.255.0";
char static_dns[16] = "1.1.1.1";
// MQTT
char mqttBroker[16] = "192.168.0.10";
char mqttPort[6]    = "1883";
char mqttUser[16];  // = "admin";
char mqttPass[16];  // = "admin";

bool shouldSaveConfig = false;  // flag for saving data

WiFiManager wifiManager;

WiFiManagerParameter customMqttBroker("broker", "mqtt server", mqttBroker, 16);
WiFiManagerParameter customMqttPort("port", "mqtt port", mqttPort, 6);
WiFiManagerParameter customMqttUser("user", "mqtt user", mqttUser, 10);
WiFiManagerParameter customMqttPass("pass", "mqtt pass", mqttPass, 10);

//----------------- OTA Web Update ------------//
/* Style */
String style =
    "<style>#file-input,input{width:100%;height:44px;border-radius:999px;margin:10px auto;font-size:15px}"
    "input{background:#202020;border: 1px solid #777;padding:0 "
    "15px;text-align:center;color:white}body{background:#202020;font-family:sans-serif;font-size:14px;color:white}"
    "#file-input{background:#202020;padding:0;border:1px solid #ddd;line-height:44px;text-align:center;display:block;cursor:pointer}"
    "#bar,#prgbar{background-color:#f1f1f1;border-radius:10px}#bar{background-color:#4C4CEA;width:0%;height:10px}"
    "form{background:#181818;max-width:258px;margin:75px auto;padding:30px;border-radius:5px;box-shadow: 0px 0px 20px 5px "
    "#777;text-align:center}"
    ".btn{background:#4C4CEA;border-radius:999px;color:white;cursor:pointer}"
    ".btn-reset{background:#e74c3c;color:#fff;width:200px;height:30px;}"
    ".center-reset{display:flex;flex-direction:column;align-items:center;margin-top:20px;}</style>";

/* Login page */
String loginIndex =
    "<form name=loginForm>"
    "<h1>ESP32 Login</h1>"
    "<input name=userid placeholder='Username'> "
    "<input name=pwd placeholder=Password type=Password> "
    "<input type=submit onclick=check(this.form) class=btn value=Login></form>"
    "<script>"
    "function check(form) {"
    "if(form.userid.value=='admin' && form.pwd.value=='admin')"
    "{window.open('/ud')}"
    "else"
    "{alert('Error Password or Username')}"
    "}"
    "</script>" +
    style;

/* Server Index Page */
String ud =
    "<script src='https://ajax.googleapis.com/ajax/libs/jquery/3.2.1/jquery.min.js'></script>"
    "<form method='POST' action='#' enctype='multipart/form-data' id='upload_form'>"
    "<input type='file' name='update' id='file' onchange='sub(this)' style=display:none>"
    "<label id='file-input' for='file'>   Choose file...</label>"
    "<input type='submit' class=btn value='Update'>"
    "<br><br>"
    "<div id='prg'></div>"
    "<br><div id='prgbar'><div id='bar'></div></div><br></form>"
    "<div class='center-reset'>"
    "<button class='btn btn-reset' onclick='resetESP()'>Restart Device</button>"
    "</div>"
    "<script>"
    "function sub(obj){"
    "var fileName = obj.value.split('\\\\');"
    "document.getElementById('file-input').innerHTML = '   '+ fileName[fileName.length-1];"
    "};"
    "$('form').submit(function(e){"
    "e.preventDefault();"
    "var form = $('#upload_form')[0];"
    "var data = new FormData(form);"
    "$.ajax({"
    "url: '/update',"
    "type: 'POST',"
    "data: data,"
    "contentType: false,"
    "processData:false,"
    "xhr: function() {"
    "var xhr = new window.XMLHttpRequest();"
    "xhr.upload.addEventListener('progress', function(evt) {"
    "if (evt.lengthComputable) {"
    "var per = evt.loaded / evt.total;"
    "$('#prg').html('progress: ' + Math.round(per*100) + '%');"
    "$('#bar').css('width',Math.round(per*100) + '%');"
    "}"
    "}, false);"
    "return xhr;"
    "},"
    "success:function(d, s) {"
    "console.log('success!') "
    "},"
    "error: function (a, b, c) {"
    "}"
    "});"
    "});"
    "function resetESP(){"
    "$.post('/reset');"
    "}"
    "</script>" +
    style;

WebServer server(80);

//*---------------- PubSubClient -------------//
#define MQTT_PUB_JSON "esp32/sensors/json"

WiFiClient   mqttClient;
PubSubClient mqtt(mqttClient);

//----------------- Time Setup ----------------//
// Sync Time with NTP Server
WiFiUDP ntpUDP;
// NTPClient timeClient(ntpUDP, "NTP Server Name", offset time(ms), update Interval(ms));
NTPClient timeClient(ntpUDP, "time.google.com", 25200 /*GMT +7*/);
// NTPClient timeClient(ntpUDP, "time.facebook.com", 25200 /*GMT +7*/);
// NTPClient timeClient(ntpUDP, "time.apple.com", 25200 /*GMT +7*/);

//----------------- DS3231  Real Time Clock --//
#define SQW_PIN 33  // pin 33 for external Wake Up (ext1)
RTC_DS3231 rtc;

uint8_t tMin;
uint8_t setMin;
uint8_t preheatTime;
bool    rtcTrigger = false;

//----------------- Sensors -------------------//
// BME680 Temperature, Humidity, & Pressure Sensor
float pressBme680;
float gasResBme680;
float tempBme680;
float humiBme680;

bsec_virtual_sensor_t sensorList[13] = {
    BSEC_OUTPUT_IAQ,
    BSEC_OUTPUT_STATIC_IAQ,
    BSEC_OUTPUT_CO2_EQUIVALENT,
    BSEC_OUTPUT_BREATH_VOC_EQUIVALENT,
    BSEC_OUTPUT_RAW_TEMPERATURE,
    BSEC_OUTPUT_RAW_PRESSURE,
    BSEC_OUTPUT_RAW_HUMIDITY,
    BSEC_OUTPUT_RAW_GAS,
    BSEC_OUTPUT_STABILIZATION_STATUS,
    BSEC_OUTPUT_RUN_IN_STATUS,
    BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE,
    BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY,
    BSEC_OUTPUT_GAS_PERCENTAGE};

Bsec iaqSensor;

// VEML7700 Illumination Sensor
float lux;

Adafruit_VEML7700 veml = Adafruit_VEML7700();

// MH-Z19B CO2 Sensor
#define rxPin2 16
#define txPin2 17
int   co2;
MHZ19 myMHZ19;  // Constructor for library

HardwareSerial mySerial(2);  // On ESP32 we do not require the SoftwareSerial library, since we have 2 USARTS available

// PMSA003A
#define pmsRX 18
#define pmsTX 19
uint16_t pm010, pm025, pm100;
SerialPM pms(PMSx003, pmsRX, pmsTX);  // SoftwareSerial

// SCD41 CO2 Sensor
#ifdef NO_ERROR
#undef NO_ERROR
#endif
#define NO_ERROR 0

static int16_t scd41Error;
static char    scd41ErrorMessage[64];

float    tempScd41;
float    humiScd41;
uint16_t co2Scd41;

SensirionI2cScd4x scd41;

// SHT40 Temperature & Humidity Sensor
float tempSht40;  // degreeC
float humiSht40;  // %RH

SensirionI2cSht4x sht40;

// SGP41 Air Quality Sensor, VOC & NOx Index
uint16_t conditioning_s = 10;
uint16_t sgp41Error;
char     sgp41ErrorMessage[256];

int32_t vocIdxSgp41;
int32_t noxIdxSgp41;

SensirionI2CSgp41 sgp41;

VOCGasIndexAlgorithm voc_algorithm;
NOxGasIndexAlgorithm nox_algorithm;

// AHT21
float tempAht21;
float humiAht21;

Adafruit_AHTX0 aht21;

// ENS160
uint8_t  aqiEns160;
uint8_t  tvocEns160;
uint16_t eco2Ens160;

ScioSense_ENS160 ens160(ENS160_I2CADDR_1);  // 0x53

// DHT22
#define DHTPIN  32
#define DHTTYPE DHT22

float tempDht22;
float humiDht22;

DHT dht(DHTPIN, DHTTYPE);

//******************************** Tasks ************************************//
void    sgp41HeatingOn();
void    sgp41HeatingOff();
TickTwo tSgp41HeatingOn(sgp41HeatingOn, 500, 0, MILLIS);  // (function, interval, iteration, interval unit)
TickTwo tSgp41HeatingOff(sgp41HeatingOff, 0, 0, MILLIS);  // (function, interval, iteration, interval unit)

void    wifiDisconnectedDetect();
TickTwo tWifiDisconnectedDetect(wifiDisconnectedDetect, 300000, 0, MILLIS);  // Every 5 minutes

void    connectMqtt();
void    reconnectMqtt();
TickTwo tConnectMqtt(connectMqtt, 0, 0, MILLIS);  // (function, interval, iteration, interval unit)
TickTwo tReconnectMqtt(reconnectMqtt, 3000, 0, MILLIS);

//******************************** Functions ********************************//
//----------------- WiFi Manager --------------//
// Loads the configuration from a file
void loadConfiguration(fs::FS& fs, const char* filename) {
    // Open file for reading
    File file = fs.open(filename, "r");
    if (!file) {
#ifdef _DEBUG_
        Serial.println(F("Failed to open data file"));
#endif
        return;
    }

    // Allocate a temporary JsonDocument
    JsonDocument doc;
    // Deserialize the JSON document
    DeserializationError error = deserializeJson(doc, file);
    if (error) {
#ifdef _DEBUG_
        Serial.println(F("Failed to read file, using default configuration"));
#endif
    }
    // Copy values from the JsonDocument to the Config
    // strlcpy(Destination_Variable, doc["Source_Variable"] /*| "Default_Value"*/, sizeof(Destination_Name));
    strlcpy(mqttBroker, doc["mqttBroker"], sizeof(mqttBroker));
    strlcpy(mqttPort, doc["mqttPort"], sizeof(mqttPort));
    strlcpy(mqttUser, doc["mqttUser"], sizeof(mqttUser));
    strlcpy(mqttPass, doc["mqttPass"], sizeof(mqttPass));
    mqttParameter = doc["mqttParameter"];

    if (doc["ip"]) {
        strlcpy(static_ip, doc["ip"], sizeof(static_ip));
        strlcpy(static_gw, doc["gateway"], sizeof(static_gw));
        strlcpy(static_sn, doc["subnet"], sizeof(static_sn));
        strlcpy(static_dns, doc["dns"], sizeof(static_dns));
    } else {
#ifdef _DEBUG_
        Serial.println(F("No custom IP in config file"));
#endif
    }

    file.close();
}

void mqttInit() {
#ifdef _DEBUG_
    Serial.print(F("MQTT parameters are "));
#endif
    if (mqttParameter) {
#ifdef _DEBUG_
        Serial.println(F(" available"));
#endif
        mqtt.setBufferSize(1024);  // Max buffer size = 1024 bytes (default: 256 bytes)
        mqtt.setServer(mqttBroker, atoi(mqttPort));
        tConnectMqtt.start();
    } else {
#ifdef _DEBUG_
        Serial.println(F(" not available."));
#endif
    }
}

void saveConfigCallback() {
    Serial.println("Should save config");
    shouldSaveConfig = true;
}

void printFile(fs::FS& fs, const char* filename) {
    // Open file for reading
    File file = fs.open(filename, "r");
    if (!file) {
#ifdef _DEBUG_
        Serial.println(F("Failed to open data file"));
#endif
        return;
    }

    // Extract each characters by one by one
    while (file.available()) {
#ifdef _DEBUG_
        Serial.print((char)file.read());
#endif
    }
    Serial.println();

    file.close();  // Close the file
}

void deleteFile(fs::FS& fs, const char* path) {
#ifdef _DEBUG_
    Serial.print(F("Deleting file: "));
    Serial.println(String(path) + "\r\n");
#endif
    if (fs.remove(path)) {
#ifdef _DEBUG_
        Serial.println(F("- file deleted"));
#endif
    } else {
#ifdef _DEBUG_
        Serial.println(F("- delete failed"));
#endif
    }
}

void wifiManagerSetup() {
#ifdef _DEBUG_
    Serial.println(F("Loading configuration..."));
#endif
    loadConfiguration(LittleFS, filename);

    // set static ip
    IPAddress _ip, _gw, _sn, _dns;
    _ip.fromString(static_ip);
    _gw.fromString(static_gw);
    _sn.fromString(static_sn);
    _dns.fromString(static_dns);
    wifiManager.setSTAStaticIPConfig(_ip, _gw, _sn, _dns);

    // add all your parameters here
    wifiManager.addParameter(&customMqttBroker);
    wifiManager.addParameter(&customMqttPort);
    wifiManager.addParameter(&customMqttUser);
    wifiManager.addParameter(&customMqttPass);

    wifiManager.setDarkMode(true);
    wifiManager.setConfigPortalTimeout(60);
    // wifiManager.setConfigPortalBlocking(false);
#ifdef _DEBUG_
    Serial.println(F("Saving configuration..."));
#endif
    wifiManager.setSaveParamsCallback(saveConfigCallback);
#ifdef _DEBUG_
    Serial.println(F("Print config file..."));
#endif
    printFile(LittleFS, filename);

#ifndef _DEBUG_
    wifiManager.setDebugOutput(false);
#endif

    if (wifiManager.autoConnect(deviceName, "password")) {
#ifdef _DEBUG_
        Serial.println(F("WiFI is connected :D"));
#endif

    } else {
#ifdef _DEBUG_
        Serial.println(F("Configportal running"));
#endif
    }

    if (shouldSaveConfig) {
        Serial.println(F("at saveParamsCallback()"));
        // saveConfiguration(LittleFS, filename);
        strcpy(mqttBroker, customMqttBroker.getValue());
        strcpy(mqttPort, customMqttPort.getValue());
        strcpy(mqttUser, customMqttUser.getValue());
        strcpy(mqttPass, customMqttPass.getValue());

#ifdef _DEBUG_
        Serial.println(F("The values are updated."));
#endif

        // Delete existing file, otherwise the configuration is appended to the file
        // LittleFS.remove(filename);
        File file = LittleFS.open(filename, "w");
        if (!file) {
#ifdef _DEBUG_
            Serial.println(F("Failed to open config file for writing"));
#endif
            return;
        }

        // Allocate a temporary JsonDocument
        JsonDocument doc;
        // Set the values in the document
        doc["mqttBroker"] = mqttBroker;
        doc["mqttPort"]   = mqttPort;
        doc["mqttUser"]   = mqttUser;
        doc["mqttPass"]   = mqttPass;

        doc["ip"]      = WiFi.localIP().toString();
        doc["gateway"] = WiFi.gatewayIP().toString();
        doc["subnet"]  = WiFi.subnetMask().toString();
        doc["dns"]     = WiFi.dnsIP().toString();

        if (doc["mqttBroker"] != "") {
            doc["mqttParameter"] = true;
            mqttParameter        = doc["mqttParameter"];
        }

        // Serialize JSON to file
        if (serializeJson(doc, file) == 0) {
#ifdef _DEBUG_
            Serial.println(F("Failed to write to file"));
#endif
        } else {
#ifdef _DEBUG_
            Serial.print(F("The configuration has been saved to "));
            Serial.println(filename);
#endif
        }

        file.close();  // Close the file

        // mqttInit();
    }
}

void resetWifiBtPressed(Button2& btn) {
    statusLed.turnON();
#ifdef _DEBUG_
    Serial.println(F("Deleting the config file and resetting WiFi."));
#endif
    deleteFile(LittleFS, filename);
    wifiManager.resetSettings();
    Serial.print(deviceName);
#ifdef _DEBUG_
    Serial.println(F(" is restarting."));
#endif
    ESP.restart();
    delay(3000);
}

//----------------- OTA Web Updater -----------//
void otaWebUpdateSetup() {
    // const char* host = "ESP32";
    /*use mdns for host name resolution*/
    if (!MDNS.begin(deviceName)) {  // http://deviceName.local
#ifdef _DEBUG_
        Serial.println(F("Error setting up MDNS responder!"));
#endif
        while (1) {
            delay(500);  // Default is 1000
        }
    }
#ifdef _DEBUG_
    Serial.println(F("mDNS responder started"));
#endif

    /*return index page which is stored in ud */
    server.on("/", HTTP_GET, []() {
        server.sendHeader("Connection", "close");
        server.send(200, "text/html", loginIndex);
    });
    server.on("/ud", HTTP_GET, []() {
        server.sendHeader("Connection", "close");
        server.send(200, "text/html", ud);
    });
    /*handling uploading firmware file */
    server.on(
        "/update", HTTP_POST,
        []() {
            server.sendHeader("Connection", "close");
            server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
            ESP.restart();
        },
        []() {
            HTTPUpload& upload = server.upload();
            if (upload.status == UPLOAD_FILE_START) {
#ifdef _DEBUG_
                Serial.print(F("Update: "));
                Serial.println(upload.filename);
#endif
                if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {  // start with max available size
                    Update.printError(Serial);
                }
            } else if (upload.status == UPLOAD_FILE_WRITE) {
                /* flashing firmware to ESP*/
                if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
                    Update.printError(Serial);
                }
            } else if (upload.status == UPLOAD_FILE_END) {
                if (Update.end(true)) {  // true to set the size to the current progress
#ifdef _DEBUG_
                    Serial.println(F("Update Success: "));
                    Serial.print(upload.totalSize);
                    Serial.println(F(" Rebooting..."));
#endif
                } else {
                    Update.printError(Serial);
                }
            }
        });

    // Reset route handler
    server.on("/reset", HTTP_POST, []() {
        server.send(200, "text/plain", "ESP32 is restarting...");
        ESP.restart();  // Restart ESP32
    });

    server.begin();
#ifdef _DEBUG_
    Serial.println(F("\tOTA Web updater setting is done."));
#endif
}
//----------------- Time Setup ----------------//
String strTime(DateTime t) {
    char buff[] = "YYYY MMM DD (DDD) hh:mm:ss";
    return t.toString(buff);
}

// uint8_t preheatTime(uint8_t setMin) { return setMin == 0 ? 57 : setMin -3;}

// Set the time to 0, 15, 30, 45 min
uint8_t setMinMatch(uint8_t a) {
    if (a >= 0 && a <= 59) {
#ifdef _5Min
        return ((a / 5) + 1) * 5 % 60;
#endif
#ifdef _10Min
        return ((a / 10) + 1) * 10 % 60;
#endif
#ifdef _15Min
        return ((a / 15) + 1) * 15 % 60;
#endif
    }
    return 0;
}

uint8_t roundSec(uint8_t sec) { return sec > 60 ? sec - 60 : sec; }

void syncRtc() {
    if (!rtc.begin()) {
#ifdef _DEBUG_
        Serial.println(F("Couldn't find RTC!"));
#endif
        // Serial.flush();
        // while (1) delay(10);
    }
    // Setup time with NTPClient library.
    timeClient.begin();
    timeClient.forceUpdate();
    if (timeClient.isTimeSet()) {
        rtc.adjust(DateTime(timeClient.getEpochTime()));
#ifdef _DEBUG_
        Serial.println(F("\nSetup time from NTP server succeeded."));
#endif
    } else {
#ifdef _DEBUG_
        Serial.println(F("\nSetup time from NTP server failed."));
#endif
    }
}

void setupAlarm() {
    //     if (!rtc.begin()) {
    // #ifdef _DEBUG_
    //         Serial.println("Couldn't find RTC!");
    // #endif
    // Serial.flush();
    // while (1) delay(10);
    // }

    if (rtc.lostPower()) {
        rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));  // this will adjust to the date and time at compilation
    }

// syncRtc();  // Sync RTC with NTP server. The internet connection is required.
// January 21, 2014 at 3am you would call:
// rtc.adjust(DateTime(2023, 12, 9, 21, 59, 35));  // Manually set time
#ifdef _DEBUG_
    Serial.println("\n\t" + strTime(rtc.now()));
#endif
    rtc.disable32K();  // we don't need the 32K Pin, so disable it
    rtc.clearAlarm(1);
    rtc.clearAlarm(2);
    rtc.writeSqwPinMode(DS3231_OFF);  // stop oscillating signals at SQW Pin, otherwise setAlarm1 will fail
    rtc.disableAlarm(2);

#ifdef _20SecTest
    // Set alarm time
    rtc.setAlarm1(rtc.now() + TimeSpan(0, 0, 0, 20), DS3231_A1_Second);  // Test
    Serial.print(F("Trigger next time: "));
    Serial.println(String(roundSec(rtc.now().second() + 20)) + "th sec.");

#else

    tMin = rtc.now().minute();
#ifdef _DEBUG_
    Serial.println("tMin: " + String(tMin));
#endif
    // uint8_t setMin = setMinMatch(tMin);
    setMin      = setMinMatch(tMin);
    preheatTime = setMin == 0 ? 58 : setMin - 2;
    rtc.setAlarm1(DateTime(2023, 2, 18, 0, setMin, 0), DS3231_A1_Minute);
// rtc.setAlarm1(DateTime(2023, 2, 18, 0, 0, 0), DS3231_A1_Minute);
// rtc.setAlarm2(DateTime(2023, 2, 18, 0, 30, 0), DS3231_A2_Minute);
// rtc.setAlarm1(DateTime(2023, 2, 18, 0, 57, 0), DS3231_A1_Minute);
// rtc.setAlarm2(DateTime(2023, 2, 18, 0, 27, 0), DS3231_A2_Minute);
#ifdef _DEBUG_
    Serial.print("Preheat Time: " + String(preheatTime) + "th min.");
    Serial.println(" | Tringger next time: " + String(setMin) + "th min.");
#endif

#endif

#ifdef _DEBUG_
    Serial.println(F("\tThe alarm setting is done."));
#endif
}

void IRAM_ATTR onRtcTrigger() { rtcTrigger = true; }

// 15 minute match 0, 15, 30, and 45
bool checkMinMatch(int tMin) {
#ifdef _5Min
    return tMin >= 0 && tMin % 5 == 0 && tMin < 60;
#endif
#ifdef _10Min
    return tMin >= 0 && tMin % 10 == 0 && tMin < 60;
#endif
#ifdef _15Min
    return tMin >= 0 && tMin % 15 == 0 && tMin < 60;
#endif
}

//----------------- Collect Data --------------//

void readScd41() {
    bool dataReady = false;

#ifdef _DEBUG_
    scd41Error = scd41.getDataReadyStatus(dataReady);
    if (scd41Error != NO_ERROR) {
        Serial.print(F("Error trying to execute getDataReadyStatus(): "));
        errorToString(scd41Error, scd41ErrorMessage, sizeof scd41ErrorMessage);
        Serial.println(scd41ErrorMessage);
        return;
    }
    while (!dataReady) {
        delay(100);
        scd41Error = scd41.getDataReadyStatus(dataReady);
        if (scd41Error != NO_ERROR) {
            Serial.print(F("Error trying to execute getDataReadyStatus(): "));
            errorToString(scd41Error, scd41ErrorMessage, sizeof scd41ErrorMessage);
            Serial.println(scd41ErrorMessage);
            return;
        }
    }
    //
    // If ambient pressure compenstation during measurement
    // is required, you should call the respective functions here.
    // Check out the header file for the function definition.
    scd41Error = scd41.readMeasurement(co2Scd41, tempScd41, humiScd41);
    if (scd41Error != NO_ERROR) {
        Serial.print(F("Error trying to execute readMeasurement(): "));
        errorToString(scd41Error, scd41ErrorMessage, sizeof scd41ErrorMessage);
        Serial.println(scd41ErrorMessage);
        return;
    }

#else

    scd41.getDataReadyStatus(dataReady);
    while (!dataReady) {
        delay(100);
    }
    // If ambient pressure compenstation during measurement
    // is required, you should call the respective functions here.
    // Check out the header file for the function definition.
    scd41.readMeasurement(co2Scd41, tempScd41, humiScd41);
#endif
}

void readSht40Sgp41() {
    humiSht40                      = 0;  // %RH
    tempSht40                      = 0;  // degreeC
    uint16_t srawVoc               = 0;
    uint16_t srawNox               = 0;
    uint16_t defaultCompenstaionRh = 0x8000;  // in ticks as defined by SGP41
    uint16_t defaultCompenstaionT  = 0x6666;  // in ticks as defined by SGP41
    uint16_t compensationRh        = 0;       // in ticks as defined by SGP41
    uint16_t compensationT         = 0;       // in ticks as defined by SGP41

    // 1. Sleep: Measure every second (1Hz), as defined by the Gas Index
    // Algorithm
    //    prerequisite
    // delay(1000);

    // 2. Measure temperature and humidity for SGP internal compensation
    sgp41Error = sht40.measureHighPrecision(tempSht40, humiSht40);
    if (sgp41Error) {
        errorToString(sgp41Error, sgp41ErrorMessage, 256);
#ifdef _DEBUG_
        Serial.print(F("SHT4x - Error trying to execute measureHighPrecision(): "));
        Serial.println(sgp41ErrorMessage);
        Serial.println(F("Fallback to use default values for humidity and temperature compensation for SGP41"));
#endif
        compensationRh = defaultCompenstaionRh;
        compensationT  = defaultCompenstaionT;
    } else {
        // convert temperature and humidity to ticks as defined by SGP41
        // interface
        // NOTE: in case you read RH and T raw signals check out the
        // ticks specification in the datasheet, as they can be different for
        // different sensors
        compensationT  = static_cast<uint16_t>((tempSht40 + 45) * 65535 / 175);
        compensationRh = static_cast<uint16_t>(humiSht40 * 65535 / 100);
    }

    // 3. Measure SGP4x signals
    if (conditioning_s > 0) {
        // During NOx conditioning (10s) SRAW NOx will remain 0
        sgp41Error = sgp41.executeConditioning(compensationRh, compensationT, srawVoc);
        conditioning_s--;
    } else {
        sgp41Error = sgp41.measureRawSignals(compensationRh, compensationT, srawVoc, srawNox);
    }

    // 4. Process raw signals by Gas Index Algorithm to get the VOC and NOx
    // index
    //    values
    if (sgp41Error) {
        errorToString(sgp41Error, sgp41ErrorMessage, 256);
#ifdef _DEBUG_
        Serial.print(F("SGP41 - Error trying to execute measureRawSignals(): "));
        Serial.println(sgp41ErrorMessage);
#endif
    } else {
        vocIdxSgp41 = voc_algorithm.process(srawVoc);
        noxIdxSgp41 = nox_algorithm.process(srawNox);
    }
}

void sgp41HeatingOn() {
#ifdef _DEBUG_
    if (tSgp41HeatingOn.counter() == 1) {
        Serial.println(F("sgp41HeatingOn: First Time"));
    }
#endif

    readSht40Sgp41();
    uint8_t now = rtc.now().minute();
    if (now == setMin) {
        tSgp41HeatingOn.stop();
        tSgp41HeatingOff.start();
    }
}

void sgp41HeatingOff() {
#ifdef _DEBUG_
    if (tSgp41HeatingOff.counter() == 1) {
        Serial.println(F("sgp41HeatingOff: First Time"));
    }
#endif

    uint8_t now = rtc.now().minute();
    if (now == preheatTime) {
        tSgp41HeatingOff.stop();
        tSgp41HeatingOn.start();
    }
}

void readEns160Aht21() {
    // AHT21
    sensors_event_t humiEvent, tempEvent;
    aht21.getEvent(&humiEvent, &tempEvent);  // populate temp and humidity objects with fresh data

    tempAht21 = tempEvent.temperature;
    humiAht21 = humiEvent.relative_humidity;

    // ENS160
    ens160.measure(true);
    ens160.measureRaw(true);

    aqiEns160  = ens160.getAQI();
    tvocEns160 = ens160.getTVOC();
    eco2Ens160 = ens160.geteCO2();
}

void readData() {
    if (iaqSensor.run()) {
        tempBme680   = iaqSensor.temperature;
        humiBme680   = iaqSensor.humidity;
        pressBme680  = iaqSensor.pressure / 100.f;
        gasResBme680 = iaqSensor.gasResistance;
    }

    // VEML7700-------------/
    lux = veml.readLux(VEML_LUX_AUTO);

    // MH-Z19B-------------/
    co2 = myMHZ19.getCO2();  // Request CO2 (as ppm)

    // PMSA003A-------------/
    pms.read();
    if (pms) {  // successfull read
        pm010 = pms.pm01;
        pm025 = pms.pm25;
        pm100 = pms.pm10;
    }

    readScd41();
    readSht40Sgp41();
    readEns160Aht21();

    // DHT22
    tempDht22 = dht.readTemperature();
    humiDht22 = dht.readHumidity();

#ifdef _DEBUG_
    Serial.print(F("AHT21: Temp: "));
    Serial.print(tempAht21, 2);
    Serial.print(F(" C | Humi: "));
    Serial.print(humiAht21, 2);
    Serial.println(F(" %"));

    Serial.print(F("BME680: Temp: "));
    Serial.print(tempBme680, 2);
    Serial.print(F(" C | Humi: "));
    Serial.print(humiBme680, 2);
    Serial.print(F(" % | Gas Resist: "));
    Serial.print(gasResBme680, 2);
    Serial.println(F(" kohm"));

    Serial.print(F("DHT22: Temp: "));
    Serial.print(tempDht22, 2);
    Serial.print(F(" C | Humi: "));
    Serial.print(humiDht22, 2);
    Serial.println(F(" %"));

    Serial.print(F("ENS160: AQI: "));
    Serial.print(aqiEns160);
    Serial.print(F(" | TVOC: "));
    Serial.print(tvocEns160);
    Serial.print(F(" ppb | eCO2: "));
    Serial.print(eco2Ens160);
    Serial.println(F(" ppm"));

    Serial.print(F("MHZ19B: CO2: "));
    Serial.print(co2);
    Serial.println(F(" ppm"));

    Serial.print(F("PMSA003A: PM1.0: "));
    Serial.print(pm010);
    Serial.print(F(" ug/m3 | PM2.5: "));
    Serial.print(pm025);
    Serial.print(F(" ug/m3 | PM10: "));
    Serial.print(pm100);
    Serial.println(F(" ug/m3"));

    Serial.print(F("SDC41: Temp: "));
    Serial.print(tempScd41, 2);
    Serial.print(F(" C | Humi: "));
    Serial.print(humiScd41, 2);
    Serial.print(F(" % | CO2: "));
    Serial.print(co2Scd41);
    Serial.println(F(" ppm"));

    Serial.print(F("SGP41: VOC Idx: "));
    Serial.print(vocIdxSgp41);
    Serial.print(F(" | NOx Idx: "));
    Serial.println(noxIdxSgp41);

    Serial.print(F("SHT40: Temp: "));
    Serial.print(tempSht40, 2);
    Serial.print(F(" C | Humi: "));
    Serial.print(humiSht40, 2);
    Serial.println(F(" %"));

    Serial.print(F("VEML7700: Lux: "));
    Serial.print(lux, 2);
    Serial.println();
#endif
}

void sendData() {
    JsonDocument doc;
    doc.clear();

    JsonObject doc_0         = doc.add<JsonObject>();
    doc_0["measurement"]     = "aht21";
    JsonObject root_0_fields = doc_0["fields"].to<JsonObject>();
    root_0_fields["temp"]    = tempAht21;
    root_0_fields["humi"]    = humiAht21;

    JsonObject doc_1         = doc.add<JsonObject>();
    doc_1["measurement"]     = "bme680";
    JsonObject root_1_fields = doc_1["fields"].to<JsonObject>();
    root_1_fields["temp"]    = tempBme680;
    root_1_fields["humi"]    = humiBme680;
    root_1_fields["press"]   = pressBme680;
    root_1_fields["gasRes"]  = gasResBme680;

    JsonObject doc_2         = doc.add<JsonObject>();
    doc_2["measurement"]     = "dht22";
    JsonObject root_2_fields = doc_2["fields"].to<JsonObject>();
    root_2_fields["temp"]    = tempDht22;
    root_2_fields["humi"]    = humiDht22;

    JsonObject doc_3         = doc.add<JsonObject>();
    doc_3["measurement"]     = "ens160";
    JsonObject root_3_fields = doc_3["fields"].to<JsonObject>();
    root_3_fields["aqi"]     = aqiEns160;
    root_3_fields["tVoc"]    = tvocEns160;
    root_3_fields["eCo2"]    = eco2Ens160;

    JsonObject doc_4       = doc.add<JsonObject>();
    doc_4["measurement"]   = "mhz19b";
    doc_4["fields"]["co2"] = co2;

    JsonObject doc_5         = doc.add<JsonObject>();
    doc_5["measurement"]     = "pmsa003a";
    JsonObject root_5_fields = doc_5["fields"].to<JsonObject>();
    root_5_fields["pm010"]   = pm010;
    root_5_fields["pm025"]   = pm025;
    root_5_fields["pm100"]   = pm100;

    JsonObject doc_6         = doc.add<JsonObject>();
    doc_6["measurement"]     = "scd41";
    JsonObject root_6_fields = doc_6["fields"].to<JsonObject>();
    root_6_fields["temp"]    = tempScd41;
    root_6_fields["humi"]    = humiScd41;
    root_6_fields["co2"]     = co2Scd41;

    JsonObject doc_7         = doc.add<JsonObject>();
    doc_7["measurement"]     = "sgp41";
    JsonObject root_7_fields = doc_7["fields"].to<JsonObject>();
    root_7_fields["vocIdx"]  = vocIdxSgp41;
    root_7_fields["noxIdx"]  = noxIdxSgp41;

    JsonObject doc_8         = doc.add<JsonObject>();
    doc_8["measurement"]     = "sht40";
    JsonObject root_8_fields = doc_8["fields"].to<JsonObject>();
    root_8_fields["temp"]    = tempSht40;
    root_8_fields["humi"]    = humiSht40;

    JsonObject doc_9       = doc.add<JsonObject>();
    doc_9["measurement"]   = "veml7700";
    doc_9["fields"]["lux"] = lux;

    doc.shrinkToFit();  // optional
    char jsonBuffer[1024];
    serializeJson(doc, jsonBuffer);

    mqtt.publish(MQTT_PUB_JSON, jsonBuffer);

#ifdef _DEBUG_
    // Serial.println("JSON: " + String(jsonBuffer));
    Serial.println(F("\nData sending is done."));
#endif
}

void IRAM_ATTR fetchData() {
    setupAlarm();

#ifdef _20SecTest
    readData();
    sendData();

#else

#ifdef _DEBUG_

#ifdef _5Min
    Serial.print(F("5min Match: "));
#endif
#ifdef _10Min
    Serial.print(F("10min Match: "));
#endif
#ifdef _15Min
    Serial.print(F("15min Match: "));
#endif
    Serial.println(checkMinMatch(tMin) ? "true" : "false");
#endif

    if (checkMinMatch(tMin)) {
        readData();
        sendData();
#ifdef _DEBUG_
        Serial.println(F("\tdata reading is done."));
#endif
    } else {
#ifdef _DEBUG_
        Serial.println(F("\tread data next time."));

#endif
    }

#endif
}

//******************************** Task Functions ***************************//
void wifiDisconnectedDetect() {
    if (WiFi.status() != WL_CONNECTED) {
        // ESP.restart();
        WiFi.disconnect();
        WiFi.reconnect();
    }
}

void reconnectMqtt() {
    if (WiFi.status() == WL_CONNECTED) {
#ifdef _DEBUG_
        Serial.print(F("Connecting MQTT... "));
#endif
        if (mqtt.connect(deviceName, mqttUser, mqttPass)) {
            tReconnectMqtt.stop();
#ifdef _DEBUG_
            Serial.println(F("connected"));
#endif
            tConnectMqtt.interval(0);
            tConnectMqtt.start();
            statusLed.blinkNumberOfTimes(200, 200, 3);  // 250ms ON, 750ms OFF, repeat 3 times, blink immediately
        } else {
#ifdef _DEBUG_
            Serial.println("failed state: " + String(mqtt.state()));
            Serial.println("counter: " + String(tReconnectMqtt.counter()));
#endif
            if (tReconnectMqtt.counter() >= 3) {
                // ESP.restart();
                tReconnectMqtt.stop();
                tConnectMqtt.interval(300 * 1000);  // 300 sec. = 5 min.Wait 5 minute before reconnecting.
                tConnectMqtt.resume();
            }
        }
    } else {
        if (tReconnectMqtt.counter() <= 1) {
#ifdef _DEBUG_
            Serial.println(F("WiFi is not connected"));
#endif
        }
    }
}

void connectMqtt() {
    if (!mqtt.connected()) {
        tConnectMqtt.pause();
        tReconnectMqtt.start();
    } else {
        mqtt.loop();
    }
}

void checkSensor(bool condition, String sensorName) {
    if (condition) {
#ifdef _DEBUG_
        Serial.println(sensorName + " : " + "Available");
#endif
    } else {
#ifdef _DEBUG_
        Serial.println(sensorName + " : " + "Failed!");
#endif
    }
}

void printScd41Config(String prefix) {
#ifdef _DEBUG_
    float    tempOffset    = 0.0f;
    uint16_t tempOffsetRaw = 0, altitude = 0;

    Serial.print(prefix);
    scd41.getTemperatureOffset(tempOffset);
    Serial.print(F(" tempOffset: "));
    Serial.print(tempOffset, 2);
    Serial.print(F(" C"));
    scd41.getTemperatureOffsetRaw(tempOffsetRaw);
    Serial.print(F(" | "));
    Serial.print(F(" TempOffsetRaw: "));
    Serial.print(175 * tempOffsetRaw / 65535);
    Serial.print(F(" C"));
    scd41.getSensorAltitude(altitude);
    Serial.print(F(" | "));
    Serial.print(F(" Altitude: "));
    Serial.print(altitude);
    Serial.println(F(" m"));
#endif
}

//******************************** Setup  ***********************************//
void setup() {
#ifdef _DEBUG_
    Serial.begin(115200);
#endif
    statusLed.turnOFF();
    Wire.begin();
    pinMode(SQW_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(SQW_PIN), onRtcTrigger, FALLING);
    resetWifiBt.begin(resetWifiBtPin);
    resetWifiBt.setLongClickTime(5000);
    resetWifiBt.setLongClickDetectedHandler(resetWifiBtPressed);
    
    while (!LittleFS.begin(FORMAT_LITTLEFS_IF_FAILED)) {
#ifdef _DEBUG_
        Serial.println(F("Failed to initialize LittleFS library"));
#endif
        delay(1000);
    }
    // BME680
    iaqSensor.begin(BME68X_I2C_ADDR_LOW, Wire);
    iaqSensor.updateSubscription(sensorList, 13, BSEC_SAMPLE_RATE_LP);
    // VEML7700
    checkSensor(veml.begin(), "VEML7700");
    // MH-Z19B
    mySerial.begin(9600, SERIAL_8N1, rxPin2, txPin2);  // (Uno example) device to MH-Z19 serial start
    myMHZ19.begin(mySerial);                           // *Serial(Stream) reference must be passed to library begin().
    myMHZ19.autoCalibration();                         // Turn auto calibration ON (OFF autoCalibration(false))

    // PMSA003
    pms.init();

    // SCD41
    scd41.begin(Wire, SCD41_I2C_ADDR_62);
    scd41.wakeUp();
    scd41.stopPeriodicMeasurement();
    scd41.reinit();  // Reload the configuration from EEPROM
    // uint64_t serialNumber = 0;
    // scd41.getSerialNumber(serialNumber);

    printScd41Config("");
    scd41.setTemperatureOffset(3.5f);  // Default: 4 C, Sweet spot: 3.5 C
    scd41.setSensorAltitude(310);      // Set altitude to 0m (default)
    scd41.persistSettings();           // Save settings to EEPROM
    printScd41Config("After");
    scd41.startPeriodicMeasurement();

    // SHT40
    sht40.begin(Wire, SHT40_I2C_ADDR_44);
    // SGP41
    sgp41.begin(Wire);
    // AHT21
    checkSensor(aht21.begin(), "AHT21");
    // ENS160
    ens160.begin();
    checkSensor(ens160.available(), "ENS160");
    ens160.setMode(ENS160_OPMODE_STD);
    // DHT22
    dht.begin();

    wifiManagerSetup();
    otaWebUpdateSetup();
    syncRtc();
    setupAlarm();

    mqttInit();

    tWifiDisconnectedDetect.start();

#ifndef _20SecTest
    tSgp41HeatingOff.start();
#endif
}
//******************************** Loop *************************************//
void loop() {
    tWifiDisconnectedDetect.update();
    statusLed.loop();  // MUST call the led.loop() function in loop()
    resetWifiBt.loop();
    server.handleClient();  // OTA Web Update
    tConnectMqtt.update();
    tReconnectMqtt.update();

#ifndef _20SecTest
    tSgp41HeatingOn.update();
    tSgp41HeatingOff.update();
#endif

    if (rtcTrigger) {
        rtcTrigger = false;
        fetchData();
    }
}
