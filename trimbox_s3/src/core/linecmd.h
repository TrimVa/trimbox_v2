// ============================================================================
//  Pose de ligne par une voie radio (v2 §4.5).
//   haut  (> +60 %) maintenu 0,5 s        → ligne de DÉPART (ou unique)
//   bas   (< −60 %) maintenu 0,5 à 3 s    → ligne d'ARRIVÉE (au relâchement)
//   bas   maintenu 3 s                    → EFFACEMENT des lignes
//  L'instant retenu pour la pose est celui où l'inter QUITTE le neutre, pas
//  celui où la commande est validée : la ligne est posée là où était la
//  voiture au moment du geste.
//  Rien n'est accepté tant que la voie n'a pas été vue au neutre (inter
//  oublié en position haute au démarrage) ni si les voies ne sont plus reçues.
// ============================================================================
#pragma once
#include <stdint.h>

namespace linecmd {

enum class Cmd : uint8_t { None, PoseStart, PoseFinish, Clear };

struct Output { Cmd cmd = Cmd::None; uint32_t atMs = 0; };

class Decoder {
public:
  static constexpr int NEUTRAL = 30, ACTIVE = 60;
  static constexpr uint32_t HOLD_MS = 500, CLEAR_MS = 3000, TIMEOUT_MS = 500;

  Output update(int percent, uint32_t nowMs);   // à chaque trame de voies
  void tick(uint32_t nowMs);                    // appelé périodiquement
  bool armed() const { return armed_; }
private:
  enum St : uint8_t { Neutral, High, Low, Wait } st_ = Wait;
  bool armed_ = false, fired_ = false;
  uint32_t since_ = 0, last_ = 0;
};

} // namespace linecmd
