
#include <Arduino.h>

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAddress.h>
#include <BLEAdvertisedDevice.h>
#include <BLEEddystoneURL.h>
#include <BLEEddystoneTLM.h>
#include <BLEBeacon.h>
#include <BLEClient.h>

#include "wifi.h"
#include "mqtt.h"
#include "secrets.h"

//#define FULL_LOGS
#define DEBUG

#define CYCLE_DURATION 500
#define SECOND_IN_CYCLE ((1000.0 / (float)CYCLE_DURATION))
#define CYCLE_IN_SECOND (((float)CYCLE_DURATION / 1000.0))

typedef struct SEQ{
  const char* name;
  const char* desc;
  const char* desc_prec;
  int init;
  int initialized;
  int etape;
  int etape_prec;
  int etape_front;
  int duree;
  void (*func)(SEQ *);
};

typedef struct TIME{
  const char* name;
  int count;
};

float TIMER(TIME* tm, bool expression)
{
    if(expression == true)
    {
      tm->count++;
    }

    return (float)tm->count * CYCLE_IN_SECOND;
}

// reset auto si l'expression tombe à false
float TIMER_AUTO_RESET(TIME* tm, bool expression)
{
    if(expression == true)
    {
      tm->count++;
    }
    else
    {
      tm->count = 0;
    }

    return (float)tm->count * CYCLE_IN_SECOND;
}

void TIMER_Init(TIME* tm, const char* name)
{
  tm->name = name;
  tm->count = 0;
}

void TIMER_Raz(TIME* tm)
{
#if defined(FULL_LOGS)
  Serial.print("TIMER_Raz : ");
  Serial.print(tm->name);
  Serial.println();
#endif
  tm->count = 0;
}

void TIMER_Debug(TIME* time) {
  Serial.print(time->name);
  Serial.print(" -> count:");
  Serial.print(time->count);
  Serial.println();
}

void SEQ_Debug(SEQ* seq) {
  Serial.print(seq->name);
  Serial.print(" -> init:");
  Serial.print(seq->init);
  Serial.print(" -> initialized:");
  Serial.print(seq->initialized);
  Serial.print(" -> etape:");
  Serial.print(seq->etape);
  Serial.print(" -> duree:");
  Serial.print(seq->duree);
  Serial.println();
  Serial.println(seq->desc);

  publishSeqMqtt(seq->name, seq->init, seq->initialized, seq->etape, seq->duree);
  publish(seq->name, seq->desc);
}

void SEQ_Init(SEQ * seq, const char* name, void (*func)(SEQ *))
{
#if defined(FULL_LOGS)
  Serial.print("SEQ_Init : ");
  Serial.print(seq->name);
  Serial.println();
#endif
  seq->init = 1;
  seq->initialized = 0;
  seq->name = name;
  seq->desc = "";
  seq->desc_prec = seq->desc;
  seq->etape = 0;
  seq->etape_prec = -1;
  seq->duree = 0;
  seq->func = func;
}
 
void SEQ_Reset(SEQ * seq)
{
#if defined(FULL_LOGS)
  Serial.print("SEQ_Reset : ");
  Serial.print(seq->name);
  Serial.println();
#endif
  seq->init = 1;
  seq->initialized = 0;
  seq->etape = 0;
  seq->etape_prec = -1;
  seq->duree = 0;
}
 
void SEQ_Run(SEQ * seq)
{
  // Changement desc
  if(seq->desc_prec != seq->desc)
  {
    Serial.print(seq->name);
    Serial.print(" : ");
    Serial.println(seq->desc);
    seq->desc_prec = seq->desc;
  }
  
  // Changement étape ?
  if(seq->etape_prec != seq->etape)
  {
    Serial.print(seq->name);
    Serial.print(" : E");
    Serial.print(seq->etape_prec);
    Serial.print(" > E");
    Serial.print(seq->etape);
    Serial.println();
    seq->etape_prec = seq->etape;
    seq->etape_front = 1;
    seq->duree = 0;//durée étape
  }
  else
  {
    seq->etape_front = 0;
    seq->duree++;
  }

  seq->func(seq);
}
 
struct SEQ G7_Reset;
struct SEQ G7_Principal;
struct SEQ G7_Commandes;

struct TIME TIMER_Echo;
struct TIME TIMER_Echo2;
struct TIME TIMER_Reset;
struct TIME TIMER_Reset2;
struct TIME TIMER_Ouverture;
struct TIME TIMER_Reconnect;
struct TIME TIMER_Scan;

struct _IN
{
  int Echo1;
  int Echo2;
  int PortillonOuvert; // (0=fermé,1=ouvert)
  int PortailOuvert; // (0=fermé,1=ouvert)
  int BoutonReset;
  int MqttCommand;
}IN, IN_PREV;

struct _OUT
{
  int OuverturePortail;
  int DeverrouillagePortillon;
  int Led;
  int EchoTrigger;
}OUT, OUT_PREV;

struct _STATES
{
  int UUID_Identifie; // numero du badge trouvé, 0 = aucun
  bool bEndofScan = true; // false si scan en cours
  int rssi_add = 0; // valeur compensatoire max RSSI
}STATES, STATES_PREV;

#define LED_VERT 1
#define LED_ORANGE 2
#define LED_ROUGE 3

#define PIN_ECHO1 10 // echo portillon
#define PIN_ECHO2 7 // echo portail
#define PIN_OPEN1 4 // ouverture portail 
#define PIN_CLOS1 5 // verrouillage portillon
#define PIN_STAT1 2 // portail fermé
#define PIN_STAT2 6 // portillon fermé
#define PIN_BUTT1 3 // bouton reset
#define PIN_TRIG1 9 // trigger echo portillon
#define PIN_TRIG2 8 // trigger echo portail

// Tags
static const BLEUUID TagsUUID[]={
  BLEUUID(BLE_ACCEPT_UUID_1),
  BLEUUID(BLE_ACCEPT_UUID_2),
  BLEUUID(BLE_ACCEPT_UUID_3),
  BLEUUID(BLE_ACCEPT_UUID_4)
};

static int TagsRSSI[]={
  -1000,
  -1000,
  -1000,
  -1000
};

// Timer scan BLE Tag avant abandon
#define PARAM_TIMER_SCAN 10 //En secondes

// Timer avant reset du G7 principal
// Prévient un éventuel bloquage dans une étape autre que 0,10,11
#define PARAM_TIMER_BEFORE_RESET 60 //En secondes

// Temps de présence de l'écho 1 et 2 avant ouverture portail
#define PARAM_TIMER_PRESENCE_ECHO_1_2 2 //En secondes

// Temps de scan du tag
#define PARAM_SCAN_DURATION 4 //En seconds

// Temps d'attente après ouverture portail
#define PARAM_TIMER_OUVERTURE 10 //En seconds

// Temps d'attente avant reconnection
#define PARAM_TIMER_RECONNECT 30 //En seconds

// Distance de détection min/max de la présence d'une personne/voiture (>= min && <= max)
#define PARAM_DIST_ECHO_MIN 1.0 // En cm
#define PARAM_DIST_ECHO_MAX 300.0 // En cm

// Signal max pour déclenchement
// Le RSSI est une valeur négative exprimée en dBm.
// Plus la valeur est proche de 0, plus l'appareil est près (ex: -40 dBm).
// Plus la valeur est petite, plus l'appareil est loin (ex: -95 dBm).
#define PARAM_BLE_TAG_RSSI_MAX -100 // a ajuster en fonction de l'environnement

BLEScan *pBLEScan;

int count = 0;

unsigned long scanStartTime = 0;
unsigned long cyclesExceeded = 0; // nombre de cycles dépassés
unsigned long cyclesOverflow = 0; // dépasssement en ms

#if defined(DEBUG)
int distanceEcho1 = 0;
int distanceEcho2 = 0;
int _distanceEcho1 = 0;
int _distanceEcho2 = 0;
int _UUID_Identifie = 0;
#endif

#if defined(DEBUG)
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice device) {
        if (device.haveServiceUUID())
        {
          static char message[200];

          snprintf(message, sizeof(message), "%s: %d db, %s", device.getName().c_str(), device.getRSSI(), device.getServiceUUID().toString().c_str());
          Serial.println(message);

          publish("esp32/adversing", message);
        }
    }
};
#endif

void EndofScan(BLEScanResults results) {
#if defined(DEBUG)
    Serial.println("EndofScan");
#endif

    for(size_t j=0; j < sizeof(TagsUUID)/sizeof(TagsUUID[0]); j++)
    {
      TagsRSSI[j] = -1000;

      for(int i=0; i < results.getCount(); i++)
      {
          BLEAdvertisedDevice device = results.getDevice(i);

          // On récupère le RSSI
          int rssi = device.getRSSI();
          
          if (device.haveServiceUUID())
          {
            BLEUUID devUUID = device.getServiceUUID();

            if (devUUID.equals(TagsUUID[j]))
            {
              TagsRSSI[j] = rssi;
            }
          }
      }
    }
    pBLEScan->clearResults(); // libère la mémoire
    STATES.bEndofScan = true;
}

void setup() {
  memset(&IN, 0, sizeof(_IN));
  memset(&OUT, 0, sizeof(_OUT));
  memset(&STATES, 0, sizeof(_STATES));

  setupWifi();

  setupMqtt();

  SEQ_Init(&G7_Principal, "Principal", g7_principal);
  SEQ_Init(&G7_Reset, "Reset", g7_reset);
  SEQ_Init(&G7_Commandes, "Commandes", g7_commandes);

  TIMER_Init(&TIMER_Echo, "Echo portail");
  TIMER_Init(&TIMER_Echo2, "Echo portillon");
  TIMER_Init(&TIMER_Ouverture, "Attente ouverture portail");
  TIMER_Init(&TIMER_Reset, "Reset Principal");
  TIMER_Init(&TIMER_Reset2, "Reset Commandes");
  TIMER_Init(&TIMER_Reconnect, "Attente reconnection");
  TIMER_Init(&TIMER_Scan, "Scan Tag BLE");

  Serial.begin(9600);

  pinMode(PIN_ECHO1, INPUT);
  pinMode(PIN_ECHO2, INPUT);
  pinMode(PIN_OPEN1, OUTPUT);
  pinMode(PIN_CLOS1, OUTPUT);
  pinMode(PIN_STAT1, INPUT_PULLUP);
  pinMode(PIN_STAT2, INPUT_PULLUP);
  pinMode(PIN_BUTT1, INPUT_PULLUP);
  pinMode(PIN_TRIG1, OUTPUT);
  pinMode(PIN_TRIG2, OUTPUT);

  pinMode(LED_RED, OUTPUT);

  pinMode(LED_GREEN, OUTPUT);

  pinMode(LED_BLUE, OUTPUT);

  pinMode(LED_BUILTIN, OUTPUT);

  digitalWrite(LED_BUILTIN, LOW);
  
  digitalWrite(PIN_OPEN1, LOW);
  digitalWrite(PIN_CLOS1, LOW);

  OUT.Led = LED_VERT;
  
  
  BLEDevice::init("ESP32");
  BLEDevice::setMTU(23);//BLE spec default
  pBLEScan = BLEDevice::getScan(); //create new scan
#if defined(DEBUG)
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
#endif
  pBLEScan->setActiveScan(true); //Plus d'infos, mais consomme plus d'énergie et est détectable.
//  pBLEScan->setActiveScan(false); //Moins d'infos, discret, économe (probleme le Tag semble dormir au bout d'un moment)
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(100); // less or equal setInterval value


  delay(3000);
  
  Serial.println();
  Serial.println("************************************");
  Serial.println("Initialized");
  Serial.println("************************************");
  Serial.println();

  scanStartTime = millis();
}

void waitCycle() {
    unsigned long elapsed = millis() - scanStartTime;
    if (elapsed < 500) // cycle pas encore écoulé
      delay(500 - elapsed);
    else
    {
      cyclesExceeded++;
      cyclesOverflow = 500 - elapsed;
    }

    // Cycle ≥ 500ms atteint, on relance
    scanStartTime = millis();
}

void loop()
{
  // pour debug
#if defined(DEBUG)
  static char message[200];

  _distanceEcho1 = distanceEcho1;
  _distanceEcho2 = distanceEcho2;
  _UUID_Identifie = STATES.UUID_Identifie;

  if(count % 2 == 0) // pas au dessous de 1sec max
  {
    if(cyclesOverflow != 0)
    {
      Serial.print("Depassements de cycles: ");
      Serial.print(cyclesExceeded);
      Serial.print(", ");
      Serial.print(cyclesOverflow);
      Serial.println("ms");

      cyclesOverflow = 0;
    }
  }
#endif

  Inputs();

  // TEMPORAIRE ----------------------------------------------------------------
  // pour le moment on associe un seul echo
  IN.Echo1 = IN.Echo2;
  // TEMPORAIRE ----------------------------------------------------------------

  // etats calculés ----------------------------------------------------------------
  // Badge identifié
  
  STATES.UUID_Identifie = 0;
  for(size_t j=0; j < sizeof(TagsUUID)/sizeof(TagsUUID[0]); j++)
  {
    if(TagsRSSI[j] > (PARAM_BLE_TAG_RSSI_MAX + STATES.rssi_add))
    {
      STATES.UUID_Identifie = j+1; // 0 = non trouvé
      break;
    }
  }

  SEQ_Run(&G7_Principal);
  SEQ_Run(&G7_Reset);
  SEQ_Run(&G7_Commandes);

  Outputs();

  Commandes();

  waitCycle();

  digitalWrite(LED_BUILTIN, count % 2 == 0 ? LOW : HIGH);

  count++;

#if defined(DEBUG)
  if(abs(distanceEcho1 - _distanceEcho1) > 5)
  {
    snprintf(message, sizeof(message), "%d", distanceEcho1);
    publish("esp32/distanceEcho1", message);
  }
  
  if(abs(distanceEcho2 - _distanceEcho2) > 5)
  {
    snprintf(message, sizeof(message), "%d", distanceEcho2);
    publish("esp32/distanceEcho2", message);
  }

  if(STATES.UUID_Identifie != _UUID_Identifie)
  {
    snprintf(message, sizeof(message), "%d", STATES.UUID_Identifie);
    publish("esp32/UUID_Identifie", message);
  }

  if(G7_Principal.etape != G7_Principal.etape_prec || G7_Principal.desc != G7_Principal.desc_prec)
    SEQ_Debug(&G7_Principal);

  if(G7_Reset.etape != G7_Reset.etape_prec || G7_Reset.desc != G7_Reset.desc_prec)
    SEQ_Debug(&G7_Reset);

  if(G7_Commandes.etape != G7_Commandes.etape_prec || G7_Commandes.desc != G7_Commandes.desc_prec)
    SEQ_Debug(&G7_Commandes);

  if(memcmp(&STATES, &STATES_PREV, sizeof(_STATES)) != 0)
    publishStatesMqtt(
      STATES.UUID_Identifie,
      STATES.bEndofScan,
      STATES.rssi_add
    );

  if(memcmp(&IN, &IN_PREV, sizeof(_IN)) != 0)
    publishInputsMqtt(
      IN.Echo1,
      IN.Echo2,
      IN.PortillonOuvert,
      IN.PortailOuvert,
      IN.BoutonReset,
      IN.MqttCommand
    );
    
  if(memcmp(&OUT, &OUT_PREV, sizeof(_OUT)) != 0)
    publishOutputsMqtt(
      OUT.OuverturePortail,
      OUT.DeverrouillagePortillon,
      OUT.Led,
      OUT.EchoTrigger
    );
#endif

  // copie les états précédents
  memcpy(&IN_PREV, &IN, sizeof(_IN));
  memcpy(&OUT_PREV, &OUT, sizeof(_OUT));
  memcpy(&STATES_PREV, &STATES, sizeof(_STATES));
}


void Inputs()
{
  IN.PortailOuvert = digitalRead(PIN_STAT1) == LOW; // logique inverse avec INPUT_PULLUP
  IN.PortillonOuvert = digitalRead(PIN_STAT2) == LOW; // logique inverse avec INPUT_PULLUP
  IN.BoutonReset = digitalRead(PIN_BUTT1) == LOW; // logique inverse avec INPUT_PULLUP

  IN.MqttCommand = checkMqtt();

  digitalWrite(PIN_TRIG1, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG1, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG1, LOW);
  // Ajout d'un TIMEOUT de 20000 µs (~3 mètres max)
  // Si rien n'est reçu après 20ms, la fonction rend la main immédiatement
  auto duration = pulseIn(PIN_ECHO1, HIGH, 20000); // µs == ~3.4metres
  auto distance = (duration*.0343)/2; // cm
  distanceEcho1 = distance;
#if defined(FULL_LOGS)
  Serial.print("distance1: ");
  Serial.println(distance);
#endif
  if(duration > 0)
    IN.Echo1 = distance > PARAM_DIST_ECHO_MIN && distance < PARAM_DIST_ECHO_MAX ? 1 : 0;
  else
    IN.Echo1 = 0;

  delayMicroseconds(10);

  digitalWrite(PIN_TRIG2, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG2, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG2, LOW);
  
  // Ajout d'un TIMEOUT de 20000 µs (~3 mètres max)
  // Si rien n'est reçu après 20ms, la fonction rend la main immédiatement
  duration = pulseIn(PIN_ECHO2, HIGH, 20000);
  distance = (duration*.0343)/2; // cm
  distanceEcho2 = distance;
#if defined(FULL_LOGS)
  Serial.print("distance2: ");
  Serial.println(distance);
#endif

  if(duration > 0)
    IN.Echo2 = distance > PARAM_DIST_ECHO_MIN && distance < PARAM_DIST_ECHO_MAX ? 1 : 0;
  else
    IN.Echo2 = 0;
}

void Outputs()
{
  OUT.OuverturePortail = (G7_Principal.etape == 40 && G7_Principal.etape_front) || (G7_Commandes.etape == 10 && G7_Commandes.etape_front) ? 1 : 0;
  OUT.DeverrouillagePortillon = G7_Principal.etape == 41 ? 1 : 0;
  OUT.Led = G7_Reset.etape == 10 || G7_Commandes.etape == 11 ? LED_ORANGE : G7_Principal.etape == 11 ? LED_ROUGE : LED_VERT;
  OUT.EchoTrigger = G7_Principal.etape == 10 || G7_Principal.etape == 30 ? 1 : 0;
}

void Commandes()
{
  digitalWrite(PIN_OPEN1, OUT.OuverturePortail == 1 ? HIGH : LOW);
  digitalWrite(PIN_CLOS1, OUT.DeverrouillagePortillon == 1 ? HIGH : LOW);

  switch(OUT.Led){
    case LED_VERT:
      digitalWrite(LED_RED, HIGH);
      digitalWrite(LED_GREEN, LOW);
      digitalWrite(LED_BLUE, HIGH);
      break;
    case LED_ORANGE:
      digitalWrite(LED_RED, LOW);
      digitalWrite(LED_GREEN, LOW);
      digitalWrite(LED_BLUE, HIGH);
      break;
    case LED_ROUGE:
      digitalWrite(LED_RED, LOW);
      digitalWrite(LED_GREEN, HIGH);
      digitalWrite(LED_BLUE, HIGH);
      break;
    default:
      digitalWrite(LED_RED, LOW);
      digitalWrite(LED_GREEN, LOW);
      digitalWrite(LED_BLUE, LOW);
      break;
  }
}

void g7_principal(SEQ * seq)
{
  if(seq->init == 1)
  {
    seq->init = 0;

    STATES.UUID_Identifie = 0;
    STATES.bEndofScan = true;

    seq->initialized = 1;
    return;
  }

  // Init
  if(seq->etape == 0)
  {
    seq->desc = "Initialisation";

    if(seq->initialized == 1)
      seq->etape = 10;//OK
    else
      seq->etape = 11;//Erreur

    return;
  }
  
  if(seq->etape == 10)
  {
      seq->desc = "Lecture echo";

      // Lecture echo
      //...

      // Presence echo ?
      if(IN.Echo1 == 1 || IN.Echo2 == 1)
      {
        seq->desc = "echo 1 ou 2 trouvé";
        seq->etape = 20;
      }

      return;
  }
  
  if(seq->etape == 11)
  {
      seq->desc = "Erreur initialisation, attente reset";

      // Activation led rouge

      // Bouton Reset ?
      if(IN.BoutonReset == 1)
      {
        seq->desc = "bouton reset";
        SEQ_Reset(seq);
      }

      return;
  }
  
  if(seq->etape == 20)
  {
      seq->desc = "Lecture badge";

      // Front montant
      if(seq->etape_front)
      {
        TIMER_Raz(&TIMER_Scan);
          
        for(size_t j=0; j < sizeof(TagsUUID)/sizeof(TagsUUID[0]); j++)
          TagsRSSI[j] = -1000;

        STATES.UUID_Identifie = 0;
      }

      // Lecture OK
      if(STATES.bEndofScan == true)
      {
        if(STATES.UUID_Identifie != 0)
        {
          seq->desc = "Identification OK";
          seq->etape = 30;
        }
        else
        {
          // on continue le scan
          seq->desc = "Scan des tags...";

          STATES.bEndofScan = false;

          pBLEScan->start(PARAM_SCAN_DURATION, EndofScan);// appel non-bloquant jusque la fin du scan
        }
      }

      // Echec
      if(TIMER(&TIMER_Scan, STATES.UUID_Identifie == 0) > PARAM_TIMER_SCAN)
      {
        seq->desc = "Identification échouée";
        seq->etape = 10;
      }

      return;
  }
  
  if(seq->etape == 30)
  {
      seq->desc = "Vérification présence écho";

      if(seq->etape_front)
      {
        TIMER_Raz(&TIMER_Echo);
        TIMER_Raz(&TIMER_Echo2);
      }

      // Lecture echo
      //...

      // Presence echo 1et2 pendant 2sec, ouverture portail
      if(TIMER(&TIMER_Echo, IN.Echo1 == 1 && IN.Echo2 == 1) > PARAM_TIMER_PRESENCE_ECHO_1_2)
      {
        seq->desc = "Presence echo 1et2 pendant 2sec, ouverture portail...";
        seq->etape = 40;
      }

      // Autre (Echo 1 ou 2 pendant 2sec), ouverture portillon
      if(TIMER(&TIMER_Echo2, (IN.Echo1 == 1 || IN.Echo2 == 1) && (IN.Echo1 != IN.Echo2)) > PARAM_TIMER_PRESENCE_ECHO_1_2)
      {
        seq->desc = "Presence echo autre (!= 1 et 2), ouverture portillon";
        seq->etape = 41;
      }

#if defined(debug)
      TIMER_Debug(&TIMER_Echo);
#endif

      return;
  }
  
  if(seq->etape == 40)
  {
      seq->desc = "Ouverture portail";

      if(seq->etape_front)
      {
        TIMER_Raz(&TIMER_Ouverture);
      }

      // Portail ouvert ?
      if(IN.PortailOuvert == 1 && TIMER(&TIMER_Ouverture, true) > PARAM_TIMER_OUVERTURE)
      {
        seq->desc = "Portail ouvert";
        seq->etape = 42;
      }

      return;
  }

  if(seq->etape == 41)
  {
      seq->desc = "Déverrouillage portillon";

      // ouverture portillon
      //...

      // Attente perte Identification et portillon fermé pour verrouillage ?
      if(STATES.UUID_Identifie == 0 && IN.PortillonOuvert == 0)
      {
        seq->desc = "Perte Identification et portillon fermé";
        seq->etape = 10;
      }

      return;
  }
  
  if(seq->etape == 42)
  {
      seq->desc = "Portail ouvert";

      // Portail fermé
      if(IN.PortailOuvert == 0)
      {
        seq->desc = "Portail fermé";
        seq->etape = 0;
      }

      return;
  }
}
 
void g7_reset(SEQ * seq)
{
  if(seq->init == 1)
  {
      seq->init = 0;
      seq->initialized = 1;
      return;
  }

  // Reset ?
  if(seq->etape == 0)
  {
      seq->desc = "Initialisation";

    if(seq->etape_front || G7_Principal.etape != G7_Principal.etape_prec)
    {
      TIMER_Raz(&TIMER_Reset);
      TIMER_Raz(&TIMER_Reset2);
    }

    // +10sec dans une étape et pas en initialisation ou erreur
    if(G7_Principal.etape != 0 && G7_Principal.etape != 10 && G7_Principal.etape != 11 && TIMER(&TIMER_Reset, true) > PARAM_TIMER_BEFORE_RESET /*&& G7_Principal.duree > PARAM_TIMER_BEFORE_RESET*/)
    {
      seq->etape = 10;
    }
    
    // +10sec dans une étape et pas en initialisation ou erreur
    if(G7_Commandes.etape != 0 && G7_Commandes.etape != 1 && G7_Commandes.etape != 11 && TIMER(&TIMER_Reset2, true) > PARAM_TIMER_BEFORE_RESET /*&& G7_Commandes.duree > PARAM_TIMER_BEFORE_RESET*/)
    {
      seq->etape = 11;
    }
    
    // bouton reset
    if(IN.BoutonReset == 1)
    {
        seq->desc = "bouton reset";
      seq->etape = 10;
    }

    return;
  }
  
  // Reset !
  if(seq->etape == 10)
  {
    seq->desc = "Reset !";

    SEQ_Reset(&G7_Principal);
    // orange
    OUT.Led = LED_ORANGE;
    
    seq->etape = 0;
    return;
  }
  
  // Reset !
  if(seq->etape == 11)
  {
    seq->desc = "Reset !";

    SEQ_Reset(&G7_Commandes);
    // orange
    OUT.Led = LED_ORANGE;
    
    seq->etape = 0;
    return;
  }
}
 
 
void g7_commandes(SEQ * seq)
{
  if(seq->init == 1)
  {
      seq->init = 0;
      seq->initialized = 1;
      return;
  }

  // Init (wifi et subscriber)
  if(seq->etape == 0)
  {
    seq->desc = "Initialisation";

    if(connectWifi() == 0 || connectMqtt() == 0)
      seq->etape = 11;//Erreur

    seq->etape = 1;//OK

    return;
  }
  
  
  // Check MQTT
  if(seq->etape == 1)
  {
    seq->desc = "Check MQTT";

    if(statusWifi() != 1 || statusMqtt() != 1)
    {
      seq->etape = 11;
    }

    if(IN.MqttCommand == CMD_OPEN && IN.PortailOuvert == 0 && G7_Principal.etape != 40)
    {
      seq->etape = 10;
    }

    if(IN.MqttCommand == CMD_DB_UP)
    {
      STATES.rssi_add += -1;
    }

    if(IN.MqttCommand == CMD_DB_DOWN)
    {
      STATES.rssi_add += 1;
    }

    return;
  }
  
  // Ouverture portail
  if(seq->etape == 10)
  {
      seq->desc = "Ouverture portail";

      if(seq->etape_front)
      {
        TIMER_Raz(&TIMER_Ouverture);
      }

      // Portail ouvert ?
      if(IN.PortailOuvert == 1 && TIMER(&TIMER_Ouverture, true) > PARAM_TIMER_OUVERTURE)
      {
        seq->desc = "Portail ouvert";
        seq->etape = 0;
      }

      return;
  }
  
  // Erreur
  if(seq->etape == 11)
  {
      seq->desc = "Erreur initialisation, attente reconnection...";
      
      // bleu
      OUT.Led = LED_BLUE;

      if(seq->etape_front)
      {
        TIMER_Raz(&TIMER_Reconnect);
        switchWifi();
      }

      // Attente 30 sec
      if(TIMER(&TIMER_Reconnect, true) > PARAM_TIMER_RECONNECT)
      {
        seq->desc = "reconnect";
        SEQ_Reset(seq);
      }

      return;
  }
  
}