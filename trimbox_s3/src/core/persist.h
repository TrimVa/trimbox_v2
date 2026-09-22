// ============================================================================
//  Configuration persistante en DOUBLE EXEMPLAIRE (v1 §4.4, v2 §3.4).
//  Sérialisation explicite octet par octet (pas de memcpy de structure :
//  le format sur flash ne doit pas dépendre du compilateur).
// ============================================================================
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace persist {

struct RecConfig {
  uint8_t  enabled = 0;
  uint8_t  dataRate = 0;          // 0=25 Hz 1=10 2=5 3=1 4=20
  uint8_t  flags = 0x1F;
  uint16_t statSpeed = 1389;      // mm/s
  uint16_t statInterval = 30;     // s
  uint16_t noFixInterval = 30;    // s
  uint16_t autoOffInterval = 300; // s
  uint8_t  gnssDynModel = 7;
  uint8_t  gnss3dSpeed = 0;
  uint8_t  gnssMinAcc = 0;        // m, 0 = sans limite
  uint32_t seq = 0;
};
constexpr uint32_t REC_MAGIC = 0x534D4252;   // v1
constexpr uint8_t  REC_VERSION = 2;
constexpr size_t   REC_BYTES = 28;

struct LineConfig {
  uint8_t mode = 0;               // 0 aucune, 1 circuit, 2 dragster
  uint8_t crsfChannel = 8;
  int32_t startLat = 0, startLon = 0, startHeading = 0;    // deg×1e7, deg×1e5
  int32_t finishLat = 0, finishLon = 0, finishHeading = 0;
  uint32_t seq = 0;
};
constexpr uint32_t LINE_MAGIC = 0x4C494E45;  // "LINE"
constexpr uint8_t  LINE_VERSION = 1;
constexpr size_t   LINE_BYTES = 40;

void encode(const RecConfig& c, uint8_t out[REC_BYTES]);
bool decode(const uint8_t in[REC_BYTES], RecConfig& c);       // magie + version + CRC
void encode(const LineConfig& c, uint8_t out[LINE_BYTES]);
bool decode(const uint8_t in[LINE_BYTES], LineConfig& c);

// Charge utile FF F1 (28 octets) : sans magie, séquence ni CRC.
void encodeLinesPayload(const LineConfig& c, uint8_t out[28]);
void decodeLinesPayload(const uint8_t in[28], LineConfig& c);

// Le plus récent de deux exemplaires valides, par comparaison de séquence
// TOLÉRANTE AU REBOUCLAGE : (int32_t)(b − a) > 0. Renvoie 0 (A), 1 (B) ou −1.
int newest(bool aValid, uint32_t aSeq, bool bValid, uint32_t bSeq);

} // namespace persist
