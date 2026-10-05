// ============================================================================
//  Identité mémorisée (nom du véhicule, mot de passe du point d'accès) dans la
//  partition NVS de l'ESP32 : la partition « cfg » est pleine (A/B des réglages
//  et des lignes) et la table de partitions ne change jamais (§2.4).
//  À défaut de valeur valide : DEVICE_NICKNAME et WIFI_PASS de config.h.
// ============================================================================
#pragma once
#include "../core/ident.h"

namespace identity {

struct Ident {
  char name[ident::NAME_MAX + 1];
  char pass[ident::PASS_MAX + 1];
  bool passCustom;                 // mot de passe différent de celui de config.h
};

void load(Ident& id);
bool save(const ident::Change& c);   // n'écrit que les champs du masque
bool reset();                         // retour aux valeurs de config.h

} // namespace identity
