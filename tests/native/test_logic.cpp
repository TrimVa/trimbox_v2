// Automate d'enregistrement (v1 §4.6) et pose de ligne par la voie (v2 §4.5).
#include "t.h"
#include "../../trimbox_s3/src/core/recorder.h"
#include "../../trimbox_s3/src/core/linecmd.h"
#include "../../trimbox_s3/src/core/records.h"
#include "../../trimbox_s3/src/core/persist.h"
#include <string.h>

static void testRecorder(){
  recd::Recorder R; recd::Settings s; R.configure(s);   // défauts : 0x1F, 5 km/h, 30 s
  uint32_t t = 0;
  CHECK(R.start(t), "démarrage");
  // pas de fix : rien n'est stocké (attente de fix)
  auto r = R.epoch(t += 40, false, 0);
  CHECK(!r.store && R.state() == recd::RECORDING, "rien avant le premier fix");
  // fix + roule : stocké
  r = R.epoch(t += 40, true, 10000);
  CHECK(r.store, "stocké avec fix");
  // arrêt pendant 29,9 s : toujours en cours
  uint32_t t0 = t;
  while(t - t0 < 29900){ r = R.epoch(t += 40, true, 100); CHECK(!r.changed, "pas de pause avant 30 s"); }
  // … puis pause à 30 s, changement STOCKÉ
  r = R.epoch(t += 200, true, 100);
  CHECK(r.changed && r.storeChange && R.state() == recd::PAUSED && r.reason == rec::REASON_STATIONARY, "pause à l'arrêt, stockée");
  // reprise : notifiée, NON stockée comme changement
  r = R.epoch(t += 40, true, 3000);
  CHECK(r.changed && !r.storeChange && R.state() == recd::RECORDING, "reprise notifiée non stockée");
  // perte du fix 30 s → pause sans fix
  t0 = t;
  while(t - t0 < 29960){ r = R.epoch(t += 40, false, 0); if(r.changed) break; }
  CHECK(!r.changed, "pas de pause sans fix avant 30 s");
  r = R.epoch(t += 80, false, 0);
  CHECK(r.changed && r.reason == rec::REASON_NOFIX, "pause sans fix");
  // pause prolongée → extinction… sauf si aucun point n'a été stocké (0x10)
  r = R.epoch(t += 301000, false, 0);
  CHECK(!r.powerOff, "pas d'extinction sans point stocké (drapeau 0x10)");
  R.notePointStored();
  r = R.epoch(t += 40, false, 0);
  CHECK(r.powerOff, "extinction après 300 s de pause");
  CHECK(R.stop() && R.state() == recd::STOPPED, "arrêt");

  // Sans le drapeau d'attente de fix : stockage immédiat.
  s.flags = 0; R.configure(s); R.start(t);
  r = R.epoch(t += 40, false, 0);
  CHECK(r.store, "stockage sans fix si le filtre est désactivé");
}

static void testLineCmd(){
  linecmd::Decoder D; linecmd::Output o; uint32_t t = 1000;
  // inter oublié en haut au démarrage : ignoré tant que le neutre n'est pas vu
  for(int i=0;i<50;i++){ o = D.update(100, t += 20); CHECK(o.cmd == linecmd::Cmd::None, "inter haut au démarrage ignoré"); }
  for(int i=0;i<5;i++) o = D.update(0, t += 20);
  CHECK(D.armed(), "armé après le neutre");
  // haut 0,5 s → départ, horodaté au DÉBUT du geste
  uint32_t start = t + 20; int got = 0; uint32_t at = 0;
  for(int i=0;i<40;i++){ o = D.update(100, t += 20); if(o.cmd == linecmd::Cmd::PoseStart){ got++; at = o.atMs; } }
  CHECK(got == 1 && at == start, "pose départ unique, horodatée au début (%u / %u)", at, start);
  for(int i=0;i<5;i++) D.update(0, t += 20);
  // bas 1 s puis relâché → arrivée
  start = t + 20; got = 0;
  for(int i=0;i<50;i++){ o = D.update(-100, t += 20); CHECK(o.cmd == linecmd::Cmd::None, "rien pendant l'appui bas"); }
  o = D.update(0, t += 20);
  CHECK(o.cmd == linecmd::Cmd::PoseFinish && o.atMs == start, "pose arrivée au relâchement");
  // bas 3 s → effacement, pas d'arrivée au relâchement
  got = 0;
  for(int i=0;i<160;i++){ o = D.update(-100, t += 20); if(o.cmd == linecmd::Cmd::Clear) got++; }
  o = D.update(0, t += 20);
  CHECK(got == 1 && o.cmd == linecmd::Cmd::None, "effacement à 3 s, sans arrivée");
  // appui trop bref : rien
  for(int i=0;i<10;i++) o = D.update(100, t += 20);
  o = D.update(0, t += 20);
  CHECK(o.cmd == linecmd::Cmd::None, "appui bref ignoré");
  // perte des voies : désarmement
  D.tick(t += 1000);
  CHECK(!D.armed(), "désarmé si les voies ne sont plus reçues");
}

static void testPersist(){
  persist::RecConfig c; c.enabled = 1; c.dataRate = 2; c.statSpeed = 2000; c.seq = 41;
  uint8_t b[persist::REC_BYTES]; persist::encode(c, b);
  persist::RecConfig d; CHECK(persist::decode(b, d) && d.dataRate == 2 && d.statSpeed == 2000 && d.seq == 41, "config : aller-retour");
  b[10] ^= 1; CHECK(!persist::decode(b, d), "config corrompue rejetée (CRC)");
  uint8_t blank[persist::REC_BYTES]; memset(blank, 0xFF, sizeof blank);
  CHECK(!persist::decode(blank, d), "secteur effacé rejeté");
  // séquence tolérante au rebouclage
  CHECK(persist::newest(true, 5, true, 6) == 1 && persist::newest(true, 6, true, 5) == 0, "plus récent");
  CHECK(persist::newest(true, 0xFFFFFFFF, true, 0) == 1, "rebouclage : 0 plus récent que 0xFFFFFFFF");
  CHECK(persist::newest(false, 0, true, 3) == 1 && persist::newest(false, 0, false, 0) == -1, "exemplaire invalide");
  persist::LineConfig L; L.mode = 2; L.startLat = 453456789; L.finishHeading = -9000000; L.seq = 7;
  uint8_t lb[persist::LINE_BYTES]; persist::encode(L, lb);
  persist::LineConfig M; CHECK(persist::decode(lb, M) && M.mode == 2 && M.startLat == 453456789 && M.finishHeading == -9000000, "lignes : aller-retour");
}

int main(){ testRecorder(); testLineCmd(); testPersist(); DONE("logique"); }
