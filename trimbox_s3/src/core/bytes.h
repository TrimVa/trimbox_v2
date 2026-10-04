// ============================================================================
//  Lecture / écriture d'entiers dans un tampon d'octets.
//
//  DEUX BOUTISMES COEXISTENT DANS CE FIRMWARE (cahier des charges v2 §10.4) :
//   - protocole TrimBox (console) et UBX (GNSS) : LITTLE-endian  -> *_le
//   - CRSF (radio)                               : BIG-endian     -> *_be
//  Ne jamais copier une structure C par memcpy vers une trame : toujours
//  passer par ces fonctions, qui rendent le boutisme explicite à la lecture.
// ============================================================================
#pragma once
#include <stdint.h>

static inline void put_le16(uint8_t* p, uint16_t v){ p[0]=v; p[1]=v>>8; }
static inline void put_le32(uint8_t* p, uint32_t v){ p[0]=v; p[1]=v>>8; p[2]=v>>16; p[3]=v>>24; }
static inline uint16_t get_le16(const uint8_t* p){ return (uint16_t)(p[0] | (p[1]<<8)); }
static inline uint32_t get_le32(const uint8_t* p){
  return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
static inline int16_t  get_le16s(const uint8_t* p){ return (int16_t)get_le16(p); }
static inline int32_t  get_le32s(const uint8_t* p){ return (int32_t)get_le32(p); }

static inline void put_be16(uint8_t* p, uint16_t v){ p[0]=v>>8; p[1]=v; }
static inline void put_be24(uint8_t* p, uint32_t v){ p[0]=v>>16; p[1]=v>>8; p[2]=v; }
static inline void put_be32(uint8_t* p, uint32_t v){ p[0]=v>>24; p[1]=v>>16; p[2]=v>>8; p[3]=v; }
static inline uint16_t get_be16(const uint8_t* p){ return (uint16_t)((p[0]<<8) | p[1]); }
static inline uint32_t get_be32(const uint8_t* p){
  return ((uint32_t)p[0]<<24) | ((uint32_t)p[1]<<16) | ((uint32_t)p[2]<<8) | p[3];
}
