#include "mqtt.h"

// MQTT
const char* mqtt_server = "192.168.1.23"; // IP du broker MQTT
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

void publishMqtt(const char* topic, const char* value) {

  if(client.connected())
  {
      client.publish(topic, value);
  }
}