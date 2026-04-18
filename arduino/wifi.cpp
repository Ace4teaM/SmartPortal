#include "wifi.h"

//const char* ssid = "aceteam_ext";
//const char* password = "uGe395qLM%@Fza740%#WW6538!";
const char* ssid = "aceteam2G";
const char* password = "emyleplusbeaudesbebes";

void checkSignal() {
  int n = WiFi.scanNetworks();
  for (int i = 0; i < n; ++i) {
    Serial.print("Signal trouvé : ");
    Serial.print(WiFi.SSID(i));
    Serial.print(": ");
    Serial.print(WiFi.RSSI(i)); // Puissance en dBm
    Serial.println(" dBm");
  }
}

void setupWifi() {
    Serial.println("Connexion au WiFi...");
    checkSignal();
    WiFi.begin(ssid, password, 0, NULL, true);
}

int statusWifi() {
  int status = WiFi.waitForConnectResult();
  if (status == WL_CONNECTED)
      return 1;
  return 0;
}

int connectWifi() {
    int status = WiFi.waitForConnectResult();

    Serial.print("WiFi status = ");
    Serial.println(status);

    if (status == WL_NO_SHIELD) {
        Serial.println("WiFi shield not present");
        return 0;
    }

    if (status == WL_IDLE_STATUS ) {
        Serial.print("Connexion au WiFi ");
        Serial.print(ssid);
        Serial.println(" en cours... ");
        return 0;
    }

    if (status == WL_NO_SSID_AVAIL ) {
        Serial.println("Aucun SSID disponible");
        return 0;
    }

    if (status == WL_CONNECT_FAILED ) {
        Serial.println("Echec de la connexion");
        // nouvelle tentative ...
    }

    if (status == WL_CONNECTION_LOST ) {
        Serial.println("Connexion perdue");
        // nouvelle tentative ...
    }

    if (status == WL_DISCONNECTED ) {
        Serial.println("Déconnecté");
        // nouvelle tentative ...
    }

    if (status != WL_CONNECTED) {
        setupWifi();
        return WiFi.waitForConnectResult() == WL_CONNECTED ? 1 : 0;
    }

    Serial.println("WiFi connecté");
    return 1;
}