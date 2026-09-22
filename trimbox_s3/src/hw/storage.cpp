#include "storage.h"
#include "../config.h"
#include "../core/records.h"
#include <esp_partition.h>
#include <string.h>
#include <Arduino.h>

namespace storage {

static const esp_partition_t* s_log = nullptr;
static const esp_partition_t* s_cfg = nullptr;
static uint32_t s_slots = 0, s_used = 0;
static bool s_erasing = false;
static int32_t s_eraseSector = -1, s_eraseTotal = 0;
static const char* s_err = "";
static uint8_t s_recSlot = 0, s_lineSlot = 0;   // exemplaire (0=A, 1=B) le plus récent
static constexpr uint32_t SECTOR = 4096;
static constexpr uint32_t CFG_REC_A = 0, CFG_REC_B = 4096, CFG_LINE_A = 8192, CFG_LINE_B = 12288;

const char* lastError(){ return s_err; }
uint32_t capacitySlots(){ return s_slots; }
uint32_t usedSlots(){ return s_used; }
uint8_t fillPercent(){ return s_slots ? (uint8_t)((uint64_t)s_used * 100 / s_slots) : 0; }
bool erasing(){ return s_erasing; }

static uint8_t typeAt(uint32_t i){
  uint8_t t = 0xFF;
  esp_partition_read(s_log, (size_t)i * rec::SLOT_SIZE, &t, 1);
  return t;
}

static bool blank(uint32_t off, uint32_t len){
  uint8_t b[64];
  while(len){
    const uint32_t n = len < sizeof b ? len : sizeof b;
    if(esp_partition_read(s_log, off, b, n) != ESP_OK) return false;
    for(uint32_t k=0;k<n;k++) if(b[k] != 0xFF) return false;
    off += n; len -= n;
  }
  return true;
}

bool begin(){
  s_log = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t)0x41, "log");
  s_cfg = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t)0x40, "cfg");
  if(!s_log || !s_cfg){ s_err = "partitions 'log'/'cfg' absentes : partitions.csv non pris en compte"; return false; }
  s_slots = s_log->size / rec::SLOT_SIZE;

  // Fin du journal par dichotomie : premier emplacement dont le type vaut
  // 0xFF. Valable parce que le journal est contigu depuis 0 (écriture en
  // ajout, effacement à rebours).
  uint32_t lo = 0, hi = s_slots;
  while(lo < hi){
    const uint32_t mid = lo + (hi - lo) / 2;
    if(typeAt(mid) == 0xFF) hi = mid; else lo = mid + 1;
  }
  s_used = lo;

  // Contrôle de la zone qui suit : elle doit être vierge. Un effacement
  // interrompu en plein secteur peut y laisser des octets quelconques.
  const uint32_t end = s_used * rec::SLOT_SIZE;
  if(end < s_log->size){
    const uint32_t sectorEnd = ((end + SECTOR - 1) / SECTOR) * SECTOR;
    uint32_t len = sectorEnd > end ? sectorEnd - end : 0;
    if(len < 256) len = 256;
    if(len > s_log->size - end) len = s_log->size - end;
    if(!blank(end, len)){
      uint32_t eraseFrom = sectorEnd;
      if(end % SECTOR == 0){
        eraseFrom = end;                         // secteur entier après la fin : effaçable
      }else{
        // On ne peut pas effacer un demi-secteur sans perdre ce qui précède.
        // Les emplacements de la zone douteuse sont marqués « bourrage »
        // (type 0x00, obtenu en programmant des bits à 0, sans effacement) :
        // le journal reste contigu et la dichotomie reste valable.
        const uint32_t firstFree = (sectorEnd + rec::SLOT_SIZE - 1) / rec::SLOT_SIZE;
        const uint8_t pad = 0x00;
        for(uint32_t k = s_used; k < firstFree && k < s_slots; k++)
          esp_partition_write(s_log, (size_t)k * rec::SLOT_SIZE, &pad, 1);
        s_used = firstFree < s_slots ? firstFree : s_slots;
      }
      const uint32_t n = (s_log->size - eraseFrom) < 2*SECTOR ? (s_log->size - eraseFrom) : 2*SECTOR;
      if(n) esp_partition_erase_range(s_log, eraseFrom, n);
      s_err = "zone non vierge après la fin du journal : nettoyée";
    }
  }
  return true;
}

bool append(uint8_t type, const uint8_t payload[80]){
  if(!s_log || s_erasing || s_used >= s_slots) return false;
  uint8_t slot[rec::SLOT_SIZE];
  slot[0] = type;
  memcpy(slot + 1, payload, 80);
  // L'octet de type est écrit avec le reste : une coupure en cours d'écriture
  // laisse un emplacement « occupé » dont la charge contient des 0xFF, écarté
  // à la lecture par rec::validData().
  if(esp_partition_write(s_log, (size_t)s_used * rec::SLOT_SIZE, slot, rec::SLOT_SIZE) != ESP_OK) return false;
  s_used++;
  return true;
}

bool readSlot(uint32_t i, uint8_t& type, uint8_t payload[80]){
  if(!s_log || i >= s_used) return false;
  uint8_t slot[rec::SLOT_SIZE];
  if(esp_partition_read(s_log, (size_t)i * rec::SLOT_SIZE, slot, rec::SLOT_SIZE) != ESP_OK) return false;
  type = slot[0];
  memcpy(payload, slot + 1, 80);
  return true;
}

void eraseBegin(){
  if(!s_log) return;
  const uint32_t bytes = s_used * rec::SLOT_SIZE;
  s_eraseTotal = (int32_t)((bytes + SECTOR - 1) / SECTOR);
  s_eraseSector = s_eraseTotal - 1;
  s_erasing = true;
}

bool eraseStep(uint8_t& pct){
  if(!s_erasing){ pct = 100; return true; }
  if(s_eraseSector < 0){
    s_erasing = false; s_used = 0; pct = 100;
    return true;
  }
  esp_partition_erase_range(s_log, (size_t)s_eraseSector * SECTOR, SECTOR);
  // Le pointeur d'écriture recule avec l'effacement : l'état reste cohérent
  // à chaque instant, même en cas de coupure.
  const uint32_t firstSlotOfSector = ((uint32_t)s_eraseSector * SECTOR + rec::SLOT_SIZE - 1) / rec::SLOT_SIZE;
  if(firstSlotOfSector < s_used) s_used = firstSlotOfSector;
  s_eraseSector--;
  const int32_t done = s_eraseTotal - 1 - s_eraseSector;
  pct = s_eraseTotal ? (uint8_t)(done * 100 / s_eraseTotal) : 100;
  if(pct >= 100) pct = 99;               // 100 % seulement à la fin réelle
  return false;
}

// ---------------------------------------------------------------- config A/B
template<typename T, size_t N>
static bool loadAB(uint32_t offA, uint32_t offB, T& out, uint8_t& slot){
  uint8_t a[N], b[N]; T ca, cb;
  const bool va = esp_partition_read(s_cfg, offA, a, N) == ESP_OK && persist::decode(a, ca);
  const bool vb = esp_partition_read(s_cfg, offB, b, N) == ESP_OK && persist::decode(b, cb);
  const int w = persist::newest(va, ca.seq, vb, cb.seq);
  if(w < 0){ slot = 1; return false; }     // prochaine écriture en A
  out = w == 0 ? ca : cb;
  slot = (uint8_t)w;
  return true;
}

template<typename T, size_t N>
static bool saveAB(uint32_t offA, uint32_t offB, T& c, uint8_t& slot){
  // On écrit TOUJOURS dans l'exemplaire qui n'est pas la référence : une
  // coupure pendant l'effacement/écriture laisse l'autre intact.
  const uint8_t target = slot == 0 ? 1 : 0;
  const uint32_t off = target == 0 ? offA : offB;
  c.seq++;
  uint8_t buf[N];
  persist::encode(c, buf);
  if(esp_partition_erase_range(s_cfg, off, SECTOR) != ESP_OK) return false;
  if(esp_partition_write(s_cfg, off, buf, N) != ESP_OK) return false;
  // Relecture de contrôle : un exemplaire invalide ne devient pas la référence.
  uint8_t chk[N]; T back;
  if(esp_partition_read(s_cfg, off, chk, N) != ESP_OK || !persist::decode(chk, back)) return false;
  slot = target;
  return true;
}

bool loadRecConfig(persist::RecConfig& c){ return s_cfg && loadAB<persist::RecConfig, persist::REC_BYTES>(CFG_REC_A, CFG_REC_B, c, s_recSlot); }
bool saveRecConfig(persist::RecConfig& c){ return s_cfg && saveAB<persist::RecConfig, persist::REC_BYTES>(CFG_REC_A, CFG_REC_B, c, s_recSlot); }
bool loadLineConfig(persist::LineConfig& c){ return s_cfg && loadAB<persist::LineConfig, persist::LINE_BYTES>(CFG_LINE_A, CFG_LINE_B, c, s_lineSlot); }
bool saveLineConfig(persist::LineConfig& c){ return s_cfg && saveAB<persist::LineConfig, persist::LINE_BYTES>(CFG_LINE_A, CFG_LINE_B, c, s_lineSlot); }

} // namespace storage
