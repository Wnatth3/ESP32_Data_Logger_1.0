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

#include <TickTwo.h>
#include <EasyButton.h>

//******************************** Configulation ****************************//
#define DebugMode  // Uncomment this line if you want to debug
// #define syncRtcWithNtp // Uncomment this line if you want to sync RTC with NTP
// #define _20SecTest  // Uncomment this line if you want 20sec Sensors Test

#ifdef DebugMode
#define de(x)   Serial.print(x)
#define deln(x) Serial.println(x)
#else
#define de(x)
#define deln(x)
#endif

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

byte tMin;
bool rtcTrigger = false;

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
float             lux;
Adafruit_VEML7700 veml = Adafruit_VEML7700();

// MH-Z19B CO2 Sensor
int            co2;
MHZ19          myMHZ19;      // Constructor for library
HardwareSerial mySerial(2);  // On ESP32 we do not require the SoftwareSerial library, since we have 2 USARTS available

// PMSA003A
#define pmsRX 18
#define pmsTX 19
uint16_t pm_01, pm_25, pm_10;
SerialPM pms(PMSx003, pmsRX, pmsTX);

//******************************** Tasks ************************************//
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
    deln("Should save config");
    shouldSaveConfig = true;
}

void wifiManagerSetup() {
    // clean FS, for testing
    //  SPIFFS.format();

    // read configuration from FS json
    deln("mounting FS...");

    if (SPIFFS.begin()) {
        deln("mounted file system");
        if (SPIFFS.exists("/config.json")) {
            // file exists, reading and loading
            deln("reading config file");
            File configFile = SPIFFS.open("/config.json", "r");
            if (configFile) {
                deln("opened config file");
                size_t size = configFile.size();
                // Allocate a buffer to store contents of the file.
                std::unique_ptr<char[]> buf(new char[size]);

                configFile.readBytes(buf.get(), size);
                JsonDocument json;
                auto         deserializeError = deserializeJson(json, buf.get());
                serializeJson(json, Serial);
                if (!deserializeError) {
                    deln("\nparsed json");
                    strcpy(mqtt_server, json["mqtt_server"]);
                    strcpy(mqtt_port, json["mqtt_port"]);
                    strcpy(mqtt_user, json["mqtt_user"]);
                    strcpy(mqtt_pass, json["mqtt_pass"]);
                    if (json["ip"]) {
                        deln("setting custom ip from config");
                        strcpy(static_ip, json["ip"]);
                        strcpy(static_gw, json["gateway"]);
                        strcpy(static_sn, json["subnet"]);
                        strcpy(static_dns, json["dns"]);
                    } else {
                        deln("no custom ip in config");
                    }
                } else {
                    deln("failed to load json config");
                }
                // configFile.close();
            }
        }
    } else {
        deln("failed to mount FS");
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
        deln("failed to connect and hit timeout");
        delay(3000);
        // reset and try again, or maybe put it to deep sleep
        ESP.restart();
        delay(5000);
    }

    // if you get here you have connected to the WiFi
    deln("WiFi connected...yeey :)");

    // read updated parameters
    strcpy(mqtt_server, custom_mqtt_server.getValue());
    strcpy(mqtt_port, custom_mqtt_port.getValue());
    strcpy(mqtt_user, custom_mqtt_user.getValue());
    strcpy(mqtt_pass, custom_mqtt_pass.getValue());
    deln("The values in the file are: ");
    deln("\tmqtt_server : " + String(mqtt_server));
    deln("\tmqtt_port : " + String(mqtt_port));
    deln("\tmqtt_user : " + String(mqtt_user));
    deln("\tmqtt_pass : " + String(mqtt_pass));

    // save the custom parameters to FS
    if (shouldSaveConfig) {
        deln("saving config");
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
            deln("failed to open config file for writing");
        }

        serializeJson(json, Serial);
        serializeJson(json, configFile);
        configFile.close();
        // end save
    }

    deln("local ip: ");
    deln(WiFi.localIP());
    deln(WiFi.gatewayIP());
    deln(WiFi.subnetMask());
    deln(WiFi.dnsIP());
}

void resetWifiBtPressed() {
    statusLed.turnON();
    Serial.println("WiFi resetting started.");
    SPIFFS.format();
    wifiManager.resetSettings();
    Serial.println(String(deviceName) + " is restarting.");
    ESP.restart();
}

//----------------- OTA Web Updater -----------//
void OtaWebUpdateSetup() {
    // const char* host = "ESP32";
    /*use mdns for host name resolution*/
    if (!MDNS.begin(deviceName)) {  // http://deviceName.local
        deln("Error setting up MDNS responder!");
        while (1) {
            delay(500);  // Default is 1000
        }
    }
    deln("mDNS responder started");

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
      Serial.printf("Update: %s\n", upload.filename.c_str());
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
        Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
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
    deln("\tOTA Web updater setting is done.");
}

//----------------- Time Setup ----------------//
String strTime(DateTime t) {
    char buff[] = "YYYY MMM DD (DDD) hh:mm:ss";
    return t.toString(buff);
}

int set15Min(byte a) {
    if (a >= 0 && a <= 14) {
        a = 15;
    } else if (a >= 15 && a <= 29) {
        a = 30;
    } else if (a >= 30 && a <= 44) {
        a = 45;
    } else {
        a = 0;
    }
    return a;
}

uint8_t roundSec(uint8_t sec) {
    if (sec > 60) sec -= 60;
    return sec;
}

void SyncRtc() {
#ifdef syncRtcWithNtp
    // Setup time with NTPClient library.
    timeClient.begin();
    timeClient.forceUpdate();
    if (timeClient.isTimeSet()) {
        rtc.adjust(DateTime(timeClient.getEpochTime()));
        Serial.println("\nSetup time from NTP server succeeded.");
    } else {
        Serial.println("\nSetup time from NTP server failed.");
    }
#endif
}

void SetupAlarm() {
    if (!rtc.begin()) {
        deln("Couldn't find RTC!");
        // Serial.flush();
        // while (1) delay(10);
    }

    if (rtc.lostPower()) {
        rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));  // this will adjust to the date and time at compilation
    }

    SyncRtc();  // To ync RTC with NTP server. The internet connection is required.
    // January 21, 2014 at 3am you would call:
    // rtc.adjust(DateTime(2023, 12, 9, 21, 59, 35));  // Manually set time

    deln("\n\t" + strTime(rtc.now()));

    rtc.disable32K();  // we don't need the 32K Pin, so disable it
    rtc.clearAlarm(1);
    rtc.clearAlarm(2);
    rtc.writeSqwPinMode(DS3231_OFF);  // stop oscillating signals at SQW Pin, otherwise setAlarm1 will fail
    rtc.disableAlarm(2);

#ifdef _20SecTest
    // Set alarm time
    rtc.setAlarm1(rtc.now() + TimeSpan(0, 0, 0, 20), DS3231_A1_Second);  // Test
    de("Trigger next time: ");
    deln(String(roundSec(rtc.now().second() + 20)) + "th sec.");
#else
    tMin = rtc.now().minute();
    deln("tMin: " + String(tMin));
    int setMin = set15Min(tMin);
    rtc.setAlarm1(DateTime(2023, 2, 18, 0, setMin, 0), DS3231_A1_Minute);
    // rtc.setAlarm1(DateTime(2023, 2, 18, 0, 0, 0), DS3231_A1_Minute);
    // rtc.setAlarm2(DateTime(2023, 2, 18, 0, 30, 0), DS3231_A2_Minute);
    // rtc.setAlarm1(DateTime(2023, 2, 18, 0, 57, 0), DS3231_A1_Minute);
    // rtc.setAlarm2(DateTime(2023, 2, 18, 0, 27, 0), DS3231_A2_Minute);
    deln("Tringger next time: " + String(setMin) + "th min.");
#endif

    deln("\tThe alarm setting is done.");
}

void IRAM_ATTR onRtcTrigger() { rtcTrigger = true; }

// 15 minute match
bool t15MinMatch(int tMin) {
    switch (tMin) {
        case 0:
        case 15:
        case 30:
        case 45: return true; break;
        default: return false; break;
    }
}

//----------------- Collect Data --------------//
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

    de("Temp: " + String(Temperature) + ", Humi: " + String(Humidity) + ", press: " + String(Pressure));
    de(", GasResistance: " + String(GasResistance));
    de(", Illu: " + String(lux) + ", CO2: " + String(co2));
    deln(", pm1.0: " + String(pm_01) + ", pm2.5: " + String(pm_25) + ", pm10: " + String(pm_10));
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

    deln("Data sending is done.");
}

void IRAM_ATTR fetchData() {
    SetupAlarm();

#ifdef _20SecTest
    ReadData();
    SendData();
#else
    de("15min Match: ");
    deln(t15MinMatch(tMin) ? "true" : "false");

    if (t15MinMatch(tMin)) {
        ReadData();
        SendData();
        deln("\tdata reading is done.");
    } else {
        deln("\tread data next time.");
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
        de("Connecting MQTT... ");
        if (mqtt.connect(deviceName, mqtt_user, mqtt_pass)) {
            tReconnectMqtt.stop();
            deln("connected");
            tConnectMqtt.interval(0);
            tConnectMqtt.start();
            statusLed.blinkNumberOfTimes(200, 200, 3);  // 250ms ON, 750ms OFF, repeat 3 times, blink immediately
        } else {
            deln("failed state: " + String(mqtt.state()));
            deln("counter: " + String(tReconnectMqtt.counter()));
            if (tReconnectMqtt.counter() >= 3) {
                // ESP.restart();
                tReconnectMqtt.stop();
                tConnectMqtt.interval(300 * 1000);  // 300 sec. = 5 min.Wait 5 minute before reconnecting.
                tConnectMqtt.resume();
            }
        }
    } else {
        if (tReconnectMqtt.counter() <= 1) deln("WiFi is not connected");
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

//******************************** Setup  ***********************************//
void setup() {
    statusLed.turnOFF();
    Serial.begin(115200);
    Wire.begin();
    pinMode(SQW_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(SQW_PIN), onRtcTrigger, FALLING);
    resetWifiBt.begin();
    resetWifiBt.onPressedFor(5000, resetWifiBtPressed);

    // BME680
    iaqSensor.begin(BME68X_I2C_ADDR_LOW, Wire);
    iaqSensor.updateSubscription(sensorList, 13, BSEC_SAMPLE_RATE_LP);

    // VEML7700
    if (!veml.begin()) deln("VEML7700 can't be found!");

    // MH-Z19B Init
    mySerial.begin(9600);       // (Uno example) device to MH-Z19 serial start
    myMHZ19.begin(mySerial);    // *Serial(Stream) reference must be passed to library begin().
    myMHZ19.autoCalibration();  // Turn auto calibration ON (OFF autoCalibration(false))

    // PMSA003
    pms.init();

    wifiManagerSetup();
    OtaWebUpdateSetup();

    SetupAlarm();

    mqtt.setServer(mqtt_server, atoi(mqtt_port));
    tWifiDisconnectedDetect.start();
    tConnectMqtt.start();
}

//******************************** Loop *************************************//
void loop() {
    tWifiDisconnectedDetect.update();
    statusLed.loop();  // MUST call the led.loop() function in loop()
    resetWifiBt.read();
    server.handleClient();  // OTA Web Update
    tConnectMqtt.update();
    tReconnectMqtt.update();

    if (rtcTrigger) {
        rtcTrigger = false;
        fetchData();
    }
}
