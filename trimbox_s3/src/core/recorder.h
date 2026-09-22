// ============================================================================
//  Automate d'enregistrement — filtres de v1 §3.6 et §4.6.
//  Sans dépendance matérielle : testé sur PC.
// ============================================================================
#pragma once
#include <stdint.h>

namespace recd {

enum State : uint8_t { STOPPED = 0, RECORDING = 1, PAUSED = 2 };

enum Flags : uint8_t {
  F_WAIT_FIX   = 0x01,   // attendre un fix 3D avant de stocker
  F_STATIONARY = 0x02,   // pause à l'arrêt
  F_NOFIX      = 0x04,   // pause sans signal
  F_AUTOOFF    = 0x08,   // extinction après inactivité
  F_WAIT_DATA  = 0x10,   // pas d'extinction avant un premier point
};

struct Settings {
  uint8_t  flags = 0x1F;
  uint16_t statSpeed = 1389;       // mm/s (~5 km/h)
  uint16_t statInterval = 30;      // s
  uint16_t noFixInterval = 30;     // s
  uint16_t autoOffInterval = 300;  // s
};

struct Result {
  bool store = false;              // stocker le point de cette époque
  bool changed = false;            // l'état vient de changer
  bool storeChange = false;        // … et ce changement doit être écrit
  uint8_t reason = 0;              // rec::StateReason
  bool powerOff = false;           // extinction automatique demandée
};

class Recorder {
public:
  void configure(const Settings& s){ s_ = s; }
  const Settings& settings() const { return s_; }
  State state() const { return st_; }

  // Commandes (console, radio, port série). Renvoient true si l'état change.
  bool start(uint32_t nowMs);
  bool stop();

  // Une époque GNSS. `fix3d` : fixType ≥ 3 et gnssFixOK.
  Result epoch(uint32_t nowMs, bool fix3d, int32_t gSpeedMms);

  void notePointStored(){ stored_++; }
  uint32_t pointsStored() const { return stored_; }
  void setPointsStored(uint32_t n){ stored_ = n; }

private:
  Settings s_;
  State st_ = STOPPED;
  bool everFix_ = false;
  uint32_t lastFixMs_ = 0, stillSinceMs_ = 0, pausedSinceMs_ = 0;
  bool still_ = false;
  uint8_t pauseReason_ = 0;
  uint32_t stored_ = 0;
};

} // namespace recd
