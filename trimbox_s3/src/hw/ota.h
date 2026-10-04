// ============================================================================
//  Mise à jour du firmware par Wi-Fi (v2 §7.2).
//
//  Réception au fil de l'eau dans la partition OTA inactive, contrôles
//  (core/otacheck + SHA-256 de l'image par esp_ota_end), bascule, redémarrage.
//  Le nouveau firmware démarre « en attente de validation » : il ne se
//  confirme qu'après un autotest (mémoire lisible) et 15 s de fonctionnement.
//  S'il plante ou redémarre avant, le chargeur de démarrage revient TOUT
//  SEUL à la version précédente.
// ============================================================================
#pragma once
#include <stdint.h>
#include "../core/httpws.h"

namespace ota {

// Autorisation demandée à l'application : nullptr si la mise à jour est
// possible, sinon la raison du refus.
typedef const char* (*Gate)();
void begin(Gate gate);
const web::OtaSink* sink();

bool inProgress();
bool rebootPending();
uint32_t progressPercent();

// Validation du firmware courant s'il vient d'être installé.
// À appeler dans la boucle ; `selfTestOk` : mémoire et configuration lues.
void serviceValidation(bool selfTestOk);
bool pendingValidation();
const char* runningPartition();

} // namespace ota
