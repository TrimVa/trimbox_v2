// ============================================================================
//  TrimBox DIY S3 — enregistreur GPS/inertiel pour voitures RC
//  ESP32-S3 (N16R8), GNSS u-blox M10, IMU LSM6DS3, télémétrie CRSF.
//
//  Spécification : CAHIER-DES-CHARGES.md (v1) + CAHIER-DES-CHARGES-V2.md.
//  Tout le code est dans src/ : ce fichier ne fait que l'appeler.
//  La logique sans matériel (src/core) est testée sur PC : tests/native.
// ============================================================================
#include "src/app.h"

void setup(){ app::setup(); }
void loop(){ app::loop(); }
