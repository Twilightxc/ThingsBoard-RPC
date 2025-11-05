// ***************    RELAY   ******************
// ********** CONNECTION PART ******************
// library
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// config
char WIFI_SSID[] = "Wokwi-GUEST";
char WIFI_PASSWORD[] = "";

const char* ID = "";
const char* token = "lWCVMelxZnzHhgsl6Skj"; // <-- Device Token From LAB Hello-ThingsBoard
const char* password = "";
const char* thingsboard_server = "104.196.24.70";
const int port = 1883;

const char* STUDENT_ID = "";   // <-- Replace with your Student ID
const char* FIRMWARE_VERSION = "";   // <-- Replace here with your Firmware Version

String DEVICE_STATUS = "";

// Subscribe to ThingsBoard RPC request topics
const char* mqtt_sub_topic = "v1/devices/me/rpc/request/+";

// Network client
WiFiClient espClient;
PubSubClient client(espClient);

// define connection function 
void chkConnection();
void InitWiFi();
bool reconnect();
void connectToMQTTBroker();
void callback(const char* topic, byte* message, unsigned int length);

// ************** HARDWARE PART ****************
// pin
#define relay_1 35
#define relay_2 36
#define relay_3 37
#define relay_4 38

// define hardware function
void setHardware();

// ******************** MAIN ********************
void setup() {
  Serial.begin(115200);

  InitWiFi();

  client.setServer(thingsboard_server, port);
  client.setCallback(callback);

  setHardware(); 
}
void loop() {
  chkConnection();
  client.loop();
  delay(100);
}

// ******************** Function ********************
void setHardware() {
  pinMode(relay_1, OUTPUT);
  pinMode(relay_2, OUTPUT);
  pinMode(relay_3, OUTPUT);
  pinMode(relay_4, OUTPUT);

  digitalWrite(relay_1, LOW);
  digitalWrite(relay_2, LOW);
  digitalWrite(relay_3, LOW);
  digitalWrite(relay_4, LOW);
}

void chkConnection() {
  if (!reconnect()) {
    return;
  }
  if (!client.connected()) {
    connectToMQTTBroker();
  }
}

void InitWiFi() {
  Serial.println("🛜 Connecting to AP ...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 30000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ Connected to AP Success");
  } else {
    Serial.println("\n❌ Failed to connect to WiFi, restarting...");
    ESP.restart();
  }
}

bool reconnect() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("⚠️ WiFi disconnected! Reconnecting...");
    unsigned long startAttemptTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 15000) {
      delay(500);
      Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\n✅ Reconnected to WiFi");
      return true;
    } else {
      Serial.println("\n❌ WiFi reconnect failed");
      return false;
    }
  }
  return true;
}

void connectToMQTTBroker() {
  while (!client.connected()) {
    Serial.println("☁️ Connecting to ThingsBoard ...");
    if (client.connect(ID, token, password)) {
      Serial.println("✅Connected to ThingsBoard Success");
      client.subscribe(mqtt_sub_topic);  // Subscribe after successful connection
  DEVICE_STATUS = "ONLINE";
    } else {
      Serial.println("❌ Connection to ThingsBoard FAILED, retrying in 5 seconds");
      delay(5000);
    }
  }
}

// ---------------- RPC helper functions ----------------
String extractRequestId(const char* topic) {
  String t(topic);
  int idx = t.lastIndexOf('/');
  if (idx < 0) return String("0");
  return t.substring(idx + 1);
}

void publishRpcResponse(const String &reqId, JsonDocument &resp) {
  if (!client.connected()) {
    connectToMQTTBroker();
  }
  String t = "v1/devices/me/rpc/response/" + reqId;
  String payload;
  serializeJson(resp, payload);
  client.publish(t.c_str(), payload.c_str());
}

int resolvePin(int requested) {
  if (requested >= 1 && requested <= 4) {
    switch (requested) {
      case 1: return relay_1;
      case 2: return relay_2;
      case 3: return relay_3;
      case 4: return relay_4;
    }
  }
  if (requested == relay_1 || requested == relay_2 || requested == relay_3 || requested == relay_4) {
    return requested;
  }
  return -1;
}

void processRpc(const String &method, JsonObject &params, const String &reqId) {
  if (method != "setGPIO" && method != "setGpioStatus") {
    Serial.print("Unknown RPC method: "); Serial.println(method);
    DynamicJsonDocument resp(128);
    resp["status"] = "error: unknown method";
    publishRpcResponse(reqId, resp);
    return;
  }

  if (params["pin"].isNull()) {
    DynamicJsonDocument resp(128);
    resp["status"] = "error: missing pin";
    publishRpcResponse(reqId, resp);
    return;
  }

  int requestedPin = params["pin"].as<int>();
  int pin = resolvePin(requestedPin);
  if (pin < 0) {
    DynamicJsonDocument resp(128);
    resp["status"] = "error: invalid pin";
    publishRpcResponse(reqId, resp);
    return;
  }

  int value = 0;
  bool enabled = false;

  if (method == "setGPIO") {
    if (params["value"].isNull()) {
      DynamicJsonDocument resp(128);
      resp["status"] = "error: missing value";
      publishRpcResponse(reqId, resp);
      return;
    }
    value = params["value"].as<int>();
    enabled = (value != 0);
  } else {
    if (params["enabled"].isNull()) {
      DynamicJsonDocument resp(128);
      resp["status"] = "error: missing enabled";
      publishRpcResponse(reqId, resp);
      return;
    }
    enabled = params["enabled"].as<bool>();
    value = enabled ? 1 : 0;
  }

  //Control the GPIO pin
  Serial.print("Setting pin "); Serial.print(pin); Serial.print(" to "); Serial.println(value);
  pinMode(pin, OUTPUT);
  digitalWrite(pin, value ? HIGH : LOW);

  DynamicJsonDocument resp(256);
  resp["pin"] = requestedPin;
  if (method == "setGpioStatus") resp["enabled"] = enabled; else resp["value"] = value;
  resp["status"] = "success";
  publishRpcResponse(reqId, resp);
}
void callback(const char* topic, byte* message, unsigned int length) {
  String messageTemp;
  for (unsigned int i = 0; i < length; i++) {
    messageTemp += (char)message[i];
  }

  Serial.print("📨 Received RPC JSON on topic: ");
  Serial.println(topic);
  Serial.print("Payload: ");
  Serial.println(messageTemp);

  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, messageTemp);
  if (error) {
    Serial.print("JSON Parsing failed: "); Serial.println(error.c_str());
    return;
  }

  if (!doc["method"].is<const char*>()) {
    Serial.println("❌ RPC payload missing 'method'");
    return;
  }

  String method = String((const char*)doc["method"]);
  JsonObject params = doc["params"].is<JsonObject>() ? doc["params"].as<JsonObject>() : JsonObject();

  // unwrap ThingsBoard wrapper if present
  if (method == "setState" && !params["method"].isNull() && params["params"].is<JsonObject>()) {
    if (params["method"].is<const char*>() && params["params"].is<JsonObject>()) {
      String inner = String((const char*)params["method"]);
      JsonObject innerParams = params["params"].as<JsonObject>();
      Serial.print("↳ Unwrapped setState -> method: "); Serial.println(inner);
      method = inner;
      params = innerParams;
    }
  }

  String reqId = extractRequestId(topic);
  processRpc(method, params, reqId);
}

