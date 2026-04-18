#include "wifi.h"
#include "secrets.h"

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
    WiFi.begin(SECRET_SSID, SECRET_PASSWORD, 0, NULL, true);
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
        Serial.print(SECRET_SSID);
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

    Serial.print("WiFi connecté: ");
    Serial.println(SECRET_SSID);
    return 1;
}