// ============================================================================
//  Radios de la console (Wi-Fi et Bluetooth) : coupées quand la voiture roule,
//  rallumées après un arrêt prolongé (cahier des charges §6.1 et §12.10).
//
//  La radio de commande (ExpressLRS 2,4 GHz) passe avant la console : dès que
//  la voiture roule, les émetteurs 2,4 GHz du module se taisent, quel que soit
//  le mode choisi. Ils reviennent seuls quand elle est arrêtée depuis un
//  certain temps (30 s par défaut), si le mode automatique est actif.
//
//  Deux étages, sans dépendance matérielle (testés sur PC) :
//    Motion : à chaque solution GNSS, décide « roule » / « à l'arrêt » ;
//    Gate   : à chaque tour de boucle, dit s'il faut allumer ou couper.
// ============================================================================
#pragma once
#include <stdint.h>

namespace airgate {

// « Roule » : `moveEpochs` solutions de suite, avec fix, au-dessus de
// `moveMms`. « À l'arrêt » : sous `stillMms`, ou sans fix (on ne roule pas
// en course sans GNSS). Entre les deux : ni l'un ni l'autre.
struct Motion {
  int32_t moveMms = 2000;      // 7,2 km/h
  uint8_t moveEpochs = 3;      // 120 ms à 25 Hz
  int32_t stillMms = 1389;     // 5 km/h
  uint8_t moveCnt = 0;
  bool moving = false;         // roule (confirmé)
  bool still = true;           // dernière solution : à l'arrêt
  void epoch(bool fix, int32_t gSpeedMms);
};

enum class Act : uint8_t { None, On, Off };

class Gate {
public:
  explicit Gate(uint32_t onAfterMs = 30000) : onAfter_(onAfterMs) {}
  void setDelay(uint32_t ms){ onAfter_ = ms; }
  // stale : pas de solution GNSS récente (réputé à l'arrêt) ;
  // isOn : état actuel de la radio ; autoOn : rallumage automatique permis.
  Act step(uint32_t now, bool stale, bool moving, bool still, bool isOn, bool autoOn);
  uint32_t stillSince() const { return since_; }
private:
  uint32_t onAfter_;
  uint32_t since_ = 0;         // début de l'arrêt en cours (0 : pas à l'arrêt)
};

} // namespace airgate
