#include "gnss.h"
#include "../config.h"
#include "../core/ubx.h"
#include <Arduino.h>

namespace gnss {

static HardwareSerial& S = GNSS_SERIAL;
static ubx::Parser s_parser;
static Stats s_stats = {};
static uint8_t s_rate = 0;
static uint32_t s_rateT0 = 0, s_rateN = 0, s_checkAt = 0;
static bool s_galileoChecked = false;

const Stats& stats(){ return s_stats; }

void powerOn(){  pinMode(PIN_GPS_EN, OUTPUT); digitalWrite(PIN_GPS_EN, GPS_EN_ACTIVE); }
void powerOff(){ pinMode(PIN_GPS_EN, OUTPUT); digitalWrite(PIN_GPS_EN, !GPS_EN_ACTIVE); }

// Envoie un VALSET et attend l'ACK (ou le NAK) correspondant.
static int sendValSet(ubx::ValSet& vs, uint32_t timeoutMs = 300){
  uint8_t f[600];
  const size_t n = vs.finish(f, sizeof f);
  if(!n) return -2;
  S.write(f, n);
  S.flush();
  const uint32_t t0 = millis();
  while(millis() - t0 < timeoutMs){
    while(S.available()){
      if(s_parser.feed((uint8_t)S.read()) && s_parser.cls() == ubx::CLS_ACK && s_parser.len() >= 2 &&
         s_parser.payload()[0] == ubx::CLS_CFG && s_parser.payload()[1] == ubx::ID_CFG_VALSET){
        if(s_parser.id() == ubx::ID_ACK_ACK){ s_stats.acks++; return 1; }
        s_stats.naks++; return 0;
      }
    }
    delay(1);
  }
  return -1;   // pas de réponse
}

static int valset1(uint32_t key, uint32_t val){ ubx::ValSet v; v.add(key, val); return sendValSet(v); }

static void signals(bool galileo){
  // Constellations superflues désactivées AVANT de demander 25 Hz, sinon
  // le module refuse la cadence (v1 §4.8). Le changement de signaux fait
  // redémarrer le moteur GNSS : on laisse 1 s avant la suite.
  ubx::ValSet v;
  v.add(ubx::key::SIGNAL_GPS_ENA, 1);
  v.add(ubx::key::SIGNAL_GAL_ENA, galileo ? 1 : 0);
  v.add(ubx::key::SIGNAL_BDS_ENA, 0);
  v.add(ubx::key::SIGNAL_GLO_ENA, 0);
  v.add(ubx::key::SIGNAL_QZSS_ENA, 0);
  v.add(ubx::key::SIGNAL_SBAS_ENA, 0);
  const int r = sendValSet(v, 1500);
  Serial.printf("[gnss] signaux GPS%s : %s\n", galileo ? "+Galileo" : " seul",
                r == 1 ? "ACK" : r == 0 ? "NAK" : "sans réponse");
  s_stats.galileo = galileo;
  delay(1000);
}

void setRate(uint8_t dataRate){
  s_rate = dataRate;
  ubx::ValSet v;
  v.add(ubx::key::RATE_MEAS, ubx::measPeriodMs(dataRate));
  v.add(ubx::key::RATE_NAV, 1);
  const int r = sendValSet(v);
  Serial.printf("[gnss] période %u ms : %s\n", ubx::measPeriodMs(dataRate), r == 1 ? "ACK" : r == 0 ? "NAK" : "sans réponse");
  s_rateT0 = millis(); s_rateN = 0; s_checkAt = millis() + 5000; s_galileoChecked = false;
}

void setDynModel(uint8_t model){
  const int r = valset1(ubx::key::NAVSPG_DYNMODEL, model);
  Serial.printf("[gnss] modèle dynamique %u : %s\n", model, r == 1 ? "ACK" : r == 0 ? "NAK" : "sans réponse");
}

bool begin(uint8_t dataRate, uint8_t dynModel){
  powerOn();
  delay(600);
  // Détection de la vitesse : le module ne garde pas sa configuration sans
  // pile de sauvegarde. On lui demande de passer à GNSS_BAUD depuis chaque
  // vitesse plausible, puis on vérifie qu'il répond à GNSS_BAUD.
  static const uint32_t bauds[] = {38400, 9600, 115200, 57600, 230400};
  s_stats.baudFound = 0;
  for(uint32_t b : bauds){
    S.end();
    S.setRxBufferSize(4096);
    S.begin(b, SERIAL_8N1, PIN_GNSS_RX, PIN_GNSS_TX);
    delay(50);
    ubx::ValSet v; v.add(ubx::key::UART1_BAUDRATE, GNSS_BAUD);
    uint8_t f[64]; const size_t n = v.finish(f, sizeof f);
    S.write(f, n); S.flush();
    delay(120);
    S.end();
    S.setRxBufferSize(4096);
    S.begin(GNSS_BAUD, SERIAL_8N1, PIN_GNSS_RX, PIN_GNSS_TX);
    delay(50);
    while(S.available()) S.read();
    if(valset1(ubx::key::UART1OUTPROT_UBX, 1) == 1){ s_stats.baudFound = b; break; }
  }
  if(!s_stats.baudFound){
    Serial.println("[gnss] AUCUNE RÉPONSE : vérifier câblage (TX↔RX croisés), alimentation, GPS_EN");
    return false;
  }
  Serial.printf("[gnss] module trouvé (vitesse d'origine %lu bauds)\n", (unsigned long)s_stats.baudFound);

  ubx::ValSet v;
  v.add(ubx::key::UART1OUTPROT_NMEA, 0);         // NMEA coupé : bande passante
  v.add(ubx::key::MSGOUT_NAV_PVT_UART1, 1);      // un NAV-PVT par solution
  v.add(ubx::key::ODO_OUTLPVEL, 0);              // pas de lissage de la vitesse
  v.add(ubx::key::ODO_OUTLPCOG, 0);              // ni du cap (retard sur transitoires)
  v.add(ubx::key::MOT_GNSSSPEED_THRS, 0);        // pas de maintien statique
  const int r = sendValSet(v);
  Serial.printf("[gnss] sorties et filtres : %s\n", r == 1 ? "ACK" : r == 0 ? "NAK" : "sans réponse");
  setDynModel(dynModel);
  signals(true);
  setRate(dataRate);
  return true;
}

bool poll(rec::Pvt& out){
  bool got = false;
  int budget = 512;                     // ne pas monopoliser la boucle
  while(S.available() && budget--){
    if(!s_parser.feed((uint8_t)S.read())) continue;
    if(s_parser.cls() == ubx::CLS_NAV && s_parser.id() == ubx::ID_NAV_PVT &&
       rec::decodeNavPvt(s_parser.payload(), s_parser.len(), out)){
      got = true; s_stats.pvtCount++; s_rateN++;
      break;                            // une solution par appel
    }
  }
  s_stats.badChecksums = s_parser.badChecksums();
  const uint32_t now = millis();
  if(now - s_rateT0 >= 2000){
    s_stats.rateHz = s_rateN * 1000.0f / (now - s_rateT0);
    s_rateT0 = now; s_rateN = 0;
  }
  // Cadence non tenue avec Galileo : on se replie sur le GPS seul. [À VALIDER]
  // sur le M100 : la cadence maximale dépend du nombre de constellations.
  if(!s_galileoChecked && s_checkAt && (int32_t)(now - s_checkAt) > 0){
    s_galileoChecked = true;
    const float want = 1000.0f / ubx::measPeriodMs(s_rate);
    if(s_stats.galileo && s_stats.rateHz < want * 0.9f){
      Serial.printf("[gnss] %.1f Hz au lieu de %.0f : Galileo désactivé\n", s_stats.rateHz, want);
      signals(false);
      setRate(s_rate);
      s_galileoChecked = true;
    }
  }
  return got;
}

} // namespace gnss
