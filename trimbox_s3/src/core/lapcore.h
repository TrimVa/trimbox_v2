// ============================================================================
//  Chronométrage embarqué — PORTAGE À L'IDENTIQUE de la console (v1 §5.3,
//  §5.4 ; v2 §4.6 et §10.8).
//
//  Chaîne de traitement, dans cet ordre, exactement comme computeStats() :
//    1. plausibilité + fix ≥ 2 + |lat| > 0,0001
//    2. despike()       — point à > 30 m du précédent ET du suivant : supprimé
//    3. despikeSpeed()  — écart > 20 km/h avec les deux voisins : moyenne
//    4. franchissements — intersection, ancrage (halfM × 1,15), sens (> 0,3),
//                         déduplication 1,5 s, temps interpolé
//    5. tours (une ligne) ou parcours départ → arrivée (deux lignes),
//       chronos intermédiaires interpolés
//
//  Les étapes 2 et 3 ont besoin du point SUIVANT : la chaîne a donc deux
//  échantillons de retard (80 ms à 25 Hz), sans effet sur les temps, qui sont
//  horodatés par l'iTOW GNSS et non par l'heure de traitement.
//
//  Ce fichier n'a AUCUNE dépendance Arduino : il est compilé et testé sur PC
//  par la CI (tests/native). Toute modification doit être répercutée dans la
//  console, et inversement.
// ============================================================================
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace lap {

constexpr double MAX_STEP_M   = 30.0;   // console : MAX_STEP_M
constexpr double MAX_DV_KMH   = 20.0;   // console : MAX_DV_KMH
constexpr double HALF_M       = 4.0;    // demi-longueur de ligne
constexpr double ANCHOR_K     = 1.15;   // tolérance d'ancrage
constexpr double DIR_MIN      = 0.3;    // produit scalaire minimal
constexpr double DEDUP_S      = 1.5;    // traversées confondues
constexpr int    MAX_SPLITS   = 6;
constexpr int    MAX_PENDING  = 4;      // départs en attente d'arrivée

struct Point {
  double t;              // secondes (iTOW / 1000, continu)
  double lat, lon;       // degrés
  double speed;          // km/h
  uint8_t fix, sats;
  double alt;            // m
  double ax, ay, az;     // g
};

// Plausibilité — IDENTIQUE à plausible() de la console.
bool plausible(const Point& p);

struct Line {
  bool set = false;
  double anchorLat = 0, anchorLon = 0;
  double dirX = 0, dirY = 0;          // cap de référence (est, nord), normé
  double p1Lat = 0, p1Lon = 0, p2Lat = 0, p2Lon = 0;
  double halfM = HALF_M;
};

// Ligne perpendiculaire à un cap (degrés, 0 = nord, sens horaire).
Line makeLineFromHeading(double lat, double lon, double headingDeg, double halfM = HALF_M);
// Ligne perpendiculaire à un vecteur de déplacement (dx en degrés de longitude
// × cos(lat), dy en degrés de latitude) — forme utilisée par makeLine().
Line makeLineFromVector(double lat, double lon, double dx, double dy, double halfM = HALF_M);

enum class EventKind : uint8_t { Crossing, Lap, Run, Split };

struct Event {
  EventKind kind;
  uint8_t line;          // 0 = A (départ / unique), 1 = B (arrivée)
  uint16_t n;            // n° de tour ou de parcours
  double t;              // Crossing : instant (s) ; Lap/Run : durée (s) ;
                         // Split : temps depuis le départ (s)
  double best;           // Lap/Run : meilleur temps après cet événement
  uint8_t splitKind;     // Split : 0 = vitesse, 1 = distance
  double splitValue;     // Split : km/h ou mètres
};

class Engine {
public:
  typedef void (*Sink)(const Event&, void* ctx);
  void setSink(Sink s, void* ctx){ sink_ = s; ctx_ = ctx; }

  void setLines(const Line& a, const Line& b);   // b.set = false : circuit
  const Line& lineA() const { return A_; }
  const Line& lineB() const { return B_; }

  void setSplits(const double* speedsKmh, int nSpeeds, const double* distsM, int nDists);

  // Nouvelle session : tout l'historique de filtrage et de chrono est oublié.
  void reset();
  // Point brut (avant filtrage), dans l'ordre chronologique.
  void feed(const Point& raw);
  // Fin de session : traite les points encore en attente (le dernier point
  // est conservé, comme dans la console).
  void flush();

  uint16_t lapCount() const { return lapN_; }
  double bestLap() const { return bestLap_; }
  double bestRun() const { return bestRun_; }
  uint32_t cleanCount() const { return clean_; }

private:
  // étapes du filtre
  void stageDespike(const Point& p);
  void stageSpeed(const Point& p);
  void flushDespike();
  void flushSpeed();
  void process(const Point& p);                 // point définitif
  void emit(const Event& e){ if(sink_) sink_(e, ctx_); }
  bool crossing(const Line& L, const Point& a, const Point& b, double& tOut) const;

  Sink sink_ = nullptr; void* ctx_ = nullptr;
  Line A_, B_;
  double splitV_[MAX_SPLITS] = {30, 50, 80}; int nSplitV_ = 3;   // console : splitSpeeds
  double splitD_[MAX_SPLITS] = {25, 50, 100}; int nSplitD_ = 3; // console : splitDists

  // despike
  bool dsHavePrev_ = false, dsHaveCur_ = false; Point dsPrev_, dsCur_;
  // despikeSpeed
  bool spHavePrev_ = false, spHaveCur_ = false; Point spPrev_, spCur_;
  // traitement
  bool havePrev_ = false; Point prev_; double cosL_ = 1;
  double lastCross_[2] = {0, 0}; bool hasCross_[2] = {false, false};
  // circuit
  uint16_t lapN_ = 0; double bestLap_ = 0; double lastLapCross_ = 0; bool lapArmed_ = false;
  // dragster
  struct Pending {
    double t0; bool active;
    double cum; bool first; Point last;       // distance cumulée depuis s.i
    bool vHit[MAX_SPLITS]; bool dHit[MAX_SPLITS];
    uint16_t n;
  };
  Pending pend_[MAX_PENDING]; int nPend_ = 0;
  uint16_t runN_ = 0; double bestRun_ = 0;
  uint32_t clean_ = 0;
  void splitsStep(Pending& P, const Point& p);
};

} // namespace lap
