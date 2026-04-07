#include "wifi.h"

const char* ssid = "aceteam2G";
const char* password = "emyleplusbeaudesbebes";

void setupWifi() {
    Serial.println("Connexion au WiFi...");
    WiFi.begin(ssid, password);
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
        Serial.println("Connexion au WiFi en cours...");
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