// ============================================================================
//  UBX (u-blox) : réassemblage des messages et construction des commandes
//  CFG-VALSET pour les récepteurs de génération M10 (HGLRC M100 Mini).
//
//  Les identifiants de clés proviennent de la description d'interface u-blox
//  M10 (UBX-21035062). Chaque VALSET est confirmé par ACK-ACK ; une clé
//  erronée produit un ACK-NAK, journalisé au démarrage : c'est le moyen de
//  détecter une clé fausse sans analyseur logique.
// ============================================================================
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace ubx {

constexpr uint8_t SYNC1 = 0xB5, SYNC2 = 0x62;
constexpr uint8_t CLS_NAV = 0x01, ID_NAV_PVT = 0x07;
constexpr uint8_t CLS_ACK = 0x05, ID_ACK_ACK = 0x01, ID_ACK_NAK = 0x00;
constexpr uint8_t CLS_CFG = 0x06, ID_CFG_VALSET = 0x8A;

// --- Clés de configuration (taille codée dans les bits 28-30) ---
namespace key {
  constexpr uint32_t UART1_BAUDRATE        = 0x40520001; // U4
  constexpr uint32_t UART1OUTPROT_UBX      = 0x10740001; // L
  constexpr uint32_t UART1OUTPROT_NMEA     = 0x10740002; // L
  constexpr uint32_t MSGOUT_NAV_PVT_UART1  = 0x20910007; // U1
  constexpr uint32_t RATE_MEAS             = 0x30210001; // U2 ms
  constexpr uint32_t RATE_NAV              = 0x30210002; // U2
  constexpr uint32_t NAVSPG_DYNMODEL       = 0x20110021; // E1 (7 = airborne 2g)
  constexpr uint32_t ODO_OUTLPVEL          = 0x10220003; // L — filtre passe-bas vitesse
  constexpr uint32_t ODO_OUTLPCOG          = 0x10220004; // L — filtre passe-bas cap
  constexpr uint32_t MOT_GNSSSPEED_THRS    = 0x20250038; // U1 — maintien statique
  constexpr uint32_t SIGNAL_GPS_ENA        = 0x1031001f; // L
  constexpr uint32_t SIGNAL_SBAS_ENA       = 0x10310020; // L
  constexpr uint32_t SIGNAL_GAL_ENA        = 0x10310021; // L
  constexpr uint32_t SIGNAL_BDS_ENA        = 0x10310022; // L
  constexpr uint32_t SIGNAL_QZSS_ENA       = 0x10310024; // L
  constexpr uint32_t SIGNAL_GLO_ENA        = 0x10310025; // L
}

// Construit un CFG-VALSET (couche RAM uniquement : le module n'a pas de
// mémoire de sauvegarde, la configuration est renvoyée à chaque démarrage).
class ValSet {
public:
  ValSet();
  void add(uint32_t key, uint32_t value);   // taille déduite de la clé
  // Écrit la trame complète dans `out` ; renvoie sa taille.
  size_t finish(uint8_t* out, size_t cap);
  size_t count() const { return nkeys_; }
private:
  uint8_t payload_[4 + 64*8];
  size_t len_;
  size_t nkeys_ = 0;
};

size_t build(uint8_t* out, uint8_t cls, uint8_t id, const uint8_t* payload, uint16_t len);

// Réassembleur octet par octet (le flux GNSS est continu et fiable ; on
// repart simplement sur la synchronisation suivante en cas d'erreur).
class Parser {
public:
  // Renvoie true quand un message complet et valide est disponible.
  bool feed(uint8_t b);
  uint8_t cls() const { return cls_; }
  uint8_t id()  const { return id_; }
  uint16_t len() const { return len_; }
  const uint8_t* payload() const { return buf_; }
  uint32_t badChecksums() const { return bad_; }
private:
  enum St { S1, S2, CLS, ID, L1, L2, PAY, CKA, CKB } st_ = S1;
  uint8_t cls_ = 0, id_ = 0, ckA_ = 0, ckB_ = 0, rxA_ = 0;
  uint16_t len_ = 0, pos_ = 0;
  uint8_t buf_[256];
  uint32_t bad_ = 0;
};

// Période de mesure (ms) correspondant au champ `dataRate` de FF 25.
uint16_t measPeriodMs(uint8_t dataRate);

} // namespace ubx
