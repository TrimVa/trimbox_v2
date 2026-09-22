#include "wifiap.h"
#include "../config.h"
#include "../core/httpws.h"
#include "../console_gz.h"
#include "../core/updatepage.h"
#include "ota.h"
#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>

namespace wifiap {

static const IPAddress AP_IP(192, 168, 4, 1), AP_MASK(255, 255, 255, 0);
static char s_ssid[33], s_pass[65];
static bool s_on = false;
static WiFiServer s_server(80);
static DNSServer s_dns;
static web::Site s_site;
static uint32_t s_pages = 0;

struct Slot {
  bool used = false;
  WiFiClient client;
  web::Conn conn;
  uint32_t since = 0;
};
static constexpr int MAX_CONN = 4;
static Slot s_slots[MAX_CONN];
static int s_ws = -1;                  // connexion WebSocket active

static void writeFn(void* ctx, const uint8_t* d, size_t n){
  WiFiClient* c = (WiFiClient*)ctx;
  size_t off = 0; const uint32_t t0 = millis();
  // La page (≈ 48 Ko) part en plusieurs fois ; on borne l'attente pour ne
  // jamais figer la boucle plus de 1,5 s (voiture à l'arrêt de toute façon).
  while(off < n && c->connected() && millis() - t0 < 1500){
    const size_t w = c->write(d + off, n - off);
    if(w) off += w; else delay(1);
  }
}

void begin(const char* ssid, const char* pass){
  strncpy(s_ssid, ssid, sizeof s_ssid - 1);
  strncpy(s_pass, pass, sizeof s_pass - 1);
  s_site.page = CONSOLE_GZ; s_site.pageLen = CONSOLE_GZ_LEN;
  s_site.home = "http://192.168.4.1/"; s_site.hostName = "192.168.4.1";
  s_site.ota = ota::sink(); s_site.updatePage = UPDATE_PAGE;
}

const char* ssid(){ return s_ssid; }
bool on(){ return s_on; }
uint32_t pagesServed(){ return s_pages; }
uint8_t stations(){
#ifdef TRIMBOX_QEMU
  return 0;
#else
  return s_on ? WiFi.softAPgetStationNum() : 0;
#endif
}
bool wsConnected(){ return s_on && s_ws >= 0; }

bool start(){
  if(s_on) return true;
#ifdef TRIMBOX_QEMU      // pas de Wi-Fi dans l'émulateur : on trace la décision
  s_on = true; Serial.println("[wifi] point d'accès actif (émulateur)"); return true;
#endif
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(AP_IP, AP_IP, AP_MASK);
  // Canal fixe, 2 clients au plus ; puissance réduite : la portée d'un
  // stand suffit et on limite la gêne pour une radio 2,4 GHz (v2 §10.2).
  if(!WiFi.softAP(s_ssid, s_pass, WIFI_CHANNEL, 0, 2)){ WiFi.mode(WIFI_OFF); return false; }
  WiFi.setTxPower(WIFI_TX_POWER);
  s_server.begin();
  s_server.setNoDelay(true);
  // Portail captif : toute résolution DNS renvoie le module, le téléphone
  // propose alors d'ouvrir la console de lui-même.
  s_dns.start(53, "*", AP_IP);
  s_on = true;
  Serial.printf("[wifi] point d'accès « %s » actif — http://192.168.4.1/ (console v%s)\n", s_ssid, CONSOLE_EMBED_VER);
  return true;
}

void stop(){
  if(!s_on) return;
#ifdef TRIMBOX_QEMU
  s_on = false; Serial.println("[wifi] point d'accès coupé (émulateur)"); return;
#endif
  for(auto& s : s_slots) if(s.used){ s.conn.onClosed(); s.client.stop(); s.used = false; }
  s_ws = -1;
  s_dns.stop();
  s_server.end();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  s_on = false;
  Serial.println("[wifi] point d'accès coupé");
}

void service(){
  if(!s_on) return;
#ifdef TRIMBOX_QEMU
  return;
#endif
  // nouvelles connexions
  WiFiClient c = s_server.accept();
  if(c){
    int free = -1;
    for(int i = 0; i < MAX_CONN; i++) if(!s_slots[i].used){ free = i; break; }
    if(free < 0){ c.stop(); }
    else {
      Slot& s = s_slots[free];
      s.used = true; s.client = c; s.client.setNoDelay(true); s.since = millis();
      s.conn.begin(&s_site, writeFn, &s.client);
    }
  }
  // lecture et traitement
  for(int i = 0; i < MAX_CONN; i++){
    Slot& s = s_slots[i];
    if(!s.used) continue;
    bool keep = s.client.connected();
    uint8_t buf[1024];
    // Une mise à jour peut arriver vite : on lit davantage par tour pour elle.
    int budget = s.conn.state() == web::Conn::Upload ? 32 : 8;
    while(keep && s.client.available() && budget--){
      const int n = s.client.read(buf, sizeof buf);
      if(n <= 0) break;
      const uint32_t before = s.conn.pages;
      keep = s.conn.onData(buf, (size_t)n);
      s_pages += s.conn.pages - before;
      if(s.conn.isWebSocket() && s_ws != i){
        // Une seule console à la fois : la nouvelle remplace l'ancienne.
        if(s_ws >= 0){ s_slots[s_ws].conn.sendClose(); s_slots[s_ws].client.stop(); s_slots[s_ws].used = false; }
        s_ws = i;
        Serial.println("[wifi] console connectée (WebSocket)");
      }
    }
    // requête HTTP dont l'en-tête n'arrive jamais : on libère la place
    // (pas pendant l'envoi d'un firmware, qui peut durer plusieurs secondes)
    if(keep && s.conn.state() == web::Conn::ReadingRequest && millis() - s.since > 5000) keep = false;
    if(!keep){
      s.conn.onClosed();
      s.client.stop(); s.used = false;
      if(s_ws == i){ s_ws = -1; Serial.println("[wifi] console déconnectée"); }
    }
  }
}

size_t read(uint8_t* buf, size_t cap){
  if(!wsConnected()) return 0;
  return s_slots[s_ws].conn.takeBinary(buf, cap);
}

void wsSend(const uint8_t* d, size_t n){
  if(!wsConnected()) return;
  s_slots[s_ws].conn.sendBinary(d, n);
}

} // namespace wifiap
