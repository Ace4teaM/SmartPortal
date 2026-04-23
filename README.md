# SmartPortal
Un projet arduino de gestion d'ouverture/fermeture de portail basé sur les SmartTag



# Arduino

**secrets.h**

```c
#ifndef SECRETS_H
#define SECRETS_H

#define SECRET_SSID "your-wifi-ssid"
#define SECRET_PASSWORD "your-wifi-password"
#define SECRET_SSID_BIS "your-wifi-ssid"
#define SECRET_PASSWORD_BIS "your-wifi-password"

#define BLE_ACCEPT_UUID_1 "your-uuid-here"
#define BLE_ACCEPT_UUID_2 "your-uuid-here"
#define BLE_ACCEPT_UUID_3 "your-uuid-here"
#define BLE_ACCEPT_UUID_4 "your-uuid-here"

#endif // SECRETS_H
```

# Android

La solution que je préconise

**local.properties**

```ini
# géolocalisation du portail
TARGET_LAT=0.0
TARGET_LNG=0.0

# distance du point de RDV en metres
TARGET_RADIUS=200.0

# Ble UUID secret à émettre pour ouvrir le portail
BLE_UUID=your-uuid-here

# temps de diffusion du UUID lors du déclenchement
ADVERTISING_DURATION_MS=10000
```



# BLE Tags

## Dans le commerce

Le problème des tags du commerce (environnement clos pas de contrôle sur le mode de fonctionnement)

**Galaxy SmartTag2** : N'est pas toujours visible au scan

**Voccolink** : Passe en mode veille

**Echo SmartTag** : Inutilisable ?

## Simulateur

Pas toujours gratuit, pas toujours paramétrable (service / réveille)

## Montage custom

Utiliser un module arduino

~20€ : Nécessite de créer un montage avec pile et boitier (puissance émission à tester)
