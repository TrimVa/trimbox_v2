// ============================================================================
//  TrimBox DIY S3 — application
//
//  Boucle principale NON BLOQUANTE, dans cet ordre strict (v1 §4.1) : le
//  protocole passe avant tout le reste, pour qu'aucune commande ne reste
//  sans réponse.
//    1. commandes de secours (port série USB)
//    2. commandes de la console (Bluetooth) → réponses dans la file unique
//    3. effacement progressif (un secteur par tour)
//    4. téléchargement progressif (tant que la file a de la place)
//    5. vidange de la file vers le Bluetooth
//    6. extinction automatique
//    7. acquisition : IMU, GNSS, radio, enregistrement, chronométrage
// ============================================================================
#include "app.h"
#include "config.h"
#include "core/tbproto.h"
#include "core/records.h"
#include "core/recorder.h"
#include "core/lapcore.h"
#include "core/linecmd.h"
#include "core/persist.h"
#include "core/bytes.h"
#include "hw/storage.h"
#include "hw/gnss.h"
#include "hw/imu.h"
#include "hw/bridge.h"
#include "hw/radio.h"
#include "hw/wifiap.h"
#include "hw/ota.h"
#include <Arduino.h>
#include <esp_sleep.h>
#include <esp_mac.h>
#include <math.h>

namespace app {

// ---------------------------------------------------------------------------
//  État — tout est déclaré ici, en tête (leçon de v1 §9.9, valable aussi en C++
//  pour la lisibilité : aucune variable d'état dispersée dans le fichier).
// ---------------------------------------------------------------------------
static persist::RecConfig  g_cfg;
static persist::LineConfig g_lines;
static recd::Recorder      g_rec;
static lap::Engine         g_lap;
static tb::Parser          g_rx(true);
static bool     g_storageOk = false, g_imuOk = false, g_gnssOk = false;
static char     g_serial[20];

static struct {                 // téléchargement en cours
  bool active; bool extended; uint32_t idx, end, sent, rejected;
} g_dl = {};
static uint8_t  g_erasePct = 255;

static rec::Pvt g_pvt = {};     // dernière solution
static bool     g_havePvt = false;
static uint32_t g_itowPrev = 0; static double g_weekOffset = 0;   // iTOW continu

// Historique court des solutions, horodaté à l'arrivée, pour situer la
// voiture à l'instant exact d'un geste sur l'inter (pose de ligne).
struct Hist { uint32_t ms; int32_t lat, lon, head, gSpeed; bool fix; };
static Hist     g_hist[64]; static uint8_t g_histN = 0, g_histHead = 0;

static int32_t  g_prevBestMs = 0;     // pour l'écart au meilleur tour

// Point d'accès Wi-Fi automatique (v2 §7.1)
static tb::Parser g_rxWs(true);       // commandes reçues par WebSocket
static bool     g_wifiAuto = WIFI_AUTO_DEFAULT;
static bool     g_moving = false;     // la voiture roule (confirmé)
static bool     g_stillEpoch = true;  // dernière solution : « à l'arrêt »
static uint8_t  g_moveCnt = 0;
static uint32_t g_stillSince = 0, g_lastPvtMs = 0;
static uint32_t g_btnDown = 0; static bool g_btnFired = false;
static char     g_ssid[33];
static uint32_t g_bootMs = 0, g_lastLed = 0;

// ---------------------------------------------------------------------------
//  Outils
// ---------------------------------------------------------------------------
static void ack(uint8_t id){ uint8_t p[2] = {tb::CLS, id}; bridge::send(tb::CLS, tb::ID_ACK, p, 2); }
static void nack(uint8_t id){ uint8_t p[2] = {tb::CLS, id}; bridge::send(tb::CLS, tb::ID_NACK, p, 2); }

// Temps en millisecondes → "21.345" (entiers uniquement, v2 §4.4).
static void fmtMs(char* out, size_t cap, int32_t ms, bool sign){
  const char* s = ms < 0 ? "-" : (sign ? "+" : "");
  const uint32_t a = (uint32_t)(ms < 0 ? -ms : ms);
  snprintf(out, cap, "%s%lu.%03lu", s, (unsigned long)(a / 1000), (unsigned long)(a % 1000));
}

static void configPayload(uint8_t p[12]){
  p[0] = g_cfg.enabled; p[1] = g_cfg.dataRate; p[2] = g_cfg.flags; p[3] = 0;
  put_le16(p+4, g_cfg.statSpeed); put_le16(p+6, g_cfg.statInterval);
  put_le16(p+8, g_cfg.noFixInterval); put_le16(p+10, g_cfg.autoOffInterval);
}

static void applyRecorderSettings(){
  recd::Settings s;
  s.flags = g_cfg.flags; s.statSpeed = g_cfg.statSpeed; s.statInterval = g_cfg.statInterval;
  s.noFixInterval = g_cfg.noFixInterval; s.autoOffInterval = g_cfg.autoOffInterval;
  g_rec.configure(s);
}

// Texte d'état envoyé à la radio : « S REC C » = enregistre, lignes en
// circuit (C), dragster (D) ou aucune (-). Le script Lua en tire l'état des
// lignes, conservées par le module d'un démarrage à l'autre.
static const char* stateText(){
  static char t[16];
  const char* st;
  switch(g_rec.state()){
    case recd::RECORDING: st = (g_havePvt && g_pvt.fixType >= 3) ? "REC" : "NOFIX"; break;
    case recd::PAUSED:    st = "PAUSE"; break;
    default:              st = "STOP"; break;
  }
  snprintf(t, sizeof t, "S %s %c", st, g_lines.mode == 2 ? 'D' : g_lines.mode == 1 ? 'C' : '-');
  return t;
}

// Changement d'état : notifié à la console, stocké si demandé.
static void stateChanged(uint8_t reason, bool store){
  uint8_t p[12];
  rec::buildState(p, g_rec.state(), reason, g_havePvt ? g_pvt.iTOW : 0, g_rec.pointsStored());
  bridge::send(tb::CLS, tb::ID_STATE, p, 12);
  if(store && g_storageOk){
    uint8_t slot[80] = {0}; memcpy(slot, p, 12);
    storage::append(rec::SLOT_STATE, slot);
  }
  radio::status(stateText());
  Serial.printf("[rec] état %u (motif %u)%s\n", g_rec.state(), reason, store ? ", stocké" : "");
  // Frontière de session : le chrono repart de zéro, comme la console qui
  // découpe les sessions aux changements d'état stockés.
  if(store){ g_lap.flush(); g_lap.reset(); }
}

static void startRecording(uint8_t reason){
  if(g_rec.start(millis())) stateChanged(reason, true);
}
static void stopRecording(uint8_t reason){
  if(g_rec.stop()) stateChanged(reason, true);
}

static void saveCfg(){ if(g_storageOk && !storage::saveRecConfig(g_cfg)) Serial.println("[cfg] ÉCHEC d'écriture"); }

// ---------------------------------------------------------------------------
//  Lignes
// ---------------------------------------------------------------------------
static bool startSet(){  return g_lines.startLat  != 0 || g_lines.startLon  != 0; }
static bool finishSet(){ return g_lines.finishLat != 0 || g_lines.finishLon != 0; }

static void applyLines(){
  g_lines.mode = startSet() && finishSet() ? 2 : (startSet() || finishSet()) ? 1 : 0;
  lap::Line A, B;
  if(startSet())  A = lap::makeLineFromHeading(g_lines.startLat / 1e7,  g_lines.startLon / 1e7,  g_lines.startHeading / 1e5);
  if(finishSet()) B = lap::makeLineFromHeading(g_lines.finishLat / 1e7, g_lines.finishLon / 1e7, g_lines.finishHeading / 1e5);
  // Circuit avec seule l'arrivée posée : la console utilise « B || A » ;
  // le moteur, lui, prend la ligne A quand B est seule.
  if(!A.set && B.set){ A = B; B = lap::Line(); }
  g_lap.setLines(A, B);
  g_prevBestMs = 0;
}

static bool positionAt(uint32_t atMs, int32_t& lat, int32_t& lon, int32_t& head, int32_t& speed){
  if(g_histN < 2) return false;
  for(uint8_t k = 0; k + 1 < g_histN; k++){
    const Hist& a = g_hist[(g_histHead + 64 - g_histN + k) % 64];
    const Hist& b = g_hist[(g_histHead + 64 - g_histN + k + 1) % 64];
    if(atMs < a.ms || atMs > b.ms) continue;
    if(!a.fix || !b.fix) return false;
    const double f = b.ms > a.ms ? (double)(atMs - a.ms) / (b.ms - a.ms) : 0;
    lat = (int32_t)llround(a.lat + (b.lat - a.lat) * f);
    lon = (int32_t)llround(a.lon + (b.lon - a.lon) * f);
    head = b.head; speed = b.gSpeed < a.gSpeed ? b.gSpeed : a.gSpeed;
    return true;
  }
  // Geste plus récent que la dernière solution reçue : on prend celle-ci.
  const Hist& last = g_hist[(g_histHead + 63) % 64];
  if(atMs >= last.ms && atMs - last.ms < 200 && last.fix){
    lat = last.lat; lon = last.lon; head = last.head; speed = last.gSpeed; return true;
  }
  return false;
}

static void onLineCommand(const linecmd::Output& o){
  if(o.cmd == linecmd::Cmd::Clear){
    g_lines.startLat = g_lines.startLon = g_lines.startHeading = 0;
    g_lines.finishLat = g_lines.finishLon = g_lines.finishHeading = 0;
    applyLines();
    if(g_storageOk) storage::saveLineConfig(g_lines);
    radio::event("K EFFACE");
    Serial.println("[ligne] lignes effacées");
    return;
  }
  int32_t lat, lon, head, speed;
  if(!positionAt(o.atMs, lat, lon, head, speed)){ radio::event("K PAS DE FIX"); return; }
  if(speed < LINE_MIN_SPEED_MMS){ radio::event("K VIT FAIBLE"); return; }
  const bool start = o.cmd == linecmd::Cmd::PoseStart;
  if(start){ g_lines.startLat = lat; g_lines.startLon = lon; g_lines.startHeading = head; }
  else     { g_lines.finishLat = lat; g_lines.finishLon = lon; g_lines.finishHeading = head; }
  applyLines();
  if(g_storageOk) storage::saveLineConfig(g_lines);
  radio::event(start ? "K DEPART OK" : "K ARRIVEE OK");
  Serial.printf("[ligne] %s posée : %.7f %.7f cap %.1f°\n", start ? "départ" : "arrivée", lat / 1e7, lon / 1e7, head / 1e5);
}

// ---------------------------------------------------------------------------
//  Événements du chronométrage
// ---------------------------------------------------------------------------
static void storeLapSlot(const lap::Event& e){
  if(!g_storageOk || g_rec.state() != recd::RECORDING) return;
  uint8_t s[80] = {0};
  // [0] u32 iTOW (ms)  [4] u8 ligne  [5] u8 nature (0 passage, 1 tour,
  // 2 parcours, 3 chrono intermédiaire)  [6] u16 n°  [8] u32 temps (ms)
  // [12] u8 type de chrono (0 vitesse, 1 distance)  [14] u16 valeur
  const uint32_t itow = e.kind == lap::EventKind::Crossing
                      ? (uint32_t)fmod(llround(e.t * 1000.0), 604800000.0) : (g_havePvt ? g_pvt.iTOW : 0);
  put_le32(s, itow); s[4] = e.line; s[5] = (uint8_t)e.kind; put_le16(s+6, e.n);
  if(e.kind != lap::EventKind::Crossing) put_le32(s+8, (uint32_t)llround(e.t * 1000.0));
  s[12] = e.splitKind; put_le16(s+14, (uint16_t)e.splitValue);
  storage::append(rec::SLOT_LAP, s);
}

static void onLapEvent(const lap::Event& e, void*){
  storeLapSlot(e);
  char txt[40], a[16], b[16];
  const int32_t ms = (int32_t)llround(e.t * 1000.0);
  switch(e.kind){
    case lap::EventKind::Lap:
    case lap::EventKind::Run: {
      // UN seul message par tour (ExpressLRS n'en transmet qu'un à la fois) :
      // « L12 21.345+0.35 » = tour 12, 21,345 s, 0,35 s de plus que le
      // meilleur tour précédent (négatif : nouveau meilleur tour).
      // L'écart est omis au premier tour, ou si le texte dépasserait 15
      // caractères. Le script Lua déduit lui-même le meilleur tour.
      fmtMs(a, sizeof a, ms, false);
      snprintf(txt, sizeof txt, "L%u %s", e.n, a);
      if(e.n > 1){
        const int32_t dcs = (int32_t)lround((ms - g_prevBestMs) / 10.0);   // centièmes
        const uint32_t ad = (uint32_t)(dcs < 0 ? -dcs : dcs);
        snprintf(b, sizeof b, "%c%lu.%02lu", dcs < 0 ? '-' : '+', (unsigned long)(ad / 100), (unsigned long)(ad % 100));
        if(strlen(txt) + strlen(b) <= 15) strcat(txt, b);
      }
      radio::event(txt);
      g_prevBestMs = (int32_t)llround(e.best * 1000.0);
      Serial.printf("[chrono] %s %u : %s\n", e.kind == lap::EventKind::Lap ? "tour" : "parcours", e.n, a);
      break;
    }
    case lap::EventKind::Split:
      fmtMs(a, sizeof a, ms, false);
      if(e.splitKind == 0) snprintf(txt, sizeof txt, "R 0-%u %s", (unsigned)e.splitValue, a);
      else                 snprintf(txt, sizeof txt, "R %um %s", (unsigned)e.splitValue, a);
      radio::event(txt);
      break;
    default: break;
  }
}

// ---------------------------------------------------------------------------
//  Point d'accès Wi-Fi
// ---------------------------------------------------------------------------
static void wifiSet(bool want, const char* why){
  if(want == wifiap::on()) return;
  if(want){
    if(wifiap::start()){ radio::event("W WIFI ON"); Serial.printf("[wifi] allumé (%s)\n", why); }
  }else{
    wifiap::stop(); radio::event("W WIFI OFF"); Serial.printf("[wifi] éteint (%s)\n", why);
  }
}

// Allumage après WIFI_AUTO_ON_S s d'arrêt, coupure IMMÉDIATE dès que la
// voiture roule (3 solutions GNSS de suite au-dessus de 7,2 km/h), quel que
// soit le mode : la radio de commande passe avant la console.
static void serviceWifi(uint32_t now){
  const bool stale = !g_havePvt || now - g_lastPvtMs > 2000;   // pas de GNSS : réputé à l'arrêt
  const bool moving = !stale && g_moving;
  const bool still = stale || g_stillEpoch;
  if(moving){
    g_stillSince = 0;
    if(wifiap::on()) wifiSet(false, "la voiture roule");
    return;
  }
  if(still){ if(!g_stillSince) g_stillSince = now ? now : 1; }
  else g_stillSince = 0;                       // entre 5 et 7,2 km/h : on attend
  if(g_wifiAuto && !wifiap::on() && g_stillSince && now - g_stillSince >= WIFI_AUTO_ON_S * 1000u)
    wifiSet(true, "arrêt prolongé");
}

// Appui long sur BOOT : bascule marche / arrêt du Wi-Fi.
static void serviceButton(uint32_t now){
  const bool down = digitalRead(PIN_BUTTON) == LOW;
  if(!down){ g_btnDown = 0; g_btnFired = false; return; }
  if(!g_btnDown) g_btnDown = now ? now : 1;
  if(!g_btnFired && now - g_btnDown >= BUTTON_LONG_MS){
    g_btnFired = true;
    if(wifiap::on()){ g_wifiAuto = false; wifiSet(false, "bouton"); }
    else if(!g_moving){ g_wifiAuto = true; wifiSet(true, "bouton"); }
  }
}

// ---------------------------------------------------------------------------
//  Mise à jour par Wi-Fi : conditions (v2 §7.2)
// ---------------------------------------------------------------------------
static const char* otaGate(){
  if(g_rec.state() != recd::STOPPED) return "arrêtez d'abord l'enregistrement";
  if(g_moving) return "la voiture roule";
  if(g_dl.active) return "un téléchargement est en cours";
  if(storage::erasing()) return "un effacement est en cours";
  return nullptr;
}

// ---------------------------------------------------------------------------
//  Commandes de la console
// ---------------------------------------------------------------------------
static void onCommand(const tb::Frame& f){
  if(f.cls != tb::CLS){ return; }
  switch(f.id){
    case tb::ID_STATUS: {
      uint8_t p[12] = {0};
      p[0] = g_rec.state() != recd::STOPPED ? 1 : 0;
      p[1] = storage::fillPercent();
      p[2] = 0;                                   // jamais verrouillée
      put_le32(p+4, storage::usedSlots()); put_le32(p+8, storage::capacitySlots());
      bridge::send(tb::CLS, tb::ID_STATUS, p, 12);
      break;
    }
    case tb::ID_CONFIG: {
      if(f.len == 0){ uint8_t p[12]; configPayload(p); bridge::send(tb::CLS, tb::ID_CONFIG, p, 12); break; }
      const uint8_t* p = f.payload;
      if(p[1] > 4){ nack(tb::ID_CONFIG); break; }
      const bool rateChanged = p[1] != g_cfg.dataRate;
      g_cfg.enabled = p[0] ? 1 : 0; g_cfg.dataRate = p[1]; g_cfg.flags = p[2];
      g_cfg.statSpeed = get_le16(p+4); g_cfg.statInterval = get_le16(p+6);
      g_cfg.noFixInterval = get_le16(p+8); g_cfg.autoOffInterval = get_le16(p+10);
      applyRecorderSettings();
      if(rateChanged && g_gnssOk) gnss::setRate(g_cfg.dataRate);
      saveCfg();
      if(g_cfg.enabled) startRecording(rec::REASON_COMMAND);
      else              stopRecording(rec::REASON_COMMAND);
      ack(tb::ID_CONFIG);
      break;
    }
    case tb::ID_GNSSCFG: {
      if(f.len == 0){ uint8_t p[3] = {g_cfg.gnssDynModel, g_cfg.gnss3dSpeed, g_cfg.gnssMinAcc}; bridge::send(tb::CLS, tb::ID_GNSSCFG, p, 3); break; }
      const bool dyn = f.payload[0] != g_cfg.gnssDynModel;
      g_cfg.gnssDynModel = f.payload[0]; g_cfg.gnss3dSpeed = f.payload[1]; g_cfg.gnssMinAcc = f.payload[2];
      if(dyn && g_gnssOk) gnss::setDynModel(g_cfg.gnssDynModel);
      saveCfg();
      ack(tb::ID_GNSSCFG);
      break;
    }
    case tb::ID_DOWNLOAD: {
      if(f.len == 1 && f.payload[0] == 0x00 && g_dl.active){   // annulation
        g_dl.active = false; ack(tb::ID_DOWNLOAD); break;
      }
      if(!g_storageOk || storage::erasing() || g_dl.active){ nack(tb::ID_DOWNLOAD); break; }
      g_dl.active = true;
      g_dl.extended = f.len == 1 && (f.payload[0] & 0x02);   // + emplacements v2 (ESC, chrono)
      g_dl.idx = 0; g_dl.end = storage::usedSlots(); g_dl.sent = 0; g_dl.rejected = 0;
      uint8_t p[4]; put_le32(p, g_dl.end);
      bridge::send(tb::CLS, tb::ID_DOWNLOAD, p, 4);
      Serial.printf("[dl] début : %lu emplacements\n", (unsigned long)g_dl.end);
      break;
    }
    case tb::ID_ERASE:
      // Refusé pendant un enregistrement ou un téléchargement. Une fois lancé,
      // il n'est PAS annulable (v1 §9.7).
      if(!g_storageOk || g_rec.state() != recd::STOPPED || g_dl.active || storage::erasing()){ nack(tb::ID_ERASE); break; }
      storage::eraseBegin(); g_erasePct = 255;
      Serial.println("[mem] effacement");
      break;
    case tb::ID_UNLOCK:
      ack(tb::ID_UNLOCK);
      break;
    case tb::ID_BUILD: {
      char t[96];
      const int n = snprintf(t, sizeof t, "%s|%s|%s|%d|%s", BRAND, FIRMWARE_VER, BUILD_STAMP, ACCEL_RANGE_G, DEVICE_NICKNAME);
      bridge::send(tb::CLS, tb::ID_BUILD, (const uint8_t*)t, (uint16_t)n);
      break;
    }
    case tb::ID_LINES: {
      if(f.len == 0){ uint8_t p[28]; persist::encodeLinesPayload(g_lines, p); bridge::send(tb::CLS, tb::ID_LINES, p, 28); break; }
      persist::decodeLinesPayload(f.payload, g_lines);
      applyLines();
      radio::setLineChannel(g_lines.crsfChannel);
      if(g_storageOk) storage::saveLineConfig(g_lines);
      ack(tb::ID_LINES);
      break;
    }
    case tb::ID_WIFI: {
      // 0 : couper et désactiver l'automatique ; 1 : allumer maintenant
      // (refusé si la voiture roule) ; 2 : mode automatique seul.
      if(f.len == 0){
        uint8_t p[4] = {(uint8_t)wifiap::on(), (uint8_t)g_wifiAuto, wifiap::stations(), (uint8_t)wifiap::wsConnected()};
        bridge::send(tb::CLS, tb::ID_WIFI, p, 4); break;
      }
      const uint8_t m = f.payload[0];
      if(m == 0){ g_wifiAuto = false; ack(tb::ID_WIFI); wifiSet(false, "commande"); }
      else if(m == 1){ if(g_moving){ nack(tb::ID_WIFI); break; } g_wifiAuto = true; ack(tb::ID_WIFI); wifiSet(true, "commande"); }
      else if(m == 2){ g_wifiAuto = true; ack(tb::ID_WIFI); }
      else nack(tb::ID_WIFI);
      break;
    }
    default:
      nack(f.id);
      break;
  }
}

// ---------------------------------------------------------------------------
//  Tâches progressives
// ---------------------------------------------------------------------------
static void serviceErase(){
  if(!storage::erasing()) return;
  uint8_t pct;
  const bool done = storage::eraseStep(pct);
  if(done){
    uint8_t p = 100; bridge::send(tb::CLS, tb::ID_ERASE, &p, 1);
    ack(tb::ID_ERASE);
    g_rec.setPointsStored(0);
    Serial.println("[mem] effacement terminé");
  }else if(pct != g_erasePct){
    g_erasePct = pct;
    bridge::sendLive(tb::CLS, tb::ID_ERASE, &pct, 1);   // progression : sacrifiable
  }
}

static void serviceDownload(){
  if(!g_dl.active) return;
  if(!bridge::connected()){ g_dl.active = false; return; }
  while(g_dl.idx < g_dl.end && bridge::freeSpace() >= 88 + 256){
    uint8_t type, p[80];
    if(!storage::readSlot(g_dl.idx++, type, p)) continue;
    if(type == rec::SLOT_DATA){
      if(rec::validData(p)){ bridge::send(tb::CLS, tb::ID_HIST, p, 80); g_dl.sent++; }
      else g_dl.rejected++;
    }else if(type == rec::SLOT_STATE){
      bridge::send(tb::CLS, tb::ID_STATE, p, 12);
    }else if(g_dl.extended && (type == rec::SLOT_ESC || type == rec::SLOT_LAP)){
      bridge::send(tb::CLS, type, p, 80);
    }
    // autres types (0x00 bourrage, inconnus) : ignorés
  }
  if(g_dl.idx >= g_dl.end && bridge::freeSpace() >= 16){
    ack(tb::ID_DOWNLOAD);
    g_dl.active = false;
    Serial.printf("[dl] terminé : %lu points envoyés, %lu enregistrement(s) corrompu(s) écarté(s)\n",
                  (unsigned long)g_dl.sent, (unsigned long)g_dl.rejected);
  }
}

static void powerDown(){
  Serial.println("[alim] extinction automatique (appui sur BOOT pour réveiller)");
  Serial.flush();
  wifiap::stop();
  gnss::powerOff();
  esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_BUTTON, 0);
  esp_deep_sleep_start();
}

// ---------------------------------------------------------------------------
//  Acquisition
// ---------------------------------------------------------------------------
static void onPvt(const rec::Pvt& p){
  g_pvt = p; g_havePvt = true;
  const uint32_t now = millis();
  g_lastPvtMs = now;
  const rec::Imu m = imu::take();

  // iTOW continu (passage de semaine GNSS)
  if(g_itowPrev && p.iTOW + 302400000u < g_itowPrev) g_weekOffset += 604800.0;
  g_itowPrev = p.iTOW;

  bool fix = p.fixType >= 3 && p.gnssFixOK();
  if(fix && g_cfg.gnssMinAcc && p.hAcc > (uint32_t)g_cfg.gnssMinAcc * 1000u) fix = false;

  // Mouvement, pour le point d'accès : sans fix, la voiture est réputée
  // à l'arrêt (on ne roule pas en course sans GNSS).
  const bool movingEpoch = fix && p.gSpeed >= WIFI_MOVE_MMS;
  g_moveCnt = movingEpoch ? (g_moveCnt < 255 ? g_moveCnt + 1 : 255) : 0;
  g_moving = g_moveCnt >= WIFI_MOVE_EPOCHS;
  g_stillEpoch = !(fix && p.gSpeed >= WIFI_STILL_MMS);

  uint8_t data[80];
  rec::buildData(data, p, m, 0 /* batterie : fournie par l'ESC plus tard */, g_cfg.gnss3dSpeed);

  // Enregistrement
  const recd::Result r = g_rec.epoch(now, fix, p.gSpeed);
  if(r.changed) stateChanged(r.reason, r.storeChange);
  if(r.store && g_storageOk){
    if(storage::append(rec::SLOT_DATA, data)) g_rec.notePointStored();
    else if(!storage::erasing()){
      Serial.println("[mem] PLEINE : arrêt de l'enregistrement");
      radio::event("S PLEIN");
      g_cfg.enabled = 0; saveCfg();
      stopRecording(rec::REASON_COMMAND);
    }
  }
  if(r.powerOff && !HWCDC::isPlugged() && millis() - g_bootMs > MIN_UPTIME_BEFORE_SLEEP_S * 1000u){
    g_cfg.enabled = 0; saveCfg();
    powerDown();
  }

  // Données en direct (suspendues pendant un téléchargement)
  if(bridge::connected() && !g_dl.active) bridge::sendLive(tb::CLS, tb::ID_LIVE, data, 80);

  // Chronométrage embarqué
  lap::Point lp;
  lp.t = g_weekOffset + p.iTOW / 1000.0;
  lp.lat = p.lat / 1e7; lp.lon = p.lon / 1e7;
  lp.speed = p.gSpeed * 0.0036; lp.fix = p.fixType; lp.sats = p.numSV;
  lp.alt = p.hMSL / 1000.0;
  lp.ax = m.ax / 1000.0; lp.ay = m.ay / 1000.0; lp.az = m.az / 1000.0;
  g_lap.feed(lp);

  // Radio
  radio::setGps(p);
  radio::status(stateText());

  // Historique pour la pose de ligne
  g_hist[g_histHead] = {now, p.lat, p.lon, p.headMot, p.gSpeed, fix};
  g_histHead = (g_histHead + 1) % 64; if(g_histN < 64) g_histN++;
}

// ---------------------------------------------------------------------------
//  Port série de secours (v1 §4.7)
// ---------------------------------------------------------------------------
static void printInfo(){
  Serial.printf("\n== %s « %s » %s — %s ==\n", BRAND, DEVICE_NICKNAME, FIRMWARE_VER, BUILD_STAMP);
  Serial.printf("mémoire : %lu / %lu emplacements (%u %%)%s\n", (unsigned long)storage::usedSlots(),
                (unsigned long)storage::capacitySlots(), storage::fillPercent(), g_storageOk ? "" : " — INDISPONIBLE");
  Serial.printf("enregistrement : état %u, %lu points cette session\n", g_rec.state(), (unsigned long)g_rec.pointsStored());
  Serial.printf("config : cadence %u, filtres 0x%02X, arrêt %u mm/s / %u s, sans fix %u s, extinction %u s\n",
                g_cfg.dataRate, g_cfg.flags, g_cfg.statSpeed, g_cfg.statInterval, g_cfg.noFixInterval, g_cfg.autoOffInterval);
  Serial.printf("lignes : mode %u, voie CH%u, départ %s, arrivée %s\n", g_lines.mode, g_lines.crsfChannel,
                startSet() ? "posé" : "—", finishSet() ? "posée" : "—");
}

static void printBench(){
  const gnss::Stats& g = gnss::stats();
  const radio::Stats& r = radio::stats();
  Serial.printf("[banc] GNSS %.1f Hz (%lu PVT, %lu erreurs, %s, ACK %u / NAK %u)\n", g.rateHz,
                (unsigned long)g.pvtCount, (unsigned long)g.badChecksums, g.galileo ? "GPS+Galileo" : "GPS seul", g.acks, g.naks);
  if(g_havePvt) Serial.printf("[banc] fix %u, %u sat, hAcc %.2f m, %.1f km/h\n", g_pvt.fixType, g_pvt.numSV, g_pvt.hAcc / 1000.0, g_pvt.gSpeed * 0.0036);
  Serial.printf("[banc] IMU %lu échantillons (0x%02X)\n", (unsigned long)imu::samples(), imu::whoAmI());
  Serial.printf("[banc] CRSF %.0f trames/s, %lu CRC faux, %lu télémétries, liaison %s", r.rcRateHz,
                (unsigned long)r.badCrc, (unsigned long)r.txFrames, r.linkUp ? "OK" : "absente");
  if(r.haveLink) Serial.printf(", LQ %u %%, RSSI -%u dBm", r.link.lq, r.link.rssi1);
  Serial.printf("\n[banc] BLE %s, MTU %u, file %u octets libres\n", bridge::bleConnected() ? "connecté" : "libre", bridge::mtu(), (unsigned)bridge::freeSpace());
  Serial.printf("[banc] firmware sur %s%s\n", ota::runningPartition(), ota::pendingValidation() ? " (EN VALIDATION)" : "");
  Serial.printf("[banc] Wi-Fi %s (« %s », auto %s), %u appareil(s), console %s, %lu page(s) servie(s)\n",
                wifiap::on() ? "ALLUMÉ" : "éteint", g_ssid, g_wifiAuto ? "oui" : "non", wifiap::stations(),
                wifiap::wsConnected() ? "connectée" : "—", (unsigned long)wifiap::pagesServed());
}

static void serviceSerialCommands(){
  while(Serial.available()){
    const char c = (char)Serial.read();
    switch(c){
      case 's': g_cfg.enabled = 0; saveCfg(); stopRecording(rec::REASON_SERIAL); Serial.println("ARRÊT D'URGENCE"); break;
      case 'r': g_cfg.enabled = 1; saveCfg(); startRecording(rec::REASON_SERIAL); Serial.println("enregistrement démarré"); break;
      case 'i': printInfo(); break;
      case 'w': if(wifiap::on()){ g_wifiAuto = false; wifiSet(false, "port série"); }
                else if(!g_moving){ g_wifiAuto = true; wifiSet(true, "port série"); }
                break;
      case 'b': printBench(); break;
      case 'z': {
        persist::RecConfig d; d.seq = g_cfg.seq; g_cfg = d;
        applyRecorderSettings(); saveCfg();
        if(g_gnssOk){ gnss::setRate(g_cfg.dataRate); gnss::setDynModel(g_cfg.gnssDynModel); }
        stopRecording(rec::REASON_SERIAL);
        Serial.println("configuration par défaut (données conservées)");
        break;
      }
      case '?': Serial.println("s arrêt d'urgence | r démarrer l'enregistrement | w Wi-Fi marche/arrêt | i état | b banc d'essai | z config par défaut | ? aide"); break;
      default: break;
    }
  }
}

static void serviceLed(){
  const uint32_t now = millis();
  if(now - g_lastLed < 250) return;
  g_lastLed = now;
  const bool blink = (now / 500) & 1;
  uint8_t r = 0, g = 0, b = 0;
  if(!g_storageOk || !g_gnssOk)                    r = blink ? 40 : 0;         // défaut matériel
  else if(g_rec.state() == recd::RECORDING)        r = 40;
  else if(g_rec.state() == recd::PAUSED)           { r = 20; g = 10; }
  else if(g_havePvt && g_pvt.fixType >= 3)         g = blink ? 20 : 4;
  if(bridge::connected())                            b = 30;
  if(wifiap::on() && !bridge::connected())          b = blink ? 30 : 0;   // point d'accès en attente
#ifndef TRIMBOX_QEMU      // RMT non émulé : l'écriture bloquerait indéfiniment
  rgbLedWrite(PIN_LED_RGB, r, g, b);
#else
  (void)r; (void)g; (void)b;
#endif
}

// ---------------------------------------------------------------------------
void setup(){
  g_bootMs = millis();
  Serial.begin(115200);
  delay(300);
  uint8_t mac[6]; esp_read_mac(mac, ESP_MAC_WIFI_STA);
  snprintf(g_serial, sizeof g_serial, "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  Serial.printf("\n%s « %s » firmware %s, compilé le %s, partition %s\n", BRAND, DEVICE_NICKNAME, FIRMWARE_VER, BUILD_STAMP,
                ota::runningPartition());
#ifdef TRIMBOX_CRASH_TEST
  // Variante de test UNIQUEMENT (tests/qemu/ota_rollback.py) : firmware qui
  // plante au démarrage, pour vérifier le retour automatique à la version
  // précédente. Jamais compilée pour la carte.
  Serial.println("[test] plantage volontaire");
  Serial.flush();
  abort();
#endif

  g_storageOk = storage::begin();
  if(!g_storageOk) Serial.printf("[mem] %s\n", storage::lastError());
  else if(*storage::lastError()) Serial.printf("[mem] %s\n", storage::lastError());
  if(g_storageOk && !storage::loadRecConfig(g_cfg)){ g_cfg = persist::RecConfig(); saveCfg(); }
  // Pas de reprise automatique (v1 §4.5) : « actif » au démarrage signifie
  // que la session précédente s'est mal terminée (coupure d'alimentation).
  if(g_cfg.enabled && !AUTO_RESUME_RECORDING){
    Serial.println("[rec] session précédente interrompue : l'appareil repart À L'ARRÊT");
    g_cfg.enabled = 0; saveCfg();
  }
  if(g_storageOk) storage::loadLineConfig(g_lines);
  applyRecorderSettings();
  g_lap.setSink(onLapEvent, nullptr);
  applyLines();

  g_imuOk  = imu::begin();
  g_gnssOk = gnss::begin(g_cfg.dataRate, g_cfg.gnssDynModel);
  radio::begin(g_lines.crsfChannel);
  // SSID : « TrimBox-<pseudo> », espaces remplacés par des tirets.
  snprintf(g_ssid, sizeof g_ssid, "TrimBox-%s", DEVICE_NICKNAME);
  for(char* c = g_ssid; *c; c++) if(*c == ' ') *c = '-';
  ota::begin(otaGate);          // avant wifiap::begin (qui référence son puits)
  wifiap::begin(g_ssid, WIFI_PASS);
  if(ota::pendingValidation()) radio::event("U VALIDATION");
  pinMode(PIN_BUTTON, INPUT_PULLUP);
#ifndef TRIMBOX_QEMU      // l'émulateur n'a pas de radio Bluetooth
  bridge::begin(DEVICE_NAME, g_serial);
#endif
  radio::status(stateText());
  printInfo();
  Serial.println("tapez ? pour l'aide");
#ifdef TRIMBOX_QEMU
  g_cfg.enabled = 1; saveCfg(); startRecording(rec::REASON_SERIAL);
#endif
  // Chien de garde de la boucle : si elle se fige plus de 5 s, la carte
  // redémarre. Juste après une mise à jour, ce redémarrage déclenche le
  // retour à la version précédente (v2 §7.2). Aucune opération de la boucle
  // ne dure plus de 3 s (reconfiguration GNSS, la plus longue).
  enableLoopWDT();
}

void loop(){
#ifndef TRIMBOX_QEMU
  serviceSerialCommands();                             // 1
#else
  // Émulateur : UART0 est partagé avec le faux GNSS, pas de commandes.
  static uint32_t lastInfo = 0;
  if(millis() - lastInfo > 10000){ lastInfo = millis(); printInfo(); printBench(); }
#endif
  uint8_t buf[128]; size_t n;                          // 2
  while((n = bridge::read(buf, sizeof buf)) > 0) g_rx.push(buf, n);
  wifiap::service();
  while((n = wifiap::read(buf, sizeof buf)) > 0) g_rxWs.push(buf, n);
  tb::Frame f;
  while(g_rx.next(f)) onCommand(f);
  while(g_rxWs.next(f)) onCommand(f);
  serviceErase();                                      // 3
  serviceDownload();                                   // 4
  bridge::service();                                     // 5
  // 6 : l'extinction est décidée par l'automate, à chaque époque (onPvt)
  imu::poll();                                         // 7
  rec::Pvt p;
  if(g_gnssOk && gnss::poll(p)) onPvt(p);
  const linecmd::Output o = radio::poll();
  if(o.cmd != linecmd::Cmd::None) onLineCommand(o);
  const uint32_t now = millis();
  serviceWifi(now);
  {
    static bool wasPending = ota::pendingValidation(), wasReboot = false;
    ota::serviceValidation(g_storageOk);
    if(wasPending && !ota::pendingValidation()){ wasPending = false; radio::event("U MAJ OK"); }
    if(!wasReboot && ota::rebootPending()){ wasReboot = true; radio::event("U REDEMARRAGE"); }
  }
#ifndef TRIMBOX_QEMU      // GPIO 0 non câblé dans l'émulateur
  serviceButton(now);
#endif
  serviceLed();
}

} // namespace app
