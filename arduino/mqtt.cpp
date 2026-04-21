#include "mqtt.h"

// MQTT
const char* mqtt_server = "192.168.1.10"; // IP du broker MQTT
const char* subscribe = "esp32/commands";
const char* id = "esp32";
const char* user = "home";
const char* pass = "client_mqtt_pass$";

WiFiClient espClient;
PubSubClient client(espClient);

int mqtt_command = CMD_NONE;

void callback(char* topic, byte* message, unsigned int length) {

  Serial.print("Message reçu sur le topic: ");
  Serial.print(topic);
  Serial.print(" Message: ");

  String messageTemp;

  for (int i = 0; i < length; i++) {
    Serial.print((char)message[i]);
    messageTemp += (char)message[i];
  }

  Serial.println();

  if(strcmp(topic,subscribe) == 0)
  {
    if(messageTemp == "OPEN")
    {
      mqtt_command = CMD_OPEN;
    }
    if(messageTemp == "CLOSE")
    {
      mqtt_command = CMD_CLOSE;
    }
    if(messageTemp == "DB_UP")
    {
      mqtt_command = CMD_DB_UP;
    }
    if(messageTemp == "DB_DOWN")
    {
      mqtt_command = CMD_DB_DOWN;
    }
  }
}

int connectMqtt() {

  Serial.print("Mqtt connected = ");
  Serial.println(client.connected());

  if (client.connected())
      return 1;
    
  Serial.println("Connexion MQTT...");

  if (client.connect(id, user, pass)) {

    Serial.println("connecté");

    client.subscribe(subscribe);

    return 1;
  } 

  Serial.print("échec, state=");
  Serial.print(client.state());
  Serial.println();
  return 0;
}

int statusMqtt() {
  if (client.connected())
      return 1;
  return 0;
}

void setupMqtt() {

  client.setServer(mqtt_server, 1883);

  client.setCallback(callback);
}

int checkMqtt() {

  mqtt_command = CMD_NONE;

  if(client.connected())
    client.loop();

  return mqtt_command;
}

static char topic[80];
static char payload[280];

void publishSeqMqtt(const char* name, int init, int initialized, int etape, int duree) {

  snprintf(topic, sizeof(topic), "esp32/seq/%s", name);

  if(client.connected()) {
    // On crée un format JSON : {"init":1,"initd":0,"et":3,"dur":1500}
    snprintf(payload, sizeof(payload), 
             "{\"init\":%d,\"initd\":%d,\"etape\":%d,\"duree\":%d}", 
             init, initialized, etape, duree);
    
    client.publish(topic, payload);
  }
}

void publishStatesMqtt(
  int UUID_Identifie,
  bool bEndofScan,
  int rssi_add
) {

  if(client.connected()) {
    
    snprintf(topic, sizeof(topic), "esp32/states/vars");

    snprintf(payload, sizeof(payload), 
             "{\"UUID_Identifie\":%d,\"bEndofScan\":%d,\"rssi_add\":%d}", 
             UUID_Identifie, bEndofScan, rssi_add);
    
    client.publish(topic, payload);
  }
}


void publishInputsMqtt(
  int IN_Echo1,
  int IN_Echo2,
  int IN_PortillonOuvert,
  int IN_PortailOuvert,
  int IN_BoutonReset,
  int IN_MqttCommand
) {

  if(client.connected()) {
    
    snprintf(topic, sizeof(topic), "esp32/states/in");

    snprintf(payload, sizeof(payload), 
             "{\"IN_Echo1\":%d,\"IN_Echo2\":%d,\"IN_PortillonOuvert\":%d,\"IN_PortailOuvert\":%d,\"IN_BoutonReset\":%d,\"IN_MqttCommand\":%d}", 
             IN_Echo1, IN_Echo2, IN_PortillonOuvert, IN_PortailOuvert, IN_BoutonReset, IN_MqttCommand);
    
    client.publish(topic, payload);
  }
}


void publishOutputsMqtt(
  int OUT_OuverturePortail,
  int OUT_DeverrouillagePortillon,
  int OUT_Led,
  int OUT_EchoTrigger
) {

  if(client.connected()) {
    
    snprintf(topic, sizeof(topic), "esp32/states/out");

    snprintf(payload, sizeof(payload), 
             "{\"OUT_OuverturePortail\":%d,\"OUT_DeverrouillagePortillon\":%d,\"OUT_Led\":%d,\"OUT_EchoTrigger\":%d}", 
             OUT_OuverturePortail, OUT_DeverrouillagePortillon, OUT_Led, OUT_EchoTrigger);
    
    client.publish(topic, payload);
  }
}

void publishDebug(
  const char* message
) {

  if(client.connected()) {
    client.publish("esp32/debug", message);
  }
}

