// ============================================================================
//  CRSF (Crossfire / ExpressLRS) — le module se présente au récepteur comme
//  un contrôleur de vol. Cahier des charges v2 §4.
//
//  Trame : [adresse] [longueur] [type] [charge…] [CRC8 D5]
//          longueur = 1 (type) + taille(charge) + 1 (CRC)
//  ATTENTION : entiers en BIG-endian (v2 §10.4).
// ============================================================================
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace crsf {

constexpr uint8_t ADDR_FC       = 0xC8;   // contrôleur de vol (nous)
constexpr uint8_t ADDR_RADIO    = 0xEA;
constexpr uint8_t ADDR_RX       = 0xEC;
constexpr uint8_t ADDR_TXMODULE = 0xEE;

constexpr uint8_t T_GPS         = 0x02;
constexpr uint8_t T_BATTERY     = 0x08;
constexpr uint8_t T_LINK_STATS  = 0x14;
constexpr uint8_t T_RC_CHANNELS = 0x16;
constexpr uint8_t T_FLIGHT_MODE = 0x21;

constexpr size_t MAX_FRAME = 64;
constexpr uint32_t BAUD = 420000;

// Valeurs brutes des voies : 172 (−100 %) … 992 (0) … 1811 (+100 %).
constexpr int CH_MIN = 172, CH_MID = 992, CH_MAX = 1811;
inline int channelPercent(uint16_t raw){ return ((int)raw - CH_MID) * 100 / (CH_MAX - CH_MID); }

struct LinkStats {
  uint8_t rssi1, rssi2, lq; int8_t snr;
  uint8_t antenna, rfMode, txPower, dlRssi, dlLq; int8_t dlSnr;
};

// --- Émission ---
// Chaque fonction écrit une trame complète (adresse FC) et renvoie sa taille.
size_t buildGps(uint8_t* out, int32_t lat_e7, int32_t lon_e7, uint32_t gSpeed_mms,
                int32_t headMot_e5, int32_t hMSL_mm, uint8_t sats);
size_t buildBattery(uint8_t* out, uint16_t volt_dV, uint16_t curr_dA, uint32_t mAh, uint8_t pct);
size_t buildFlightMode(uint8_t* out, const char* text);   // ≤ 15 caractères utiles
size_t buildFrame(uint8_t* out, uint8_t type, const uint8_t* payload, uint8_t len);

// --- Réception ---
class Parser {
public:
  bool feed(uint8_t b);                 // true : trame valide disponible
  uint8_t type() const { return frame_[2]; }
  uint8_t payloadLen() const { return (uint8_t)(frame_[1] - 2); }
  const uint8_t* payload() const { return frame_ + 3; }
  uint32_t badCrc() const { return bad_; }
private:
  uint8_t frame_[MAX_FRAME];
  uint8_t pos_ = 0, need_ = 0;
  uint32_t bad_ = 0;
};

// Décode les 16 voies (11 bits chacune, octets de poids faible d'abord).
bool decodeChannels(const uint8_t* payload, uint8_t len, uint16_t ch[16]);
bool decodeLinkStats(const uint8_t* payload, uint8_t len, LinkStats& ls);

} // namespace crsf
