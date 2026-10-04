// ============================================================================
//  Protocole TrimBox (trames B5 62, format hérité RaceBox rév. 8)
//  Cahier des charges v1 §3, v2 §6.
// ============================================================================
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace tb {

constexpr uint8_t SYNC1 = 0xB5, SYNC2 = 0x62, CLS = 0xFF;

enum : uint8_t {
  ID_LIVE     = 0x01,  // → console, 80 o
  ID_ACK      = 0x02,  // → console, 2 o (classe, id acquittés)
  ID_NACK     = 0x03,  // → console, 2 o
  ID_HIST     = 0x21,  // → console, 80 o (enregistrement mémoire)
  ID_STATUS   = 0x22,  // ↔ 0 / 12
  ID_DOWNLOAD = 0x23,  // ↔ 0|1 / 4
  ID_ERASE    = 0x24,  // ↔ 0 / 1 (progression %)
  ID_CONFIG   = 0x25,  // ↔ 0 / 12
  ID_STATE    = 0x26,  // → console, 12 o
  ID_GNSSCFG  = 0x27,  // ↔ 0 / 3
  ID_ESC      = 0x28,  // → console, 80 o (v2)
  ID_LAP      = 0x29,  // → console, 80 o (v2)
  ID_UNLOCK   = 0x30,  // → appareil, 4 o
  ID_BUILD    = 0xF0,  // ↔ 0 / texte (extension maison)
  ID_LINES    = 0xF1,  // ↔ 0 / 28 (v2)
  ID_WIFI     = 0xF2,  // ↔ 0 / 1  (v2)
};

constexpr size_t HEADER = 6, TRAILER = 2, OVERHEAD = HEADER + TRAILER;
constexpr size_t MAX_PAYLOAD = 256;

// Construit une trame complète dans `out` (capacité ≥ len + 8).
// Renvoie la taille totale.
size_t build(uint8_t* out, uint8_t cls, uint8_t id, const uint8_t* payload, uint16_t len);

struct Frame {
  uint8_t cls = 0, id = 0;
  uint16_t len = 0;
  uint8_t payload[MAX_PAYLOAD];
};

// Réassembleur de trames — même règle que feed() dans la console
// (v1 §5.2 et §9.5) : en cas de longueur incohérente ou de somme de contrôle
// fausse, on avance de DEUX octets, JAMAIS de la longueur annoncée.
class Parser {
public:
  // `strictCommands` : n'accepte que les longueurs attendues des COMMANDES
  // (sens console → appareil). Les tests l'utilisent à false pour relire
  // les trames émises.
  explicit Parser(bool strictCommands = true) : strict_(strictCommands) {}
  // Ajoute des octets reçus. Renvoie le nombre d'octets réellement acceptés
  // (le tampon est borné ; un débordement réinitialise la recherche).
  size_t push(const uint8_t* data, size_t n);
  // Extrait la prochaine trame valide. false s'il n'y en a pas (encore).
  bool next(Frame& f);
  uint32_t badFrames() const { return bad_; }
private:
  bool lengthPlausible(uint8_t cls, uint8_t id, uint16_t len) const;
  bool strict_;
  uint8_t buf_[1024];
  size_t n_ = 0;
  uint32_t bad_ = 0;
};

} // namespace tb
