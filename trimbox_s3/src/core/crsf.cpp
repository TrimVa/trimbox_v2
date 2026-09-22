#include "crsf.h"
#include "checksums.h"
#include "bytes.h"
#include <string.h>

namespace crsf {

size_t buildFrame(uint8_t* out, uint8_t type, const uint8_t* payload, uint8_t len){
  if(len > MAX_FRAME - 4) len = MAX_FRAME - 4;
  out[0] = ADDR_FC;
  out[1] = (uint8_t)(len + 2);
  out[2] = type;
  if(len) memcpy(out + 3, payload, len);
  out[3 + len] = crc8_d5(out + 2, 1 + len);
  return 4 + len;
}

size_t buildGps(uint8_t* out, int32_t lat, int32_t lon, uint32_t gSpeed_mms,
                int32_t headMot_e5, int32_t hMSL_mm, uint8_t sats){
  uint8_t p[15];
  put_be32(p+0, (uint32_t)lat);
  put_be32(p+4, (uint32_t)lon);
  // vitesse : km/h × 10 ; mm/s × 0.036 = km/h×10
  uint32_t kmh10 = (uint32_t)((gSpeed_mms * 36u + 500u) / 1000u);
  if(kmh10 > 0xFFFF) kmh10 = 0xFFFF;
  put_be16(p+8, (uint16_t)kmh10);
  // cap : degrés × 100 (NAV-PVT : × 1e5)
  int32_t h = headMot_e5 / 1000;
  while(h < 0) h += 36000;
  h %= 36000;
  put_be16(p+10, (uint16_t)h);
  // altitude : mètres + 1000
  int32_t alt = hMSL_mm / 1000 + 1000;
  if(alt < 0) alt = 0;
  if(alt > 0xFFFF) alt = 0xFFFF;
  put_be16(p+12, (uint16_t)alt);
  p[14] = sats;
  return buildFrame(out, T_GPS, p, sizeof p);
}

size_t buildBattery(uint8_t* out, uint16_t volt_dV, uint16_t curr_dA, uint32_t mAh, uint8_t pct){
  uint8_t p[8];
  put_be16(p+0, volt_dV);
  put_be16(p+2, curr_dA);
  put_be24(p+4, mAh > 0xFFFFFF ? 0xFFFFFF : mAh);
  p[7] = pct;
  return buildFrame(out, T_BATTERY, p, sizeof p);
}

size_t buildFlightMode(uint8_t* out, const char* text){
  uint8_t p[16];
  size_t n = 0;
  while(text[n] && n < 15){ p[n] = (uint8_t)text[n]; n++; }
  p[n++] = 0;                         // chaîne terminée par un nul
  return buildFrame(out, T_FLIGHT_MODE, p, (uint8_t)n);
}

bool Parser::feed(uint8_t b){
  if(pos_ == 0){
    // Adresses admises en tête de trame. Tout autre octet est ignoré.
    if(b == ADDR_FC || b == ADDR_RADIO || b == ADDR_RX || b == ADDR_TXMODULE){
      frame_[pos_++] = b;
    }
    return false;
  }
  if(pos_ == 1){
    if(b < 2 || b > MAX_FRAME - 2){ pos_ = 0; return false; }
    frame_[pos_++] = b;
    need_ = (uint8_t)(b + 2);
    return false;
  }
  frame_[pos_++] = b;
  if(pos_ < need_) return false;
  pos_ = 0;
  const uint8_t len = frame_[1];
  if(crc8_d5(frame_ + 2, len - 1) == frame_[1 + len]) return true;
  bad_++;
  return false;
}

bool decodeChannels(const uint8_t* p, uint8_t len, uint16_t ch[16]){
  if(len < 22) return false;
  uint32_t acc = 0; int bits = 0, idx = 0;
  for(int i = 0; i < 22 && idx < 16; i++){
    acc |= (uint32_t)p[i] << bits;
    bits += 8;
    while(bits >= 11 && idx < 16){
      ch[idx++] = (uint16_t)(acc & 0x7FF);
      acc >>= 11; bits -= 11;
    }
  }
  return idx == 16;
}

bool decodeLinkStats(const uint8_t* p, uint8_t len, LinkStats& s){
  if(len < 10) return false;
  s.rssi1 = p[0]; s.rssi2 = p[1]; s.lq = p[2]; s.snr = (int8_t)p[3];
  s.antenna = p[4]; s.rfMode = p[5]; s.txPower = p[6];
  s.dlRssi = p[7]; s.dlLq = p[8]; s.dlSnr = (int8_t)p[9];
  return true;
}

} // namespace crsf
