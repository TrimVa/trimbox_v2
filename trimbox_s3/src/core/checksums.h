// ============================================================================
//  Sommes de contrôle utilisées par les trois protocoles du module.
// ============================================================================
#pragma once
#include <stdint.h>
#include <stddef.h>

// Fletcher-8 : trames TrimBox (B5 62) et UBX. Calculée sur classe, id,
// longueur et charge — PAS sur les deux octets de synchronisation.
struct Fletcher8 {
  uint8_t a = 0, b = 0;
  void add(uint8_t o){ a = (uint8_t)(a + o); b = (uint8_t)(b + a); }
  void add(const uint8_t* p, size_t n){ for(size_t i=0;i<n;i++) add(p[i]); }
};

// CRC8 DVB-S2 (polynôme 0xD5) : trames CRSF, calculé sur type + charge.
uint8_t crc8_d5(const uint8_t* p, size_t n);

// CRC32 IEEE (réfléchi, 0xEDB88320) : structures de configuration en flash.
uint32_t crc32_ieee(const uint8_t* p, size_t n);
