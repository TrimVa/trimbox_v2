// ============================================================================
//  Liaison radio CRSF avec le récepteur ExpressLRS (RadioMaster ER5C-i).
//  v2 §4 : télémétrie GPS + messages texte vers la MT12, lecture de la voie
//  de pose de ligne.
// ============================================================================
#pragma once
#include <stdint.h>
#include "../core/records.h"
#include "../core/linecmd.h"
#include "../core/crsf.h"

namespace radio {

struct Stats {
  uint32_t rcFrames, badCrc, txFrames;
  float rcRateHz;
  bool linkUp;                  // voies reçues récemment
  crsf::LinkStats link;
  bool haveLink;
};

void begin(uint8_t lineChannel);
void setLineChannel(uint8_t ch);
// À appeler en boucle. Renvoie une commande de pose de ligne éventuelle.
linecmd::Output poll();
// Dernière solution GNSS à relayer (5 Hz).
void setGps(const rec::Pvt& p);
// Message texte prioritaire (chrono, accusé) : mis en file, tenu seul
// CRSF_EVENT_HOLD_MS, puis le suivant (ExpressLRS ne garde que le dernier).
void event(const char* text);
// Texte d'état, rappelé à 1 Hz quand aucun événement n'est en attente.
void status(const char* text);
const Stats& stats();

} // namespace radio
