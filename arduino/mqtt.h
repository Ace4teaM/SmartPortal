#ifndef MQTT_H
#define MQTT_H

#include <PubSubClient.h>
#include <WiFi.h>

void setupMqtt();
int connectMqtt();
int checkMqtt();
void publishMqtt(const char* topic, const char* value);

#define CMD_NONE 0
#define CMD_OPEN 1
#define CMD_CLOSE 2

#endif
