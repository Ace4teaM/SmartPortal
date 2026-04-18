
#include <Arduino.h>

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <BLEEddystoneURL.h>
#include <BLEEddystoneTLM.h>
#include <BLEBeacon.h>

#include "wifi.h"
#include "mqtt.h"

//#define FULL_LOGS

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
    if(expression == false)
      tm->count = 0;
    else
    {
      tm->count++;
      
#if defined(FULL_LOGS)
      Serial.print("TIMER : ");
      Serial.print(tm->name);
      Serial.print("(");
      Serial.print(tm->count);
      Serial.print(")");
      Serial.println();
#endif
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

  publishSeqMqtt(seq->name, seq->init, seq->initialized, seq->etape, seq->duree);
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
struct TIME TIMER_Ouverture;
struct TIME TIMER_Reconnect;

int IN_Echo1;
int IN_Echo2;
int IN_PortillonOuvert; // (0=fermé,1=ouvert)
int IN_PortailOuvert; // (0=fermé,1=ouvert)
int IN_BoutonReset;
int IN_MqttCommand;

int OUT_OuverturePortail;
int OUT_DeverrouillagePortillon;
int OUT_Led;
int OUT_EchoTrigger;

int UUID_Identifie; // numero du badge trouvé, 0 = aucun
bool bEndofScan = true; // false si scan en cours

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

//const BLEUUID Tag_Vocolinc = BLEUUID(std::string("0000fd44-0000-1000-8000-00805f9b34fb"));
//const BLEUUID Tag_SmartTag = BLEUUID(std::string("0000fd5a-0000-1000-8000-00805f9b34fb"));

const std::string Tag_VocolincA = std::string("0000fd44-0000-1000-8000-00805f9b34fb");
const std::string Tag_SmartTagA = std::string("0000fd5a-0000-1000-8000-00805f9b34fb");

// Timer avant reset du G7 principal
// Prévient un éventuel bloquage dans une étape autre que 0,10,11
#define PARAM_TIMER_BEFORE_RESET 60 //En nb cycles

// Temps de présence de l'écho 1 et 2 avant ouverture portail
#define PARAM_TIMER_PRESENCE_ECHO_1_2 2 //En nb cycles

// Temps de scan du tag
#define PARAM_TIMER_SCAN_TAG 5 //En seconds

// Temps d'attente après ouverture portail
#define PARAM_TIMER_OUVERTURE 10 //En seconds

// Temps d'attente avant reconnection
#define PARAM_TIMER_RECONNECT 5 // 30 //En seconds

// Distance de détection min/max de la présence d'une personne/voiture (>= min && <= max)
#define PARAM_DIST_ECHO_MIN 1.0 // En cm
#define PARAM_DIST_ECHO_MAX 50.0 // En cm

BLEScan *pBLEScan;

#if DEBUG
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice device) {
      Serial.printf("Advertised Device: %s \n", device.toString().c_str());
    }
};
#endif

void EndofScan(BLEScanResults results) {
#if DEBUG
    Serial.println("EndofScan");
#endif
    for(int i=0;i<results.getCount();i++)
    {
        BLEAdvertisedDevice device = results.getDevice(i);

        if (device.haveServiceUUID())
        {
          BLEUUID devUUID = device.getServiceUUID();

          if (devUUID.toString().compare(Tag_VocolincA) == 0)
          {
            UUID_Identifie = 1;
            Serial.print("Found ServiceUUID: [");
            Serial.print(UUID_Identifie);
            Serial.print("] ");
            Serial.print(devUUID.toString().c_str());
            Serial.println("");
            break;
          }
          if (devUUID.toString().compare(Tag_SmartTagA) == 0)
          {
            UUID_Identifie = 2;
            Serial.print("Found ServiceUUID: [");
            Serial.print(UUID_Identifie);
            Serial.print("] ");
            Serial.print(devUUID.toString().c_str());
            Serial.println("");
            break;
          }
        }
    }
    pBLEScan->clearResults(); // libère la mémoire
    bEndofScan = true;
}

void setup() {

  setupWifi();

  setupMqtt();

  SEQ_Init(&G7_Principal, "Principal", g7_principal);
  SEQ_Init(&G7_Reset, "Reset", g7_reset);
  SEQ_Init(&G7_Commandes, "Commandes", g7_commandes);

  TIMER_Init(&TIMER_Echo, "Echo portail");
  TIMER_Init(&TIMER_Echo2, "Echo portillon");
  TIMER_Init(&TIMER_Ouverture, "Attente ouverture portail");
  TIMER_Init(&TIMER_Reset, "Reset");
  TIMER_Init(&TIMER_Reconnect, "Attente reconnection");

  Serial.begin(9600);

  pinMode(PIN_ECHO1, INPUT);
  pinMode(PIN_ECHO2, INPUT);
  pinMode(PIN_OPEN1, OUTPUT);
  pinMode(PIN_CLOS1, OUTPUT);
  pinMode(PIN_STAT1, INPUT_PULLUP);
  pinMode(PIN_STAT2, INPUT_PULLUP);
  pinMode(PIN_BUTT1, INPUT);
  pinMode(PIN_TRIG1, OUTPUT);
  pinMode(PIN_TRIG2, OUTPUT);

  pinMode(LED_RED, OUTPUT);

  pinMode(LED_GREEN, OUTPUT);

  pinMode(LED_BLUE, OUTPUT);

  pinMode(LED_BUILTIN, OUTPUT);

  digitalWrite(LED_BUILTIN, LOW);
  
  digitalWrite(PIN_OPEN1, LOW);
  digitalWrite(PIN_CLOS1, LOW);

  OUT_Led = LED_VERT;
  
  
  BLEDevice::init("");
  BLEDevice::setMTU(23);//BLE spec default
  pBLEScan = BLEDevice::getScan(); //create new scan
#if DEBUG
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
#endif
  pBLEScan->setActiveScan(true); //active scan uses more power, but get results faster
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99); // less or equal setInterval value


  delay(3000);
  
  Serial.println();
  Serial.println("************************************");
  Serial.println("Initialized");
  Serial.println("************************************");
  Serial.println();
}

int count = 0;

void loop()
{
  Inputs();

  SEQ_Run(&G7_Principal);
  SEQ_Run(&G7_Reset);
  SEQ_Run(&G7_Commandes);

  Outputs();

  Commandes();

  delay(CYCLE_DURATION);
  
  digitalWrite(LED_BUILTIN, count % 2 == 0 ? LOW : HIGH);

  count++;
}


void Inputs()
{
  IN_PortailOuvert = digitalRead(PIN_STAT1) == LOW; // logique inverse avec INPUT_PULLUP
  IN_PortillonOuvert = digitalRead(PIN_STAT2) == LOW; // logique inverse avec INPUT_PULLUP
  IN_BoutonReset = digitalRead(PIN_BUTT1);

  IN_MqttCommand = checkMqtt();

  digitalWrite(PIN_TRIG1, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG1, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG1, LOW);
  // Ajout d'un TIMEOUT de 20000 µs (~3 mètres max)
  // Si rien n'est reçu après 20ms, la fonction rend la main immédiatement
  auto duration = pulseIn(PIN_ECHO1, HIGH, 20000);
  auto distance = (duration*.0343)/2; // cm
#if defined(FULL_LOGS)
  Serial.print("distance1: ");
  Serial.println(distance);
#endif
  IN_Echo1 = distance > PARAM_DIST_ECHO_MIN && distance < PARAM_DIST_ECHO_MAX ? 1 : 0;

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
#if defined(FULL_LOGS)
  Serial.print("distance2: ");
  Serial.println(distance);
#endif
  IN_Echo2 = distance > PARAM_DIST_ECHO_MIN && distance < PARAM_DIST_ECHO_MAX ? 1 : 0;
}

void Outputs()
{
  OUT_OuverturePortail = (G7_Principal.etape == 40 && G7_Principal.etape_front) || (G7_Commandes.etape == 10 && G7_Commandes.etape_front) ? 1 : 0;
  OUT_DeverrouillagePortillon = G7_Principal.etape == 41 ? 1 : 0;
  //OUT_Led = G7_Reset.etape == 10 || G7_Commandes.etape == 11 ? LED_ORANGE : G7_Principal.etape == 11 ? LED_ROUGE : LED_VERT;
  OUT_Led = LED_VERT;
  OUT_EchoTrigger = G7_Principal.etape == 10 || G7_Principal.etape == 30 ? 1 : 0;
  
  SEQ_Debug(&G7_Principal);
  SEQ_Debug(&G7_Reset);
  SEQ_Debug(&G7_Commandes);
  publishStatesMqtt(
    IN_Echo1,
    IN_Echo2,
    IN_PortillonOuvert,
    IN_PortailOuvert,
    IN_BoutonReset,
    IN_MqttCommand,

    OUT_OuverturePortail,
    OUT_DeverrouillagePortillon,
    OUT_Led,
    OUT_EchoTrigger,

    UUID_Identifie,
    bEndofScan
  );
}

void Commandes()
{
  digitalWrite(PIN_OPEN1, OUT_OuverturePortail == 1 ? HIGH : LOW);
  digitalWrite(PIN_CLOS1, OUT_DeverrouillagePortillon == 1 ? HIGH : LOW);

  switch(OUT_Led){
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

    UUID_Identifie = 0;
    bEndofScan = true;

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
      if(IN_Echo1 == 1 || IN_Echo2 == 1)
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
      if(IN_BoutonReset == 1)
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
          // Lecture badge
          Serial.println("start scan");
          bEndofScan = false;
          UUID_Identifie = 0;
          pBLEScan->start(PARAM_TIMER_SCAN_TAG, EndofScan);// appel non-bloquant jusque la fin du scan, assigne UUID_Identifie
      }

      // Lecture OK
      if(bEndofScan == true)
      {
        seq->desc = "fin lecture badge";
        seq->etape = 21;
      }

      return;
  }
  
  if(seq->etape == 21)
  {
      seq->desc = "Identification badge";

      Serial.println("UUID_Identifie");
      Serial.println(UUID_Identifie);
      // Identification OK ?
      if(UUID_Identifie != 0)
      {
        seq->desc = "identification OK";
        seq->etape = 30;
      }
      else
      {
        seq->desc = "identification échouée";
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
      if(TIMER(&TIMER_Echo, IN_Echo1 == 1 && IN_Echo2 == 1) > PARAM_TIMER_PRESENCE_ECHO_1_2)
      {
        seq->desc = "Presence echo 1et2 pendant 2sec, ouverture portail...";
        seq->etape = 40;
      }

      // Autre (Echo 1 ou 2 pendant 2sec), ouverture portillon
      if(TIMER(&TIMER_Echo2, (IN_Echo1 == 1 || IN_Echo2 == 1) && (IN_Echo1 != IN_Echo2)) > PARAM_TIMER_PRESENCE_ECHO_1_2)
      {
        seq->desc = "Presence echo autre (!= 1 et 2), ouverture portillon";
        seq->etape = 41;
      }

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
      if(IN_PortailOuvert == 1 && TIMER(&TIMER_Ouverture, true) > PARAM_TIMER_OUVERTURE)
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
      if(UUID_Identifie == 0 && IN_PortillonOuvert == 0)
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
      if(IN_PortailOuvert == 0)
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

  /*  if(seq->etape_front)
    {
      TIMER_Raz(&TIMER_Reset);
    }*/

    // +10sec dans une étape et pas en initialisation ou erreur
   if(G7_Principal.etape != 0 && G7_Principal.etape != 10 && G7_Principal.etape != 11 && G7_Principal.duree > PARAM_TIMER_BEFORE_RESET)
    {
      seq->etape = 10;
    }
    
    // bouton reset
   /*  if(IN_BoutonReset == 1)
    {
        seq->desc = "bouton reset";
      seq->etape = 10;
    }*/

    return;
  }
  
  // Reset !
  if(seq->etape == 10)
  {
      seq->desc = "Reset !";

    SEQ_Reset(&G7_Principal);
    // orange
    OUT_Led = LED_ORANGE;
    
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

//#if defined(FULL_LOGS)
    Serial.print("IN_MqttCommand: ");
    Serial.println(IN_MqttCommand);
    Serial.print("IN_PortailOuvert: ");
    Serial.println(IN_PortailOuvert);
//#endif

    if(statusWifi() != 1 || statusMqtt() != 1)
    {
      seq->etape = 0;
    }

    if(IN_MqttCommand == CMD_OPEN && IN_PortailOuvert == 0 && G7_Principal.etape != 40)
    {
      seq->etape = 10;
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
      if(IN_PortailOuvert == 1 && TIMER(&TIMER_Ouverture, true) > PARAM_TIMER_OUVERTURE)
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
      OUT_Led = LED_BLUE;

      if(seq->etape_front)
      {
        TIMER_Raz(&TIMER_Reconnect);
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