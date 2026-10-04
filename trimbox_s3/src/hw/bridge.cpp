#include "bridge.h"
#include "../config.h"
#include "../core/tbproto.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include "wifiap.h"

namespace bridge {

static const char* UUID_NUS = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
static const char* UUID_RX  = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";   // écriture
static const char* UUID_TX  = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";   // notification

static NimBLEServer* s_server = nullptr;
static NimBLECharacteristic* s_tx = nullptr;
static volatile bool s_connected = false;
static volatile uint16_t s_mtu = 23, s_conn = 0xFFFF;

// ---- file d'émission (utilisée uniquement depuis la boucle principale) ----
static uint8_t q_buf[TXQ_SIZE];
static size_t q_head = 0, q_tail = 0, q_len = 0;

size_t freeSpace(){ return TXQ_SIZE - q_len; }
void clear(){ q_head = q_tail = q_len = 0; }

static void qput(const uint8_t* p, size_t n){
  for(size_t i=0;i<n;i++){ q_buf[q_head] = p[i]; q_head = (q_head + 1) % TXQ_SIZE; }
  q_len += n;
}

bool send(uint8_t cls, uint8_t id, const uint8_t* payload, uint16_t len){
  if(len > tb::MAX_PAYLOAD) return false;
  if(freeSpace() < len + tb::OVERHEAD) return false;
  uint8_t f[tb::MAX_PAYLOAD + tb::OVERHEAD];
  qput(f, tb::build(f, cls, id, payload, len));
  return true;
}

bool sendLive(uint8_t cls, uint8_t id, const uint8_t* payload, uint16_t len){
  if(freeSpace() < LIVE_RESERVE) return false;     // sacrifiable
  return send(cls, id, payload, len);
}

void service(){
  // Le Wi-Fi a la priorité : une seule console à la fois reçoit le flux.
  if(wifiap::wsConnected()){
    uint8_t tmp[1024];
    for(int k = 0; k < 8 && q_len; k++){
      const size_t n = q_len < sizeof tmp ? q_len : sizeof tmp;
      for(size_t i=0;i<n;i++) tmp[i] = q_buf[(q_tail + i) % TXQ_SIZE];
      wifiap::wsSend(tmp, n);
      q_tail = (q_tail + n) % TXQ_SIZE;
      q_len -= n;
    }
    return;
  }
  if(!s_connected){ clear(); return; }
  uint16_t chunk = s_mtu > 3 ? s_mtu - 3 : 20;
  if(chunk > 244) chunk = 244;
  uint8_t tmp[244];
  for(int k = 0; k < 12 && q_len; k++){
    const size_t n = q_len < chunk ? q_len : chunk;
    for(size_t i=0;i<n;i++) tmp[i] = q_buf[(q_tail + i) % TXQ_SIZE];
    // notify() échoue quand la pile n'a plus de tampon : les octets restent
    // dans la file et partiront au prochain tour, sans perte ni réordonnancement.
    if(!s_tx->notify(tmp, n)) break;
    q_tail = (q_tail + n) % TXQ_SIZE;
    q_len -= n;
  }
}

// ---- réception (callbacks NimBLE, tâche de la pile) ----
static uint8_t r_buf[1024];
static volatile size_t r_head = 0, r_tail = 0;
static portMUX_TYPE r_mux = portMUX_INITIALIZER_UNLOCKED;

size_t read(uint8_t* buf, size_t cap){
  size_t n = 0;
  portENTER_CRITICAL(&r_mux);
  while(r_tail != r_head && n < cap){ buf[n++] = r_buf[r_tail]; r_tail = (r_tail + 1) % sizeof r_buf; }
  portEXIT_CRITICAL(&r_mux);
  return n;
}

class RxCb : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo&) override {
    const NimBLEAttValue v = c->getValue();
    portENTER_CRITICAL(&r_mux);
    for(size_t i=0;i<v.length();i++){
      const size_t nx = (r_head + 1) % sizeof r_buf;
      if(nx == r_tail) break;                     // plein : octets perdus
      r_buf[r_head] = v.data()[i]; r_head = nx;
    }
    portEXIT_CRITICAL(&r_mux);
  }
};

class ServerCb : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer*, NimBLEConnInfo& ci) override {
    s_connected = true; s_conn = ci.getConnHandle(); s_mtu = ci.getMTU();
  }
  void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int) override {
    s_connected = false; s_mtu = 23; s_conn = 0xFFFF;
    NimBLEDevice::startAdvertising();
  }
  void onMTUChange(uint16_t m, NimBLEConnInfo&) override { s_mtu = m; }
};

bool connected(){ return s_connected || wifiap::wsConnected(); }
bool bleConnected(){ return s_connected; }
uint16_t mtu(){ return s_mtu; }

void begin(const char* name, const char* serial){
  NimBLEDevice::init(name);
  NimBLEDevice::setPower(9);                 // pleine puissance (dBm)
  NimBLEDevice::setMTU(247);
  s_server = NimBLEDevice::createServer();
  s_server->setCallbacks(new ServerCb());

  NimBLEService* nus = s_server->createService(UUID_NUS);
  // Longueur maximale d'écriture : NimBLE accepte jusqu'à 512 octets par
  // défaut. (Sur la v1, Bluefruit plafonnait à 20 sans setMaxLen : v1 §9.5a.)
  NimBLECharacteristic* rx = nus->createCharacteristic(UUID_RX, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  rx->setCallbacks(new RxCb());
  s_tx = nus->createCharacteristic(UUID_TX, NIMBLE_PROPERTY::NOTIFY);


  NimBLEService* dis = s_server->createService("180A");
  dis->createCharacteristic("2A24", NIMBLE_PROPERTY::READ)->setValue(BRAND);
  dis->createCharacteristic("2A25", NIMBLE_PROPERTY::READ)->setValue(serial);
  dis->createCharacteristic("2A26", NIMBLE_PROPERTY::READ)->setValue(FIRMWARE_VER);
  dis->createCharacteristic("2A27", NIMBLE_PROPERTY::READ)->setValue(HARDWARE_VER);
  dis->createCharacteristic("2A29", NIMBLE_PROPERTY::READ)->setValue(MANUFACTURER);
  s_server->start();

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  NimBLEAdvertisementData ad, sr;
  ad.setFlags(0x06);
  ad.setName(name);                          // le filtre de la console : « TrimBox… »
  sr.addServiceUUID(UUID_NUS);               // UUID 128 bits : dans la réponse de scan
  adv->setAdvertisementData(ad);
  adv->setScanResponseData(sr);
  adv->start();
}

} // namespace bridge
