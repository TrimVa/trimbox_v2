// ============================================================================
//  Contrôle d'un firmware reçu par Wi-Fi, AU FIL DE L'EAU (v2 §7.2).
//  Sans dépendance : testé sur PC.
//
//  Trois vérifications avant d'accepter l'image :
//   1. en-tête d'image ESP : octet magique 0xE9, puce ESP32-S3 (id 9) ;
//   2. taille : ni vide, ni plus grande que la partition d'application ;
//   3. marque TrimBox présente dans l'image : on ne flashe pas par erreur le
//      firmware d'un autre appareil, ni le fichier « complet » (qui commence
//      par le chargeur de démarrage et fait 16 Mo), ni un .uf2 de la v1.
//  L'intégrité (somme SHA-256 de l'image) est vérifiée ensuite par
//  esp_ota_end(), côté matériel.
// ============================================================================
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace ota {

extern const char MARK[];        // présente dans CE firmware (voir otacheck.cpp)
constexpr uint8_t  IMAGE_MAGIC = 0xE9;
constexpr uint16_t CHIP_ESP32S3 = 9;

class Check {
public:
  // `declared` : taille annoncée (Content-Length) ; `maxSize` : partition.
  const char* begin(size_t declared, size_t maxSize);    // nullptr si accepté
  const char* feed(const uint8_t* d, size_t n);          // nullptr si OK
  const char* finish();                                  // nullptr si l'image est acceptable
  size_t received() const { return got_; }
private:
  size_t want_ = 0, got_ = 0;
  uint8_t head_[16]; size_t headN_ = 0;
  bool headChecked_ = false, markFound_ = false;
  char tail_[64]; size_t tailN_ = 0;
};

} // namespace ota
