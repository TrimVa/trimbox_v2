// Réassembleur TrimBox + CRSF + UBX + enregistrements.
#include "t.h"
#include "../../trimbox_s3/src/core/tbproto.h"
#include "../../trimbox_s3/src/core/crsf.h"
#include "../../trimbox_s3/src/core/ubx.h"
#include "../../trimbox_s3/src/core/records.h"
#include "../../trimbox_s3/src/core/checksums.h"
#include "../../trimbox_s3/src/core/bytes.h"
#include "../../trimbox_s3/src/core/httpws.h"
#include "../../trimbox_s3/src/core/otacheck.h"
#include <vector>
#include <string>
#include <string.h>
#include <stdlib.h>
#include <vector>

static void testTbStream(){
  // Banc v1 §8.3 n°1 : 500 trames avec des « B5 62 » dans les charges,
  // 3 octets perdus au milieu. Attendu : ≥ 99 % de trames valides.
  std::vector<uint8_t> s;
  srand(1);
  for(int k=0;k<500;k++){
    uint8_t p[80]; for(int i=0;i<80;i++) p[i] = rand();
    p[10] = 0xB5; p[11] = 0x62; p[40] = 0xB5; p[41] = 0x62; p[42] = 0xFF; p[43] = 0x01;
    put_le32(p, k);
    uint8_t f[96]; size_t n = tb::build(f, 0xFF, 0x21, p, 80);
    s.insert(s.end(), f, f+n);
  }
  s.erase(s.begin() + s.size()/2, s.begin() + s.size()/2 + 3);
  tb::Parser P(false); tb::Frame fr; int ok = 0;
  // flux découpé en fragments de taille variable (comme en Bluetooth)
  size_t i = 0;
  while(i < s.size()){
    size_t n = 1 + rand() % 60; if(i + n > s.size()) n = s.size() - i;
    P.push(&s[i], n); i += n;
    while(P.next(fr)) if(fr.id == 0x21 && fr.len == 80) ok++;
  }
  CHECK(ok >= 495, "réassembleur : %d / 500 trames", ok);
}

static void testTbCommands(){
  tb::Parser P(true); tb::Frame f;
  uint8_t b[64];
  size_t n = tb::build(b, 0xFF, tb::ID_STATUS, nullptr, 0);
  P.push(b, n);
  CHECK(P.next(f) && f.id == tb::ID_STATUS && f.len == 0, "commande FF 22 vide");
  uint8_t cfg[12] = {1,0,0x1F};
  n = tb::build(b, 0xFF, tb::ID_CONFIG, cfg, 12);
  P.push(b, n);
  CHECK(P.next(f) && f.id == tb::ID_CONFIG && f.len == 12 && f.payload[0] == 1, "commande FF 25 12 o");
  // longueur incohérente pour la commande : rejetée
  n = tb::build(b, 0xFF, tb::ID_STATUS, cfg, 5);
  P.push(b, n);
  CHECK(!P.next(f), "FF 22 de 5 octets rejetée");
}

static void testCrsf(){
  // voies : encodage 11 bits de référence puis décodage
  uint16_t in[16], out[16];
  for(int i=0;i<16;i++) in[i] = 172 + i*100;
  uint8_t p[22] = {0}; int bit = 0;
  for(int i=0;i<16;i++) for(int b=0;b<11;b++,bit++) if(in[i] & (1<<b)) p[bit/8] |= 1 << (bit%8);
  CHECK(crsf::decodeChannels(p, 22, out), "décodage voies");
  bool same = true; for(int i=0;i<16;i++) same &= in[i] == out[i];
  CHECK(same, "voies identiques après décodage");
  CHECK(crsf::channelPercent(1811) == 100 && crsf::channelPercent(172) == -100 && crsf::channelPercent(992) == 0, "pourcentages");

  // trame GPS : big-endian (v2 §10.4)
  uint8_t f[64];
  size_t n = crsf::buildGps(f, 453456789, -12345678, 13889 /*50 km/h*/, 9000000 /*90°*/, 150000, 12);
  CHECK(n == 19 && f[0] == 0xC8 && f[1] == 17 && f[2] == 0x02, "en-tête GPS");
  CHECK((int32_t)get_be32(f+3) == 453456789 && (int32_t)get_be32(f+7) == -12345678, "lat/lon big-endian");
  CHECK(get_be16(f+11) == 500, "vitesse km/h×10 : %u", get_be16(f+11));
  CHECK(get_be16(f+13) == 9000, "cap ×100 : %u", get_be16(f+13));
  CHECK(get_be16(f+15) == 1150 && f[17] == 12, "altitude +1000, sats");
  // relecture par le réassembleur CRSF (CRC vérifié)
  crsf::Parser P; int got = 0;
  for(size_t i=0;i<n;i++) if(P.feed(f[i])) got++;
  CHECK(got == 1 && P.type() == 0x02, "CRC GPS valide");
  n = crsf::buildFlightMode(f, "L12 21.345");
  got = 0; for(size_t i=0;i<n;i++) if(P.feed(f[i])) got++;
  CHECK(got == 1 && !strcmp((const char*)P.payload(), "L12 21.345"), "mode de vol texte");
  // vecteurs de référence du catalogue des CRC
  CHECK(crc8_d5((const uint8_t*)"123456789", 9) == 0xBC, "CRC8 DVB-S2 « 123456789 » = 0xBC (obtenu 0x%02X)", crc8_d5((const uint8_t*)"123456789", 9));
  CHECK(crc32_ieee((const uint8_t*)"123456789", 9) == 0xCBF43926u, "CRC32 « 123456789 »");
}

static void testUbx(){
  ubx::ValSet vs;
  vs.add(ubx::key::RATE_MEAS, 40);            // U2
  vs.add(ubx::key::UART1_BAUDRATE, 115200);   // U4
  vs.add(ubx::key::SIGNAL_GAL_ENA, 0);        // L
  uint8_t f[128]; size_t n = vs.finish(f, sizeof f);
  CHECK(n == 8 + 4 + (4+2) + (4+4) + (4+1), "taille VALSET : %zu", n);
  ubx::Parser P; int got = 0;
  for(size_t i=0;i<n;i++) if(P.feed(f[i])) got++;
  CHECK(got == 1 && P.cls() == 0x06 && P.id() == 0x8A, "relecture VALSET");
  CHECK(get_le32(P.payload()+4) == 0x30210001 && get_le16(P.payload()+8) == 40, "clé et valeur U2");
}

static void testRecords(){
  uint8_t pvt[92] = {0};
  put_le32(pvt+0, 123456); put_le16(pvt+4, 2026); pvt[6]=9; pvt[7]=20;
  pvt[20] = 3; pvt[21] = 1; pvt[23] = 14;
  put_le32(pvt+24, (uint32_t)-12345678); put_le32(pvt+28, 453456789);
  put_le32(pvt+60, 13889); put_le32(pvt+64, 9000000); put_le16(pvt+76, 123);
  rec::Pvt p; CHECK(rec::decodeNavPvt(pvt, 92, p), "décodage NAV-PVT");
  rec::Imu m{1000, -2000, 15990, 100, -200, 300};
  uint8_t d[80]; rec::buildData(d, p, m, 0x55, false);
  CHECK(get_le32(d) == 123456 && d[20] == 3 && d[23] == 14, "en-tête données");
  CHECK((int32_t)get_le32(d+24) == -12345678 && (int32_t)get_le32(d+28) == 453456789, "position");
  CHECK(get_le32(d+48) == 13889 && get_le32(d+52) == 9000000 && get_le16(d+64) == 123, "vitesse, cap, pDOP aux offsets 48/52/64");
  CHECK(d[67] == 0x55 && (int16_t)get_le16(d+72) == 15990 && (int16_t)get_le16(d+78) == 300, "batterie, accél., gyro");
  CHECK(rec::validData(d), "enregistrement valide");
  uint8_t blank[80]; memset(blank, 0xFF, 80);
  CHECK(!rec::validData(blank), "enregistrement effacé (0xFF) rejeté");
  memcpy(blank, d, 40);   // écriture interrompue à mi-chemin
  CHECK(!rec::validData(blank), "enregistrement interrompu rejeté");
  NEAR(rec::distM(45, 5, 45, 5.001), 111320*0.001*cos(45*M_PI/180), 1e-6, "distM longitude à 45°");
}

static std::string g_out;
static void sinkOut(void*, const uint8_t* d, size_t n){ g_out.append((const char*)d, n); }

static void testWeb(){
  uint8_t h[20]; char hx[41];
  web::sha1((const uint8_t*)"abc", 3, h);
  for(int i=0;i<20;i++) snprintf(hx+2*i, 3, "%02x", h[i]);
  CHECK(!strcmp(hx, "a9993e364706816aba3e25717850c26c9cd0d89d"), "SHA-1(abc) = %s", hx);
  std::string big(1000, 'a');
  web::sha1((const uint8_t*)big.data(), big.size(), h);
  for(int i=0;i<20;i++) snprintf(hx+2*i, 3, "%02x", h[i]);
  CHECK(!strcmp(hx, "291e9a6c66994949b57ba5e650361e98fc36b1ba"), "SHA-1(1000 × a) = %s", hx);
  char acc[29]; web::acceptKey("dGhlIHNhbXBsZSBub25jZQ==", acc);
  CHECK(!strcmp(acc, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo="), "clé WebSocket RFC 6455 : %s", acc);

  static const uint8_t page[] = {0x1f, 0x8b, 1, 2, 3};
  web::Site site; site.page = page; site.pageLen = sizeof page;
  web::Conn c;
  // page, requête arrivée en deux morceaux
  g_out.clear(); c.begin(&site, sinkOut, nullptr);
  const char* r1 = "GET / HTTP/1.1\r\nHo"; const char* r2 = "st: 192.168.4.1\r\n\r\n";
  CHECK(c.onData((const uint8_t*)r1, strlen(r1)), "en-tête partiel : on attend");
  CHECK(!c.onData((const uint8_t*)r2, strlen(r2)), "page envoyée puis fermeture");
  CHECK(g_out.find("200 OK") != std::string::npos && g_out.find("Content-Encoding: gzip") != std::string::npos
        && g_out.find("Content-Length: 5") != std::string::npos, "en-têtes de la console");
  CHECK(g_out.size() >= 5 && !memcmp(g_out.data() + g_out.size() - 5, page, 5), "corps gzip");
  // portail captif
  g_out.clear(); c.begin(&site, sinkOut, nullptr);
  const char* cap = "GET /generate_204 HTTP/1.1\r\nHost: connectivitycheck.gstatic.com\r\n\r\n";
  c.onData((const uint8_t*)cap, strlen(cap));
  CHECK(g_out.find("302 Found") != std::string::npos && g_out.find("Location: http://192.168.4.1/") != std::string::npos, "redirection captive");
  // WebSocket : poignée de main + trame masquée entrante + trame sortante
  g_out.clear(); c.begin(&site, sinkOut, nullptr);
  std::string up = "GET /ws HTTP/1.1\r\nHost: 192.168.4.1\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                   "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n";
  // la première trame arrive collée à l'en-tête
  const uint8_t payload[8] = {0xB5,0x62,0xFF,0x22,0,0,0x21,0x64};
  const uint8_t mask[4] = {0x11,0x22,0x33,0x44};
  up += (char)0x82; up += (char)(0x80 | 8); up.append((const char*)mask, 4);
  for(int i=0;i<8;i++) up += (char)(payload[i] ^ mask[i & 3]);
  CHECK(c.onData((const uint8_t*)up.data(), up.size()) && c.isWebSocket(), "WebSocket ouvert");
  CHECK(g_out.find("101 Switching Protocols") != std::string::npos && g_out.find("s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") != std::string::npos, "réponse 101");
  uint8_t got[16]; size_t n = c.takeBinary(got, sizeof got);
  CHECK(n == 8 && !memcmp(got, payload, 8), "trame démasquée (%zu octets)", n);
  g_out.clear(); uint8_t big2[300]; memset(big2, 7, sizeof big2);
  c.sendBinary(big2, 300);
  CHECK(g_out.size() == 304 && (uint8_t)g_out[0] == 0x82 && (uint8_t)g_out[1] == 126 && (uint8_t)g_out[2] == 1 && (uint8_t)g_out[3] == 44, "trame sortante, longueur 16 bits");
  // fermeture demandée par le client
  const uint8_t closeF[6] = {0x88, 0x80, 1, 2, 3, 4};
  CHECK(!c.onData(closeF, 6), "fermeture");
}

// Image synthétique : en-tête ESP (0xE9, puce), marque placée à `markAt`.
static std::vector<uint8_t> fakeImage(size_t n, uint8_t magic, uint16_t chip, long markAt){
  std::vector<uint8_t> v(n);
  for(size_t i=0;i<n;i++) v[i] = (uint8_t)(i * 7 + 3);
  v[0] = magic; v[12] = (uint8_t)chip; v[13] = (uint8_t)(chip >> 8);
  if(markAt >= 0) memcpy(v.data() + markAt, ota::MARK, strlen(ota::MARK));
  return v;
}
static const char* feedAll(ota::Check& c, const std::vector<uint8_t>& v, size_t chunk){
  if(const char* e = c.begin(v.size(), 0x200000)) return e;
  for(size_t i=0;i<v.size();i+=chunk){ const size_t n = i+chunk<=v.size()?chunk:v.size()-i; if(const char* e = c.feed(v.data()+i, n)) return e; }
  return c.finish();
}

static void testOtaCheck(){
  ota::Check c;
  CHECK(!feedAll(c, fakeImage(200000, 0xE9, 9, 150000), 1024), "image valide acceptée");
  // marque à cheval sur deux morceaux, morceaux de taille irrégulière
  CHECK(!feedAll(c, fakeImage(100000, 0xE9, 9, 1024 - 5), 1024), "marque à cheval sur deux morceaux");
  CHECK(!feedAll(c, fakeImage(100000, 0xE9, 9, 50000), 1), "octet par octet");
  CHECK(feedAll(c, fakeImage(200000, 0xE9, 9, -1), 1024) != nullptr, "sans marque : refusée");
  CHECK(feedAll(c, fakeImage(200000, 0x55, 9, 1000), 1024) != nullptr, "mauvais octet magique (UF2 v1) : refusée");
  CHECK(feedAll(c, fakeImage(200000, 0xE9, 5, 1000), 1024) != nullptr, "autre puce (ESP32-C3) : refusée");
  CHECK(c.begin(16u * 1024 * 1024, 0x200000) != nullptr, "fichier « complet » de 16 Mo : refusé d'emblée");
  CHECK(c.begin(100, 0x200000) != nullptr, "fichier minuscule : refusé");
  // transfert tronqué
  auto v = fakeImage(100000, 0xE9, 9, 500);
  c.begin(v.size(), 0x200000); c.feed(v.data(), 50000);
  CHECK(c.finish() != nullptr, "transfert incomplet : refusé");
}

// Envoi HTTP d'un firmware par le serveur du firmware, avec un puits de test.
static std::vector<uint8_t> g_flash; static bool g_aborted = false; static ota::Check g_chk;
static const char* tBegin(void*, size_t n){ g_flash.clear(); g_aborted = false; return g_chk.begin(n, 0x200000); }
static const char* tWrite(void*, const uint8_t* d, size_t n){ const char* e = g_chk.feed(d, n); if(!e) g_flash.insert(g_flash.end(), d, d+n); return e; }
static const char* tEnd(void*){ return g_chk.finish(); }
static void tAbort(void*){ g_aborted = true; }

static void testUploadHttp(){
  web::OtaSink sink; sink.begin = tBegin; sink.write = tWrite; sink.end = tEnd; sink.abort = tAbort;
  web::Site site; static const uint8_t pg[1] = {0}; site.page = pg; site.pageLen = 1; site.ota = &sink;
  auto img = fakeImage(120000, 0xE9, 9, 90000);
  web::Conn c;
  for(int variant = 0; variant < 2; variant++){
    g_out.clear(); c.begin(&site, sinkOut, nullptr);
    std::string h = "POST /update HTTP/1.1\r\nHost: 192.168.4.1\r\nContent-Length: " + std::to_string(img.size()) + "\r\n";
    if(variant) h += "Expect: 100-continue\r\n";
    h += "\r\n";
    std::string first = h; first.append((const char*)img.data(), 777);    // début du corps collé à l'en-tête
    bool keep = c.onData((const uint8_t*)first.data(), first.size());
    for(size_t i = 777; keep && i < img.size(); i += 1460) keep = c.onData(img.data() + i, std::min<size_t>(1460, img.size() - i));
    CHECK(!keep && g_flash == img, "envoi HTTP %s : image reçue intacte (%zu octets)", variant ? "avec Expect: 100-continue" : "direct", g_flash.size());
    CHECK(g_out.find("200 OK") != std::string::npos && (!variant || g_out.find("100 Continue") != std::string::npos), "réponse 200%s", variant ? " après 100 Continue" : "");
  }
  // image refusée en cours de route : le reste est lu, puis message d'erreur
  auto bad = fakeImage(120000, 0x55, 9, 5000);
  g_out.clear(); c.begin(&site, sinkOut, nullptr);
  std::string h = "POST /update HTTP/1.1\r\nContent-Length: " + std::to_string(bad.size()) + "\r\n\r\n";
  bool keep = c.onData((const uint8_t*)h.data(), h.size());
  for(size_t i = 0; keep && i < bad.size(); i += 1460) keep = c.onData(bad.data() + i, std::min<size_t>(1460, bad.size() - i));
  CHECK(!keep && g_aborted && g_out.find("400") != std::string::npos && g_out.find("pas une image ESP32") != std::string::npos, "refus expliqué au navigateur");
  // coupure du client en plein envoi
  g_out.clear(); c.begin(&site, sinkOut, nullptr);
  h = "POST /update HTTP/1.1\r\nContent-Length: 120000\r\n\r\n";
  c.onData((const uint8_t*)h.data(), h.size()); c.onData(img.data(), 3000);
  c.onClosed();
  CHECK(g_aborted, "connexion coupée pendant l'envoi : mise à jour annulée");
}

int main(){
  testOtaCheck();
  testUploadHttp();
  testWeb();
  testTbStream(); testTbCommands(); testCrsf(); testUbx(); testRecords();
  DONE("protocoles");
}
