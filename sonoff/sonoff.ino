/*
 * Sonoff-Tasmota by Theo Arends
 *
 * ====================================================
 * Prerequisites:
 *   - Change libraries/PubSubClient/src/PubSubClient.h
 *       #define MQTT_MAX_PACKET_SIZE 512
 *
 *   - Select IDE Tools - Flash size: "1M (64K SPIFFS)"
 * ====================================================
*/

//#define ALLOW_MIGRATE_TO_V3
#ifdef ALLOW_MIGRATE_TO_V3
  #define VERSION              0x03091D00   // 3.9.29
#else
  #define VERSION              0x04000600   // 4.0.6
#endif  // ALLOW_MIGRATE_TO_V3

enum log_t   {LOG_LEVEL_NONE, LOG_LEVEL_ERROR, LOG_LEVEL_INFO, LOG_LEVEL_DEBUG, LOG_LEVEL_DEBUG_MORE, LOG_LEVEL_ALL};
enum week_t  {Last, First, Second, Third, Fourth};
enum dow_t   {Sun=1, Mon, Tue, Wed, Thu, Fri, Sat};
enum month_t {Jan=1, Feb, Mar, Apr, May, Jun, Jul, Aug, Sep, Oct, Nov, Dec};
enum wifi_t  {WIFI_RESTART, WIFI_SMARTCONFIG, WIFI_MANAGER, WIFI_WPSCONFIG, WIFI_RETRY, MAX_WIFI_OPTION};
enum swtch_t {TOGGLE, FOLLOW, FOLLOW_INV, PUSHBUTTON, PUSHBUTTON_INV, MAX_SWITCH_OPTION};
enum led_t   {LED_OFF, LED_POWER, LED_MQTTSUB, LED_POWER_MQTTSUB, LED_MQTTPUB, LED_POWER_MQTTPUB, LED_MQTT, LED_POWER_MQTT, MAX_LED_OPTION};
enum emul_t  {EMUL_NONE, EMUL_WEMO, EMUL_HUE, EMUL_MAX};

#include "sonoff_template.h"

#include "user_config.h"
#include "user_config_override.h"

/*********************************************************************************************\
 * Enable feature by removing leading // or disable feature by adding leading //
\*********************************************************************************************/

//#define USE_SPIFFS                          // Switch persistent configuration from flash to spiffs (+24k code, +0.6k mem)

/*********************************************************************************************\
 * No user configurable items below
\*********************************************************************************************/

#define MODULE                 SONOFF_BASIC // [Module] Select default model

#define USE_DHT                             // Default DHT11 sensor needs no external library
#ifndef USE_DS18x20
#define USE_DS18B20                         // Default DS18B20 sensor needs no external library
#endif
//#define DEBUG_THEO                          // Add debug code

#ifndef SWITCH_MODE
#define SWITCH_MODE            TOGGLE       // TOGGLE, FOLLOW or FOLLOW_INV (the wall switch state)
#endif

#ifndef MQTT_FINGERPRINT
#define MQTT_FINGERPRINT       "A5 02 FF 13 99 9F 8B 39 8E F1 83 4F 11 23 65 0B 32 36 FC 07"
#endif

#ifndef WS2812_LEDS
#define WS2812_LEDS            30           // [Pixels] Number of LEDs
#endif

#define WIFI_HOSTNAME          "%s-%04d"    // Expands to <MQTT_TOPIC>-<last 4 decimal chars of MAC address>
#define CONFIG_FILE_SIGN       0xA5         // Configuration file signature
#define CONFIG_FILE_XOR        0x5A         // Configuration file xor (0 = No Xor)

#define HLW_PREF_PULSE         12530        // was 4975us = 201Hz = 1000W
#define HLW_UREF_PULSE         1950         // was 1666us = 600Hz = 220V
#define HLW_IREF_PULSE         3500         // was 1666us = 600Hz = 4.545A

#define MQTT_SUBTOPIC          "POWER"      // Default MQTT subtopic (POWER or LIGHT)
#define MQTT_RETRY_SECS        10           // Seconds to retry MQTT connection
#define APP_POWER              0            // Default saved power state Off
#define MAX_DEVICE             1            // Max number of devices
#define MAX_PULSETIMERS        4            // Max number of supported pulse timers
#define WS2812_MAX_LEDS        256          // Max number of LEDs

#define MAX_POWER_HOLD         10           // Time in SECONDS to allow max agreed power (Pow)
#define MAX_POWER_WINDOW       30           // Time in SECONDS to disable allow max agreed power (Pow)
#define SAFE_POWER_HOLD        10           // Time in SECONDS to allow max unit safe power (Pow)
#define SAFE_POWER_WINDOW      30           // Time in MINUTES to disable allow max unit safe power (Pow)
#define MAX_POWER_RETRY        5            // Retry count allowing agreed power limit overflow (Pow)

#define STATES                 10           // loops per second
#define SYSLOG_TIMER           600          // Seconds to restore syslog_level
#define SERIALLOG_TIMER        600          // Seconds to disable SerialLog
#define OTA_ATTEMPTS           10           // Number of times to try fetching the new firmware

#define INPUT_BUFFER_SIZE      100          // Max number of characters in serial buffer
#define TOPSZ                  60           // Max number of characters in topic string
#define LOGSZ                  128          // Max number of characters in log string
#ifdef USE_MQTT_TLS
  #define MAX_LOG_LINES        10           // Max number of lines in weblog
#else
  #define MAX_LOG_LINES        20           // Max number of lines in weblog
#endif

#define APP_BAUDRATE           115200       // Default serial baudrate
#define MAX_STATUS             11           // Max number of status lines

enum butt_t {PRESSED, NOT_PRESSED};

#include "support.h"                        // Global support
#include <PubSubClient.h>                   // MQTT

#define MESSZ                  360          // Max number of characters in JSON message string (4 x DS18x20 sensors)
#if (MQTT_MAX_PACKET_SIZE -TOPSZ -7) < MESSZ  // If the max message size is too small, throw an error at compile time
                                            // See pubsubclient.c line 359
  #error "MQTT_MAX_PACKET_SIZE is too small in libraries/PubSubClient/src/PubSubClient.h, increase it to at least 427"
#endif

#include <Ticker.h>                         // RTC, HLW8012, OSWatch
#include <ESP8266WiFi.h>                    // MQTT, Ota, WifiManager
#include <ESP8266HTTPClient.h>              // MQTT, Ota
#include <ESP8266httpUpdate.h>              // Ota
#include <ArduinoJson.h>                    // WemoHue, IRremote, Domoticz
#ifdef USE_WEBSERVER
  #include <ESP8266WebServer.h>             // WifiManager, Webserver
  #include <DNSServer.h>                    // WifiManager
#endif  // USE_WEBSERVER
#ifdef USE_DISCOVERY
  #include <ESP8266mDNS.h>                  // MQTT, Webserver
#endif  // USE_DISCOVERY
#ifdef USE_SPIFFS
  #include <FS.h>                           // Config
#endif  // USE_SPIFFS
#ifdef USE_I2C
  #include <Wire.h>                         // I2C support library
#endif  // USE_I2C
#include "settings.h"

typedef void (*rtcCallback)();

extern "C" uint32_t _SPIFFS_start;
extern "C" uint32_t _SPIFFS_end;

#define MAX_BUTTON_COMMANDS    5            // Max number of button commands supported
const char commands[MAX_BUTTON_COMMANDS][14] PROGMEM = {
  {"wificonfig 1"},   // Press button three times
  {"wificonfig 2"},   // Press button four times
  {"wificonfig 3"},   // Press button five times
  {"restart 1"},      // Press button six times
  {"upgrade 1"}};     // Press button seven times

const char wificfg[5][12] PROGMEM = { "Restart", "Smartconfig", "Wifimanager", "WPSconfig", "Retry" };

struct TIME_T {
  uint8_t       Second;
  uint8_t       Minute;
  uint8_t       Hour;
  uint8_t       Wday;      // day of week, sunday is day 1
  uint8_t       Day;
  uint8_t       Month;
  char          MonthName[4];
  uint16_t      DayOfYear;
  uint16_t      Year;
  unsigned long Valid;
} rtcTime;

struct TimeChangeRule
{
  uint8_t       week;      // 1=First, 2=Second, 3=Third, 4=Fourth, or 0=Last week of the month
  uint8_t       dow;       // day of week, 1=Sun, 2=Mon, ... 7=Sat
  uint8_t       month;     // 1=Jan, 2=Feb, ... 12=Dec
  uint8_t       hour;      // 0-23
  int           offset;    // offset from UTC in minutes
};

TimeChangeRule myDST = { TIME_DST };  // Daylight Saving Time
TimeChangeRule mySTD = { TIME_STD };  // Standard Time

#ifdef USE_STATIC_IP_ADDRESS
const uint8_t ipadd[4] = { WIFI_IP_ADDRESS }; // Static ip
const uint8_t ipgat[4] = { WIFI_GATEWAY };    // Local router gateway ip
const uint8_t ipdns[4] = { WIFI_DNS };        // DNS ip
const uint8_t ipsub[4] = { WIFI_SUBNETMASK }; // Subnetmask
#endif  // USE_STATIC_IP_ADDRESS

int Baudrate = APP_BAUDRATE;          // Serial interface baud rate
byte SerialInByte;                    // Received byte
int SerialInByteCounter = 0;          // Index in receive buffer
char serialInBuf[INPUT_BUFFER_SIZE + 2];  // Receive buffer
byte Hexcode = 0;                     // Sonoff dual input flag
uint16_t ButtonCode = 0;              // Sonoff dual received code
int16_t savedatacounter;              // Counter and flag for config save to Flash or Spiffs
char Version[16];                     // Version string from VERSION define
char Hostname[33];                    // Composed Wifi hostname
char MQTTClient[33];                  // Composed MQTT Clientname
uint8_t mqttcounter = 0;              // MQTT connection retry counter
unsigned long timerxs = 0;            // State loop timer
int state = 0;                        // State per second flag
int mqttflag = 2;                     // MQTT connection messages flag
int otaflag = 0;                      // OTA state flag
int otaok = 0;                        // OTA result
byte otaretry = OTA_ATTEMPTS;         // OTA retry counter
int restartflag = 0;                  // Sonoff restart flag
int wificheckflag = WIFI_RESTART;     // Wifi state flag
int uptime = 0;                       // Current uptime in hours
int tele_period = 0;                  // Tele period timer
String Log[MAX_LOG_LINES];            // Web log buffer
byte logidx = 0;                      // Index in Web log buffer
byte logajaxflg = 0;                  // Reset web console log
byte Maxdevice = MAX_DEVICE;          // Max number of devices supported
int status_update_timer = 0;          // Refresh initial status
uint16_t pulse_timer[MAX_PULSETIMERS] = { 0 }; // Power off timer
uint16_t blink_timer = 0;             // Power cycle timer
uint16_t blink_counter = 0;           // Number of blink cycles
uint8_t blink_power;                  // Blink power state
uint8_t blink_mask = 0;               // Blink relay active mask
uint8_t blink_powersave;              // Blink start power save state
uint16_t mqtt_cmnd_publish = 0;       // ignore flag for publish command
uint8_t latching_power = 0;           // Power state at latching start
uint8_t latching_relay_pulse = 0;     // Latching relay pulse timer

#ifdef USE_MQTT_TLS
  WiFiClientSecure espClient;         // Wifi Secure Client
#else
  WiFiClient espClient;               // Wifi Client
#endif
PubSubClient mqttClient(espClient);   // MQTT Client
WiFiUDP portUDP;                      // UDP Syslog and Alexa

uint8_t power;                        // Current copy of sysCfg.power
byte syslog_level;                    // Current copy of sysCfg.syslog_level
uint16_t syslog_timer = 0;            // Timer to re-enable syslog_level
byte seriallog_level;                 // Current copy of sysCfg.seriallog_level
uint16_t seriallog_timer = 0;         // Timer to disable Seriallog
uint8_t sleep;                        // Current copy of sysCfg.sleep

int blinks = 201;                     // Number of LED blinks
uint8_t blinkstate = 0;               // LED state

uint8_t lastbutton[4] = { NOT_PRESSED, NOT_PRESSED, NOT_PRESSED, NOT_PRESSED };     // Last button states
uint8_t holdcount = 0;                // Timer recording button hold
uint8_t multiwindow = 0;              // Max time between button presses to record press count
uint8_t multipress = 0;               // Number of button presses within multiwindow
uint8_t lastwallswitch[4];            // Last wall switch states

mytmplt my_module;                    // Active copy of GPIOs
uint8_t pin[GPIO_MAX];                // Possible pin configurations
uint8_t rel_inverted[4] = { 0 };      // Relay inverted flag (1 = (0 = On, 1 = Off))
uint8_t led_inverted[4] = { 0 };      // LED inverted flag (1 = (0 = On, 1 = Off))
uint8_t swt_flg = 0;                  // Any external switch configured
uint8_t dht_type = 0;                 // DHT type (DHT11, DHT21 or DHT22)
uint8_t hlw_flg = 0;                  // Power monitor configured
uint8_t i2c_flg = 0;                  // I2C configured

boolean mDNSbegun = false;

/********************************************************************************************/

void getClient(char* output, const char* input, byte size)
{
  char *token;
  uint8_t digits = 0;

  if (strstr(input, "%")) {
    strlcpy(output, input, size);
    token = strtok(output, "%");
    if (strstr(input, "%") == input) {
      output[0] = '\0';
    } else {
      token = strtok(NULL, "");
    }
    if (token != NULL) {
      digits = atoi(token);
      if (digits) {
        snprintf_P(output, size, PSTR("%s%c0%dX"), output, '%', digits);
        snprintf_P(output, size, output, ESP.getChipId());
      }
    }
  }
  if (!digits) strlcpy(output, input, size);
}

void setLatchingRelay(uint8_t power, uint8_t state)
{
  power &= 1;
  if (state == 2) {           // Reset relay
    state = 0;
    latching_power = power;
    latching_relay_pulse = 0;
  }
  else if (state && !latching_relay_pulse) {  // Set port power to On
    latching_power = power;
    latching_relay_pulse = 2; // max 200mS (initiated by stateloop())
  }
  if (pin[GPIO_REL1 +latching_power] < 99) digitalWrite(pin[GPIO_REL1 +latching_power], rel_inverted[latching_power] ? !state : state);
}

void setRelay(uint8_t power)
{
  uint8_t state;
  
  if ((sysCfg.module == SONOFF_DUAL) || (sysCfg.module == CH4)) {
    Serial.write(0xA0);
    Serial.write(0x04);
    Serial.write(power);
    Serial.write(0xA1);
    Serial.write('\n');
    Serial.flush();
  }
  else if (sysCfg.module == SONOFF_LED) {
    sl_setPower(power &1);
  }
  else if (sysCfg.module == EXS_RELAY) {
    setLatchingRelay(power, 1);
  }
  else {
    for (byte i = 0; i < Maxdevice; i++) {
      state = power &1;
      if (pin[GPIO_REL1 +i] < 99) digitalWrite(pin[GPIO_REL1 +i], rel_inverted[i] ? !state : state);
      power >>= 1;
    }
  }
  hlw_setPowerSteadyCounter(2);
}

void setLed(uint8_t state)
{
  if (state) state = 1;
  digitalWrite(pin[GPIO_LED1], (led_inverted[0]) ? !state : state);
}

/********************************************************************************************/

void json2legacy(char* stopic, char* svalue)
{
  char *p, *token;
  uint16_t i, j;

  if (!strstr(svalue, "{\"")) return;  // No JSON

// stopic = stat/sonoff/RESULT
// svalue = {"POWER2":"ON"}
// --> stopic = "stat/sonoff/POWER2", svalue = "ON"
// svalue = {"Upgrade":{"Version":"2.1.2", "OtaUrl":"%s"}}
// --> stopic = "stat/sonoff/UPGRADE", svalue = "2.1.2"
// svalue = {"SerialLog":2}
// --> stopic = "stat/sonoff/SERIALLOG", svalue = "2"
// svalue = {"POWER":""}
// --> stopic = "stat/sonoff/POWER", svalue = ""

  token = strtok(svalue, "{\"");      // Topic
  p = strrchr(stopic, '/') +1;
  i = p - stopic;
  for (j = 0; j < strlen(token)+1; j++) stopic[i+j] = toupper(token[j]);
  token = strtok(NULL, "\"");         // : or :3} or :3, or :{
  if (strstr(token, ":{")) {
    token = strtok(NULL, "\"");       // Subtopic
    token = strtok(NULL, "\"");       // : or :3} or :3,
  }
  if (strlen(token) > 1) {
    token++;
    p = strchr(token, ',');
    if (!p) p = strchr(token, '}');
    i = p - token;
    token[i] = '\0';                  // Value
  } else {
    token = strtok(NULL, "\"");       // Value or , or }
    if ((token[0] == ',') || (token[0] == '}')) {  // Empty parameter
      token = NULL;
    }
  }
  if (token == NULL) {
    svalue[0] = '\0';
  } else {
    memcpy(svalue, token, strlen(token)+1);
  }
}

/********************************************************************************************/

void mqtt_publish_sec(const char* topic, const char* data, boolean retained)
{
  char log[TOPSZ + MESSZ];

  if (sysCfg.mqtt_enabled) {
    if (mqttClient.publish(topic, data, retained)) {
      snprintf_P(log, sizeof(log), PSTR("MQTT: %s = %s%s"), topic, data, (retained) ? " (retained)" : "");
//      mqttClient.loop();  // Do not use here! Will block previous publishes
    } else  {
      snprintf_P(log, sizeof(log), PSTR("RSLT: %s = %s"), topic, data);
    }
  } else {
    snprintf_P(log, sizeof(log), PSTR("RSLT: %s = %s"), strrchr(topic,'/')+1, data);
  }

  addLog(LOG_LEVEL_INFO, log);
  if (sysCfg.ledstate &0x04) blinks++;
}

void mqtt_publish(const char* topic, const char* data, boolean retained)
{
  char *me;

  if (!strcmp(SUB_PREFIX,PUB_PREFIX)) {
    me = strstr(topic,SUB_PREFIX);
    if (me == topic) mqtt_cmnd_publish += 8;
  }
  mqtt_publish_sec(topic, data, retained);
}

void mqtt_publish(const char* topic, const char* data)
{
  mqtt_publish(topic, data, false);
}

void mqtt_publish_topic_P(uint8_t prefix, const char* subtopic, const char* data)
{
  char romram[16], stopic[TOPSZ];  

  snprintf_P(romram, sizeof(romram), subtopic);
  snprintf_P(stopic, sizeof(stopic), PSTR("%s/%s/%s"), (prefix) ? PUB_PREFIX2 : PUB_PREFIX, sysCfg.mqtt_topic, romram);
  mqtt_publish(stopic, data);
}

void mqtt_publishPowerState(byte device)
{
  char stopic[TOPSZ], sdevice[10], svalue[64];  // was MESSZ

  if ((device < 1) || (device > Maxdevice)) device = 1;
  snprintf_P(sdevice, sizeof(sdevice), PSTR("%d"), device);
  snprintf_P(stopic, sizeof(stopic), PSTR("%s/%s/RESULT"), PUB_PREFIX, sysCfg.mqtt_topic);
  snprintf_P(svalue, sizeof(svalue), PSTR("{\"%s%s\":\"%s\"}"),
    sysCfg.mqtt_subtopic, (Maxdevice > 1) ? sdevice : "", (power & (0x01 << (device -1))) ? MQTT_STATUS_ON : MQTT_STATUS_OFF);
  mqtt_publish(stopic, svalue);
  json2legacy(stopic, svalue);
  mqtt_publish(stopic, svalue, sysCfg.mqtt_power_retain);
}

void mqtt_publishPowerBlinkState(byte device)
{
  char sdevice[10], svalue[64];  // was MESSZ

  if ((device < 1) || (device > Maxdevice)) device = 1;
  snprintf_P(sdevice, sizeof(sdevice), PSTR("%d"), device);
  snprintf_P(svalue, sizeof(svalue), PSTR("{\"%s%s\":\"BLINK %s\"}"),
    sysCfg.mqtt_subtopic, (Maxdevice > 1) ? sdevice : "", (blink_mask & (0x01 << (device -1))) ? MQTT_STATUS_ON : MQTT_STATUS_OFF);
  mqtt_publish_topic_P(0, PSTR("RESULT"), svalue);
}

void mqtt_connected()
{
  char stopic[TOPSZ], svalue[128];  // was MESSZ

  if (sysCfg.mqtt_enabled) {

    // Satisfy iobroker (#299)
    snprintf_P(stopic, sizeof(stopic), PSTR("%s/%s/POWER"), SUB_PREFIX, sysCfg.mqtt_topic);
    svalue[0] ='\0';
    mqtt_publish(stopic, svalue);
    
    snprintf_P(stopic, sizeof(stopic), PSTR("%s/%s/#"), SUB_PREFIX, sysCfg.mqtt_topic);
    mqttClient.subscribe(stopic);
    mqttClient.loop();  // Solve LmacRxBlk:1 messages
    snprintf_P(stopic, sizeof(stopic), PSTR("%s/%s/#"), SUB_PREFIX, sysCfg.mqtt_grptopic);
    mqttClient.subscribe(stopic);
    mqttClient.loop();  // Solve LmacRxBlk:1 messages
    snprintf_P(stopic, sizeof(stopic), PSTR("%s/%s/#"), SUB_PREFIX, MQTTClient); // Fall back topic
    mqttClient.subscribe(stopic);
    mqttClient.loop();  // Solve LmacRxBlk:1 messages
#ifdef USE_DOMOTICZ
    domoticz_mqttSubscribe();
#endif  // USE_DOMOTICZ
  }

  if (mqttflag) {
    snprintf_P(svalue, sizeof(svalue), PSTR("{\"Module\":\"%s\", \"Version\":\"%s\", \"FallbackTopic\":\"%s\", \"GroupTopic\":\"%s\"}"),
      my_module.name, Version, MQTTClient, sysCfg.mqtt_grptopic);
    mqtt_publish_topic_P(1, PSTR("INFO1"), svalue);
#ifdef USE_WEBSERVER
    if (sysCfg.webserver) {
      snprintf_P(svalue, sizeof(svalue), PSTR("{\"WebserverMode\":\"%s\", \"Hostname\":\"%s\", \"IPaddress\":\"%s\"}"),
        (sysCfg.webserver == 2) ? "Admin" : "User", Hostname, WiFi.localIP().toString().c_str());
      mqtt_publish_topic_P(1, PSTR("INFO2"), svalue);
    }
#endif  // USE_WEBSERVER
    snprintf_P(svalue, sizeof(svalue), PSTR("{\"Started\":\"%s\"}"),
      (getResetReason() == "Exception") ? ESP.getResetInfo().c_str() : getResetReason().c_str());
    mqtt_publish_topic_P(1, PSTR("INFO3"), svalue);
    if (!spiffsPresent()) {
      snprintf_P(svalue, sizeof(svalue), PSTR("{\"Warning1\":\"No persistent config. Please reflash with at least 16K SPIFFS\"}"));
      mqtt_publish_topic_P(1, PSTR("WARNING1"), svalue);
    }
    if (sysCfg.tele_period) tele_period = sysCfg.tele_period -9;
    status_update_timer = 2;
#ifdef USE_DOMOTICZ
    domoticz_setUpdateTimer(2);
#endif  // USE_DOMOTICZ
  }
  mqttflag = 0;
}

void mqtt_reconnect()
{
  char stopic[TOPSZ], svalue[TOPSZ], log[LOGSZ];

  mqttcounter = MQTT_RETRY_SECS;

  if (!sysCfg.mqtt_enabled) {
    mqtt_connected();
    return;
  }
#ifdef USE_EMULATION
  UDP_Disconnect();
#endif  // USE_EMULATION
  if (mqttflag > 1) {
#ifdef USE_MQTT_TLS
    addLog_P(LOG_LEVEL_INFO, PSTR("MQTT: Verify TLS fingerprint..."));
    if (!espClient.connect(sysCfg.mqtt_host, sysCfg.mqtt_port)) {
      snprintf_P(log, sizeof(log), PSTR("MQTT: TLS CONNECT FAILED USING WRONG MQTTHost (%s) or MQTTPort (%d). Retry in %d seconds"),
        sysCfg.mqtt_host, sysCfg.mqtt_port, mqttcounter);
      addLog(LOG_LEVEL_DEBUG, log);
      return;
    }
    if (espClient.verify(sysCfg.mqtt_fingerprint, sysCfg.mqtt_host)) {
      addLog_P(LOG_LEVEL_INFO, PSTR("MQTT: Verified"));
    } else {
      addLog_P(LOG_LEVEL_DEBUG, PSTR("MQTT: WARNING - Insecure connection due to invalid Fingerprint"));
    }
#endif  // USE_MQTT_TLS
    mqttClient.setCallback(mqttDataCb);
    mqttflag = 1;
    mqttcounter = 1;
    return;
  }

  addLog_P(LOG_LEVEL_INFO, PSTR("MQTT: Attempting connection..."));
#ifndef USE_MQTT_TLS
#ifdef USE_DISCOVERY
#ifdef MQTT_HOST_DISCOVERY
  mdns_discoverMQTTServer();
#endif  // MQTT_HOST_DISCOVERY
#endif  // USE_DISCOVERY
#endif  // USE_MQTT_TLS
  mqttClient.setServer(sysCfg.mqtt_host, sysCfg.mqtt_port);
  snprintf_P(stopic, sizeof(stopic), PSTR("%s/%s/LWT"), PUB_PREFIX2, sysCfg.mqtt_topic);
  snprintf_P(svalue, sizeof(svalue), PSTR("Offline"));
  if (mqttClient.connect(MQTTClient, sysCfg.mqtt_user, sysCfg.mqtt_pwd, stopic, 1, true, svalue)) {
    addLog_P(LOG_LEVEL_INFO, PSTR("MQTT: Connected"));
    mqttcounter = 0;
    snprintf_P(svalue, sizeof(svalue), PSTR("Online"));
    mqtt_publish(stopic, svalue, true);
    mqtt_connected();
  } else {
    snprintf_P(log, sizeof(log), PSTR("MQTT: Connect FAILED to %s:%d, rc %d. Retry in %d seconds"),
      sysCfg.mqtt_host, sysCfg.mqtt_port, mqttClient.state(), mqttcounter);  //status codes are documented here http://pubsubclient.knolleary.net/api.html#state
    addLog(LOG_LEVEL_INFO, log);
  }
}

/********************************************************************************************/

boolean mqtt_command(boolean grpflg, char *type, uint16_t index, char *dataBuf, uint16_t data_len, int16_t payload, char *svalue, uint16_t ssvalue)
{
  boolean serviced = true;
  char stemp1[TOPSZ], stemp2[10];
  uint16_t i;
  
  if (!strcmp(type,"MQTTHOST")) {
    if ((data_len > 0) && (data_len < sizeof(sysCfg.mqtt_host))) {
      strlcpy(sysCfg.mqtt_host, (payload == 1) ? MQTT_HOST : dataBuf, sizeof(sysCfg.mqtt_host));
      restartflag = 2;
    }
    snprintf_P(svalue, ssvalue, PSTR("{\"MqttHost\",\"%s\"}"), sysCfg.mqtt_host);
  }
  else if (!strcmp(type,"MQTTPORT")) {
    if ((data_len > 0) && (payload > 0) && (payload < 32766)) {
      sysCfg.mqtt_port = (payload == 1) ? MQTT_PORT : payload;
      restartflag = 2;
    }
    snprintf_P(svalue, ssvalue, PSTR("{\"MqttPort\":%d}"), sysCfg.mqtt_port);
  }
#ifdef USE_MQTT_TLS
  else if (!strcmp(type,"MQTTFINGERPRINT")) {
    if ((data_len > 0) && (data_len < sizeof(sysCfg.mqtt_fingerprint))) {
      strlcpy(sysCfg.mqtt_fingerprint, (payload == 1) ? MQTT_FINGERPRINT : dataBuf, sizeof(sysCfg.mqtt_fingerprint));
      restartflag = 2;
    }
    snprintf_P(svalue, ssvalue, PSTR("{\"MqttFingerprint\":\"%s\"}"), sysCfg.mqtt_fingerprint);
  }
#endif
  else if (!grpflg && !strcmp(type,"MQTTCLIENT")) {
    if ((data_len > 0) && (data_len < sizeof(sysCfg.mqtt_client))) {
      strlcpy(sysCfg.mqtt_client, (payload == 1) ? MQTT_CLIENT_ID : dataBuf, sizeof(sysCfg.mqtt_client));
      restartflag = 2;
    }
    snprintf_P(svalue, ssvalue, PSTR("{\"MqttClient\":\"%s\"}"), sysCfg.mqtt_client);
  }
  else if (!strcmp(type,"MQTTUSER")) {
    if ((data_len > 0) && (data_len < sizeof(sysCfg.mqtt_user))) {
      strlcpy(sysCfg.mqtt_user, (payload == 1) ? MQTT_USER : dataBuf, sizeof(sysCfg.mqtt_user));
      restartflag = 2;
    }
    snprintf_P(svalue, ssvalue, PSTR("[\"MqttUser\":\"%s\"}"), sysCfg.mqtt_user);
  }
  else if (!strcmp(type,"MQTTPASSWORD")) {
    if ((data_len > 0) && (data_len < sizeof(sysCfg.mqtt_pwd))) {
      strlcpy(sysCfg.mqtt_pwd, (payload == 1) ? MQTT_PASS : dataBuf, sizeof(sysCfg.mqtt_pwd));
      restartflag = 2;
    }
    snprintf_P(svalue, ssvalue, PSTR("{\"MqttPassword\":\"%s\"}"), sysCfg.mqtt_pwd);
  }
  else if (!strcmp(type,"GROUPTOPIC")) {
    if ((data_len > 0) && (data_len < sizeof(sysCfg.mqtt_grptopic))) {
      for(i = 0; i <= data_len; i++)
        if ((dataBuf[i] == '/') || (dataBuf[i] == '+') || (dataBuf[i] == '#')) dataBuf[i] = '_';
      if (!strcmp(dataBuf, MQTTClient)) payload = 1;
      strlcpy(sysCfg.mqtt_grptopic, (payload == 1) ? MQTT_GRPTOPIC : dataBuf, sizeof(sysCfg.mqtt_grptopic));
      restartflag = 2;
    }
    snprintf_P(svalue, ssvalue, PSTR("{\"GroupTopic\":\"%s\"}"), sysCfg.mqtt_grptopic);
  }
  else if (!grpflg && !strcmp(type,"TOPIC")) {
    if ((data_len > 0) && (data_len < sizeof(sysCfg.mqtt_topic))) {
      for(i = 0; i <= data_len; i++)
        if ((dataBuf[i] == '/') || (dataBuf[i] == '+') || (dataBuf[i] == '#') || (dataBuf[i] == ' ')) dataBuf[i] = '_';
      if (!strcmp(dataBuf, MQTTClient)) payload = 1;
      strlcpy(sysCfg.mqtt_topic, (payload == 1) ? MQTT_TOPIC : dataBuf, sizeof(sysCfg.mqtt_topic));
      restartflag = 2;
    }
    snprintf_P(svalue, ssvalue, PSTR("{\"Topic\":\"%s\"}"), sysCfg.mqtt_topic);
  }
  else if (!grpflg && !strcmp(type,"BUTTONTOPIC")) {
    if ((data_len > 0) && (data_len < sizeof(sysCfg.button_topic))) {
      for(i = 0; i <= data_len; i++)
        if ((dataBuf[i] == '/') || (dataBuf[i] == '+') || (dataBuf[i] == '#') || (dataBuf[i] == ' ')) dataBuf[i] = '_';
      if (!strcmp(dataBuf, MQTTClient)) payload = 1;
      strlcpy(sysCfg.button_topic, (payload == 1) ? sysCfg.mqtt_topic : dataBuf, sizeof(sysCfg.button_topic));
    }
    snprintf_P(svalue, ssvalue, PSTR("{\"ButtonTopic\":\"%s\"}"), sysCfg.button_topic);
  }
  else if (!grpflg && !strcmp(type,"SWITCHTOPIC")) {
    if ((data_len > 0) && (data_len < sizeof(sysCfg.switch_topic))) {
      for(i = 0; i <= data_len; i++)
        if ((dataBuf[i] == '/') || (dataBuf[i] == '+') || (dataBuf[i] == '#') || (dataBuf[i] == ' ')) dataBuf[i] = '_';
      if (!strcmp(dataBuf, MQTTClient)) payload = 1;
      strlcpy(sysCfg.switch_topic, (payload == 1) ? sysCfg.mqtt_topic : dataBuf, sizeof(sysCfg.switch_topic));
    }
    snprintf_P(svalue, ssvalue, PSTR("{\"SwitchTopic\":\"%s\"}"), sysCfg.switch_topic);
  }
  else if (!strcmp(type,"BUTTONRETAIN")) {
    if ((data_len > 0) && (payload >= 0) && (payload <= 1)) {
      strlcpy(sysCfg.button_topic, sysCfg.mqtt_topic, sizeof(sysCfg.button_topic));
      if (!payload) {
        for(i = 1; i <= Maxdevice; i++) {
          send_button_power(0, i, 3);  // Clear MQTT retain in broker
        }
      }
      sysCfg.mqtt_button_retain = payload;
    }
    snprintf_P(svalue, ssvalue, PSTR("{\"ButtonRetain\":\"%s\"}"), (sysCfg.mqtt_button_retain) ? MQTT_STATUS_ON : MQTT_STATUS_OFF);
  }
  else if (!strcmp(type,"SWITCHRETAIN")) {
    if ((data_len > 0) && (payload >= 0) && (payload <= 1)) {
        strlcpy(sysCfg.button_topic, sysCfg.mqtt_topic, sizeof(sysCfg.button_topic));
      if (!payload) {
        for(i = 1; i <= 4; i++) {
          send_button_power(1, i, 3);  // Clear MQTT retain in broker
        }
      }
      sysCfg.mqtt_switch_retain = payload;
    }
    snprintf_P(svalue, ssvalue, PSTR("{\"SwitchRetain\":\"%s\"}"), (sysCfg.mqtt_switch_retain) ? MQTT_STATUS_ON : MQTT_STATUS_OFF);
  }
  else if (!strcmp(type,"POWERRETAIN") || !strcmp(type,"LIGHTRETAIN")) {
    if ((data_len > 0) && (payload >= 0) && (payload <= 1)) {
      if (!payload) {
        for(i = 1; i <= Maxdevice; i++) {  // Clear MQTT retain in broker
          snprintf_P(stemp2, sizeof(stemp2), PSTR("%d"), i);
          snprintf_P(stemp1, sizeof(stemp1), PSTR("%s/%s/POWER%s"), PUB_PREFIX, sysCfg.mqtt_topic, (Maxdevice > 1) ? stemp2 : "");
          mqtt_publish(stemp1, "", sysCfg.mqtt_power_retain);
          snprintf_P(stemp1, sizeof(stemp1), PSTR("%s/%s/LIGHT%s"), PUB_PREFIX, sysCfg.mqtt_topic, (Maxdevice > 1) ? stemp2 : "");
          mqtt_publish(stemp1, "", sysCfg.mqtt_power_retain);
        }
      }
      sysCfg.mqtt_power_retain = payload;
    }
    snprintf_P(stemp1, sizeof(stemp1), PSTR("%s"), (!strcmp(sysCfg.mqtt_subtopic,"POWER")) ? "Power" : "Light");
    snprintf_P(svalue, ssvalue, PSTR("{\"%sRetain\":\"%s\"}"), stemp1, (sysCfg.mqtt_power_retain) ? MQTT_STATUS_ON : MQTT_STATUS_OFF);
  }
#ifdef USE_DOMOTICZ
  else if (domoticz_command(type, index, dataBuf, data_len, payload, svalue, ssvalue)) {
    // Serviced
  }
#endif  // USE_DOMOTICZ
  else {
    serviced = false;
  }
  return serviced;
}

/********************************************************************************************/

void mqttDataCb(char* topic, byte* data, unsigned int data_len)
{
  char *str;

  if (!strcmp(SUB_PREFIX,PUB_PREFIX)) {
    str = strstr(topic,SUB_PREFIX);
    if ((str == topic) && mqtt_cmnd_publish) {
      if (mqtt_cmnd_publish > 8) mqtt_cmnd_publish -= 8; else mqtt_cmnd_publish = 0;
      return;
    }
  }

  char topicBuf[TOPSZ], dataBuf[data_len+1], dataBufUc[128], svalue[MESSZ], stemp1[TOPSZ];
  char *p, *mtopic = NULL, *type = NULL;
  uint16_t i = 0, grpflg = 0, index;

  strncpy(topicBuf, topic, sizeof(topicBuf));
  memcpy(dataBuf, data, sizeof(dataBuf));
  dataBuf[sizeof(dataBuf)-1] = 0;

  snprintf_P(svalue, sizeof(svalue), PSTR("RSLT: Receive topic %s, data size %d, data %s"), topicBuf, data_len, dataBuf);
  addLog(LOG_LEVEL_DEBUG_MORE, svalue);
//  if (LOG_LEVEL_DEBUG_MORE <= seriallog_level) Serial.println(dataBuf);

#ifdef USE_DOMOTICZ
  if (sysCfg.mqtt_enabled) {
    if (domoticz_mqttData(topicBuf, sizeof(topicBuf), dataBuf, sizeof(dataBuf))) return;
  }
#endif  // USE_DOMOTICZ

  memmove(topicBuf, topicBuf+sizeof(SUB_PREFIX), sizeof(topicBuf)-sizeof(SUB_PREFIX));  // Remove SUB_PREFIX

  i = 0;
  for (str = strtok_r(topicBuf, "/", &p); str && i < 2; str = strtok_r(NULL, "/", &p)) {
    switch (i++) {
    case 0:  // Topic / GroupTopic / DVES_123456
      mtopic = str;
      break;
    case 1:  // TopicIndex / Text
      type = str;
    }
  }
  if (!strcmp(mtopic, sysCfg.mqtt_grptopic)) grpflg = 1;

  index = 1;
  if (type != NULL) {
    for (i = 0; i < strlen(type); i++) type[i] = toupper(type[i]);
    while (isdigit(type[i-1])) i--;
    if (i < strlen(type)) index = atoi(type +i);
    type[i] = '\0';
  }

  for (i = 0; i <= sizeof(dataBufUc); i++) dataBufUc[i] = toupper(dataBuf[i]);

  snprintf_P(svalue, sizeof(svalue), PSTR("RSLT: DataCb Topic %s, Group %d, Index %d, Type %s, Data %s (%s)"),
    mtopic, grpflg, index, type, dataBuf, dataBufUc);
  addLog(LOG_LEVEL_DEBUG, svalue);

//  snprintf_P(stopic, sizeof(stopic), PSTR("%s/%s/RESULT"), PUB_PREFIX, sysCfg.mqtt_topic);
  if (type != NULL) {
    snprintf_P(svalue, sizeof(svalue), PSTR("{\"Command\":\"Error\"}"));
    if (sysCfg.ledstate &0x02) blinks++;

    if (!strcmp(dataBufUc,"?")) data_len = 0;
    int16_t payload = atoi(dataBuf);     // -32766 - 32767
    uint16_t payload16 = atoi(dataBuf);  // 0 - 65535
    if (!strcmp(dataBufUc,"OFF") || !strcmp(dataBufUc,"FALSE") || !strcmp(dataBufUc,"STOP")) payload = 0;
    if (!strcmp(dataBufUc,"ON") || !strcmp(dataBufUc,"TRUE") || !strcmp(dataBufUc,"START") || !strcmp(dataBufUc,"USER")) payload = 1;
    if (!strcmp(dataBufUc,"TOGGLE") || !strcmp(dataBufUc,"ADMIN")) payload = 2;
    if (!strcmp(dataBufUc,"BLINK")) payload = 3;
    if (!strcmp(dataBufUc,"BLINKOFF")) payload = 4;

    if ((!strcmp(type,"POWER") || !strcmp(type,"LIGHT")) && (index > 0) && (index <= Maxdevice)) {
      snprintf_P(sysCfg.mqtt_subtopic, sizeof(sysCfg.mqtt_subtopic), PSTR("%s"), type);
      if ((data_len == 0) || (payload > 4)) payload = 9;
      do_cmnd_power(index, payload);
      return;
    }
    else if (!strcmp(type,"STATUS")) {
      if ((data_len == 0) || (payload < 0) || (payload > MAX_STATUS)) payload = 99;
      publish_status(payload);
      return;
    }
    else if ((sysCfg.module != MOTOR) && !strcmp(type,"POWERONSTATE")) {
      if ((data_len > 0) && (payload >= 0) && (payload <= 3)) {
        sysCfg.poweronstate = payload;
      }
      snprintf_P(svalue, sizeof(svalue), PSTR("{\"PowerOnState\":%d}"), sysCfg.poweronstate);
    }
    else if (!strcmp(type,"PULSETIME") && (index > 0) && (index <= MAX_PULSETIMERS)) {
      if (data_len > 0) {
        sysCfg.pulsetime[index -1] = payload16;  // 0 - 65535
        pulse_timer[index -1] = 0;
      }
      snprintf_P(svalue, sizeof(svalue), PSTR("{\"PulseTime%d\":%d}"), index, sysCfg.pulsetime[index -1]);
    }
    else if (!strcmp(type,"BLINKTIME")) {
      if ((data_len > 0) && (payload > 2) && (payload <= 3600)) {
        sysCfg.blinktime = payload;
        if (blink_timer) blink_timer = sysCfg.blinktime;
      }
      snprintf_P(svalue, sizeof(svalue), PSTR("{\"BlinkTime\":%d}"), sysCfg.blinktime);
    }
    else if (!strcmp(type,"BLINKCOUNT")) {
      if (data_len > 0) {
        sysCfg.blinkcount = payload16;  // 0 - 65535
        if (blink_counter) blink_counter = sysCfg.blinkcount *2;
      }
      snprintf_P(svalue, sizeof(svalue), PSTR("{\"BlinkCount\":%d}"), sysCfg.blinkcount);
    }
    else if ((sysCfg.module == SONOFF_LED) && sl_command(type, index, dataBufUc, data_len, payload, svalue, sizeof(svalue))) {
      // Serviced
    }
    else if (!strcmp(type,"SAVEDATA")) {
      if ((data_len > 0) && (payload >= 0) && (payload <= 3600)) {
        sysCfg.savedata = payload;
        savedatacounter = sysCfg.savedata;
      }
      if (sysCfg.savestate) sysCfg.power = power;
      CFG_Save();
      if (sysCfg.savedata > 1) snprintf_P(stemp1, sizeof(stemp1), PSTR("Every %d seconds"), sysCfg.savedata);
      snprintf_P(svalue, sizeof(svalue), PSTR("{\"SaveData\":\"%s\"}"), (sysCfg.savedata) ? (sysCfg.savedata > 1) ? stemp1 : MQTT_STATUS_ON : MQTT_STATUS_OFF);
    }
    else if (!strcmp(type,"SAVESTATE")) {
      if ((data_len > 0) && (payload >= 0) && (payload <= 1)) {
        sysCfg.savestate = payload;
      }
      snprintf_P(svalue, sizeof(svalue), PSTR("{\"SaveState\":\"%s\"}"), (sysCfg.savestate) ? MQTT_STATUS_ON : MQTT_STATUS_OFF);
    }
    else if (!strcmp(type,"BUTTONRESTRICT")) {
      if ((data_len > 0) && (payload >= 0) && (payload <= 1)) {
        sysCfg.button_restrict = payload;
      }
      snprintf_P(svalue, sizeof(svalue), PSTR("{\"ButtonRestrict\":\"%s\"}"), (sysCfg.button_restrict) ? MQTT_STATUS_ON : MQTT_STATUS_OFF);
    }
    else if (!strcmp(type,"UNITS")) {
      if ((data_len > 0) && (payload >= 0) && (payload <= 1)) {
        sysCfg.value_units = payload;
      }
      snprintf_P(svalue, sizeof(svalue), PSTR("{\"Units\":\"%s\"}"), (sysCfg.value_units) ? MQTT_STATUS_ON : MQTT_STATUS_OFF);
    }
    else if (!strcmp(type,"MQTT")) {
      if ((data_len > 0) && (payload >= 0) && (payload <= 1)) {
        sysCfg.mqtt_enabled = payload;
        restartflag = 2;
      }
      snprintf_P(svalue, sizeof(svalue), PSTR("{\"Mqtt\":\"%s\"}"), (sysCfg.mqtt_enabled) ? MQTT_STATUS_ON : MQTT_STATUS_OFF);
    }
    else if (!strcmp(type,"MODULE")) {
      if ((data_len > 0) && (payload > 0) && (payload <= MAXMODULE)) {
        sysCfg.module = payload -1;
        restartflag = 2;
      }
      snprintf_P(stemp1, sizeof(stemp1), modules[sysCfg.module].name);
      snprintf_P(svalue, sizeof(svalue), PSTR("{\"Module\":\"%s (%d)\"}"), stemp1, sysCfg.module +1);
    }
    else if (!strcmp(type,"MODULES")) {
      snprintf_P(svalue, sizeof(svalue), PSTR("{\"Modules1\":\""), svalue);
      byte jsflg = 0;
      for (byte i = 0; i < MAXMODULE /2; i++) {
        if (jsflg) snprintf_P(svalue, sizeof(svalue), PSTR("%s, "), svalue);
        jsflg = 1;
        snprintf_P(stemp1, sizeof(stemp1), modules[i].name);
        snprintf_P(svalue, sizeof(svalue), PSTR("%s%s (%d)"), svalue, stemp1, i +1);
      }
      snprintf_P(svalue, sizeof(svalue), PSTR("%s\"}"), svalue);
      mqtt_publish_topic_P(0, PSTR("RESULT"), svalue);
      snprintf_P(svalue, sizeof(svalue), PSTR("{\"Modules2\":\""), svalue);
      jsflg = 0;
      for (byte i = MAXMODULE /2; i < MAXMODULE; i++) {
        if (jsflg) snprintf_P(svalue, sizeof(svalue), PSTR("%s, "), svalue);
        jsflg = 1;
        snprintf_P(stemp1, sizeof(stemp1), modules[i].name);
        snprintf_P(svalue, sizeof(svalue), PSTR("%s%s (%d)"), svalue, stemp1, i +1);
      }
      snprintf_P(svalue, sizeof(svalue), PSTR("%s\"}"), svalue);
    }
    else if (!strcmp(type,"GPIO") && (index < MAX_GPIO_PIN)) {
      mytmplt cmodule;
      memcpy_P(&cmodule, &modules[sysCfg.module], sizeof(cmodule));
      if ((data_len > 0) && (cmodule.gp.io[index] == GPIO_USER) && (payload >= 0) && (payload < GPIO_SENSOR_END)) {
        for (byte i = 0; i < MAX_GPIO_PIN; i++) {
          if ((cmodule.gp.io[i] == GPIO_USER) && (sysCfg.my_module.gp.io[i] == payload)) sysCfg.my_module.gp.io[i] = 0;
        }
        sysCfg.my_module.gp.io[index] = payload;
        restartflag = 2;
      }
      snprintf_P(svalue, sizeof(svalue), PSTR("{"), svalue);
      byte jsflg = 0;
      for (byte i = 0; i < MAX_GPIO_PIN; i++) {
        if (cmodule.gp.io[i] == GPIO_USER) {
          if (jsflg) snprintf_P(svalue, sizeof(svalue), PSTR("%s, "), svalue);
          jsflg = 1;
          snprintf_P(stemp1, sizeof(stemp1), sensors[sysCfg.my_module.gp.io[i]]);
          snprintf_P(svalue, sizeof(svalue), PSTR("%s\"GPIO%d\":%d (%s)"), svalue, i, sysCfg.my_module.gp.io[i], stemp1);
        }
      }
      if (jsflg) {
        snprintf_P(svalue, sizeof(svalue), PSTR("%s}"), svalue);
      } else {
        snprintf_P(svalue, sizeof(svalue), PSTR("{\"GPIO\":\"Not supported\"}"));
      }
    }
    else if (!strcmp(type,"GPIOS")) {
      snprintf_P(svalue, sizeof(svalue), PSTR("{\"GPIOs1\":\""), svalue);
      byte jsflg = 0;
      for (byte i = 0; i < GPIO_SENSOR_END /2; i++) {
        if (jsflg) snprintf_P(svalue, sizeof(svalue), PSTR("%s, "), svalue);
        jsflg = 1;
        snprintf_P(stemp1, sizeof(stemp1), sensors[i]);
        snprintf_P(svalue, sizeof(svalue), PSTR("%s%s (%d)"), svalue, stemp1, i);
      }
      snprintf_P(svalue, sizeof(svalue), PSTR("%s\"}"), svalue);
      mqtt_publish_topic_P(0, PSTR("RESULT"), svalue);
      snprintf_P(svalue, sizeof(svalue), PSTR("{\"GPIOs2\":\""), svalue);
      jsflg = 0;
      for (byte i = GPIO_SENSOR_END /2; i < GPIO_SENSOR_END; i++) {
        if (jsflg) snprintf_P(svalue, sizeof(svalue), PSTR("%s, "), svalue);
        jsflg = 1;
        snprintf_P(stemp1, sizeof(stemp1), sensors[i]);
        snprintf_P(svalue, sizeof(svalue), PSTR("%s%s (%d)"), svalue, stemp1, i);
      }
      snprintf_P(svalue, sizeof(svalue), PSTR("%s\"}"), svalue);
    }
    else if (!strcmp(type,"SLEEP")) {
      if ((data_len > 0) && (payload >= 0) && (payload < 251)) {
        if ((!sysCfg.sleep && payload) || (sysCfg.sleep && !payload)) restartflag = 2;
        sysCfg.sleep = payload;
        sleep = payload;
//        restartflag = 2;
      }
      snprintf_P(svalue, sizeof(svalue), PSTR("{\"Sleep\":\"%d%s (%d%s)\"}"), sleep, (sysCfg.value_units) ? " mS" : "", sysCfg.sleep, (sysCfg.value_units) ? " mS" : "");
    }
    else if (!strcmp(type,"FLASHCHIPMODE")) {
      if ((data_len > 0) && (payload >= 0) && (payload <= 3)) {
        if (ESP.getFlashChipMode() != payload) setFlashChipMode(0, payload &3);
      }
      snprintf_P(svalue, sizeof(svalue), PSTR("{\"FlashChipMode\":%d}"), ESP.getFlashChipMode());
    }
    else if (!strcmp(type,"UPGRADE") || !strcmp(type,"UPLOAD")) {
      if ((data_len > 0) && (payload == 1)) {
        otaflag = 3;
        snprintf_P(svalue, sizeof(svalue), PSTR("{\"Upgrade\":\"Version %s from %s\"}"), Version, sysCfg.otaUrl);
      } else {
        snprintf_P(svalue, sizeof(svalue), PSTR("{\"Upgrade\":\"Option 1 to upgrade\"}"));
      }
    }
    else if (!strcmp(type,"OTAURL")) {
      if ((data_len > 0) && (data_len < sizeof(sysCfg.otaUrl)))
        strlcpy(sysCfg.otaUrl, (payload == 1) ? OTA_URL : dataBuf, sizeof(sysCfg.otaUrl));
      snprintf_P(svalue, sizeof(svalue), PSTR("{\"OtaUrl\":\"%s\"}"), sysCfg.otaUrl);
    }
    else if (!strcmp(type,"SERIALLOG")) {
      if ((data_len > 0) && (payload >= LOG_LEVEL_NONE) && (payload <= LOG_LEVEL_ALL)) {
        sysCfg.seriallog_level = payload;
        seriallog_level = payload;
        seriallog_timer = 0;
      }
      snprintf_P(svalue, sizeof(svalue), PSTR("{\"SerialLog\":\"%d (Active %d)\"}"), sysCfg.seriallog_level, seriallog_level);
    }
    else if (!strcmp(type,"SYSLOG")) {
      if ((data_len > 0) && (payload >= LOG_LEVEL_NONE) && (payload <= LOG_LEVEL_ALL)) {
        sysCfg.syslog_level = payload;
        syslog_level = (sysCfg.emulation) ? 0 : payload;
        syslog_timer = 0;
      }
      snprintf_P(svalue, sizeof(svalue), PSTR("{\"SysLog\":\"%d (Active %d)\"}"), sysCfg.syslog_level, syslog_level);
    }
    else if (!strcmp(type,"LOGHOST")) {
      if ((data_len > 0) && (data_len < sizeof(sysCfg.syslog_host))) {
        strlcpy(sysCfg.syslog_host, (payload == 1) ? SYS_LOG_HOST : dataBuf, sizeof(sysCfg.syslog_host));
      }
      snprintf_P(svalue, sizeof(svalue), PSTR("{\"LogHost\":\"%s\"}"), sysCfg.syslog_host);
    }
    else if (!strcmp(type,"LOGPORT")) {
      if ((data_len > 0) && (payload > 0) && (payload < 32766)) {
        sysCfg.syslog_port = (payload == 1) ? SYS_LOG_PORT : payload;
      }
      snprintf_P(svalue, sizeof(svalue), PSTR("{\"LogPort\":%d}"), sysCfg.syslog_port);
    }
    else if (!strcmp(type,"NTPSERVER") && (index > 0) && (index <= 3)) {
      if ((data_len > 0) && (data_len < sizeof(sysCfg.ntp_server[0]))) {
        strlcpy(sysCfg.ntp_server[index -1], (payload == 1) ? (index==1)?NTP_SERVER1:(index==2)?NTP_SERVER2:NTP_SERVER3 : dataBuf, sizeof(sysCfg.ntp_server[0]));
        for (i = 0; i < strlen(sysCfg.ntp_server[index -1]); i++) if (sysCfg.ntp_server[index -1][i] == ',') sysCfg.ntp_server[index -1][i] = '.';
        restartflag = 2;
      }
      snprintf_P(svalue, sizeof(svalue), PSTR("{\"NTPServer%d\":\"%s\"}"), index, sysCfg.ntp_server[index -1]);
    }
    else if (!strcmp(type,"AP")) {
      if ((data_len > 0) && (payload >= 0) && (payload <= 2)) {
        switch (payload) {
        case 0:  // Toggle
          sysCfg.sta_active ^= 1;
          break;
        case 1:  // AP1
        case 2:  // AP2
          sysCfg.sta_active = payload -1;
        }
        restartflag = 2;
      }
      snprintf_P(svalue, sizeof(svalue), PSTR("{\"Ap\":\"%d (%s)\"}"), sysCfg.sta_active +1, sysCfg.sta_ssid[sysCfg.sta_active]);
    }
    else if (!strcmp(type,"SSID") && (index > 0) && (index <= 2)) {
      if ((data_len > 0) && (data_len < sizeof(sysCfg.sta_ssid[0]))) {
        strlcpy(sysCfg.sta_ssid[index -1], (payload == 1) ? (index == 1) ? STA_SSID1 : STA_SSID2 : dataBuf, sizeof(sysCfg.sta_ssid[0]));
        sysCfg.sta_active = 0;
        restartflag = 2;
      }
      snprintf_P(svalue, sizeof(svalue), PSTR("{\"SSid%d\":\"%s\"}"), index, sysCfg.sta_ssid[index -1]);
    }
    else if (!strcmp(type,"PASSWORD") && (index > 0) && (index <= 2)) {
      if ((data_len > 0) && (data_len < sizeof(sysCfg.sta_pwd[0]))) {
        strlcpy(sysCfg.sta_pwd[index -1], (payload == 1) ? (index == 1) ? STA_PASS1 : STA_PASS2 : dataBuf, sizeof(sysCfg.sta_pwd[0]));
        sysCfg.sta_active = 0;
        restartflag = 2;
      }
      snprintf_P(svalue, sizeof(svalue), PSTR("{\"Password%d\":\"%s\"}"), index, sysCfg.sta_pwd[index -1]);
    }
    else if (!grpflg && !strcmp(type,"HOSTNAME")) {
      if ((data_len > 0) && (data_len < sizeof(sysCfg.hostname))) {
        strlcpy(sysCfg.hostname, (payload == 1) ? WIFI_HOSTNAME : dataBuf, sizeof(sysCfg.hostname));
        if (strstr(sysCfg.hostname,"%")) strlcpy(sysCfg.hostname, WIFI_HOSTNAME, sizeof(sysCfg.hostname));
        restartflag = 2;
      }
      snprintf_P(svalue, sizeof(svalue), PSTR("{\"Hostname\":\"%s\"}"), sysCfg.hostname);
    }
    else if (!strcmp(type,"WIFICONFIG") || !strcmp(type,"SMARTCONFIG")) {
      if ((data_len > 0) && (payload >= WIFI_RESTART) && (payload < MAX_WIFI_OPTION)) {
        sysCfg.sta_config = payload;
        wificheckflag = sysCfg.sta_config;
        snprintf_P(stemp1, sizeof(stemp1), wificfg[sysCfg.sta_config]);
        snprintf_P(svalue, sizeof(svalue), PSTR("{\"WifiConfig\":\"%s selected\"}"), stemp1);
        if (WIFI_State() != WIFI_RESTART) {
//          snprintf_P(svalue, sizeof(svalue), PSTR("%s after restart"), svalue);
          restartflag = 2;
        }
      } else {
        snprintf_P(stemp1, sizeof(stemp1), wificfg[sysCfg.sta_config]);
        snprintf_P(svalue, sizeof(svalue), PSTR("{\"WifiConfig\":\"%d (%s)\"}"), sysCfg.sta_config, stemp1);
      }
    }
    else if (!strcmp(type,"FRIENDLYNAME") && (index > 0) && (index <= 4)) {
      if ((data_len > 0) && (data_len < sizeof(sysCfg.friendlyname[0]))) {
        if (index == 1) {
          snprintf_P(stemp1, sizeof(stemp1), PSTR(FRIENDLY_NAME));
        } else {
          snprintf_P(stemp1, sizeof(stemp1), PSTR(FRIENDLY_NAME "%d"), index);
        }
        strlcpy(sysCfg.friendlyname[index -1], (payload == 1) ? stemp1 : dataBuf, sizeof(sysCfg.friendlyname[index -1]));
      }
      snprintf_P(svalue, sizeof(svalue), PSTR("{\"FriendlyName%d\":\"%s\"}"), index, sysCfg.friendlyname[index -1]);
    }
    else if (swt_flg && !strcmp(type,"SWITCHMODE") && (index > 0) && (index <= 4)) {
      if ((data_len > 0) && (payload >= 0) && (payload < MAX_SWITCH_OPTION)) {
        sysCfg.switchmode[index -1] = payload;
      }
      snprintf_P(svalue, sizeof(svalue), PSTR("{\"SwitchMode%d\":%d}"), index, sysCfg.switchmode[index-1]);
    }
#ifdef USE_WEBSERVER
    else if (!strcmp(type,"WEBSERVER")) {
      if ((data_len > 0) && (payload >= 0) && (payload <= 2)) {
        sysCfg.webserver = payload;
      }
      if (sysCfg.webserver) {
        snprintf_P(svalue, sizeof(svalue), PSTR("{\"Webserver\":\"Active for %s on %s with IP address %s\"}"),
          (sysCfg.webserver == 2) ? "ADMIN" : "USER", Hostname, WiFi.localIP().toString().c_str());
      } else {
        snprintf_P(svalue, sizeof(svalue), PSTR("{\"Webserver\":\"%s\"}"), MQTT_STATUS_OFF);
      }
    }
    else if (!strcmp(type,"WEBPASSWORD")) {
      if ((data_len > 0) && (data_len < sizeof(sysCfg.web_password))) {
        if (payload == 0) {
          sysCfg.web_password[0] = 0;  // No password
        } else {
          strlcpy(sysCfg.web_password, (payload == 1) ? WEB_PASSWORD : dataBuf, sizeof(sysCfg.web_password));
        }
      }
      snprintf_P(svalue, sizeof(svalue), PSTR("{\"WebPassword\":\"%s\"}"), sysCfg.web_password);
    }
    else if (!strcmp(type,"WEBLOG")) {
      if ((data_len > 0) && (payload >= LOG_LEVEL_NONE) && (payload <= LOG_LEVEL_ALL)) {
        sysCfg.weblog_level = payload;
      }
      snprintf_P(svalue, sizeof(svalue), PSTR("{\"WebLog\":%d}"), sysCfg.weblog_level);
    }
#ifdef USE_EMULATION
    else if (!strcmp(type,"EMULATION")) {
      if ((data_len > 0) && (payload >= 0) && (payload <= 2)) {
        sysCfg.emulation = payload;
        restartflag = 2;
      }
      snprintf_P(svalue, sizeof(svalue), PSTR("{\"Emulation\":%d}"), sysCfg.emulation);
    }
#endif  // USE_EMULATION
#endif  // USE_WEBSERVER
    else if (!strcmp(type,"TELEPERIOD")) {
      if ((data_len > 0) && (payload >= 0) && (payload < 3601)) {
        sysCfg.tele_period = (payload == 1) ? TELE_PERIOD : payload;
        if ((sysCfg.tele_period > 0) && (sysCfg.tele_period < 10)) sysCfg.tele_period = 10;   // Do not allow periods < 10 seconds
        tele_period = sysCfg.tele_period;
      }
      snprintf_P(svalue, sizeof(svalue), PSTR("{\"TelePeriod\":\"%d%s\"}"), sysCfg.tele_period, (sysCfg.value_units) ? " Sec" : "");
    }
    else if (!strcmp(type,"RESTART")) {
      switch (payload) {
      case 1:
        restartflag = 2;
        snprintf_P(svalue, sizeof(svalue), PSTR("{\"Restart\":\"Restarting\"}"));
        break;
      case 99:
        addLog_P(LOG_LEVEL_INFO, PSTR("APP: Restarting"));
        ESP.restart();
        break;
      default:
        snprintf_P(svalue, sizeof(svalue), PSTR("{\"Restart\":\"1 to restart\"}"));
      }
    }
    else if (!strcmp(type,"RESET")) {
      switch (payload) {
      case 1:
        restartflag = 211;
        snprintf_P(svalue, sizeof(svalue), PSTR("{\"Reset\":\"Reset and Restarting\"}"));
        break;
      case 2:
        restartflag = 212;
        snprintf_P(svalue, sizeof(svalue), PSTR("{\"Reset\":\"Erase, Reset and Restarting\"}"));
        break;
      default:
        snprintf_P(svalue, sizeof(svalue), PSTR("{\"Reset\":\"1 to reset\"}"));
      }
    }
    else if (!strcmp(type,"TIMEZONE")) {
      if ((data_len > 0) && (((payload >= -12) && (payload <= 12)) || (payload == 99))) {
        sysCfg.timezone = payload;
      }
      snprintf_P(svalue, sizeof(svalue), PSTR("{\"Timezone\":%d}"), sysCfg.timezone);
    }
    else if (!strcmp(type,"LEDPOWER")) {
      if ((data_len > 0) && (payload >= 0) && (payload <= 2)) {
        sysCfg.ledstate &= 8;
        switch (payload) {
        case 0: // Off
        case 1: // On
          sysCfg.ledstate = payload << 3;
          break;
        case 2: // Toggle
          sysCfg.ledstate ^= 8;
          break;
        }
        blinks = 0;
        setLed(sysCfg.ledstate &8);
      }
      snprintf_P(svalue, sizeof(svalue), PSTR("{\"LedPower\":\"%s\"}"), (sysCfg.ledstate &8) ? MQTT_STATUS_ON : MQTT_STATUS_OFF);
    }
    else if (!strcmp(type,"LEDSTATE")) {
      if ((data_len > 0) && (payload >= 0) && (payload < MAX_LED_OPTION)) {
        sysCfg.ledstate = payload;
        if (!sysCfg.ledstate) setLed(0);
      }
      snprintf_P(svalue, sizeof(svalue), PSTR("{\"LedState\":%d}"), sysCfg.ledstate);
    }
    else if (!strcmp(type,"CFGDUMP")) {
      CFG_Dump();
      snprintf_P(svalue, sizeof(svalue), PSTR("{\"CfgDump\":\"Done\"}"));
    }
    else if (sysCfg.mqtt_enabled && mqtt_command(grpflg, type, index, dataBuf, data_len, payload, svalue, sizeof(svalue))) {
      // Serviced
    }
    else if (hlw_flg && hlw_command(type, index, dataBuf, data_len, payload, svalue, sizeof(svalue))) {
      // Serviced
    }
#ifdef USE_I2C
    else if (i2c_flg && !strcmp(type,"I2CSCAN")) {
      i2c_scan(svalue, sizeof(svalue));
    }
#endif  // USE_I2C
#ifdef USE_WS2812
    else if ((pin[GPIO_WS2812] < 99) && ws2812_command(type, index, dataBuf, data_len, payload, svalue, sizeof(svalue))) {
      // Serviced
    }
#endif  // USE_WS2812
#ifdef USE_IR_REMOTE
    else if ((pin[GPIO_IRSEND] < 99) && ir_send_command(type, index, dataBufUc, data_len, payload, svalue, sizeof(svalue))) {
      // Serviced
    }
#endif  // USE_IR_REMOTE
#ifdef DEBUG_THEO
    else if (!strcmp(type,"EXCEPTION")) {
      if (data_len > 0) exception_tst(payload);
      snprintf_P(svalue, sizeof(svalue), PSTR("{\"Exception\":\"Triggered\"}"));
    }
#endif  // DEBUG_THEO
    else {
      type = NULL;
    }
  }
  if (type == NULL) {
    blinks = 201;
    snprintf_P(svalue, sizeof(svalue), PSTR("{\"Commands1\":\"Status, SaveData, SaveSate, Sleep, Upgrade, Otaurl, Restart, Reset, WifiConfig, Seriallog, Syslog, LogHost, LogPort, SSId1, SSId2, Password1, Password2, AP%s\"}"), (!grpflg) ? ", Hostname, Module, Modules, GPIO, GPIOs" : "");
    mqtt_publish_topic_P(0, PSTR("COMMANDS1"), svalue);

    if (sysCfg.mqtt_enabled) {
      snprintf_P(svalue, sizeof(svalue), PSTR("{\"Commands2\":\"Mqtt, MqttHost, MqttPort, MqttUser, MqttPassword%s, GroupTopic, Units, Timezone, LedState, LedPower, TelePeriod\"}"), (!grpflg) ? ", MqttClient, Topic, ButtonTopic, ButtonRetain, SwitchTopic, SwitchRetain, PowerRetain" : "");
    } else {
      snprintf_P(svalue, sizeof(svalue), PSTR("{\"Commands2\":\"Mqtt, Units, Timezone, LedState, LedPower, TelePeriod\"}"), (!grpflg) ? ", MqttClient" : "");
    }
    mqtt_publish_topic_P(0, PSTR("COMMANDS2"), svalue);

    snprintf_P(svalue, sizeof(svalue), PSTR("{\"Commands3\":\"%s%s, PulseTime, BlinkTime, BlinkCount, ButtonRestrict, NtpServer"), (Maxdevice == 1) ? "Power, Light" : "Power1, Power2, Light1 Light2", (sysCfg.module != MOTOR) ? ", PowerOnState" : "");
#ifdef USE_WEBSERVER
    snprintf_P(svalue, sizeof(svalue), PSTR("%s, Weblog, Webserver, WebPassword, Emulation"), svalue);
#endif
    if (swt_flg) snprintf_P(svalue, sizeof(svalue), PSTR("%s, SwitchMode"), svalue);
#ifdef USE_I2C
    if (i2c_flg) snprintf_P(svalue, sizeof(svalue), PSTR("%s, I2CScan"), svalue);
#endif  // USE_I2C
    if (sysCfg.module == SONOFF_LED) snprintf_P(svalue, sizeof(svalue), PSTR("%s, Color, Dimmer, Fade, Speed, Wakeup, WakeupDuration, LedTable"), svalue);
#ifdef USE_WS2812
    if (pin[GPIO_WS2812] < 99) snprintf_P(svalue, sizeof(svalue), PSTR("%s, Color, Dimmer, Fade, Speed, Wakeup, LedTable, Pixels, Led, Width, Scheme"), svalue);
#endif
#ifdef USE_IR_REMOTE
    if (pin[GPIO_IRSEND] < 99) snprintf_P(svalue, sizeof(svalue), PSTR("%s, IRSend"), svalue);
#endif
    snprintf_P(svalue, sizeof(svalue), PSTR("%s\"}"), svalue);
    mqtt_publish_topic_P(0, PSTR("COMMANDS3"), svalue);

#ifdef USE_DOMOTICZ
    domoticz_commands(svalue, sizeof(svalue));
    mqtt_publish_topic_P(0, PSTR("COMMANDS4"), svalue);
#endif  // USE_DOMOTICZ

    if (hlw_flg) {
      hlw_commands(svalue, sizeof(svalue));
      mqtt_publish_topic_P(0, PSTR("COMMANDS5"), svalue);
    }
  } else {
    mqtt_publish_topic_P(0, PSTR("RESULT"), svalue);
  }
}

/********************************************************************************************/

void send_button_power(byte key, byte device, byte state)
{
// key 0 = button_topic
// key 1 = switch_topic

  char stopic[TOPSZ], svalue[TOPSZ], stemp1[10];

  if (!key && (device > Maxdevice)) device = 1;
  snprintf_P(stemp1, sizeof(stemp1), PSTR("%d"), device);
  snprintf_P(stopic, sizeof(stopic), PSTR("%s/%s/%s%s"),
    SUB_PREFIX, (key) ? sysCfg.switch_topic : sysCfg.button_topic, sysCfg.mqtt_subtopic, (key || (Maxdevice > 1)) ? stemp1 : "");
  
  if (state == 3) {
    svalue[0] = '\0';
  } else {
    if (!strcmp(sysCfg.mqtt_topic,(key) ? sysCfg.switch_topic : sysCfg.button_topic) && (state == 2)) {
      state = ~(power >> (device -1)) & 0x01;
    }
    snprintf_P(svalue, sizeof(svalue), PSTR("%s"), (state) ? (state == 2) ? MQTT_CMND_TOGGLE : MQTT_STATUS_ON : MQTT_STATUS_OFF);
  }
#ifdef USE_DOMOTICZ
  if (!(domoticz_button(key, device, state, strlen(svalue)))) {
    mqtt_publish_sec(stopic, svalue, (key) ? sysCfg.mqtt_switch_retain : sysCfg.mqtt_button_retain);
  }
#else
  mqtt_publish_sec(stopic, svalue, (key) ? sysCfg.mqtt_switch_retain : sysCfg.mqtt_button_retain);
#endif  // USE_DOMOTICZ
}

void do_cmnd_power(byte device, byte state)
{
// device  = Relay number 1 and up
// state 0 = Relay Off
// state 1 = Relay on (turn off after sysCfg.pulsetime * 100 mSec if enabled)
// state 2 = Toggle relay
// state 3 = Blink relay
// state 4 = Stop blinking relay
// state 9 = Show power state

  if ((device < 1) || (device > Maxdevice)) device = 1;
  byte mask = 0x01 << (device -1);
  pulse_timer[(device -1)&3] = 0;
  if (state <= 2) {
    if ((blink_mask & mask)) {
      blink_mask &= (0xFF ^ mask);  // Clear device mask
      mqtt_publishPowerBlinkState(device);
    }
    switch (state) {
    case 0: { // Off
      power &= (0xFF ^ mask);
      break; }
    case 1: // On
      power |= mask;
      break;
    case 2: // Toggle
      power ^= mask;
    }
    setRelay(power);
#ifdef USE_DOMOTICZ
    domoticz_updatePowerState(device);
#endif  // USE_DOMOTICZ
    pulse_timer[(device -1)&3] = (power & mask) ? sysCfg.pulsetime[(device -1)&3] : 0;
  }
  else if (state == 3) { // Blink
    if (!(blink_mask & mask)) {
      blink_powersave = (blink_powersave & (0xFF ^ mask)) | (power & mask);  // Save state
      blink_power = (power >> (device -1))&1;  // Prep to Toggle
    }
    blink_timer = 1;
    blink_counter = ((!sysCfg.blinkcount) ? 64000 : (sysCfg.blinkcount *2)) +1;
    blink_mask |= mask;  // Set device mask
    mqtt_publishPowerBlinkState(device);
    return;
  }
  else if (state == 4) { // No Blink
    byte flag = (blink_mask & mask);
    blink_mask &= (0xFF ^ mask);  // Clear device mask
    mqtt_publishPowerBlinkState(device);
    if (flag) do_cmnd_power(device, (blink_powersave >> (device -1))&1);  // Restore state
    return;
  }
  mqtt_publishPowerState(device);
}

void stop_all_power_blink()
{
  byte i, mask;

  for (i = 1; i <= Maxdevice; i++) {
    mask = 0x01 << (i -1);
    if (blink_mask & mask) {
      blink_mask &= (0xFF ^ mask);  // Clear device mask
      mqtt_publishPowerBlinkState(i);
      do_cmnd_power(i, (blink_powersave >> (i -1))&1);  // Restore state
    }
  }
}

void do_cmnd(char *cmnd)
{
  char stopic[TOPSZ], svalue[128];
  char *start;
  char *token;

  token = strtok(cmnd, " ");
  if (token != NULL) {
    start = strrchr(token, '/');   // Skip possible cmnd/sonoff/ preamble
    if (start) token = start;
  }
  snprintf_P(stopic, sizeof(stopic), PSTR("%s/%s/%s"), SUB_PREFIX, sysCfg.mqtt_topic, token);
  token = strtok(NULL, "");
  snprintf_P(svalue, sizeof(svalue), PSTR("%s"), (token == NULL) ? "" : token);
  mqttDataCb(stopic, (byte*)svalue, strlen(svalue));
}

void publish_status(uint8_t payload)
{
  char svalue[MESSZ];
  uint8_t option = 0;

  // Workaround MQTT - TCP/IP stack queueing when SUB_PREFIX = PUB_PREFIX
  option = (!strcmp(SUB_PREFIX,PUB_PREFIX) && (!payload));

  if ((!sysCfg.mqtt_enabled) && (payload == 6)) payload = 99;
  if ((!hlw_flg) && ((payload == 8) || (payload == 9))) payload = 99;

  if ((payload == 0) || (payload == 99)) {
    snprintf_P(svalue, sizeof(svalue), PSTR("{\"Status\":{\"Module\":%d, \"FriendlyName\":\"%s\", \"Topic\":\"%s\", \"ButtonTopic\":\"%s\", \"Subtopic\":\"%s\", \"Power\":%d, \"PowerOnState\":%d, \"LedState\":%d, \"SaveData\":%d, \"SaveState\":%d, \"ButtonRetain\":%d, \"PowerRetain\":%d}}"),
      sysCfg.module +1, sysCfg.friendlyname[0], sysCfg.mqtt_topic, sysCfg.button_topic, sysCfg.mqtt_subtopic, power, sysCfg.poweronstate, sysCfg.ledstate, sysCfg.savedata, sysCfg.savestate, sysCfg.mqtt_button_retain, sysCfg.mqtt_power_retain);
    mqtt_publish_topic_P(option, PSTR("STATUS"), svalue);
  }

  if ((payload == 0) || (payload == 1)) {
    snprintf_P(svalue, sizeof(svalue), PSTR("{\"StatusPRM\":{\"Baudrate\":%d, \"GroupTopic\":\"%s\", \"OtaUrl\":\"%s\", \"Uptime\":%d, \"Sleep\":%d, \"BootCount\":%d, \"SaveCount\":%d}}"),
      Baudrate, sysCfg.mqtt_grptopic, sysCfg.otaUrl, uptime, sysCfg.sleep, sysCfg.bootcount, sysCfg.saveFlag);
    mqtt_publish_topic_P(option, PSTR("STATUS1"), svalue);
  }

  if ((payload == 0) || (payload == 2)) {
//    snprintf_P(svalue, sizeof(svalue), PSTR("{\"StatusFWR\":{\"Program\":\"%s\", \"BuildDateTime\":\"%s/%s\", \"Boot\":%d, \"Core\":\"%s\", \"SDK\":\"%s\"}}"),
//      Version, __DATE__, __TIME__, ESP.getBootVersion(), ESP.getCoreVersion().c_str(), ESP.getSdkVersion());
    snprintf_P(svalue, sizeof(svalue), PSTR("{\"StatusFWR\":{\"Program\":\"%s\", \"BuildDateTime\":\"%s\", \"Boot\":%d, \"Core\":\"%s\", \"SDK\":\"%s\"}}"),
      Version, getBuildDateTime().c_str(), ESP.getBootVersion(), ESP.getCoreVersion().c_str(), ESP.getSdkVersion());
    mqtt_publish_topic_P(option, PSTR("STATUS2"), svalue);
  }

  if ((payload == 0) || (payload == 3)) {
    snprintf_P(svalue, sizeof(svalue), PSTR("{\"StatusLOG\":{\"Seriallog\":%d, \"Weblog\":%d, \"Syslog\":%d, \"LogHost\":\"%s\", \"SSId1\":\"%s\", \"SSId2\":\"%s\", \"TelePeriod\":%d}}"),
      sysCfg.seriallog_level, sysCfg.weblog_level, sysCfg.syslog_level, sysCfg.syslog_host, sysCfg.sta_ssid[0], sysCfg.sta_ssid[1], sysCfg.tele_period);
    mqtt_publish_topic_P(option, PSTR("STATUS3"), svalue);
  }

  if ((payload == 0) || (payload == 4)) {
    snprintf_P(svalue, sizeof(svalue), PSTR("{\"StatusMEM\":{\"ProgramSize\":%d, \"Free\":%d, \"Heap\":%d, \"SpiffsStart\":%d, \"SpiffsSize\":%d, \"FlashSize\":%d, \"ProgramFlashSize\":%d, \"FlashChipMode\":%d}}"),
      ESP.getSketchSize()/1024, ESP.getFreeSketchSpace()/1024, ESP.getFreeHeap()/1024, ((uint32_t)&_SPIFFS_start - 0x40200000)/1024,
      (((uint32_t)&_SPIFFS_end - 0x40200000) - ((uint32_t)&_SPIFFS_start - 0x40200000))/1024, ESP.getFlashChipRealSize()/1024, ESP.getFlashChipSize()/1024, ESP.getFlashChipMode());
    mqtt_publish_topic_P(option, PSTR("STATUS4"), svalue);
  }

  if ((payload == 0) || (payload == 5)) {
    snprintf_P(svalue, sizeof(svalue), PSTR("{\"StatusNET\":{\"Host\":\"%s\", \"IP\":\"%s\", \"Gateway\":\"%s\", \"Subnetmask\":\"%s\", \"Mac\":\"%s\", \"Webserver\":%d, \"WifiConfig\":%d}}"),
      Hostname, WiFi.localIP().toString().c_str(), WiFi.gatewayIP().toString().c_str(), WiFi.subnetMask().toString().c_str(),
      WiFi.macAddress().c_str(), sysCfg.webserver, sysCfg.sta_config);
    mqtt_publish_topic_P(option, PSTR("STATUS5"), svalue);
  }

  if (((payload == 0) || (payload == 6)) && sysCfg.mqtt_enabled) {
    snprintf_P(svalue, sizeof(svalue), PSTR("{\"StatusMQT\":{\"Host\":\"%s\", \"Port\":%d, \"ClientMask\":\"%s\", \"Client\":\"%s\", \"User\":\"%s\", \"MAX_PACKET_SIZE\":%d, \"KEEPALIVE\":%d}}"),
      sysCfg.mqtt_host, sysCfg.mqtt_port, sysCfg.mqtt_client, MQTTClient, sysCfg.mqtt_user, MQTT_MAX_PACKET_SIZE, MQTT_KEEPALIVE);
    mqtt_publish_topic_P(option, PSTR("STATUS6"), svalue);
  }

  if ((payload == 0) || (payload == 7)) {
    snprintf_P(svalue, sizeof(svalue), PSTR("{\"StatusTIM\":{\"UTC\":\"%s\", \"Local\":\"%s\", \"StartDST\":\"%s\", \"EndDST\":\"%s\", \"Timezone\":%d}}"),
      rtc_time(0).c_str(), rtc_time(1).c_str(), rtc_time(2).c_str(), rtc_time(3).c_str(), sysCfg.timezone);
    mqtt_publish_topic_P(option, PSTR("STATUS7"), svalue);
  }

  if (hlw_flg) {
    if ((payload == 0) || (payload == 8)) {
      hlw_mqttStatus(svalue, sizeof(svalue));      
      mqtt_publish_topic_P(option, PSTR("STATUS8"), svalue);
    }

    if ((payload == 0) || (payload == 9)) {
      snprintf_P(svalue, sizeof(svalue), PSTR("{\"StatusPTH\":{\"PowerLow\":%d, \"PowerHigh\":%d, \"VoltageLow\":%d, \"VoltageHigh\":%d, \"CurrentLow\":%d, \"CurrentHigh\":%d}}"),
        sysCfg.hlw_pmin, sysCfg.hlw_pmax, sysCfg.hlw_umin, sysCfg.hlw_umax, sysCfg.hlw_imin, sysCfg.hlw_imax);
      mqtt_publish_topic_P(option, PSTR("STATUS9"), svalue);
    }
  }

  if ((payload == 0) || (payload == 10)) {
    uint8_t djson = 0;
    snprintf_P(svalue, sizeof(svalue), PSTR("{\"StatusSNS\":"));
    sensors_mqttPresent(svalue, sizeof(svalue), &djson);
    snprintf_P(svalue, sizeof(svalue), PSTR("%s}"), svalue);
    mqtt_publish_topic_P(option, PSTR("STATUS10"), svalue);
  }

  if ((payload == 0) || (payload == 11)) {
    snprintf_P(svalue, sizeof(svalue), PSTR("{\"StatusPWR\":"));
    state_mqttPresent(svalue, sizeof(svalue));
    snprintf_P(svalue, sizeof(svalue), PSTR("%s}"), svalue);
    mqtt_publish_topic_P(option, PSTR("STATUS11"), svalue);
  }
 
}

void state_mqttPresent(char* svalue, uint16_t ssvalue)
{
  char stemp1[8];
  
  snprintf_P(svalue, ssvalue, PSTR("%s{\"Time\":\"%s\", \"Uptime\":%d"), svalue, getDateTime().c_str(), uptime);
#ifdef USE_ADC_VCC
  dtostrf((double)ESP.getVcc()/1000, 1, 3, stemp1);
  snprintf_P(svalue, ssvalue, PSTR("%s, \"Vcc\":%s"), svalue, stemp1);
#endif        
  for (byte i = 0; i < Maxdevice; i++) {
    if (Maxdevice == 1) {  // Legacy
      snprintf_P(svalue, ssvalue, PSTR("%s, \"%s\":"), svalue, sysCfg.mqtt_subtopic);
    } else {
      snprintf_P(svalue, ssvalue, PSTR("%s, \"%s%d\":"), svalue, sysCfg.mqtt_subtopic, i +1);
    }
    snprintf_P(svalue, ssvalue, PSTR("%s\"%s\""), svalue, (power & (0x01 << i)) ? MQTT_STATUS_ON : MQTT_STATUS_OFF);
  }
  snprintf_P(svalue, ssvalue, PSTR("%s, \"Wifi\":{\"AP\":%d, \"SSID\":\"%s\", \"RSSI\":%d}}"),
    svalue, sysCfg.sta_active +1, sysCfg.sta_ssid[sysCfg.sta_active], WIFI_getRSSIasQuality(WiFi.RSSI()));
}

void sensors_mqttPresent(char* svalue, uint16_t ssvalue, uint8_t* djson)
{
  snprintf_P(svalue, ssvalue, PSTR("%s{\"Time\":\"%s\""), svalue, getDateTime().c_str());

#ifndef USE_ADC_VCC
  if (pin[GPIO_ADC0] < 99) {
    snprintf_P(svalue, ssvalue, PSTR("%s, \"AnalogInput0\":%d"), svalue, analogRead(A0));
    *djson = 1;
  }
#endif    
  if (pin[GPIO_DSB] < 99) {
#ifdef USE_DS18B20
    dsb_mqttPresent(svalue, ssvalue, djson);
#endif  // USE_DS18B20
#ifdef USE_DS18x20
    ds18x20_mqttPresent(svalue, ssvalue, djson);
#endif  // USE_DS18x20
  }
#ifdef USE_DHT
  if (dht_type) dht_mqttPresent(svalue, ssvalue, djson);
#endif  // USE_DHT
#ifdef USE_I2C
  if (i2c_flg) {
#ifdef USE_SHT
    sht_mqttPresent(svalue, ssvalue, djson);
#endif  // USE_SHT
#ifdef USE_HTU
    htu_mqttPresent(svalue, ssvalue, djson);
#endif  // USE_HTU
#ifdef USE_BMP
    bmp_mqttPresent(svalue, ssvalue, djson);
#endif  // USE_BMP
#ifdef USE_BH1750
    bh1750_mqttPresent(svalue, ssvalue, djson);
#endif  // USE_BH1750
  }
#endif  // USE_I2C      
  snprintf_P(svalue, ssvalue, PSTR("%s}"), svalue);
}

/********************************************************************************************/

void every_second()
{
  char svalue[MESSZ];

  for (byte i = 0; i < MAX_PULSETIMERS; i++) if (pulse_timer[i] > 111) pulse_timer[i]--;

  if (seriallog_timer) {
    seriallog_timer--;
    if (!seriallog_timer) {
      if (seriallog_level) {
        addLog_P(LOG_LEVEL_INFO, PSTR("APP: Serial logging disabled"));
      }
      seriallog_level = 0;
    }
  }

  if (syslog_timer) {  // Restore syslog level
    syslog_timer--;
    if (!syslog_timer) {
      syslog_level = (sysCfg.emulation) ? 0 : sysCfg.syslog_level;
      if (sysCfg.syslog_level) {
        addLog_P(LOG_LEVEL_INFO, PSTR("SYSL: Syslog logging re-enabled"));  // Might trigger disable again (on purpose)
      }
    }
  }

#ifdef USE_DOMOTICZ
  domoticz_mqttUpdate();
#endif  // USE_DOMOTICZ

  if (status_update_timer) {
    status_update_timer--;
    if (!status_update_timer) {
      for (byte i = 1; i <= Maxdevice; i++) mqtt_publishPowerState(i);
    }
  }

  if (sysCfg.tele_period) {
    tele_period++;
    if (tele_period == sysCfg.tele_period -1) {
      if (pin[GPIO_DSB] < 99) {
#ifdef USE_DS18B20
        dsb_readTempPrep();
#endif  // USE_DS18B20
#ifdef USE_DS18x20
        ds18x20_search();      // Check for changes in sensors number
        ds18x20_convert();     // Start Conversion, takes up to one second
#endif  // USE_DS18x20
      }
#ifdef USE_DHT
      if (dht_type) dht_readPrep();
#endif  // USE_DHT
#ifdef USE_I2C
      if (i2c_flg) {
#ifdef USE_SHT
        sht_detect();
#endif  // USE_SHT
#ifdef USE_HTU
        htu_detect();
#endif  // USE_HTU
#ifdef USE_BMP
        bmp_detect();
#endif  // USE_BMP
#ifdef USE_BH1750
        bh1750_detect();
#endif  // USE_BH1750
      }
#endif  // USE_I2C
    }
    if (tele_period >= sysCfg.tele_period) {
      tele_period = 0;

      svalue[0] = '\0';
      state_mqttPresent(svalue, sizeof(svalue));
      mqtt_publish_topic_P(1, PSTR("STATE"), svalue);

      uint8_t djson = 0;
      svalue[0] = '\0';
      sensors_mqttPresent(svalue, sizeof(svalue), &djson);
      if (djson) mqtt_publish_topic_P(1, PSTR("SENSOR"), svalue);

      if (hlw_flg) hlw_mqttPresent();
    }
  }

  if (hlw_flg) hlw_margin_chk();

  if ((rtcTime.Minute == 2) && (rtcTime.Second == 30)) {
    uptime++;
    snprintf_P(svalue, sizeof(svalue), PSTR("{\"Time\":\"%s\", \"Uptime\":%d}"), getDateTime().c_str(), uptime);
    mqtt_publish_topic_P(1, PSTR("UPTIME"), svalue);
  }
}

void stateloop()
{
  uint8_t button = NOT_PRESSED, flag, switchflag, power_now;
  char scmnd[20], log[LOGSZ], svalue[80];  // was MESSZ

  timerxs = millis() + (1000 / STATES);
  state++;
  if (state == STATES) {             // Every second
    state = 0;
    every_second();
  }

  if (mqtt_cmnd_publish) mqtt_cmnd_publish--;  // Clean up

  if (latching_relay_pulse) {
    latching_relay_pulse--;
    if (!latching_relay_pulse) setLatchingRelay(0, 0);
  }

  for (byte i = 0; i < MAX_PULSETIMERS; i++)
    if ((pulse_timer[i] > 0) && (pulse_timer[i] < 112)) {
      pulse_timer[i]--;
      if (!pulse_timer[i]) do_cmnd_power(i +1, 0);
    }

  if (blink_mask) {
    blink_timer--;
    if (!blink_timer) {
      blink_timer = sysCfg.blinktime;
      blink_counter--;
      if (!blink_counter) {
        stop_all_power_blink();
      } else {
        blink_power ^= 1;
        power_now = (power & (0xFF ^ blink_mask)) | ((blink_power) ? blink_mask : 0);
        setRelay(power_now);
      }
    }
  }

  if (sysCfg.module == SONOFF_LED) sl_animate();
  
#ifdef USE_WS2812
  if (pin[GPIO_WS2812] < 99) ws2812_animate();
#endif  // USE_WS2812

  if ((sysCfg.module == SONOFF_DUAL) || (sysCfg.module == CH4)) {
    if (ButtonCode) {
      snprintf_P(log, sizeof(log), PSTR("APP: Button code %04X"), ButtonCode);
      addLog(LOG_LEVEL_DEBUG, log);
      button = PRESSED;
      if (ButtonCode == 0xF500) holdcount = (STATES *4) -1;
      ButtonCode = 0;
    } else {
      button = NOT_PRESSED;
    }
  } else {
    if (pin[GPIO_KEY1] < 99) button = digitalRead(pin[GPIO_KEY1]);
  }
  if ((button == PRESSED) && (lastbutton[0] == NOT_PRESSED)) {
    multipress = (multiwindow) ? multipress +1 : 1;
    snprintf_P(log, sizeof(log), PSTR("APP: Multipress %d"), multipress);
    addLog(LOG_LEVEL_DEBUG, log);
    blinks = 201;
    multiwindow = STATES /2;         // 1/2 second multi press window
  }
  lastbutton[0] = button;
  if (button == NOT_PRESSED) {
    holdcount = 0;
  } else {
    holdcount++;
    if (!sysCfg.button_restrict && (holdcount == (STATES *4))) {  // 4 seconds button hold
      snprintf_P(scmnd, sizeof(scmnd), PSTR("reset 1"));
      multipress = 0;
      do_cmnd(scmnd);
    }
  }
  if (multiwindow) {
    multiwindow--;
  } else {
    if ((!restartflag) && (!holdcount) && (multipress > 0) && (multipress < MAX_BUTTON_COMMANDS +3)) {
      if ((sysCfg.module == SONOFF_DUAL) || (sysCfg.module == CH4)) {
        flag = ((multipress == 1) || (multipress == 2));
      } else  {
        flag = (multipress == 1);
      }
      if (flag && sysCfg.mqtt_enabled && mqttClient.connected() && strcmp(sysCfg.button_topic, "0")) {
        send_button_power(0, multipress, 2);  // Execute command via MQTT using ButtonTopic to sync external clients
      } else {
        if ((multipress == 1) || (multipress == 2)) {
          if (WIFI_State()) {  // WPSconfig, Smartconfig or Wifimanager active
            restartflag = 1;
          } else {
            do_cmnd_power(multipress, 2);    // Execute command internally
          }
        } else {
          if (!sysCfg.button_restrict) {
            snprintf_P(scmnd, sizeof(scmnd), commands[multipress -3]);
            do_cmnd(scmnd);
          }
        }
      }
      multipress = 0;
    }
  }

  for (byte i = 1; i < Maxdevice; i++) if (pin[GPIO_KEY1 +i] < 99) {
    button = digitalRead(pin[GPIO_KEY1 +i]);
    if ((button == PRESSED) && (lastbutton[i] == NOT_PRESSED)) {
      if (sysCfg.mqtt_enabled && mqttClient.connected() && strcmp(sysCfg.button_topic, "0")) {
        send_button_power(0, i +1, 2);   // Execute commend via MQTT
      } else {
        do_cmnd_power(i +1, 2);       // Execute command internally
      }
    }
    lastbutton[i] = button;
  }

//  for (byte i = 0; i < Maxdevice; i++) if (pin[GPIO_SWT1 +i] < 99) {
  for (byte i = 0; i < 4; i++) if (pin[GPIO_SWT1 +i] < 99) {
    button = digitalRead(pin[GPIO_SWT1 +i]);
    if (button != lastwallswitch[i]) {
      switchflag = 3;
      switch (sysCfg.switchmode[i]) {
      case TOGGLE:
        switchflag = 2;                // Toggle
        break;
      case FOLLOW:
        switchflag = button & 0x01;    // Follow wall switch state
        break;
      case FOLLOW_INV:
        switchflag = ~button & 0x01;   // Follow inverted wall switch state
        break;
      case PUSHBUTTON:
        if ((button == PRESSED) && (lastwallswitch[i] == NOT_PRESSED)) switchflag = 2;  // Toggle with pushbutton to Gnd
        break;
      case PUSHBUTTON_INV:
        if ((button == NOT_PRESSED) && (lastwallswitch[i] == PRESSED)) switchflag = 2;  // Toggle with releasing pushbutton from Gnd
      }
      if (switchflag < 3) {
        if (sysCfg.mqtt_enabled && mqttClient.connected() && strcmp(sysCfg.switch_topic,"0")) {
          send_button_power(1, i +1, switchflag);  // Execute commend via MQTT
        } else {
          do_cmnd_power(i +1, switchflag);         // Execute command internally (if i < Maxdevice)
        }
      }
      lastwallswitch[i] = button;
    }
  }

  if (!(state % ((STATES/10)*2))) {
    if (blinks || restartflag || otaflag) {
      if (restartflag || otaflag) {
        blinkstate = 1;   // Stay lit
      } else {
        blinkstate ^= 1;  // Blink
      }
      if ((!(sysCfg.ledstate &0x08)) && ((sysCfg.ledstate &0x06) || (blinks > 200) || (blinkstate))) {
        setLed(blinkstate);
      }
      if (!blinkstate) {
        blinks--;
        if (blinks == 200) blinks = 0;
      }
    } else {
      if (sysCfg.ledstate &0x01) setLed(power);
    }
  }

  switch (state) {
  case (STATES/10)*2:
    if (otaflag) {
      otaflag--;
      if (otaflag == 2){
        otaretry = OTA_ATTEMPTS;
        ESPhttpUpdate.rebootOnUpdate(false);
        sl_blank(1);
      }
      if (otaflag <= 0) {
#ifdef USE_WEBSERVER
        if (sysCfg.webserver) stopWebserver();
#endif  // USE_WEBSERVER
        otaflag = 92;
        otaok = 0;
        otaretry--;
        if (otaretry) {
//          snprintf_P(log, sizeof(log), PSTR("OTA: Attempt %d"), OTA_ATTEMPTS - otaretry);
//          addLog(LOG_LEVEL_INFO, log);
          otaok = (ESPhttpUpdate.update(sysCfg.otaUrl) == HTTP_UPDATE_OK);
          if (!otaok) otaflag = 2;
        }
      }
      if (otaflag == 90) {  // Allow MQTT to reconnect
        otaflag = 0;
        if (otaok) {
          if ((sysCfg.module == SONOFF_TOUCH) || (sysCfg.module == SONOFF_4CH)) setFlashChipMode(1, 3); // DOUT - ESP8285
          snprintf_P(svalue, sizeof(svalue), PSTR("Successful. Restarting"));
        } else {
          snprintf_P(svalue, sizeof(svalue), PSTR("Failed %s"), ESPhttpUpdate.getLastErrorString().c_str());
        }
        restartflag = 2;  // Restart anyway to keep memory clean webserver
        mqtt_publish_topic_P(0, PSTR("UPGRADE"), svalue);
      }
    }
    break;
  case (STATES/10)*4:
    if (savedatacounter) {
      savedatacounter--;
      if (savedatacounter <= 0) {
        if (sysCfg.savestate) {
          byte mask = 0xFF;
          for (byte i = 0; i < MAX_PULSETIMERS; i++)
            if ((sysCfg.pulsetime[i] > 0) && (sysCfg.pulsetime[i] < 30)) mask &= ~(1 << i);
          if (!((sysCfg.power &mask) == (power &mask))) sysCfg.power = power;
        }
        CFG_Save();
        savedatacounter = sysCfg.savedata;
      }
    }
    if (restartflag) {
      if (restartflag == 211) {
        CFG_Default();
        restartflag = 2;
      }
      if (restartflag == 212) {
        CFG_Erase();
        CFG_Default();
        restartflag = 2;
      }
      if (sysCfg.savestate) sysCfg.power = power;
      if (hlw_flg) hlw_savestate();
      CFG_Save();
      restartflag--;
      if (restartflag <= 0) {
        addLog_P(LOG_LEVEL_INFO, PSTR("APP: Restarting"));
        ESP.restart();
      }
    }
    break;
  case (STATES/10)*6:
    WIFI_Check(wificheckflag);
    wificheckflag = WIFI_RESTART;
    break;
  case (STATES/10)*8:
    if (WiFi.status() == WL_CONNECTED) {
      if (sysCfg.mqtt_enabled) {
        if (!mqttClient.connected()) {
          if (!mqttcounter) {
            mqtt_reconnect();
          } else {
            mqttcounter--;
          }
        }
      } else {
        if (!mqttcounter) {
          mqtt_reconnect();
        }
      }
    }
    break;
  }
}

void serial()
{
  char log[LOGSZ];

  while (Serial.available()) {
    yield();
    SerialInByte = Serial.read();

    // Sonoff dual 19200 baud serial interface
    if (Hexcode) {
      Hexcode--;
      if (Hexcode) {
        ButtonCode = (ButtonCode << 8) | SerialInByte;
        SerialInByte = 0;
      } else {
        if (SerialInByte != 0xA1) ButtonCode = 0;  // 0xA1 - End of Sonoff dual button code
      }
    }
    if (SerialInByte == 0xA0) {                    // 0xA0 - Start of Sonoff dual button code
      SerialInByte = 0;
      ButtonCode = 0;
      Hexcode = 3;
    }

    if (SerialInByte > 127) { // binary data...
      SerialInByteCounter = 0;
      Serial.flush();
      return;
    }
    if (isprint(SerialInByte)) {
      if (SerialInByteCounter < INPUT_BUFFER_SIZE) {  // add char to string if it still fits
        serialInBuf[SerialInByteCounter++] = SerialInByte;
      } else {
        SerialInByteCounter = 0;
      }
    }
    if (SerialInByte == '\n') {
      serialInBuf[SerialInByteCounter] = 0;  // serial data completed
      seriallog_level = (sysCfg.seriallog_level < LOG_LEVEL_INFO) ? LOG_LEVEL_INFO : sysCfg.seriallog_level;
      snprintf_P(log, sizeof(log), PSTR("CMND: %s"), serialInBuf);
      addLog(LOG_LEVEL_INFO, log);
      do_cmnd(serialInBuf);
      SerialInByteCounter = 0;
      Serial.flush();
      return;
    }
  }
}

/********************************************************************************************/

void GPIO_init()
{
  char log[LOGSZ];
  uint8_t mpin;
  mytmplt def_module;

  if (!sysCfg.module || (sysCfg.module >= MAXMODULE)) sysCfg.module = MODULE;

  memcpy_P(&def_module, &modules[sysCfg.module], sizeof(def_module));
  strlcpy(my_module.name, def_module.name, sizeof(my_module.name));
  for (byte i = 0; i < MAX_GPIO_PIN; i++) {
    if (sysCfg.my_module.gp.io[i] > GPIO_NONE) my_module.gp.io[i] = sysCfg.my_module.gp.io[i];
    if ((def_module.gp.io[i] > GPIO_NONE) && (def_module.gp.io[i] < GPIO_USER)) my_module.gp.io[i] = def_module.gp.io[i];
  }

  for (byte i = 0; i < GPIO_MAX; i++) pin[i] = 99;
  for (byte i = 0; i < MAX_GPIO_PIN; i++) {
    mpin = my_module.gp.io[i];

//  snprintf_P(log, sizeof(log), PSTR("DBG: gpio pin %d, mpin %d"), i, mpin);
//  addLog(LOG_LEVEL_DEBUG, log);
    
    if (mpin) {
      if ((mpin >= GPIO_REL1_INV) && (mpin <= GPIO_REL4_INV)) {
        rel_inverted[mpin - GPIO_REL1_INV] = 1;
        mpin -= 4;
      }
      else if ((mpin >= GPIO_LED1_INV) && (mpin <= GPIO_LED4_INV)) {
        led_inverted[mpin - GPIO_LED1_INV] = 1;
        mpin -= 4;
      }
      else if (mpin == GPIO_DHT11) dht_type = mpin;
      else if (mpin == GPIO_DHT21) {
        dht_type = mpin;
        mpin--;
      }
      else if (mpin == GPIO_DHT22) {
        dht_type = mpin;
        mpin -= 2;
      }
      pin[mpin] = i;
    }
  }

  Maxdevice = 1;
  if (sysCfg.module == SONOFF_DUAL) {
    Maxdevice = 2;
    Baudrate = 19200;
  }
  else if (sysCfg.module == CH4) {
    Maxdevice = 4;
    Baudrate = 19200;
  }
  else if (sysCfg.module == SONOFF_LED) {
    pin[GPIO_WS2812] = 99;  // I do not allow both Sonoff Led AND WS2812 led
    sl_init();
  }
  else {
    Maxdevice = 0;
    for (byte i = 0; i < 4; i++) {
      if (pin[GPIO_REL1 +i] < 99) {
        pinMode(pin[GPIO_REL1 +i], OUTPUT);
        Maxdevice++;
      }
      if (pin[GPIO_KEY1 +i] < 99) pinMode(pin[GPIO_KEY1 +i], INPUT_PULLUP);
    }
  }
  for (byte i = 0; i < 4; i++) {
    if (pin[GPIO_LED1 +i] < 99) {
      pinMode(pin[GPIO_LED1 +i], OUTPUT);
      digitalWrite(pin[GPIO_LED1 +i], led_inverted[i]);
    }
    if (pin[GPIO_SWT1 +i] < 99) {
      swt_flg = 1;
      pinMode(pin[GPIO_SWT1 +i], INPUT_PULLUP);
      lastwallswitch[i] = digitalRead(pin[GPIO_SWT1 +i]);  // set global now so doesn't change the saved power state on first switch check
    }
  }
  if (sysCfg.module == EXS_RELAY) {
    setLatchingRelay(0,2);
    setLatchingRelay(1,2);
  }
  setLed(sysCfg.ledstate &8);

  hlw_flg = ((pin[GPIO_HLW_SEL] < 99) && (pin[GPIO_HLW_CF1] < 99) && (pin[GPIO_HLW_CF] < 99));
  if (hlw_flg) hlw_init();

#ifdef USE_DHT
  if (dht_type) dht_init();
#endif  // USE_DHT

#ifdef USE_DS18x20
  if (pin[GPIO_DSB] < 99) ds18x20_init();
#endif  // USE_DS18x20

#ifdef USE_I2C
  i2c_flg = ((pin[GPIO_I2C_SCL] < 99) && (pin[GPIO_I2C_SDA] < 99));
  if (i2c_flg) Wire.begin(pin[GPIO_I2C_SDA], pin[GPIO_I2C_SCL]);
#endif  // USE_I2C

#ifdef USE_WS2812
  if (pin[GPIO_WS2812] < 99) ws2812_init();
#endif  // USE_WS2812

#ifdef USE_IR_REMOTE
  if (pin[GPIO_IRSEND] < 99) ir_send_init();
#endif // USE_IR_REMOTE
}

void setup()
{
  char log[LOGSZ];
  byte idx;

  Serial.begin(Baudrate);
  delay(10);
  Serial.println();
  seriallog_level = LOG_LEVEL_INFO;  // Allow specific serial messages until config loaded

  snprintf_P(Version, sizeof(Version), PSTR("%d.%d.%d"), VERSION >> 24 & 0xff, VERSION >> 16 & 0xff, VERSION >> 8 & 0xff);
  if (VERSION & 0x1f) {
    idx = strlen(Version);
    Version[idx] = 96 + (VERSION & 0x1f);
    Version[idx +1] = 0;
  }
  if (!spiffsPresent())
    addLog_P(LOG_LEVEL_ERROR, PSTR("SPIFFS: ERROR - No spiffs present. Please reflash with at least 16K SPIFFS"));
#ifdef USE_SPIFFS
  initSpiffs();
#endif
  CFG_Load();
  CFG_Delta();

  osw_init();

  sysCfg.bootcount++;
  snprintf_P(log, sizeof(log), PSTR("APP: Bootcount %d"), sysCfg.bootcount);
  addLog(LOG_LEVEL_DEBUG, log);
  savedatacounter = sysCfg.savedata;
  seriallog_timer = SERIALLOG_TIMER;
  seriallog_level = sysCfg.seriallog_level;
#ifndef USE_EMULATION
  sysCfg.emulation = 0;
#endif  // USE_EMULATION
  syslog_level = (sysCfg.emulation) ? 0 : sysCfg.syslog_level;
  sleep = sysCfg.sleep;

  GPIO_init();

  if (Serial.baudRate() != Baudrate) {
    if (seriallog_level) {
      snprintf_P(log, sizeof(log), PSTR("APP: Change baudrate to %d and Serial logging will be disabled in %d seconds"), Baudrate, seriallog_timer);
      addLog(LOG_LEVEL_INFO, log);
    }
    delay(100);
    Serial.flush();
    Serial.begin(Baudrate);
    delay(10);
    Serial.println();
  }

  if (strstr(sysCfg.hostname, "%")) {
    strlcpy(sysCfg.hostname, WIFI_HOSTNAME, sizeof(sysCfg.hostname));
    snprintf_P(Hostname, sizeof(Hostname)-1, sysCfg.hostname, sysCfg.mqtt_topic, ESP.getChipId() & 0x1FFF);
  } else {
    snprintf_P(Hostname, sizeof(Hostname)-1, sysCfg.hostname);
  }
  WIFI_Connect();

  getClient(MQTTClient, sysCfg.mqtt_client, sizeof(MQTTClient));

  if (sysCfg.module == MOTOR) sysCfg.poweronstate = 1;  // Needs always on else in limbo!
  if (ESP.getResetReason() == "Power on") {
    if (sysCfg.poweronstate == 0) {       // All off
      power = 0;
      setRelay(power);
    }
    else if (sysCfg.poweronstate == 1) {  // All on
      power = ((0x00FF << Maxdevice) >> 8);
      setRelay(power);
    }
    else if (sysCfg.poweronstate == 2) {  // All saved state toggle
      power = (sysCfg.power & ((0x00FF << Maxdevice) >> 8)) ^ 0xFF;
      if (sysCfg.savestate) setRelay(power);
    }
    else if (sysCfg.poweronstate == 3) {  // All saved state
      power = sysCfg.power & ((0x00FF << Maxdevice) >> 8);
      if (sysCfg.savestate) setRelay(power);
    }
  } else {
    power = sysCfg.power & ((0x00FF << Maxdevice) >> 8);
    if (sysCfg.savestate) setRelay(power);
  }
  blink_powersave = power;

  rtc_init();

  snprintf_P(log, sizeof(log), PSTR("APP: Project %s %s (Topic %s, Fallback %s, GroupTopic %s) Version %s"),
    PROJECT, sysCfg.friendlyname[0], sysCfg.mqtt_topic, MQTTClient, sysCfg.mqtt_grptopic, Version);
  addLog(LOG_LEVEL_INFO, log);
}

void loop()
{
  osw_loop();
  
#ifdef USE_WEBSERVER
  pollDnsWeb();
#endif  // USE_WEBSERVER

#ifdef USE_EMULATION
  if (sysCfg.emulation) pollUDP();
#endif  // USE_EMULATION

  if (millis() >= timerxs) stateloop();
  if (sysCfg.mqtt_enabled) mqttClient.loop();
  if (Serial.available()) serial();

//  yield();     // yield == delay(0), delay contains yield, auto yield in loop
  delay(sleep);  // https://github.com/esp8266/Arduino/issues/2021
}
