// ============================================================================
//  Récepteur GNSS u-blox M10 (HGLRC M100 Mini) sur UART1.  v1 §4.8.
// ============================================================================
#pragma once
#include <stdint.h>
#include "../core/records.h"

namespace gnss {

struct Stats {
  uint32_t baudFound;       // vitesse détectée au démarrage (0 = aucune)
  uint16_t acks, naks;      // réponses aux VALSET
  float    rateHz;          // cadence NAV-PVT mesurée
  uint32_t pvtCount, badChecksums;
  bool     galileo;         // Galileo actif (désactivé si 25 Hz non tenus)
};

void powerOn();
void powerOff();
bool begin(uint8_t dataRate, uint8_t dynModel);   // détection + configuration
void setRate(uint8_t dataRate);
void setDynModel(uint8_t model);
// À appeler en boucle ; renvoie true quand un nouveau NAV-PVT est décodé.
bool poll(rec::Pvt& out);
const Stats& stats();

} // namespace gnss
