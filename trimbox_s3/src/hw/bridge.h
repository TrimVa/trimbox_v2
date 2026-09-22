// ============================================================================
//  Liaison avec la console : Bluetooth (service Nordic UART) ou WebSocket
//  (point d'accès Wi-Fi, prioritaire quand il est connecté), et file
//  d'émission UNIQUE (v1 §4.2).
//
//  TOUTES les trames sortantes passent par la file, données en direct
//  comprises : deux chemins de sortie concurrents permettraient à un paquet
//  de s'insérer au milieu d'une réponse fragmentée (v1 §9.5).
// ============================================================================
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace bridge {

void begin(const char* name, const char* serial);
bool connected();            // Bluetooth OU WebSocket
bool bleConnected();
uint16_t mtu();

// --- file d'émission ---
size_t freeSpace();
// Empile une trame TrimBox complète. Refusée (false) si la place manque :
// l'appelant décide si la trame est sacrifiable (données en direct) ou non.
bool send(uint8_t cls, uint8_t id, const uint8_t* payload, uint16_t len);
// Données en direct : empilées seulement s'il reste LIVE_RESERVE octets.
bool sendLive(uint8_t cls, uint8_t id, const uint8_t* payload, uint16_t len);
void service();              // vidange : jusqu'à 12 notifications par appel
void clear();                // à la déconnexion

// --- réception ---
size_t read(uint8_t* buf, size_t cap);   // octets reçus de la console

} // namespace bridge
