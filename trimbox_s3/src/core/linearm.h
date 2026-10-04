// ============================================================================
//  Pose de ligne « à l'arrêt, puis on démarre ».
//
//  La voiture est posée SUR la ligne voulue, à l'arrêt. La commande de pose
//  « arme » la ligne : la position est mémorisée (ancrage), mais le cap n'est
//  pas encore connu. Dès que la voiture démarre (dans les `startWindowMs`),
//  on suit son déplacement ; le cap de la ligne est celui du départ :
//    - cap GNSS (effet Doppler, précis) dès que la voiture a parcouru
//      `headDistM` à plus de `headSpeedMms` ;
//    - à défaut, direction ancrage → position actuelle, une fois `bearingDistM`
//      parcourus (plus lent au démarrage, mais toujours juste).
//  La ligne est posée `offsetM` DEVANT l'ancrage : la voiture, partie de
//  derrière, la franchit réellement, ce qui déclenche le chrono (dragster :
//  départ arrêté ; circuit : début du premier tour).
//
//  Sans dépendance matérielle : testé sur PC.
// ============================================================================
#pragma once
#include <stdint.h>

namespace linearm {

struct Settings {
  uint32_t startWindowMs = 10000;  // délai pour DÉMARRER après la commande
  uint32_t placeWindowMs = 10000;  // délai pour obtenir le cap une fois parti
  int32_t  moveMms       = 500;    // « la voiture roule » : > 1,8 km/h…
  float    moveDistM     = 1.5f;   // … ou éloignée de 1,5 m de l'ancrage
  int32_t  headSpeedMms  = 2000;   // cap GNSS fiable au-dessus de 7,2 km/h
  float    headDistM     = 3.0f;
  float    bearingDistM  = 6.0f;
  float    offsetM       = 0.5f;   // ligne posée devant l'ancrage
};

enum class Which : uint8_t { Start = 0, Finish = 1 };

struct Result {
  enum Kind : uint8_t { None, Moving, Placed, Timeout } kind = None;
  Which which = Which::Start;
  int32_t lat = 0, lon = 0;        // ligne posée, deg × 1e7
  int32_t headingE5 = 0;           // cap, deg × 1e5 (0 = nord, sens horaire)
  bool fromDoppler = false;        // cap GNSS (true) ou direction du déplacement
};

class Armer {
public:
  void configure(const Settings& s){ s_ = s; }
  void arm(Which w, int32_t lat, int32_t lon, uint32_t nowMs);
  void cancel(){ active_ = false; }
  bool active() const { return active_; }
  bool moving() const { return moving_; }
  Which which() const { return which_; }
  uint32_t armedAt() const { return armedAt_; }

  // À chaque solution GNSS.
  Result update(uint32_t nowMs, bool fix, int32_t lat, int32_t lon, int32_t gSpeedMms, int32_t headMotE5);

  static double distM(int32_t lat1, int32_t lon1, int32_t lat2, int32_t lon2);
  static double bearingDeg(int32_t lat1, int32_t lon1, int32_t lat2, int32_t lon2);
  static void offset(int32_t& lat, int32_t& lon, double headingDeg, double meters);

private:
  Settings s_;
  bool active_ = false, moving_ = false;
  Which which_ = Which::Start;
  int32_t aLat_ = 0, aLon_ = 0;
  uint32_t armedAt_ = 0, movedAt_ = 0;
};

} // namespace linearm
