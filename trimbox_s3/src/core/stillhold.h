// ============================================================================
//  Maintien à l'arrêt : fige la position et met la vitesse à 0 quand la
//  voiture ne bouge pas.
//
//  À l'arrêt, un récepteur GNSS « promène » sa position de 1 à 3 m (bruit,
//  multitrajets) et annonce une vitesse parasite : quelques dixièmes de km/h
//  sous un ciel dégagé, jusqu'à 5 ou 6 km/h avec peu de satellites (essai
//  réel du 07/10/2026 : 4 à 5 satellites, 0 à 5,8 km/h, voiture posée). Sans
//  filtre, la vitesse affichée est incohérente, la trace dessine une pelote
//  et l'enregistrement automatique part tout seul.
//
//  Deux juges, dans cet ordre :
//
//  1. L'IMU, quand son agitation est mesurable (écart-type des échantillons
//     sur l'époque, `Sample::act`). Une voiture posée ne vibre pas : quelques
//     mg et quelques dixièmes de °/s. Une voiture qui roule, qu'on pousse ou
//     qu'on soulève dépasse largement ces valeurs. L'IMU tranche donc SEULE,
//     quelle que soit la vitesse annoncée par le GNSS :
//       entrée  : calme pendant `enterImuMs` ;
//       sortie  : agitation pendant `exitImuEpochs` époques de suite (un
//                 choc isolé sur la table ne suffit pas), ou dès la première
//                 si le GNSS confirme une vitesse significative.
//     Garde-fou (IMU décrochée, mal fixée) : vitesse GNSS au-dessus de
//     `overrideMms` et de 3 × sAcc pendant `overrideMs` → libéré.
//
//  2. Le GNSS seul (IMU absente, ou agitation non mesurable) : vitesse sous
//     un seuil qui tient compte de la précision annoncée (sAcc) pendant
//     `enterEpochs` époques ; sortie au-dessus d'un seuil plus haut.
//
//  Dans les deux cas : éloignement de l'ancrage au-delà de `maxDriftM` (ou de
//  3 × hAcc si le GNSS est moins précis) → libéré (voiture déplacée).
//  Pendant le maintien : position et altitude remplacées par un point
//  d'ancrage (moyenne des premières positions à l'arrêt), vitesse à 0.
//
//  Sans dépendance matérielle : testé sur PC.
// ============================================================================
#pragma once
#include <stdint.h>

namespace still {

struct Settings {
  // --- GNSS seul -----------------------------------------------------------
  int32_t  enterMms   = 300;    // ~1,1 km/h : seuil d'entrée minimal…
  int32_t  enterMaxMms = 1000;  // … relevé à 2 × sAcc, sans dépasser 3,6 km/h
  int32_t  exitMms    = 600;    // ~2,2 km/h : seuil de sortie minimal…
  int32_t  exitMaxMms = 2500;   // … relevé à 3 × sAcc, sans dépasser 9 km/h
  uint8_t  enterEpochs = 5;     // 0,2 s à 25 Hz
  // --- IMU (agitation sur l'époque) ---------------------------------------
  int32_t  quietAccMg   = 25;   // posée : quelques mg (bruit du capteur ~3 mg)
  int32_t  quietGyroCdps = 300; // posée : < 1 °/s
  int32_t  moveAccMg    = 50;   // hystérésis : agitation franche
  int32_t  moveGyroCdps = 600;
  uint32_t enterImuMs   = 400;  // calme continu avant de figer
  uint8_t  exitImuEpochs = 2;   // agitation continue avant de libérer
  int32_t  overrideMms  = 2778; // 10 km/h : le GNSS reprend la main…
  uint32_t overrideMs   = 500;  // … s'il l'annonce aussi longtemps
  // --- commun -------------------------------------------------------------
  float    maxDriftM  = 4.0f;
  uint16_t anchorAvg  = 50;     // positions moyennées pour l'ancrage (2 s à 25 Hz)
  // IMU sans mesure d'agitation (moyenne sur l'époque) : « calme » si
  // |‖a‖ − 1 g| et ‖ω‖ restent petits
  int32_t  accelTolMg = 80;
  int32_t  gyroTolCdps = 1500;  // 15 °/s
};

// Agitation de l'IMU sur l'époque (écart-type des échantillons).
struct Activity {
  bool     ok = false;          // mesure exploitable
  int32_t  accStdMg = 0;
  int32_t  gyroStdCdps = 0;
};

struct Sample {
  uint32_t tMs;                 // horodatage de l'époque (ms)
  bool     fix;                 // fix 3D exploitable
  int32_t  lat, lon;            // deg × 1e7
  int32_t  hMSL, height;        // mm
  int32_t  gSpeed;              // mm/s
  uint32_t sAcc;                // précision de la vitesse annoncée, mm/s (0 : inconnue)
  uint32_t hAcc;                // précision horizontale annoncée, mm (0 : inconnue)
  bool     imuOk;               // IMU présente et lecture plausible
  int32_t  ax, ay, az;          // mg (moyenne de l'époque)
  int32_t  gx, gy, gz;          // centi-deg/s
  Activity act;                 // agitation de l'époque
};

class Hold {
public:
  void configure(const Settings& s){ s_ = s; }
  const Settings& settings() const { return s_; }
  // Traite une époque. Renvoie true si la position est figée ; dans ce cas
  // lat/lon/hMSL/height sont remplacés par l'ancrage et gSpeed mis à 0.
  bool apply(Sample& x);
  bool held() const { return held_; }
  void reset(){ held_ = false; slow_ = 0; quiet_ = false; busy_ = 0; fast_ = false; }

  static bool imuCalm(const Sample& x, const Settings& s);
  static double distM(int32_t lat1, int32_t lon1, int32_t lat2, int32_t lon2);
  // Seuils GNSS tenant compte de la précision annoncée.
  static int32_t enterThr(const Sample& x, const Settings& s);
  static int32_t exitThr(const Sample& x, const Settings& s);

private:
  void release(){ held_ = false; slow_ = 0; quiet_ = false; busy_ = 0; fast_ = false; }
  Settings s_;
  bool held_ = false;
  uint8_t slow_ = 0;            // GNSS seul : époques lentes de suite
  bool quiet_ = false; uint32_t quietSince_ = 0;   // IMU calme depuis…
  uint8_t busy_ = 0;            // IMU agitée : époques de suite
  bool fast_ = false; uint32_t fastSince_ = 0;     // garde-fou GNSS
  uint16_t n_ = 0;
  double sLat_ = 0, sLon_ = 0, sH_ = 0, sE_ = 0;    // sommes pour la moyenne
  int32_t aLat_ = 0, aLon_ = 0, aH_ = 0, aE_ = 0;   // ancrage
};

} // namespace still
