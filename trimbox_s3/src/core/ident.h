// ============================================================================
//  Identité du module : nom du véhicule et mot de passe du point d'accès
//  (message FF F3, cahier des charges §3.4). Logique pure, testée sur PC.
//
//  Le nom du véhicule donne aussi le nom du point d'accès Wi-Fi
//  (« TrimBox-<nom> », espaces → tirets) et le nom Bluetooth
//  (« TrimBox <nom> ») : la console filtre les appareils sur « TrimBox ».
// ============================================================================
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace ident {

constexpr size_t NAME_MAX = 16;            // « TrimBox <nom> » tient dans l'annonce Bluetooth (31 o)
constexpr size_t PASS_MIN = 8, PASS_MAX = 63;   // WPA2 : 8 à 63 caractères ASCII
constexpr size_t SET_BYTES = 1 + NAME_MAX + PASS_MAX;   // 80 : masque + nom + mot de passe
constexpr size_t GET_BYTES = NAME_MAX + 1;              // 17 : nom + drapeaux

enum : uint8_t { F_NAME = 0x01, F_PASS = 0x02 };        // masque de la commande
enum : uint8_t { G_PASS_CUSTOM = 0x01 };                // drapeaux de la réponse

// Nom : 1 à 16 caractères parmi lettres ASCII, chiffres, espace, « - », « _ »
// et « . » ; ni espace en tête ou en fin, ni deux espaces de suite. Pas de
// « | » : il sépare les champs de FF F0.
bool validName(const char* s);
// Mot de passe : 8 à 63 caractères ASCII imprimables (0x20 à 0x7E).
bool validPass(const char* s);

void ssidFor(const char* name, char out[33]);      // « TrimBox-Buggy-1 »
void bleNameFor(const char* name, char out[32]);   // « TrimBox Buggy 1 »

struct Change {
  uint8_t mask = 0;
  char name[NAME_MAX + 1] = {0};
  char pass[PASS_MAX + 1] = {0};
};
// Décode une commande FF F3 de SET_BYTES octets. Faux si la longueur, le
// masque ou une valeur demandée est invalide (rien n'est alors appliqué).
bool decodeSet(const uint8_t* p, size_t len, Change& c);
// Construit une commande (utilisé par les bancs de test et le faux module).
void encodeSet(const Change& c, uint8_t out[SET_BYTES]);
// Réponse à FF F3 vide : nom (complété de zéros) puis drapeaux. Le mot de
// passe n'est jamais renvoyé.
void encodeGet(const char* name, bool passCustom, uint8_t out[GET_BYTES]);

} // namespace ident
