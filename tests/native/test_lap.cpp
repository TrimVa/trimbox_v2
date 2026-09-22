// Bancs de chronométrage (v1 §8.3 n°2 et 3, v2 §9 n°7).
// Écrit aussi lap_cases.json, relu par console_check.js qui rejoue les MÊMES
// points dans les fonctions de la console : les deux implémentations doivent
// donner les mêmes temps à 1 ms près.
#include "t.h"
#include "../../trimbox_s3/src/core/lapcore.h"
#include <vector>
#include <string>
#include <math.h>

using lap::Point; using lap::Event; using lap::EventKind;

static const double LAT0 = 45.0, LON0 = 5.0;
static Point mk(double t, double x, double y, double kmh){
  Point p{};
  p.t = t;
  p.lat = LAT0 + y / 110540.0;
  p.lon = LON0 + x / (111320.0 * cos(LAT0 * M_PI / 180));
  p.speed = kmh; p.fix = 3; p.sats = 14; p.alt = 200; p.az = 1.0;
  return p;
}

struct Collect { std::vector<Event> ev; };
static void sink(const Event& e, void* c){ ((Collect*)c)->ev.push_back(e); }

static std::string json;   // sortie pour la comparaison avec la console
static void jsonCase(const char* name, const std::vector<Point>& pts, int idxA, int idxB, const Collect& c){
  char b[256];
  json += std::string(json.empty() ? "[" : ",") + "{\"name\":\"" + name + "\",\"idxA\":" + std::to_string(idxA) +
          ",\"idxB\":" + std::to_string(idxB) + ",\"pts\":[";
  for(size_t i=0;i<pts.size();i++){
    const Point& p = pts[i];
    snprintf(b, sizeof b, "%s[%.3f,%.10f,%.10f,%.6f,%u,%u]", i?",":"", p.t, p.lat, p.lon, p.speed, p.fix, p.sats);
    json += b;
  }
  json += "],\"events\":[";
  for(size_t i=0;i<c.ev.size();i++){
    const Event& e = c.ev[i];
    snprintf(b, sizeof b, "%s{\"k\":%d,\"line\":%u,\"n\":%u,\"t\":%.9f,\"sk\":%u,\"sv\":%.3f}",
             i?",":"", (int)e.kind, e.line, e.n, e.t, e.splitKind, e.splitValue);
    json += b;
  }
  json += "]}";
}

// Ligne posée comme makeLine() de la console : direction pts[i-3] → pts[i+3].
static lap::Line lineAt(const std::vector<Point>& pts, int idx){
  const Point& a = pts[idx-3 < 0 ? 0 : idx-3];
  const Point& b = pts[idx+3 >= (int)pts.size() ? pts.size()-1 : idx+3];
  const double cosL = cos(pts[idx].lat * M_PI / 180);
  return lap::makeLineFromVector(pts[idx].lat, pts[idx].lon, (b.lon - a.lon)*cosL, b.lat - a.lat);
}

// --- Banc n°2 : circuit fermé, 5 tours de 20,00 s, ligne en 4 endroits ---
static void benchCircuit(){
  const double R = 20, S = 30, per = 2*S + 2*M_PI*R, lapT = 20.0, v = per / lapT;
  std::vector<Point> pts;
  for(int k=0; k < 5*25*20; k++){            // exactement 5 tours à 25 Hz
    const double t = 1000.0 + k*0.04;         // iTOW arbitraire
    double s = fmod(v * (t - 1000.0), per), x, y;
    if(s < S){ x = s; y = 0; }
    else if((s -= S) < M_PI*R){ const double a = s/R; x = S + R*sin(a); y = R - R*cos(a); }
    else if((s -= M_PI*R) < S){ x = S - s; y = 2*R; }
    else { s -= S; const double a = s/R; x = -R*sin(a); y = R + R*cos(a); }
    pts.push_back(mk(t, x, y, v*3.6));
  }
  // Artefacts du récepteur, loin des lignes : un saut de position isolé et
  // une vitesse aberrante isolée. Ils doivent être éliminés par le filtrage.
  pts[1310].lat += 60 / 110540.0;
  pts[1720].speed += 60;

  const int where[4] = {60, 200, 330, 450};   // ligne droite, virage, droite, virage
  for(int w=0; w<4; w++){
    lap::Engine E; Collect c; E.setSink(sink, &c);
    E.setLines(lineAt(pts, where[w]), lap::Line());
    for(auto& p : pts) E.feed(p);
    E.flush();
    int laps = 0; bool ok = true;
    for(auto& e : c.ev) if(e.kind == EventKind::Lap){ laps++; ok &= fabs(e.t - lapT) < 0.005; }
    CHECK(laps == 4 && ok, "circuit, ligne n°%d : %d tours (attendu 4 à 20,00 s)", w+1, laps);
    for(auto& e : c.ev) if(e.kind == EventKind::Lap) NEAR(e.t, lapT, 0.005, "temps au tour");
    char name[32]; snprintf(name, sizeof name, "circuit_%d", w+1);
    jsonCase(name, pts, where[w], -1, c);
  }

  // Ligne longue de 18 m (v1 §9.6) : le garde-fou d'ancrage doit empêcher
  // le double comptage. La demi-longueur passe à 9 m, la tolérance suit.
  {
    lap::Engine E; Collect c; E.setSink(sink, &c);
    lap::Line L = lineAt(pts, 60); L = lap::makeLineFromVector(L.anchorLat, L.anchorLon, L.dirX, L.dirY, 9.0);
    E.setLines(L, lap::Line());
    for(auto& p : pts) E.feed(p);
    E.flush();
    int laps = 0; for(auto& e : c.ev) if(e.kind == EventKind::Lap) laps++;
    CHECK(laps == 4, "ligne de 18 m : %d tours (attendu 4)", laps);
  }
}

// --- Banc n°3 : dragster, accélération constante 6 m/s² ---
static void benchDrag(){
  const double a = 6.0, head = 30 * M_PI / 180;
  std::vector<Point> pts;
  for(int k=0; k <= 8*25; k++){
    const double t = 5000.0 + k*0.04, tt = k*0.04, d = 0.5*a*tt*tt;
    pts.push_back(mk(t, d*sin(head), d*cos(head), a*tt*3.6));
  }
  const int iA = 1;                                   // départ : 2e échantillon
  int iB = 0; while(0.5*a*pow(iB*0.04, 2) < 150) iB++;   // arrivée ≈ 150 m
  lap::Engine E; Collect c; E.setSink(sink, &c);
  E.setLines(lineAt(pts, iA), lineAt(pts, iB));
  for(auto& p : pts) E.feed(p);
  E.flush();

  double tStart = -1; int runs = 0; double runT = 0;
  for(auto& e : c.ev){
    if(e.kind == EventKind::Crossing && e.line == 0 && tStart < 0) tStart = e.t - 5000.0;
    if(e.kind == EventKind::Run){ runs++; runT = e.t; }
  }
  CHECK(runs == 1, "dragster : %d parcours (attendu 1)", runs);
  NEAR(runT, iB*0.04 - tStart, 0.005, "temps de parcours");
  int nsplit = 0;
  for(auto& e : c.ev){
    if(e.kind != EventKind::Split) continue;
    nsplit++;
    const double theo = e.splitKind == 0 ? (e.splitValue/3.6)/a - tStart
                                         : sqrt(2*e.splitValue/a) - tStart;
    char what[64]; snprintf(what, sizeof what, "chrono %s %.0f", e.splitKind ? "distance" : "vitesse", e.splitValue);
    NEAR(e.t, theo, 0.005, what);
  }
  CHECK(nsplit == 6, "6 chronos intermédiaires (obtenu %d)", nsplit);
  jsonCase("dragster", pts, iA, iB, c);
}

int main(){
  benchCircuit(); benchDrag();
  FILE* f = fopen("lap_cases.json", "w");
  if(f){ fputs((json + "]").c_str(), f); fclose(f); }
  DONE("chrono");
}
