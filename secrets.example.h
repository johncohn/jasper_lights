// Copy to secrets.h (not committed) and fill in, to turn on WiFi and remote
// control. Without secrets.h the lights work from the buttons alone.
// The broker is an EMQX Cloud Serverless deployment; these are the lights' login.
#define MQTT_HOST "xxxxxxxx.ala.us-east-1.emqxsl.com"
#define MQTT_PORT 8883
#define MQTT_USER "jasper-lights"
#define MQTT_PASS "change-me"
