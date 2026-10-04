#include "otacheck.h"
#include <string.h>

namespace ota {

// La marque est une chaîne ordinaire : elle se retrouve telle quelle dans
// les données en lecture seule de l'image. « used » empêche l'éditeur de
// liens de l'écarter.
__attribute__((used)) const char MARK[] = "TRIMBOX-S3-FIRMWARE-MARK-v1";

const char* Check::begin(size_t declared, size_t maxSize){
  want_ = declared; got_ = 0; headN_ = 0; headChecked_ = markFound_ = false; tailN_ = 0;
  if(declared < 4096) return "fichier trop petit pour être un firmware";
  if(declared > maxSize) return "fichier trop gros : ce n'est pas trimbox_s3-app.bin (le fichier « complet » ne se charge qu'à l'adresse 0x0, par câble)";
  return nullptr;
}

const char* Check::feed(const uint8_t* d, size_t n){
  if(got_ + n > want_) return "plus de données qu'annoncé";
  const size_t ml = strlen(MARK);
  for(size_t i = 0; i < n; i++){
    const uint8_t b = d[i];
    if(headN_ < sizeof head_) head_[headN_++] = b;
    // Fenêtre glissante de la longueur de la marque, à cheval sur les morceaux.
    if(!markFound_){
      if(tailN_ == ml){ memmove(tail_, tail_ + 1, ml - 1); tailN_ = ml - 1; }
      tail_[tailN_++] = (char)b;
      if(tailN_ == ml && !memcmp(tail_, MARK, ml)) markFound_ = true;
    }
  }
  got_ += n;
  if(!headChecked_ && headN_ == sizeof head_){
    headChecked_ = true;
    if(head_[0] != IMAGE_MAGIC) return "ce fichier n'est pas une image ESP32 (fichier .uf2 de la v1 ?)";
    const uint16_t chip = (uint16_t)(head_[12] | (head_[13] << 8));
    if(chip != CHIP_ESP32S3) return "image compilée pour une autre puce que l'ESP32-S3";
  }
  return nullptr;
}

const char* Check::finish(){
  if(got_ != want_) return "transfert incomplet";
  if(!headChecked_) return "en-tête d'image absent";
  if(!markFound_) return "ce n'est pas un firmware TrimBox S3 (marque absente)";
  return nullptr;
}

} // namespace ota
