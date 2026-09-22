#include "checksums.h"

uint8_t crc8_d5(const uint8_t* p, size_t n){
  uint8_t crc = 0;
  for(size_t i=0;i<n;i++){
    crc ^= p[i];
    for(int k=0;k<8;k++) crc = (crc & 0x80) ? (uint8_t)((crc<<1) ^ 0xD5) : (uint8_t)(crc<<1);
  }
  return crc;
}

uint32_t crc32_ieee(const uint8_t* p, size_t n){
  uint32_t crc = 0xFFFFFFFFu;
  for(size_t i=0;i<n;i++){
    crc ^= p[i];
    for(int k=0;k<8;k++) crc = (crc & 1) ? (crc>>1) ^ 0xEDB88320u : (crc>>1);
  }
  return ~crc;
}
