/*
Log:
20.2.1  - Change tReconnectMqtt() logic
20.2    - Add easyButton library
        - Change update time to NTPClient Library
20.0    - Base on 19.3
        - Change Adafruit_BME680 to BSEC library
*/

#include <Arduino.h>
#include <FS.h>  //this needs to be first, or it all crashes and burns.../
#include <SPIFFS.h>
#include <ArduinoJson.h>  //https://github.com/bblanchon/ArduinoJson
// WiFiManager
#include <WiFiManager.h>  //https://github.com/tzapu/WiFiManager
// PubSubClient
#include <PubSubClient.h>
// OTA
#include <ESPmDNS.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFiClient.h>
//
#include <RTClib.h>
#include <SoftwareSerial.h>
#include <Wire.h>
#include <ezLED.h>
// Sensors
#include "bsec.h"  // SD0 Pkn connect to GND
#include <PMserial.h>
#include "Adafruit_VEML7700.h"
#include "MHZ19.h"
#include <NOxGasIndexAlgorithm.h>
#include <SensirionI2CSgp41.h>
#include <SensirionI2cSht4x.h>
#include <VOCGasIndexAlgorithm.h>
#include <Adafruit_AHTX0.h>    // AHT21 - Adafruit AHTX0
#include <ScioSense_ENS160.h>  // ENS160 - ENS160 Adafruit Fork
#include <TickTwo.h>
#include <EasyButton.h>

//******************************** Configulation ****************************//
// #define _DEBUG_  // Uncomment this line if you want to debug
// #define syncRtcWithNtp // Uncomment this line if you want to sync RTC with NTP
// #define _20SecTest  // Uncomment this line if you want 20sec Sensors Test

//******************************** Global Variables *************************//
#define deviceName "WeatherSt"

//----------------- esLED ---------------------//
#define led LED_BUILTIN
ezLED statusLed(led);

//----------------- Reset WiFi Button ---------//
#define resetWifiPin 0
EasyButton resetWifiBt(resetWifiPin);

//----------------- WiFi Manager --------------//

// default custom static IP
char static_ip[16]  = "192.168.0.191";
char static_gw[16]  = "192.168.0.1";
char static_sn[16]  = "255.255.255.0";
char static_dns[16] = "8.8.8.8";
// MQTT
char mqtt_server[16] = "192.168.0.10";
char mqtt_port[6]    = "1883";
char mqtt_user[16];  // = "admin";
char mqtt_pass[16];  // = "admin";

bool shouldSaveConfig = false;  // flag for saving data

WiFiManager wifiManager;
//----------------- OTA Web Update ------------//
/* Style */
String style =
    "<style>#file-input,input{width:100%;height:44px;border-radius:999px;margin:10px auto;font-size:15px}"
    "input{background:#202020;border: 1px solid #777;padding:0 15px;text-align:center;color:white}body{background:#202020;font-family:sans-serif;font-size:14px;color:white}"
    "#file-input{background:#202020;padding:0;border:1px solid #ddd;line-height:44px;text-align:center;display:block;cursor:pointer}"
    "#bar,#prgbar{background-color:#f1f1f1;border-radius:10px}#bar{background-color:#4C4CEA;width:0%;height:10px}"
    "form{background:#181818;max-width:258px;margin:75px auto;padding:30px;border-radius:5px;box-shadow: 0px 0px 20px 5px #777;text-align:center}"
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
#define MQTT_PUB_PRESSURE       "esp32/bme680/pressure"
#define MQTT_PUB_GAS_RESISTANCE "esp32/bme680/gasResistance"
#define MQTT_PUB_TEMPERATURE    "esp32/bme680/temperature"
#define MQTT_PUB_HUMIDITY       "esp32/bme680/humidity"
#define MQTT_PUB_LUX            "esp32/veml7700/illumination"
#define MQTT_PUB_CO2            "esp32/mhz19b/co2"
#define MQTT_PUB_PM01           "esp32/pmsa003/pm_01"
#define MQTT_PUB_PM25           "esp32/pmsa003/pm_25"
#define MQTT_PUB_PM10           "esp32/pmsa003/pm_10"
#define MQTT_PUB_tempSht40      "esp32/sht40/temp"
#define MQTT_PUB_humiSht40      "esp32/sht40/humi"
#define MQTT_PUB_vocIdxSgp41    "esp32/sgp41/vocIdx"
#define MQTT_PUB_noxIdxSgp41    "esp32/sgp41/noxIdx"
#define MQTT_PUB_tempAht21      "esp32/aht21/temp"
#define MQTT_PUB_huimAht21      "esp32/aht21/humi"
#define MQTT_PUB_aqiEns160      "esp32/ens160/aqi"
#define MQTT_PUB_tvocEns160     "esp32/ens160/tvoc"
#define MQTT_PUB_eco2Ens160     "esp32/ens160/eco2"

WiFiClient   mqttClient;
PubSubClient mqtt(mqttClient);

//----------------- Time Setup ----------------//
// Sync Time with NTP Server
#ifdef syncRtcWithNtp
#include <NTPClient.h>
#include <WiFiUdp.h>
WiFiUDP ntpUDP;
// NTPClient timeClient(ntpUDP, "NTP Server Name", offset time(ms), update Interval(ms));
NTPClient timeClient(ntpUDP, "time.google.com", 25200 /*GMT +7*/);
// NTPClient timeClient(ntpUDP, "time.facebook.com", 25200 /*GMT +7*/);
// NTPClient timeClient(ntpUDP, "time.apple.com", 25200 /*GMT +7*/);
#endif

//----------------- DS3231  Real Time Clock --//
#define SQW_PIN 33  // pin 33 for external Wake Up (ext1)
RTC_DS3231 rtc;

uint8_t tMin;
uint8_t setMin;
uint8_t preheatTime;
bool    rtcTrigger = false;

//----------------- Sensors -------------------//
// BME680 Temperature, Humidity, & Pressure Sensor
float Pressure;
float GasResistance;
float Temperature;
float Humidity;

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
uint16_t pm_01, pm_25, pm_10;
SerialPM pms(PMSx003, pmsRX, pmsTX);

// SHT40 Temperature & Humidity Sensor
float humiSht40;  // %RH
float tempSht40;  // degreeC

SensirionI2cSht4x sht40;

// SGP41 Air Quality Sensor, VOC & NOx Index
uint16_t conditioning_s = 10;
char     errorMessage[256];

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
uint16_t eco2Ena160;

ScioSense_ENS160 ens160(ENS160_I2CADDR_1);  // 0x53

//******************************** Tasks ************************************//
void    Sgp41HeatingOn();
void    Sgp41HeatingOff();
TickTwo tSgp41HeatingOn(Sgp41HeatingOn, 500, 0, MILLIS);  // (function, interval, iteration, interval unit)
TickTwo tSgp41HeatingOff(Sgp41HeatingOff, 0, 0, MILLIS);  // (function, interval, iteration, interval unit)

void    wifiDisconnectedDetect();
TickTwo tWifiDisconnectedDetect(wifiDisconnectedDetect, 300000, 0, MILLIS);

void    connectMqtt();
void    reconnectMqtt();
TickTwo tConnectMqtt(connectMqtt, 0, 0, MILLIS);  // (function, interval, iteration, interval unit)
TickTwo tReconnectMqtt(reconnectMqtt, 3000, 0, MILLIS);

//******************************** Functions ********************************//
//----------------- WiFi Manager --------------//
// callback notifying us of the need to save config
void saveConfigCallback() {
#ifdef _DEBUG_
    Serial.println("Should save config");
#endif
    shouldSaveConfig = true;
}

void wifiManagerSetup() {
    // clean FS, for testing
    // SPIFFS.format();

    // read configuration from FS json
#ifdef _DEBUG_
    Serial.println("mounting FS...");
#endif

    if (SPIFFS.begin()) {
#ifdef _DEBUG_
        Serial.println("Mounted file system");
#endif
        if (SPIFFS.exists("/config.json")) {
// File exists, reading and loading
#ifdef _DEBUG_
            Serial.println("Reading config file");
#endif
            File configFile = SPIFFS.open("/config.json", "r");
            if (configFile) {
#ifdef _DEBUG_
                Serial.println("Opened config file");
#endif
                size_t size = configFile.size();
                if (size > 1024) {
#ifdef _DEBUG_
                    Serial.println("Config file size is too large");
#endif
                    return;
                }
                // Allocate a buffer to store contents of the file
                std::unique_ptr<char[]> buf(new char[size]);
                configFile.readBytes(buf.get(), size);

                JsonDocument         json;
                DeserializationError error = deserializeJson(json, buf.get());
                if (error) {
#ifdef _DEBUG_
                    Serial.println("Failed to parse config file");
#endif
                    return;
                }
#ifdef _DEBUG_
                Serial.println("Parsed JSON");
#endif
                strcpy(mqtt_server, json["mqtt_server"]);
                strcpy(mqtt_port, json["mqtt_port"]);
                strcpy(mqtt_user, json["mqtt_user"]);
                strcpy(mqtt_pass, json["mqtt_pass"]);

                if (json["ip"].is<const char*>()) {
#ifdef _DEBUG_
                    Serial.println("Setting custom IP from config");
#endif
                    strcpy(static_ip, json["ip"]);
                    strcpy(static_gw, json["gateway"]);
                    strcpy(static_sn, json["subnet"]);
                    strcpy(static_dns, json["dns"]);
                } else {
#ifdef _DEBUG_
                    Serial.println("No custom IP in config");
#endif
                }
            } else {
#ifdef _DEBUG_
                Serial.println("Failed to open config file");
#endif
            }
        } else {
#ifdef _DEBUG_
            Serial.println("Config file does not exist");
#endif
        }
    } else {
#ifdef _DEBUG_
        Serial.println("Failed to mount FS");
#endif
    }
    // end read

    WiFiManagerParameter custom_mqtt_server("server", "MQTT Server", mqtt_server, 16);
    WiFiManagerParameter custom_mqtt_port("port", "MQTT Port", mqtt_port, 6);
    WiFiManagerParameter custom_mqtt_user("user", "MQTT User", mqtt_user, 16);
    WiFiManagerParameter custom_mqtt_pass("pass", "MQTT Password", mqtt_pass, 16);

    // WiFiManager
    // Local intialization. Once its business is done, there is no need to keep it around
    //   WiFiManager wifiManager;

    // set config save notify callback
    wifiManager.setSaveConfigCallback(saveConfigCallback);

    // set static ip
    //  wifiManager.setSTAStaticIPConfig(IPAddress(192, 168, 1, 101), IPAddress(192, 168, 1, 1), IPAddress(255, 255, 255, 0));
    // Or
    IPAddress _ip, _gw, _sn, _dns;
    _ip.fromString(static_ip);
    _gw.fromString(static_gw);
    _sn.fromString(static_sn);
    _dns.fromString(static_dns);
    wifiManager.setSTAStaticIPConfig(_ip, _gw, _sn, _dns);

    // add all your parameters here
    wifiManager.addParameter(&custom_mqtt_server);
    wifiManager.addParameter(&custom_mqtt_port);
    wifiManager.addParameter(&custom_mqtt_user);
    wifiManager.addParameter(&custom_mqtt_pass);

    // wifiManager.resetSettings();  // reset settings - for testing

    wifiManager.setClass("invert");  // set dark theme
    // wifiManager.setMinimumSignalQuality(30);  // defaults to 8% // set minimu quality of signal so it ignores AP's under that quality
    // wifiManager.setTimeout(120);  // auto restart after the device can't connect to wifi within 120 seconds
    wifiManager.setConfigPortalTimeout(60);  // auto close configportal after n seconds

    // fetches ssid and pass and tries to connect
    // if it does not connect it starts an access point with the specified name
    // here  "AutoConnectAP"
    // and goes into a blocking loop awaiting configuration
    if (!wifiManager.autoConnect(deviceName, "password")) {
#ifdef _DEBUG_
        Serial.println("failed to connect and hit timeout");
#endif
        delay(3000);
        // reset and try again, or maybe put it to deep sleep
        ESP.restart();
        delay(5000);
    }

// if you get here you have connected to the WiFi
#ifdef _DEBUG_
    Serial.println("WiFi connected...yeey :)");
#endif

    // read updated parameters
    strcpy(mqtt_server, custom_mqtt_server.getValue());
    strcpy(mqtt_port, custom_mqtt_port.getValue());
    strcpy(mqtt_user, custom_mqtt_user.getValue());
    strcpy(mqtt_pass, custom_mqtt_pass.getValue());
#ifdef _DEBUG_
    Serial.println("The values in the file are: ");
    Serial.println("\tmqtt_server : " + String(mqtt_server));
    Serial.println("\tmqtt_port : " + String(mqtt_port));
    Serial.println("\tmqtt_user : " + String(mqtt_user));
    Serial.println("\tmqtt_pass : " + String(mqtt_pass));
#endif

    // save the custom parameters to FS
    if (shouldSaveConfig) {
#ifdef _DEBUG_
        Serial.println("saving config");
#endif
        JsonDocument json;
        json["mqtt_server"] = mqtt_server;
        json["mqtt_port"]   = mqtt_port;
        json["mqtt_user"]   = mqtt_user;
        json["mqtt_pass"]   = mqtt_pass;
        // Static IP
        json["ip"]      = WiFi.localIP().toString();
        json["gateway"] = WiFi.gatewayIP().toString();
        json["subnet"]  = WiFi.subnetMask().toString();
        json["dns"]     = WiFi.dnsIP().toString();

        File configFile = SPIFFS.open("/config.json", "w");
        if (!configFile) {
#ifdef _DEBUG_
            Serial.println("failed to open config file for writing");
#endif
        }

        serializeJson(json, Serial);
        serializeJson(json, configFile);
        configFile.close();
        // end save
    }
#ifdef _DEBUG_
    Serial.println("local ip: ");
    Serial.println(WiFi.localIP());
    Serial.println(WiFi.gatewayIP());
    Serial.println(WiFi.subnetMask());
    Serial.println(WiFi.dnsIP());
#endif
}

void resetWifiBtPressed() {
    statusLed.turnON();
#ifdef _DEBUG_
    Serial.println("WiFi resetting started.");
#endif
    SPIFFS.format();
    wifiManager.resetSettings();
#ifdef _DEBUG_
    Serial.println(String(deviceName) + " is restarting.");
#endif
    ESP.restart();
}

//----------------- OTA Web Updater -----------//
void OtaWebUpdateSetup() {
    // const char* host = "ESP32";
    /*use mdns for host name resolution*/
    if (!MDNS.begin(deviceName)) {  // http://deviceName.local
#ifdef _DEBUG_
        Serial.println("Error setting up MDNS responder!");
#endif
        while (1) {
            delay(500);  // Default is 1000
        }
    }
#ifdef _DEBUG_
    Serial.println("mDNS responder started");
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
    server.on("/update", HTTP_POST, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
    ESP.restart(); }, []() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
#ifdef _DEBUG_
      Serial.printf("Update: %s\n", upload.filename.c_str());
#endif
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { //start with max available size
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      /* flashing firmware to ESP*/
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) { //true to set the size to the current progress
#ifdef _DEBUG_
        Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
#endif
      } else {
        Update.printError(Serial);
      }
    } });

    // Reset route handler
    server.on("/reset", HTTP_POST, []() {
        server.send(200, "text/plain", "ESP32 is restarting...");
        ESP.restart();  // Restart ESP32
    });

    server.begin();
#ifdef _DEBUG_
    Serial.println("\tOTA Web updater setting is done.");
#endif
}
//----------------- Time Setup ----------------//
String strTime(DateTime t) {
    char buff[] = "YYYY MMM DD (DDD) hh:mm:ss";
    return t.toString(buff);
}

// uint8_t preheatTime(uint8_t setMin) { return setMin == 0 ? 57 : setMin -3;}

// Set the time to 0, 15, 30, 45 min
uint8_t set15Min(uint8_t a) {
    if (a >= 0 && a <= 59) {
        return ((a / 15) + 1) * 15 % 60;
    }
    return 0;
}

uint8_t roundSec(uint8_t sec) { return sec > 60 ? sec - 60 : sec; }

void SyncRtc() {
#ifdef syncRtcWithNtp
    // Setup time with NTPClient library.
    timeClient.begin();
    timeClient.forceUpdate();
    if (timeClient.isTimeSet()) {
        rtc.adjust(DateTime(timeClient.getEpochTime()));
#ifdef _DEBUG_
        Serial.println("\nSetup time from NTP server succeeded.");
#endif
    } else {
#ifdef _DEBUG_
        Serial.println("\nSetup time from NTP server failed.");
#endif
    }
#endif
}

// void SyncRtc2() {
//     if (timeClient.getMinutes() != rtc.now().minute()) SyncRtc();
// }

void SetupAlarm() {
    if (!rtc.begin()) {
#ifdef _DEBUG_
        Serial.println("Couldn't find RTC!");
#endif
        // Serial.flush();
        // while (1) delay(10);
    }

    if (rtc.lostPower()) {
        rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));  // this will adjust to the date and time at compilation
    }

    SyncRtc();  // To ync RTC with NTP server. The internet connection is required.
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
    Serial.print("Trigger next time: ");
    Serial.println(String(roundSec(rtc.now().second() + 20)) + "th sec.");
#else
    tMin = rtc.now().minute();
#ifdef _DEBUG_
    Serial.println("tMin: " + String(tMin));
#endif
    // uint8_t setMin = set15Min(tMin);
    setMin      = set15Min(tMin);
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
    Serial.println("\tThe alarm setting is done.");
#endif
}

void IRAM_ATTR onRtcTrigger() { rtcTrigger = true; }

// 15 minute match 0, 15, 30, and 45
bool t15MinMatch(int tMin) { return tMin >= 0 && tMin % 15 == 0 && tMin < 60; }

//----------------- Collect Data --------------//
void ReadSht40Sgp41() {
    uint16_t error;
    //  float humidity = 0;     // %RH
    //  float temperature = 0;  // degreeC
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
    error = sht40.measureHighPrecision(tempSht40, humiSht40);
    if (error) {
        errorToString(error, errorMessage, 256);
#ifdef _DEBUG_
        Serial.print("SHT4x - Error trying to execute measureHighPrecision(): ");
        Serial.println(errorMessage);
        Serial.println("Fallback to use default values for humidity and temperature compensation for SGP41");
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
        error = sgp41.executeConditioning(compensationRh, compensationT, srawVoc);
        conditioning_s--;
    } else {
        error = sgp41.measureRawSignals(compensationRh, compensationT, srawVoc, srawNox);
    }

    // 4. Process raw signals by Gas Index Algorithm to get the VOC and NOx
    // index
    //    values
    if (error) {
        errorToString(error, errorMessage, 256);
#ifdef _DEBUG_
        Serial.print("SGP41 - Error trying to execute measureRawSignals(): ");
        Serial.println(errorMessage);
#endif
    } else {
        vocIdxSgp41 = voc_algorithm.process(srawVoc);
        noxIdxSgp41 = nox_algorithm.process(srawNox);
    }
}

void Sgp41HeatingOn() {
#ifdef _DEBUG_
    if (tSgp41HeatingOn.counter() == 1) {
        Serial.println("Sgp41HeatingOn: First Time");
    }
#endif
    ReadSht40Sgp41();
    uint8_t now = rtc.now().minute();
    if (now == setMin) {
        tSgp41HeatingOn.stop();
        tSgp41HeatingOff.start();
    }
}

void Sgp41HeatingOff() {
#ifdef _DEBUG_
    if (tSgp41HeatingOff.counter() == 1) {
        Serial.println("Sgp41HeatingOff: First Time");
    }
#endif
    uint8_t now = rtc.now().minute();
    if (now == preheatTime) {
        tSgp41HeatingOff.stop();
        tSgp41HeatingOn.start();
    }
}

void ReadEns160Aht21() {
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
    eco2Ena160 = ens160.geteCO2();
}

void ReadData() {
    if (iaqSensor.run()) {
        Temperature   = iaqSensor.temperature;
        Humidity      = iaqSensor.humidity;
        Pressure      = iaqSensor.pressure / 100.f;
        GasResistance = iaqSensor.gasResistance;
    }

    // VEML7700-------------/
    lux = veml.readLux(VEML_LUX_AUTO);

    // MH-Z19B-------------/
    co2 = myMHZ19.getCO2();  // Request CO2 (as ppm)

    pms.read();
    if (pms) {  // successfull read
        pm_01 = pms.pm01;
        pm_25 = pms.pm25;
        pm_10 = pms.pm10;
    }

    ReadSht40Sgp41();
    ReadEns160Aht21();

#ifdef _DEBUG_
    Serial.print("Temp: " + String(Temperature) + ", Humi: " + String(Humidity) + ", press: " + String(Pressure));
    Serial.print(", GasResistance: " + String(GasResistance));
    Serial.print(", Illu: " + String(lux) + ", CO2: " + String(co2));
    Serial.println(", pm1.0: " + String(pm_01) + ", pm2.5: " + String(pm_25) + ", pm10: " + String(pm_10));
    Serial.printf("Temp-SHT40: %.2f C | Humi-SHT40: %.2f %%", tempSht40, humiSht40);
    Serial.printf(" | VOC Idx-SGP41: %d, NOx Idx-SGP41: %d\n", vocIdxSgp41, noxIdxSgp41);
    Serial.printf("Temp-AHT21: %.2f C | Humi-AHT21: %.2f %%", tempAht21, humiAht21);
    Serial.printf(" | AQI-ENS160: %u | TVOC-ENS160: %u ppb | eCO2-ENS160: %u ppm\n", aqiEns160, tvocEns160, eco2Ena160);
#endif
}

void SendData() {
    mqtt.publish(MQTT_PUB_PRESSURE, String(Pressure).c_str());
    mqtt.publish(MQTT_PUB_GAS_RESISTANCE, String(GasResistance).c_str());
    mqtt.publish(MQTT_PUB_TEMPERATURE, String(Temperature).c_str());
    mqtt.publish(MQTT_PUB_HUMIDITY, String(Humidity).c_str());
    mqtt.publish(MQTT_PUB_LUX, String(lux).c_str());
    mqtt.publish(MQTT_PUB_CO2, String(co2).c_str());
    mqtt.publish(MQTT_PUB_PM01, String(pm_01).c_str());
    mqtt.publish(MQTT_PUB_PM25, String(pm_25).c_str());
    mqtt.publish(MQTT_PUB_PM10, String(pm_10).c_str());
    mqtt.publish(MQTT_PUB_tempSht40, String(tempSht40).c_str());
    mqtt.publish(MQTT_PUB_humiSht40, String(humiSht40).c_str());
    mqtt.publish(MQTT_PUB_vocIdxSgp41, String(vocIdxSgp41).c_str());
    mqtt.publish(MQTT_PUB_noxIdxSgp41, String(noxIdxSgp41).c_str());
    mqtt.publish(MQTT_PUB_tempAht21, String(tempAht21).c_str());
    mqtt.publish(MQTT_PUB_huimAht21, String(humiAht21).c_str());
    mqtt.publish(MQTT_PUB_aqiEns160, String(aqiEns160).c_str());
    mqtt.publish(MQTT_PUB_tvocEns160, String(tvocEns160).c_str());
    mqtt.publish(MQTT_PUB_eco2Ens160, String(eco2Ena160).c_str());

#ifdef _DEBUG_
    Serial.println("Data sending is done.");
#endif
}

void IRAM_ATTR fetchData() {
    SetupAlarm();

#ifdef _20SecTest
    ReadData();
    SendData();
#else
#ifdef _DEBUG_
    Serial.print("15min Match: ");
    Serial.println(t15MinMatch(tMin) ? "true" : "false");
#endif
    if (t15MinMatch(tMin)) {
        ReadData();
        SendData();
#ifdef _DEBUG_
        Serial.println("\tdata reading is done.");
#endif
    } else {
#ifdef _DEBUG_
        Serial.println("\tread data next time.");
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
        Serial.print("Connecting MQTT... ");
#endif
        if (mqtt.connect(deviceName, mqtt_user, mqtt_pass)) {
            tReconnectMqtt.stop();
#ifdef _DEBUG_
            Serial.println("connected");
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
            Serial.println("WiFi is not connected");
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

void CheckSensor(bool condition, String sensorName) {
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

//******************************** Setup  ***********************************//
void setup() {
    Serial.begin(115200);
    statusLed.turnOFF();
    Wire.begin();
    pinMode(SQW_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(SQW_PIN), onRtcTrigger, FALLING);
    resetWifiBt.begin();
    resetWifiBt.onPressedFor(5000, resetWifiBtPressed);

    // BME680
    iaqSensor.begin(BME68X_I2C_ADDR_LOW, Wire);
    iaqSensor.updateSubscription(sensorList, 13, BSEC_SAMPLE_RATE_LP);

    // VEML7700
    CheckSensor(veml.begin(), "VEML7700");
    // MH-Z19B Init
    // mySerial.begin(9600, SERIAL_8N1, rxPin2, txPin2);       // (Uno example) device to MH-Z19 serial start
    mySerial.begin(9600);       // (Uno example) device to MH-Z19 serial start
    myMHZ19.begin(mySerial);    // *Serial(Stream) reference must be passed to library begin().
    myMHZ19.autoCalibration();  // Turn auto calibration ON (OFF autoCalibration(false))
    // PMSA003
    pms.init();
    // SHT40
    sht40.begin(Wire, SHT40_I2C_ADDR_44);
    // SGP41
    sgp41.begin(Wire);
    // AHT21
    CheckSensor(aht21.begin(), "AHT21");
    // ENS160
    ens160.begin();
    CheckSensor(ens160.available(), "ENS160");
    ens160.setMode(ENS160_OPMODE_STD);

    wifiManagerSetup();
    OtaWebUpdateSetup();

    SetupAlarm();

    mqtt.setServer(mqtt_server, atoi(mqtt_port));
    tWifiDisconnectedDetect.start();
    tConnectMqtt.start();
#ifndef _20secTest
    tSgp41HeatingOff.start();
#endif
}
//******************************** Loop *************************************//
void loop() {
    tWifiDisconnectedDetect.update();
    statusLed.loop();  // MUST call the led.loop() function in loop()
    resetWifiBt.read();
    server.handleClient();  // OTA Web Update
    tConnectMqtt.update();
    tReconnectMqtt.update();
#ifndef _20secTest
    tSgp41HeatingOn.update();
    tSgp41HeatingOff.update();
#endif

    if (rtcTrigger) {
        rtcTrigger = false;
        fetchData();
    }
}
