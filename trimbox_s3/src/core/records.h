// ============================================================================
//  Enregistrements de 80 octets (v1 §3.5) et emplacements mémoire (v1 §4.3,
//  v2 §5.4 / §6.2).
// ============================================================================
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace rec {

constexpr size_t PAYLOAD   = 80;
constexpr size_t SLOT_SIZE = 81;          // [type][80 octets]

enum SlotType : uint8_t {
  SLOT_DATA  = 0x21,   // point GNSS + IMU
  SLOT_STATE = 0x26,   // changement d'état (12 premiers octets utiles)
  SLOT_ESC   = 0x28,   // lot de 5 échantillons ESC (v2)
  SLOT_LAP   = 0x29,   // franchissement de ligne (v2)
  SLOT_FREE  = 0xFF,
};

// Solution de navigation décodée (UBX NAV-PVT, 92 octets), champs utiles.
struct Pvt {
  uint32_t iTOW;               // ms
  uint16_t year; uint8_t month, day, hour, min, sec, valid;
  uint32_t tAcc; int32_t nano;
  uint8_t  fixType, flags, flags2, numSV;
  int32_t  lon, lat;           // deg × 1e7
  int32_t  height, hMSL;       // mm
  uint32_t hAcc, vAcc;         // mm
  int32_t  velN, velE, velD;   // mm/s
  int32_t  gSpeed;             // mm/s
  int32_t  headMot;            // deg × 1e5
  uint32_t sAcc, headAcc;
  uint16_t pDOP;
  uint8_t  flags3;
  bool gnssFixOK() const { return (flags & 0x01) != 0; }
};

// Décode une charge NAV-PVT (au moins 84 octets). false si trop courte.
bool decodeNavPvt(const uint8_t* p, size_t len, Pvt& out);

struct Imu {                    // valeurs déjà mises à l'échelle du protocole
  int16_t ax, ay, az;           // milli-g
  int16_t gx, gy, gz;           // centi-deg/s
};

// Construit les 80 octets du message de données (FF 01 / FF 21).
// `battery` : bits 0-6 = %, bit 7 = en charge.
// `speed3d` : remplace la vitesse sol par la vitesse 3D (option FF 27).
void buildData(uint8_t out[PAYLOAD], const Pvt& p, const Imu& imu, uint8_t battery, bool speed3d);

// Contrôles de v1 §4.5.4 : un enregistrement interrompu par une coupure
// contient des octets restés à 0xFF, qui produiraient des valeurs absurdes.
bool validData(const uint8_t payload[PAYLOAD]);

// Changement d'état : 12 octets utiles.
//  0 u8  état (0 arrêté, 1 en cours, 2 en pause)
//  1 u8  motif (voir StateReason)
//  2 u16 réservé
//  4 u32 iTOW au moment du changement
//  8 u32 nombre de points de données écrits jusque-là
enum StateReason : uint8_t {
  REASON_COMMAND = 0, REASON_STATIONARY = 1, REASON_NOFIX = 2,
  REASON_MOVING = 3, REASON_FIX = 4, REASON_AUTOOFF = 5, REASON_SERIAL = 6,
};
void buildState(uint8_t out[12], uint8_t state, uint8_t reason, uint32_t iTOW, uint32_t count);

// Distance entre deux positions (deg × 1e7) en mètres.
// Projection équirectangulaire — IDENTIQUE à distM() de la console :
// écarts en DEGRÉS × mètres PAR DEGRÉ, cos de la latitude moyenne.
double distM(double lat1, double lon1, double lat2, double lon2);

} // namespace rec
