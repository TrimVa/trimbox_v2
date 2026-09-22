#include "ota.h"
#include "../config.h"
#include "../core/otacheck.h"
#include <Arduino.h>
#include <esp_ota_ops.h>

// Le core Arduino confirme d'office un firmware fraîchement installé dès le
// démarrage. On reprend la main pour ne le confirmer qu'après l'autotest.
extern "C" bool verifyRollbackLater(){ return true; }

namespace ota {

static Gate s_gate = nullptr;
static web::OtaSink s_sink;
static Check s_check;
static esp_ota_handle_t s_handle = 0;
static const esp_partition_t* s_target = nullptr;
static bool s_active = false, s_reboot = false, s_pending = false, s_validated = false;
static uint32_t s_rebootAt = 0, s_bootMs = 0;
static size_t s_len = 0;

bool inProgress(){ return s_active; }
bool rebootPending(){ return s_reboot; }
bool pendingValidation(){ return s_pending && !s_validated; }
uint32_t progressPercent(){ return s_len ? (uint32_t)((uint64_t)s_check.received() * 100 / s_len) : 0; }
const char* runningPartition(){ const esp_partition_t* p = esp_ota_get_running_partition(); return p ? p->label : "?"; }

static const char* cbBegin(void*, size_t len){
  if(s_active) return "une mise à jour est déjà en cours";
  if(s_gate){ const char* why = s_gate(); if(why) return why; }
  s_target = esp_ota_get_next_update_partition(nullptr);
  if(!s_target) return "aucune partition de mise à jour (table de partitions ?)";
  if(const char* e = s_check.begin(len, s_target->size)) return e;
  // Effacement progressif, au fil de l'écriture : pas de pause de plusieurs
  // secondes au début.
  if(esp_ota_begin(s_target, OTA_WITH_SEQUENTIAL_WRITES, &s_handle) != ESP_OK) return "impossible d'ouvrir la partition de mise à jour";
  s_active = true; s_len = len;
  Serial.printf("[ota] réception de %u octets vers %s\n", (unsigned)len, s_target->label);
  return nullptr;
}

static const char* cbWrite(void*, const uint8_t* d, size_t n){
  if(!s_active) return "mise à jour non commencée";
  if(const char* e = s_check.feed(d, n)) return e;
  if(esp_ota_write(s_handle, d, n) != ESP_OK) return "erreur d'écriture en mémoire flash";
  return nullptr;
}

static void cbAbort(void*){
  if(!s_active) return;
  esp_ota_abort(s_handle);
  s_active = false;
  Serial.println("[ota] annulée");
}

static const char* cbEnd(void*){
  if(!s_active) return "mise à jour non commencée";
  if(const char* e = s_check.finish()){ cbAbort(nullptr); return e; }
  s_active = false;
  const esp_err_t r = esp_ota_end(s_handle);        // vérifie l'image (SHA-256)
  if(r == ESP_ERR_OTA_VALIDATE_FAILED) return "image corrompue (somme de contrôle fausse) : rien n'a été modifié";
  if(r != ESP_OK) return "image refusée par le système : rien n'a été modifié";
  if(esp_ota_set_boot_partition(s_target) != ESP_OK) return "impossible de sélectionner la nouvelle version";
  s_reboot = true; s_rebootAt = millis() + 1500;    // le temps d'envoyer la réponse
  Serial.printf("[ota] firmware accepté (%u octets) : redémarrage sur %s\n", (unsigned)s_len, s_target->label);
  return nullptr;
}

void begin(Gate gate){
  s_gate = gate;
  s_sink.begin = cbBegin; s_sink.write = cbWrite; s_sink.end = cbEnd; s_sink.abort = cbAbort;
  s_bootMs = millis();
  esp_ota_img_states_t st;
  const esp_partition_t* run = esp_ota_get_running_partition();
  s_pending = run && esp_ota_get_state_partition(run, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY;
  if(s_pending) Serial.printf("[ota] NOUVEAU firmware sur %s : validation après autotest et %u s de fonctionnement\n",
                              run->label, OTA_VALIDATE_AFTER_S);
}

const web::OtaSink* sink(){ return &s_sink; }

void serviceValidation(bool selfTestOk){
  if(s_reboot && (int32_t)(millis() - s_rebootAt) >= 0){
    Serial.println("[ota] redémarrage");
    Serial.flush();
    ESP.restart();
  }
  if(!s_pending || s_validated) return;
  if(!selfTestOk){
    Serial.println("[ota] AUTOTEST ÉCHOUÉ : retour à la version précédente");
    Serial.flush();
    esp_ota_mark_app_invalid_rollback_and_reboot();
    return;
  }
  if(millis() - s_bootMs >= OTA_VALIDATE_AFTER_S * 1000u){
    esp_ota_mark_app_valid_cancel_rollback();
    s_validated = true;
    Serial.println("[ota] nouveau firmware VALIDÉ");
  }
}

} // namespace ota
