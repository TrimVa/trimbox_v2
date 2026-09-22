#include "httpws.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>

namespace web {

// ---------------------------------------------------------------- SHA-1
static inline uint32_t rol(uint32_t v, int s){ return (v << s) | (v >> (32 - s)); }

void sha1(const uint8_t* d, size_t n, uint8_t out[20]){
  uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
  const uint64_t bits = (uint64_t)n * 8;
  size_t total = ((n + 8) / 64 + 1) * 64;
  for(size_t off = 0; off < total; off += 64){
    uint8_t blk[64];
    for(int i = 0; i < 64; i++){
      const size_t k = off + i;
      if(k < n) blk[i] = d[k];
      else if(k == n) blk[i] = 0x80;
      else if(k >= total - 8) blk[i] = (uint8_t)(bits >> (8 * (total - 1 - k)));
      else blk[i] = 0;
    }
    uint32_t w[80];
    for(int i = 0; i < 16; i++) w[i] = (uint32_t)blk[4*i] << 24 | (uint32_t)blk[4*i+1] << 16 | (uint32_t)blk[4*i+2] << 8 | blk[4*i+3];
    for(int i = 16; i < 80; i++) w[i] = rol(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);
    uint32_t a = h[0], b = h[1], c = h[2], e4 = h[3], e = h[4];
    for(int i = 0; i < 80; i++){
      uint32_t f, k;
      if(i < 20){ f = (b & c) | (~b & e4); k = 0x5A827999; }
      else if(i < 40){ f = b ^ c ^ e4; k = 0x6ED9EBA1; }
      else if(i < 60){ f = (b & c) | (b & e4) | (c & e4); k = 0x8F1BBCDC; }
      else { f = b ^ c ^ e4; k = 0xCA62C1D6; }
      const uint32_t t = rol(a, 5) + f + e + k + w[i];
      e = e4; e4 = c; c = rol(b, 30); b = a; a = t;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += e4; h[4] += e;
  }
  for(int i = 0; i < 5; i++){ out[4*i] = h[i] >> 24; out[4*i+1] = h[i] >> 16; out[4*i+2] = h[i] >> 8; out[4*i+3] = h[i]; }
}

size_t base64(const uint8_t* d, size_t n, char* o){
  static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t j = 0;
  for(size_t i = 0; i < n; i += 3){
    const uint32_t v = (uint32_t)d[i] << 16 | (i+1 < n ? (uint32_t)d[i+1] << 8 : 0) | (i+2 < n ? d[i+2] : 0);
    o[j++] = T[(v >> 18) & 63]; o[j++] = T[(v >> 12) & 63];
    o[j++] = i+1 < n ? T[(v >> 6) & 63] : '=';
    o[j++] = i+2 < n ? T[v & 63] : '=';
  }
  o[j] = 0;
  return j;
}

void acceptKey(const char* key, char out[29]){
  char buf[128];
  snprintf(buf, sizeof buf, "%s258EAFA5-E914-47DA-95CA-C5AB0DC85B11", key);
  uint8_t h[20]; sha1((const uint8_t*)buf, strlen(buf), h);
  base64(h, 20, out);
}

// ---------------------------------------------------------------- HTTP
void Conn::begin(const Site* s, WriteFn w, void* ctx){
  site_ = s; w_ = w; ctx_ = ctx;
  st_ = ReadingRequest; reqLen_ = inLen_ = rxLen_ = 0;
}

void Conn::outStr(const char* s){ out(s, strlen(s)); }

void Conn::respond(int code, const char* status, const char* text){
  char hdr[200];
  snprintf(hdr, sizeof hdr, "HTTP/1.1 %d %s\r\nContent-Type: text/plain; charset=utf-8\r\n"
                            "Content-Length: %u\r\nCache-Control: no-cache\r\nConnection: close\r\n\r\n",
           code, status, (unsigned)strlen(text));
  outStr(hdr); outStr(text);
}

void Conn::onClosed(){
  if(st_ == Upload && upBegun_ && site_->ota && site_->ota->abort) site_->ota->abort(site_->ota->ctx);
  upBegun_ = false;
  st_ = Done;
}

// Corps d'une mise à jour : transmis au puits au fil de l'eau. En cas de
// refus, le reste est lu et jeté, pour que le navigateur reçoive bien le
// message d'erreur (une connexion coupée en plein envoi n'afficherait
// qu'un « échec réseau » sans explication).
bool Conn::uploadData(const uint8_t* d, size_t n){
  const size_t take = n < bodyLeft_ ? n : bodyLeft_;
  if(!upErr_ && take){
    upErr_ = site_->ota->write(site_->ota->ctx, d, take);
    if(upErr_){ site_->ota->abort(site_->ota->ctx); upBegun_ = false; }
  }
  bodyLeft_ -= take;
  if(bodyLeft_) return true;
  if(!upErr_){
    upErr_ = site_->ota->end(site_->ota->ctx);
    upBegun_ = false;
  }
  if(upErr_) respond(400, "Bad Request", upErr_);
  else { respond(200, "OK", "OK : firmware accepté, redémarrage…"); updates++; }
  st_ = Done;
  return false;
}

// Valeur d'un en-tête (insensible à la casse), copiée dans `v`.
static bool header(const char* req, const char* name, char* v, size_t cap){
  const size_t nl = strlen(name);
  for(const char* p = strstr(req, "\r\n"); p && p[2]; p = strstr(p + 2, "\r\n")){
    const char* line = p + 2;
    size_t k = 0;
    while(k < nl && line[k] && tolower((unsigned char)line[k]) == tolower((unsigned char)name[k])) k++;
    if(k == nl && line[k] == ':'){
      const char* s = line + k + 1;
      while(*s == ' ') s++;
      size_t j = 0;
      while(s[j] && s[j] != '\r' && j + 1 < cap){ v[j] = s[j]; j++; }
      v[j] = 0;
      return true;
    }
  }
  return false;
}

static bool containsNoCase(const char* hay, const char* needle){
  const size_t n = strlen(needle);
  for(; *hay; hay++){
    size_t k = 0;
    while(k < n && hay[k] && tolower((unsigned char)hay[k]) == tolower((unsigned char)needle[k])) k++;
    if(k == n) return true;
  }
  return false;
}

bool Conn::handleRequest(){
  char method[8] = {0}, path[128] = {0};
  if(sscanf(req_, "%7s %127s", method, path) != 2){ outStr("HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 0\r\n\r\n"); return false; }
  char host[64] = "", upgrade[32] = "", key[64] = "";
  header(req_, "Host", host, sizeof host);
  header(req_, "Upgrade", upgrade, sizeof upgrade);
  header(req_, "Sec-WebSocket-Key", key, sizeof key);
  char* q = strchr(path, '?'); if(q) *q = 0;              // la requête ne compte pas

  // --- WebSocket ---
  if(!strcmp(path, "/ws") && containsNoCase(upgrade, "websocket") && key[0]){
    char acc[29]; acceptKey(key, acc);
    char resp[200];
    snprintf(resp, sizeof resp,
      "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
      "Sec-WebSocket-Accept: %s\r\n\r\n", acc);
    outStr(resp);
    st_ = WebSocket;
    return true;
  }

  // --- console ---
  // Hôte étranger (portail captif : le téléphone teste une adresse Internet)
  // → redirection, pour qu'il propose d'ouvrir la page tout seul.
  const bool ourHost = !host[0] || !strncmp(host, site_->hostName, strlen(site_->hostName)) ||
                       strstr(host, "localhost") || strstr(host, "127.0.0.1") || strstr(host, "trimbox");
  if(!strcmp(method, "GET") && ourHost && (!strcmp(path, "/") || !strcmp(path, "/index.html"))){
    char hdr[256];
    snprintf(hdr, sizeof hdr,
      "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Encoding: gzip\r\n"
      "Content-Length: %u\r\nCache-Control: no-cache\r\nConnection: close\r\n\r\n", (unsigned)site_->pageLen);
    outStr(hdr);
    if(!strcmp(method, "GET")) out(site_->page, site_->pageLen);
    pages++;
    return false;                     // fermer après envoi
  }
  // --- mise à jour du firmware ---
  if(!strcmp(path, "/update") && !strcmp(method, "GET") && site_->updatePage){
    char hdr[200];
    snprintf(hdr, sizeof hdr, "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n"
                              "Content-Length: %u\r\nCache-Control: no-cache\r\nConnection: close\r\n\r\n",
             (unsigned)strlen(site_->updatePage));
    outStr(hdr); outStr(site_->updatePage);
    return false;
  }
  if(!strcmp(path, "/update") && !strcmp(method, "POST")){
    char cl[24] = "", expect[32] = "";
    header(req_, "Content-Length", cl, sizeof cl);
    header(req_, "Expect", expect, sizeof expect);
    const long len = atol(cl);
    if(!site_->ota){ respond(404, "Not Found", "mise à jour indisponible"); return false; }
    if(len <= 0){ respond(411, "Length Required", "taille du fichier inconnue"); return false; }
    bodyLeft_ = (size_t)len;
    upErr_ = site_->ota->begin(site_->ota->ctx, (size_t)len);
    upBegun_ = !upErr_;
    if(containsNoCase(expect, "100-continue")){
      if(upErr_){ respond(417, "Expectation Failed", upErr_); return false; }
      outStr("HTTP/1.1 100 Continue\r\n\r\n");
    }
    st_ = Upload;
    return true;
  }
  if(!strcmp(path, "/favicon.ico")){
    outStr("HTTP/1.1 404 Not Found\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
    return false;
  }
  char resp[256];
  snprintf(resp, sizeof resp, "HTTP/1.1 302 Found\r\nLocation: %s\r\nCache-Control: no-cache\r\n"
                              "Connection: close\r\nContent-Length: 0\r\n\r\n", site_->home);
  outStr(resp);
  redirects++;
  return false;
}

bool Conn::onData(const uint8_t* d, size_t n){
  if(st_ == ReadingRequest){
    // Accumule l'en-tête jusqu'à la ligne vide.
    const size_t room = sizeof(req_) - 1 - reqLen_;
    const size_t take = n < room ? n : room;
    memcpy(req_ + reqLen_, d, take); reqLen_ += take; req_[reqLen_] = 0;
    char* end = strstr(req_, "\r\n\r\n");
    if(!end){
      if(reqLen_ >= sizeof(req_) - 1){ outStr("HTTP/1.1 431 Request Header Fields Too Large\r\nConnection: close\r\nContent-Length: 0\r\n\r\n"); st_ = Done; return false; }
      return true;
    }
    const size_t used = (size_t)(end + 4 - req_);
    const size_t already = reqLen_ - take;            // octets présents avant cet appel
    const bool keep = handleRequest();
    if(!keep){ st_ = Done; return false; }
    if(st_ == ReadingRequest) st_ = Done;   // ne devrait pas arriver
    // Octets reçus après l'en-tête (début de trame WebSocket) : à traiter.
    const size_t consumed = used > already ? used - already : 0;
    if(consumed < n) return onData(d + consumed, n - consumed);
    return true;
  }
  if(st_ == Upload) return uploadData(d, n);
  if(st_ == WebSocket){
    const size_t room = sizeof(in_) - inLen_;
    if(n > room){ sendClose(); st_ = Done; return false; }   // client trop bavard
    memcpy(in_ + inLen_, d, n); inLen_ += n;
    return wsParse();
  }
  return false;
}

// Trames client → serveur : toujours masquées (RFC 6455 §5.3).
bool Conn::wsParse(){
  while(inLen_ >= 2){
    const uint8_t op = in_[0] & 0x0F;
    const bool masked = in_[1] & 0x80;
    uint64_t len = in_[1] & 0x7F;
    size_t hdr = 2;
    if(len == 126){ if(inLen_ < 4) return true; len = (uint64_t)in_[2] << 8 | in_[3]; hdr = 4; }
    else if(len == 127){ sendClose(); st_ = Done; return false; }      // jamais utile ici
    if(!masked){ sendClose(); st_ = Done; return false; }
    if(len > sizeof(in_) - 8){ sendClose(); st_ = Done; return false; }
    if(inLen_ < hdr + 4 + len) return true;                           // incomplète
    const uint8_t* mask = in_ + hdr;
    uint8_t* p = in_ + hdr + 4;
    for(size_t i = 0; i < len; i++) p[i] ^= mask[i & 3];
    framesIn++;
    switch(op){
      case 0x0: case 0x1: case 0x2:              // données (texte accepté aussi)
        if(rxLen_ + len <= sizeof(rx_)){ memcpy(rx_ + rxLen_, p, len); rxLen_ += len; }
        break;
      case 0x8:                                   // fermeture
        sendClose(); st_ = Done; return false;
      case 0x9: {                                 // ping → pong
        uint8_t h[2] = {0x8A, (uint8_t)(len < 126 ? len : 0)};
        if(len < 126){ out(h, 2); out(p, len); }
        break;
      }
      default: break;                             // pong : ignoré
    }
    const size_t used = hdr + 4 + (size_t)len;
    memmove(in_, in_ + used, inLen_ - used); inLen_ -= used;
  }
  return true;
}

size_t Conn::takeBinary(uint8_t* o, size_t cap){
  const size_t n = rxLen_ < cap ? rxLen_ : cap;
  memcpy(o, rx_, n);
  memmove(rx_, rx_ + n, rxLen_ - n); rxLen_ -= n;
  return n;
}

void Conn::sendBinary(const uint8_t* d, size_t n){
  if(st_ != WebSocket) return;
  uint8_t h[4]; size_t hl;
  h[0] = 0x82;                                    // FIN + binaire
  if(n < 126){ h[1] = (uint8_t)n; hl = 2; }
  else { h[1] = 126; h[2] = (uint8_t)(n >> 8); h[3] = (uint8_t)n; hl = 4; }
  out(h, hl); out(d, n);
  framesOut++;
}

void Conn::sendClose(){
  const uint8_t c[2] = {0x88, 0x00};
  out(c, 2);
}

} // namespace web
