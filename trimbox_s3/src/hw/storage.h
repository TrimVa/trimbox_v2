// ============================================================================
//  Stockage en flash interne : journal linéaire + configuration A/B.
//  v1 §4.3-§4.5, v2 §2.4 et §3.3.
// ============================================================================
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "../core/persist.h"

namespace storage {

bool begin();                       // false : partitions absentes (mauvaise table)
uint32_t capacitySlots();
uint32_t usedSlots();
uint8_t  fillPercent();

// Ajoute un emplacement [type][80 octets]. false si la mémoire est pleine.
bool append(uint8_t type, const uint8_t payload[80]);
// Lit l'emplacement n° `index` (0 … usedSlots()-1).
bool readSlot(uint32_t index, uint8_t& type, uint8_t payload[80]);

// Effacement progressif, du DERNIER secteur utilisé vers le premier :
// interrompu par une coupure, le journal reste contigu depuis l'adresse 0
// (la recherche de fin par dichotomie reste valable). Un appel = un secteur.
void eraseBegin();
bool eraseStep(uint8_t& percent);   // true quand c'est terminé
bool erasing();

// Configuration en double exemplaire.
bool loadRecConfig(persist::RecConfig& c);    // false : défauts renvoyés
bool saveRecConfig(persist::RecConfig& c);    // incrémente seq, alterne A/B
bool loadLineConfig(persist::LineConfig& c);
bool saveLineConfig(persist::LineConfig& c);

const char* lastError();

} // namespace storage
