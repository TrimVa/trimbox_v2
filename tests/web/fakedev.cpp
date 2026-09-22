// ============================================================================
//  Faux module TrimBox sur PC — banc de la console embarquée.
//
//  Sert la VRAIE console compressée (console_gz.h) avec le VRAI serveur
//  HTTP/WebSocket du firmware (core/httpws), et répond aux commandes avec le
//  vrai format de trame (core/tbproto, core/records). Un navigateur sans tête
//  (console_web_test.py) ouvre ensuite la page et vérifie tout le parcours :
//  chargement, connexion Wi-Fi automatique, données en direct, état,
//  téléchargement, analyse, reconnexion après coupure du point d'accès.
//
//  Usage : fakedev <port>     (arrêt : SIGTERM)
//          Commande « coupure » : fichier /tmp/fakedev_drop_<port> → ferme le WebSocket.
// ============================================================================
#include "../../trimbox_s3/src/core/httpws.h"
#include "../../trimbox_s3/src/core/tbproto.h"
#include "../../trimbox_s3/src/core/records.h"
#include "../../trimbox_s3/src/core/bytes.h"
#include "../../trimbox_s3/src/console_gz.h"
#include "../../trimbox_s3/src/core/otacheck.h"
#include "../../trimbox_s3/src/core/updatepage.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <vector>
#include <string>

// ---- mise à jour simulée : mêmes contrôles que le firmware (core/otacheck)
static ota::Check g_check;
static uint8_t g_recState = 0;
static bool g_otaOk = false;
static uint64_t g_rebootUntil = 0;
static std::string g_stamp = "Sep 20 2026 12:00:00";
static const char* otaBegin(void*, size_t len){
  if(g_recState) return "arrêtez d'abord l'enregistrement";
  return g_check.begin(len, 0x200000);
}
static const char* otaWrite(void*, const uint8_t* d, size_t n){ return g_check.feed(d, n); }
static const char* otaEnd(void*){ const char* e = g_check.finish(); if(!e) g_otaOk = true; return e; }
static void otaAbort(void*){}

static uint64_t ms(){ timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec*1000 + t.tv_nsec/1000000; }

struct Client { int fd; web::Conn conn; std::string out; bool closeAfter = false; tb::Parser rx{true}; };
static void writeFn(void* ctx, const uint8_t* d, size_t n){ ((Client*)ctx)->out.append((const char*)d, n); }

// Piste : stade 30 m + 2 × R 20 m, tour de 15 s
static rec::Pvt pvtAt(double t){
  const double R = 20, S = 30, per = 2*S + 2*M_PI*R, v = per / 15.0;
  double s = fmod(v*t, per), x, y, h;
  if(s < S){ x = s; y = 0; h = 90; }
  else if((s -= S) < M_PI*R){ double a = s/R; x = S + R*sin(a); y = R - R*cos(a); h = 90 - a*180/M_PI; }
  else if((s -= M_PI*R) < S){ x = S - s; y = 2*R; h = 270; }
  else { s -= S; double a = s/R; x = -R*sin(a); y = R + R*cos(a); h = 270 - a*180/M_PI; }
  if(h < 0) h += 360;
  rec::Pvt p{}; p.iTOW = 400000000u + (uint32_t)llround(t*1000);
  p.year = 2026; p.month = 9; p.day = 20; p.hour = 14; p.valid = 0x37;
  p.fixType = 3; p.flags = 1; p.numSV = 15;
  p.lat = (int32_t)llround((44.6386 + y/110540.0) * 1e7);
  p.lon = (int32_t)llround((-0.8672 + x/(111320.0*cos(44.6386*M_PI/180))) * 1e7);
  p.hMSL = 45000; p.height = 95000; p.hAcc = 450; p.vAcc = 700;
  p.gSpeed = (int32_t)llround(v*1000); p.headMot = (int32_t)llround(h*1e5); p.pDOP = 120;
  return p;
}

int main(int argc, char** argv){
  const int port = argc > 1 ? atoi(argv[1]) : 8088;
  int srv = socket(AF_INET, SOCK_STREAM, 0); int one = 1;
  setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(port); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if(bind(srv, (sockaddr*)&a, sizeof a) || listen(srv, 8)){ perror("bind"); return 1; }
  fcntl(srv, F_SETFL, O_NONBLOCK);
  web::Site site; site.page = CONSOLE_GZ; site.pageLen = CONSOLE_GZ_LEN;
  web::OtaSink sink; sink.begin = otaBegin; sink.write = otaWrite; sink.end = otaEnd; sink.abort = otaAbort;
  site.ota = &sink; site.updatePage = UPDATE_PAGE;
  printf("fakedev : console v%s sur http://127.0.0.1:%d/\n", CONSOLE_EMBED_VER, port); fflush(stdout);

  std::vector<Client*> cl;
  const int N = 3 * 15 * 25;                      // 3 tours enregistrés
  uint8_t& recState = g_recState; uint64_t lastLive = 0; double tLive = 0;
  struct { bool on; int idx; } dl = {false, 0};

  for(;;){
    // Coupure simulée du point d'accès : la voiture roule.
    char dropPath[64]; snprintf(dropPath, sizeof dropPath, "/tmp/fakedev_drop_%d", port);
    if(access(dropPath, F_OK) == 0){
      unlink(dropPath);
      for(auto* c : cl) if(c->conn.isWebSocket()){ close(c->fd); c->fd = -1; }
      printf("fakedev : coupure du WebSocket\n"); fflush(stdout);
    }
    // « Redémarrage » après une mise à jour acceptée : toutes les connexions
    // tombent, le module revient 2 s plus tard avec une nouvelle empreinte.
    if(g_otaOk){
      bool pending = false;
      for(auto* c : cl) if(!c->out.empty()) pending = true;
      if(!pending){
        g_otaOk = false; g_rebootUntil = ms() + 2000; g_stamp = "Sep 21 2026 09:30:00";
        for(auto* c : cl){ close(c->fd); c->fd = -1; }
        printf("fakedev : mise à jour acceptée, redémarrage\n"); fflush(stdout);
      }
    }
    int fd = ms() < g_rebootUntil ? -1 : accept(srv, nullptr, nullptr);
    if(fd >= 0){ fcntl(fd, F_SETFL, O_NONBLOCK); auto* c = new Client; c->fd = fd; c->conn.begin(&site, writeFn, c); cl.push_back(c); }

    for(auto* c : cl){
      if(c->fd < 0) continue;
      uint8_t buf[4096]; ssize_t n = recv(c->fd, buf, sizeof buf, 0);
      if(n == 0){ close(c->fd); c->fd = -1; continue; }
      if(n > 0 && !c->conn.onData(buf, (size_t)n)) c->closeAfter = true;
      if(c->conn.isWebSocket()){
        uint8_t p[1024]; size_t k;
        while((k = c->conn.takeBinary(p, sizeof p)) > 0) c->rx.push(p, k);
        tb::Frame f;
        auto reply = [&](uint8_t id, const uint8_t* pl, uint16_t len){
          uint8_t fr[300]; size_t m = tb::build(fr, 0xFF, id, pl, len); c->conn.sendBinary(fr, m); };
        while(c->rx.next(f)){
          if(f.id == tb::ID_BUILD){ std::string t = "TrimBox DIY S3|2.0-a2|" + g_stamp + "|16|Banc PC"; reply(tb::ID_BUILD, (const uint8_t*)t.data(), (uint16_t)t.size()); }
          else if(f.id == tb::ID_STATUS){ uint8_t s[12] = {recState ? (uint8_t)1 : (uint8_t)0, 1, 0, 0}; put_le32(s+4, N + 2); put_le32(s+8, 154333); reply(tb::ID_STATUS, s, 12); }
          else if(f.id == tb::ID_CONFIG && f.len == 0){ uint8_t s[12] = {recState, 0, 0x1F, 0}; put_le16(s+4, 1389); put_le16(s+6, 30); put_le16(s+8, 30); put_le16(s+10, 300); reply(tb::ID_CONFIG, s, 12); }
          else if(f.id == tb::ID_CONFIG){ recState = f.payload[0] ? 1 : 0; uint8_t s[12] = {recState}; reply(tb::ID_STATE, s, 12); uint8_t ak[2] = {0xFF, tb::ID_CONFIG}; reply(tb::ID_ACK, ak, 2); }
          else if(f.id == tb::ID_DOWNLOAD){ uint8_t s[4]; put_le32(s, N + 2); reply(tb::ID_DOWNLOAD, s, 4); dl.on = true; dl.idx = 0; }
          else { uint8_t nk[2] = {0xFF, f.id}; reply(tb::ID_NACK, nk, 2); }
        }
        // Téléchargement : un état, les points, un état, puis ACK.
        if(dl.on){
          for(int k = 0; k < 50 && dl.idx <= N + 1; k++, dl.idx++){
            uint8_t d[80];
            if(dl.idx == 0 || dl.idx == N + 1){ uint8_t s[12] = {(uint8_t)(dl.idx ? 0 : 1)}; reply(tb::ID_STATE, s, 12); continue; }
            rec::Imu m{(int16_t)(300*sin(dl.idx/10.0)), (int16_t)(900*cos(dl.idx/25.0)), 1000, 0, 0, 1500};
            rec::buildData(d, pvtAt((dl.idx-1)*0.04), m, 80, false);
            reply(tb::ID_HIST, d, 80);
          }
          if(dl.idx > N + 1){ uint8_t ak[2] = {0xFF, tb::ID_DOWNLOAD}; reply(tb::ID_ACK, ak, 2); dl.on = false; }
        }else if(ms() - lastLive >= 40){          // direct à 25 Hz
          lastLive = ms(); tLive += 0.04;
          uint8_t d[80]; rec::Imu m{0, 0, 1000, 0, 0, 0};
          rec::buildData(d, pvtAt(tLive), m, 80, false);
          reply(tb::ID_LIVE, d, 80);
        }
      }
      if(!c->out.empty()){
        ssize_t w = send(c->fd, c->out.data(), c->out.size(), MSG_NOSIGNAL);
        if(w > 0) c->out.erase(0, (size_t)w);
      }
      if(c->closeAfter && c->out.empty()){ close(c->fd); c->fd = -1; }
    }
    for(size_t i = 0; i < cl.size();) if(cl[i]->fd < 0){ delete cl[i]; cl.erase(cl.begin() + i); } else i++;
    usleep(2000);
  }
}
