// ============================================================================
//  Maintien à l'arrêt : fige la position quand la voiture ne bouge pas.
//
//  À l'arrêt, un récepteur GNSS « promène » sa position de 1 à 3 m (bruit,
//  multitrajets) et annonce une petite vitesse parasite. Sans filtre, une
//  voiture posée dessine une trace de plusieurs mètres et la vitesse ne
//  retombe jamais à 0.
//
//  Entrée dans le maintien : vitesse sous `enterMms` pendant `enterEpochs`
//  époques de suite ET, si l'IMU est présente, aucun mouvement détecté.
//  Pendant le maintien : position et altitude remplacées par un point
//  d'ancrage (moyenne des premières positions à l'arrêt), vitesse à 0.
//  Sortie immédiate : vitesse ≥ `exitMms`, mouvement IMU, ou éloignement
//  de l'ancrage de plus de `maxDriftM` (voiture poussée lentement).
//
//  Sans dépendance matérielle : testé sur PC.
// ============================================================================
#pragma once
#include <stdint.h>

namespace still {

struct Settings {
  int32_t  enterMms   = 300;    // ~1,1 km/h
  int32_t  exitMms    = 600;    // ~2,2 km/h (hystérésis)
  uint8_t  enterEpochs = 5;     // 0,2 s à 25 Hz
  float    maxDriftM  = 4.0f;
  uint16_t anchorAvg  = 50;     // positions moyennées pour l'ancrage (2 s à 25 Hz)
  // IMU (moyenne sur l'époque) : « calme » si |‖a‖ − 1 g| et ‖ω‖ restent petits
  int32_t  accelTolMg = 80;
  int32_t  gyroTolCdps = 1500;  // 15 °/s
};

struct Sample {
  bool     fix;                 // fix 3D exploitable
  int32_t  lat, lon;            // deg × 1e7
  int32_t  hMSL, height;        // mm
  int32_t  gSpeed;              // mm/s
  bool     imuOk;               // IMU présente et lecture plausible
  int32_t  ax, ay, az;          // mg
  int32_t  gx, gy, gz;          // centi-deg/s
};

class Hold {
public:
  void configure(const Settings& s){ s_ = s; }
  // Traite une époque. Renvoie true si la position est figée ; dans ce cas
  // lat/lon/hMSL/height sont remplacés par l'ancrage et gSpeed mis à 0.
  bool apply(Sample& x);
  bool held() const { return held_; }
  void reset(){ held_ = false; slow_ = 0; }

  static bool imuCalm(const Sample& x, const Settings& s);
  static double distM(int32_t lat1, int32_t lon1, int32_t lat2, int32_t lon2);

private:
  Settings s_;
  bool held_ = false;
  uint8_t slow_ = 0;
  uint16_t n_ = 0;
  double sLat_ = 0, sLon_ = 0, sH_ = 0, sE_ = 0;    // sommes pour la moyenne
  int32_t aLat_ = 0, aLon_ = 0, aH_ = 0, aE_ = 0;   // ancrage
};

} // namespace still
