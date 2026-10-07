// ============================================================================
//  Centrale inertielle LSM6DS3 / LSM6DS3TR-C en I2C (module générique).
//  Accéléromètre ±16 g (§12.8), gyroscope ±2000 °/s, 416 Hz.
// ============================================================================
#pragma once
#include <stdint.h>
#include "../core/records.h"

namespace imu {
bool begin();                 // false : capteur absent ou non reconnu (nouvel essai dans poll)
bool ok();                    // centrale trouvée et réglée
uint8_t address();            // 0x6A / 0x6B
uint8_t whoAmI();
void poll();                  // lit les échantillons disponibles (à appeler souvent)
// Valeur pour l'époque GNSS écoulée (moyenne ou dernier échantillon selon
// IMU_AVERAGE), puis remise à zéro de l'accumulateur.
rec::Imu take();
uint32_t samples();           // échantillons lus depuis le démarrage

// Agitation pendant l'époque rendue par le dernier take() : écart-type des
// échantillons (vibrations, chocs, rotations). Une voiture posée reste à
// quelques mg ; une voiture qui roule ou qu'on porte, bien au-delà. Les
// biais du capteur n'interviennent pas (écart à la moyenne de l'époque).
struct Activity {
  bool     ok = false;        // mesure exploitable (IMU présente, ≥ 3 échantillons, capteur non figé)
  uint16_t n = 0;             // échantillons de l'époque
  int32_t  accStdMg = 0;      // accéléromètre, mg (3 axes combinés)
  int32_t  gyroStdCdps = 0;   // gyroscope, centi-°/s (3 axes combinés)
};
Activity activity();
}
