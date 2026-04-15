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

void publishSeqMqtt(const char* name, int init, int initialized, int etape, int duree) {
  static char topic[80];
  static char payload[80];

  snprintf(topic, sizeof(topic), "esp32/seq/%s", name);

  if(client.connected()) {
    // On crée un format JSON : {"init":1,"initd":0,"et":3,"dur":1500}
    snprintf(payload, sizeof(payload), 
             "{\"init\":%d,\"initd\":%d,\"etape\":%d,\"duree\":%d}", 
             init, initialized, etape, duree);
    
    client.publish(topic, payload);
  }
}
