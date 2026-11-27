// ===== WIFI AND REMOTE CONTROL =====
// Runs on core 0, so connecting and TLS never stall the animation on core 1.
//
// WiFi setup: with no saved network (or after holding the Top button 3 s) the M5
// opens a "Jasper Lights Setup" WiFi hotspot. Join it from a phone, a setup page
// opens, pick the home network and type its password. The lights keep running.
//
// Remote control: the lights connect to an MQTT broker (EMQX Cloud Serverless) given in
// secrets.h, and the snoo Cloud Run server (Google Home) sends commands through it.
//   jasperlights/cmd       in:  {"id":"abc","cmd":"bright +"}  (see handleControl())
//   jasperlights/state     out, retained: current state, with "ack" = last command id
//   jasperlights/patterns  out, retained: pattern names, in order
//   jasperlights/online    out, retained: "1", or "0" (set by the broker if we vanish)

#if REMOTE_ENABLED
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

#define TOPIC_CMD      "jasperlights/cmd"
#define TOPIC_STATE    "jasperlights/state"
#define TOPIC_PATTERNS "jasperlights/patterns"
#define TOPIC_ONLINE   "jasperlights/online"
#define SETUP_AP_NAME  "Jasper Lights Setup"

// Roots the broker's certificate may chain to: DigiCert (EMQX Cloud) and
// Let's Encrypt. Several in one string is fine; any match is accepted.
static const char ROOT_CAS[] PROGMEM = R"PEM(
-----BEGIN CERTIFICATE-----
MIIDjjCCAnagAwIBAgIQAzrx5qcRqaC7KGSxHQn65TANBgkqhkiG9w0BAQsFADBh
MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3
d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBH
MjAeFw0xMzA4MDExMjAwMDBaFw0zODAxMTUxMjAwMDBaMGExCzAJBgNVBAYTAlVT
MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j
b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IEcyMIIBIjANBgkqhkiG
9w0BAQEFAAOCAQ8AMIIBCgKCAQEAuzfNNNx7a8myaJCtSnX/RrohCgiN9RlUyfuI
2/Ou8jqJkTx65qsGGmvPrC3oXgkkRLpimn7Wo6h+4FR1IAWsULecYxpsMNzaHxmx
1x7e/dfgy5SDN67sH0NO3Xss0r0upS/kqbitOtSZpLYl6ZtrAGCSYP9PIUkY92eQ
q2EGnI/yuum06ZIya7XzV+hdG82MHauVBJVJ8zUtluNJbd134/tJS7SsVQepj5Wz
tCO7TG1F8PapspUwtP1MVYwnSlcUfIKdzXOS0xZKBgyMUNGPHgm+F6HmIcr9g+UQ
vIOlCsRnKPZzFBQ9RnbDhxSJITRNrw9FDKZJobq7nMWxM4MphQIDAQABo0IwQDAP
BgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB/wQEAwIBhjAdBgNVHQ4EFgQUTiJUIBiV
5uNu5g/6+rkS7QYXjzkwDQYJKoZIhvcNAQELBQADggEBAGBnKJRvDkhj6zHd6mcY
1Yl9PMWLSn/pvtsrF9+wX3N3KjITOYFnQoQj8kVnNeyIv/iPsGEMNKSuIEyExtv4
NeF22d+mQrvHRAiGfzZ0JFrabA0UWTW98kndth/Jsw1HKj2ZL7tcu7XUIOGZX1NG
Fdtom/DzMNU+MeKNhJ7jitralj41E6Vf8PlwUHBHQRFXGU7Aj64GxJUTFy8bJZ91
8rGOmaFvE7FBcf6IKshPECBV1/MUReXgRPTqh5Uykw7+U0b6LJ3/iyK5S9kJRaTe
pLiaWN0bfVKfjllDiIGknibVb63dDcY3fe0Dkhvld1927jyNxF1WW6LZZm6zNTfl
MrY=
-----END CERTIFICATE-----
-----BEGIN CERTIFICATE-----
MIIDrzCCApegAwIBAgIQCDvgVpBCRrGhdWrJWZHHSjANBgkqhkiG9w0BAQUFADBh
MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3
d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBD
QTAeFw0wNjExMTAwMDAwMDBaFw0zMTExMTAwMDAwMDBaMGExCzAJBgNVBAYTAlVT
MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j
b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IENBMIIBIjANBgkqhkiG
9w0BAQEFAAOCAQ8AMIIBCgKCAQEA4jvhEXLeqKTTo1eqUKKPC3eQyaKl7hLOllsB
CSDMAZOnTjC3U/dDxGkAV53ijSLdhwZAAIEJzs4bg7/fzTtxRuLWZscFs3YnFo97
nh6Vfe63SKMI2tavegw5BmV/Sl0fvBf4q77uKNd0f3p4mVmFaG5cIzJLv07A6Fpt
43C/dxC//AH2hdmoRBBYMql1GNXRor5H4idq9Joz+EkIYIvUX7Q6hL+hqkpMfT7P
T19sdl6gSzeRntwi5m3OFBqOasv+zbMUZBfHWymeMr/y7vrTC0LUq7dBMtoM1O/4
gdW7jVg/tRvoSSiicNoxBN33shbyTApOB6jtSj1etX+jkMOvJwIDAQABo2MwYTAO
BgNVHQ8BAf8EBAMCAYYwDwYDVR0TAQH/BAUwAwEB/zAdBgNVHQ4EFgQUA95QNVbR
TLtm8KPiGxvDl7I90VUwHwYDVR0jBBgwFoAUA95QNVbRTLtm8KPiGxvDl7I90VUw
DQYJKoZIhvcNAQEFBQADggEBAMucN6pIExIK+t1EnE9SsPTfrgT1eXkIoyQY/Esr
hMAtudXH/vTBH1jLuG2cenTnmCmrEbXjcKChzUyImZOMkXDiqw8cvpOp/2PV5Adg
06O/nVsJ8dWO41P0jmP6P6fbtGbfYmbW0W5BjfIttep3Sp+dWOIrWcBAI+0tKIJF
PnlUkiaY4IBIqDfv8NZ5YBberOgOzW6sRBc4L0na4UU+Krk2U886UAb3LujEV0ls
YSEY1QSteDwsOoBrp+uvFRTp2InBuThs4pFsiv9kuXclVzDAGySj4dzp30d8tbQk
CAUw7C29C79Fv1C5qfPrmAESrciIxpg0X40KPMbp1ZWVbd4=
-----END CERTIFICATE-----
-----BEGIN CERTIFICATE-----
MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw
TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh
cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4
WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu
ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY
MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc
h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+
0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U
A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW
T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH
B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC
B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv
KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn
OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn
jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw
qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI
rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV
HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq
hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL
ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ
3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK
NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5
ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur
TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC
jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc
oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq
4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA
mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d
emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=
-----END CERTIFICATE-----
)PEM";

struct RemoteCmd {
  char id[24];
  char cmd[48];
};

static WiFiManager wm;
static WiFiClientSecure tlsClient;
static PubSubClient mqtt(tlsClient);
static QueueHandle_t cmdQueue;
static SemaphoreHandle_t stateLock;
static String stateJson;          // Latest state, built by the main loop
static volatile bool statePending = false;
static volatile bool portalRequested = false;
static volatile bool forgetRequested = false;
volatile NetStatus netStatus = NET_CONNECTING;
static volatile bool wifiSaved = false;  // Read by the main loop; WiFiManager stays on core 0

static void onMqttMessage(char *topic, byte *payload, unsigned int len) {
  JsonDocument doc;
  if (deserializeJson(doc, payload, len)) return;
  RemoteCmd c;
  strlcpy(c.id, doc["id"] | "", sizeof(c.id));
  strlcpy(c.cmd, doc["cmd"] | "", sizeof(c.cmd));
  if (c.cmd[0]) xQueueSend(cmdQueue, &c, 0);
}

static bool mqttConnect() {
  String clientId = "jasperlights-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  if (!mqtt.connect(clientId.c_str(), MQTT_USER, MQTT_PASS, TOPIC_ONLINE, 1, true, "0")) {
    Serial.printf("MQTT connect failed (state %d)\n", mqtt.state());
    return false;
  }
  mqtt.subscribe(TOPIC_CMD, 1);
  mqtt.publish(TOPIC_ONLINE, "1", true);

  JsonDocument doc;
  for (int i = 0; i < NUM_PATTERNS; i++) doc.add(patternList[i].name);
  String names;
  serializeJson(doc, names);
  mqtt.publish(TOPIC_PATTERNS, names.c_str(), true);
  statePending = true;  // Publish the current state too
  Serial.println("Remote control connected");
  return true;
}

static void netTask(void *) {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("jasper-lights");
  wm.setConfigPortalBlocking(false);
  wm.setTitle("Jasper Lights");
  const char *menu[] = { "wifi" };
  wm.setMenu(menu, 1);
  bool portal = false;
  wifiSaved = wm.getWiFiIsSaved();

  if (wifiSaved) {
    WiFi.setAutoReconnect(true);
    WiFi.begin();
  } else {
    portalRequested = true;
  }

  tlsClient.setCACert(ROOT_CAS);
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onMqttMessage);
  mqtt.setBufferSize(1024);
  mqtt.setKeepAlive(30);
  mqtt.setSocketTimeout(10);
  unsigned long nextMqttTry = 0;

  for (;;) {
    if (forgetRequested) {
      forgetRequested = false;
      mqtt.disconnect();
      wm.resetSettings();  // Forget the saved network
      wifiSaved = false;
      portalRequested = true;
    }
    if (portalRequested) {
      portalRequested = false;
      mqtt.disconnect();
      wm.startConfigPortal(SETUP_AP_NAME);  // Returns at once; served by process()
      portal = true;
      Serial.println("WiFi setup: join the '" SETUP_AP_NAME "' network from a phone");
    }
    if (portal) {
      wm.process();
      if (!wm.getConfigPortalActive()) {
        wifiSaved = wm.getWiFiIsSaved();
        portal = false;
        WiFi.mode(WIFI_STA);
        WiFi.setAutoReconnect(true);
        if (WiFi.status() != WL_CONNECTED) WiFi.begin();
      }
    }

    if (WiFi.status() == WL_CONNECTED && !portal) {
      if (!mqtt.connected() && millis() >= nextMqttTry) {
        if (!mqttConnect()) nextMqttTry = millis() + 15000;
      }
      mqtt.loop();
      if (mqtt.connected() && statePending) {
        xSemaphoreTake(stateLock, portMAX_DELAY);
        String s = stateJson;
        statePending = false;
        xSemaphoreGive(stateLock);
        mqtt.publish(TOPIC_STATE, s.c_str(), true);
      }
    }

    netStatus = portal ? NET_SETUP
              : WiFi.status() != WL_CONNECTED ? NET_CONNECTING
              : mqtt.connected() ? NET_ONLINE : NET_NO_SERVER;
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void netBegin() {
  cmdQueue = xQueueCreate(8, sizeof(RemoteCmd));
  stateLock = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(netTask, "net", 12288, nullptr, 1, nullptr, 0);
}

// True once a home network has been set up, so "off" can stay listening for Google
bool netConfigured() {
  return wifiSaved;
}

void netStartSetup() { portalRequested = true; }
void netForget() { forgetRequested = true; }

// Main loop: run commands that arrived, and send the state when it changes
void netPoll() {
  static String lastState;
  static char ack[24] = "";
  bool acked = false;

  RemoteCmd c;
  while (xQueueReceive(cmdQueue, &c, 0)) {
    Serial.printf("Remote: %s\n", c.cmd);
    if (!handleControl(c.cmd)) Serial.println("  ?? Unknown remote command");
    strlcpy(ack, c.id, sizeof(ack));
    acked = true;
  }
  static unsigned long lastBuild = 0;  // Check for changes 4 times a second
  if (!acked && millis() - lastBuild < 250) return;
  lastBuild = millis();

  JsonDocument doc;
  doc["on"] = lightsOn;
  doc["auto"] = autoMode;
  doc["pattern"] = currentPattern + 1;
  doc["name"] = patternList[currentPattern].name;
  doc["bright"] = brightnessIndex + 1;
  doc["brightLevels"] = NUM_BRIGHTNESS_LEVELS;
  doc["speed"] = speedIndex + 1;
  doc["speedLevels"] = NUM_SPEED_LEVELS;
  doc["version"] = VERSION;
  doc["ack"] = ack;
  String s;
  serializeJson(doc, s);
  if (s != lastState || acked) {  // Always answer a command, even if nothing changed
    lastState = s;
    xSemaphoreTake(stateLock, portMAX_DELAY);
    stateJson = s;
    statePending = true;
    xSemaphoreGive(stateLock);
  }
}

#else  // No secrets.h: no WiFi, everything works from the buttons alone
volatile NetStatus netStatus = NET_DISABLED;
void netBegin() {}
bool netConfigured() { return false; }
void netStartSetup() { Serial.println("  ?? Remote control is off: add secrets.h and rebuild"); }
void netForget() { netStartSetup(); }
void netPoll() {}
#endif
