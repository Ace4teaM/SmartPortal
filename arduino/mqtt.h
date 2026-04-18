#ifndef MQTT_H
#define MQTT_H

#include <PubSubClient.h>
#include <WiFi.h>

int statusMqtt();
void setupMqtt();
int connectMqtt();
int checkMqtt();
void publishSeqMqtt(const char* name, int init, int initialized, int etape, int duree);
void publishStatesMqtt(
  int UUID_Identifie,
  bool bEndofScan
);
void publishInputsMqtt(
  int IN_Echo1,
  int IN_Echo2,
  int IN_PortillonOuvert,
  int IN_PortailOuvert,
  int IN_BoutonReset,
  int IN_MqttCommand
);
void publishOutputsMqtt(
  int OUT_OuverturePortail,
  int OUT_DeverrouillagePortillon,
  int OUT_Led,
  int OUT_EchoTrigger
);


#define CMD_NONE 0
#define CMD_OPEN 1
#define CMD_CLOSE 2

#endif
