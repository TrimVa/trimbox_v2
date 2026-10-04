// ============================================================================
//  Point d'accès Wi-Fi du module : console embarquée + WebSocket (v2 §7).
//  L'activation (automatique à l'arrêt, coupure dès que la voiture roule)
//  est décidée par l'application ; ce module ne fait qu'exécuter.
// ============================================================================
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace wifiap {

void begin(const char* ssid, const char* pass);
bool start();                 // renvoie false en cas d'échec
void stop();
bool on();
void service();               // à appeler à chaque tour de boucle

bool wsConnected();
size_t read(uint8_t* buf, size_t cap);          // octets reçus de la console
void wsSend(const uint8_t* data, size_t len);   // une trame WebSocket binaire
uint8_t stations();
uint32_t pagesServed();
const char* ssid();

} // namespace wifiap
