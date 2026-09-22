// ============================================================================
//  Centrale inertielle LSM6DS3 / LSM6DS3TR-C en I2C (module générique).
//  Accéléromètre ±16 g (v1 §9.11), gyroscope ±2000 °/s, 416 Hz.
// ============================================================================
#pragma once
#include <stdint.h>
#include "../core/records.h"

namespace imu {
bool begin();                 // false : capteur absent ou non reconnu
uint8_t address();            // 0x6A / 0x6B
uint8_t whoAmI();
void poll();                  // lit les échantillons disponibles (à appeler souvent)
// Valeur pour l'époque GNSS écoulée (moyenne ou dernier échantillon selon
// IMU_AVERAGE), puis remise à zéro de l'accumulateur.
rec::Imu take();
uint32_t samples();           // échantillons lus depuis le démarrage
}
