// ============================================================================
//  Serveur HTTP + WebSocket minimal, SANS dépendance (v2 §6.1, §7.3).
//
//  Une connexion TCP = un objet Conn. La couche matérielle (hw/wifiap) lui
//  passe les octets reçus et lui fournit une fonction d'écriture ; tout le
//  reste (analyse de la requête, poignée de main WebSocket, trames) est ici,
//  et donc testé sur PC avec de vraies sockets et un vrai navigateur
//  (tests/web).
//
//  Routes :
//    GET /  ou /index.html      → console, compressée gzip
//    GET /ws  (Upgrade)          → WebSocket : trames TrimBox B5 62 en binaire,
//                                  exactement le flux du Bluetooth
//    GET /update                 → page de secours de mise à jour du firmware
//    POST /update (corps = .bin) → mise à jour OTA, transmise au fil de l'eau
//                                  au « puits » OtaSink (hw/ota)
//    tout le reste               → 302 vers la console (portail captif : le
//                                  téléphone propose d'ouvrir la page tout seul)
// ============================================================================
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace web {

// Destinataire d'une mise à jour. Chaque fonction renvoie nullptr si tout va
// bien, sinon un message d'erreur en français, renvoyé tel quel à la page.
struct OtaSink {
  const char* (*begin)(void* ctx, size_t len) = nullptr;
  const char* (*write)(void* ctx, const uint8_t* d, size_t n) = nullptr;
  const char* (*end)(void* ctx) = nullptr;
  void (*abort)(void* ctx) = nullptr;
  void* ctx = nullptr;
};

struct Site {
  const OtaSink* ota = nullptr;
  const char* updatePage = nullptr;           // HTML de secours (non compressé)
  const uint8_t* page = nullptr;   // index.html compressé (gzip)
  size_t pageLen = 0;
  const char* home = "http://192.168.4.1/";   // cible des redirections
  const char* hostName = "192.168.4.1";       // hôte « légitime »
};

typedef void (*WriteFn)(void* ctx, const uint8_t* data, size_t len);

class Conn {
public:
  enum State : uint8_t { Idle, ReadingRequest, WebSocket, Upload, Done };

  void begin(const Site* site, WriteFn w, void* ctx);
  // Octets reçus du client. Renvoie false quand la connexion doit être fermée.
  bool onData(const uint8_t* d, size_t n);
  State state() const { return st_; }
  bool isWebSocket() const { return st_ == WebSocket; }

  // WebSocket : charges binaires reçues, à récupérer par l'application.
  size_t takeBinary(uint8_t* out, size_t cap);
  // WebSocket : envoie une trame binaire (non masquée, sens serveur → client).
  void sendBinary(const uint8_t* d, size_t n);
  void sendClose();

  // Connexion coupée par le client : une mise à jour en cours est annulée.
  void onClosed();

  // Statistiques pour le banc d'essai
  uint32_t framesIn = 0, framesOut = 0, pages = 0, redirects = 0, updates = 0;

private:
  bool handleRequest();
  bool wsParse();
  bool uploadData(const uint8_t* d, size_t n);
  void respond(int code, const char* status, const char* text);
  void out(const void* d, size_t n){ w_(ctx_, (const uint8_t*)d, n); }
  void outStr(const char* s);

  const Site* site_ = nullptr;
  WriteFn w_ = nullptr; void* ctx_ = nullptr;
  State st_ = Idle;
  char req_[1536]; size_t reqLen_ = 0;
  uint8_t in_[4200]; size_t inLen_ = 0;      // trames WebSocket entrantes
  uint8_t rx_[4096]; size_t rxLen_ = 0;      // charges binaires à consommer
  size_t bodyLeft_ = 0; const char* upErr_ = nullptr; bool upBegun_ = false;
};

// Clé d'acceptation WebSocket : base64(SHA1(clé + GUID)). `out` : 29 octets.
void acceptKey(const char* clientKey, char out[29]);
// Outils exposés pour les tests
void sha1(const uint8_t* d, size_t n, uint8_t out[20]);
size_t base64(const uint8_t* d, size_t n, char* out);   // renvoie la longueur

} // namespace web
