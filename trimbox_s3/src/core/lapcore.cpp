#include "lapcore.h"
#include "records.h"
#include <math.h>
#include <string.h>

namespace lap {

static const double RAD = M_PI / 180.0;

static inline double dist(const Point& a, const Point& b){
  return rec::distM(a.lat, a.lon, b.lat, b.lon);
}

bool plausible(const Point& r){
  return fabs(r.lat) <= 90 && fabs(r.lon) <= 180 &&
         r.speed >= 0 && r.speed < 500 &&
         r.fix <= 5 && r.sats <= 60 &&
         fabs(r.alt) < 10000 &&
         fabs(r.ax) <= 20 && fabs(r.ay) <= 20 && fabs(r.az) <= 20;
}

Line makeLineFromVector(double lat, double lon, double dx, double dy, double halfM){
  Line L;
  const double cosL = cos(lat * RAD);
  double n = hypot(dx, dy); if(n == 0) n = 1e-9;
  dx /= n; dy /= n;
  const double dLat = halfM / 110540.0, dLon = halfM / (111320.0 * cosL);
  L.set = true; L.halfM = halfM;
  L.dirX = dx; L.dirY = dy;
  L.anchorLat = lat; L.anchorLon = lon;
  L.p1Lat = lat + dx*dLat; L.p1Lon = lon - dy*dLon;
  L.p2Lat = lat - dx*dLat; L.p2Lon = lon + dy*dLon;
  return L;
}

Line makeLineFromHeading(double lat, double lon, double headingDeg, double halfM){
  // cap : 0 = nord, 90 = est → vecteur (est, nord) = (sin, cos)
  return makeLineFromVector(lat, lon, sin(headingDeg * RAD), cos(headingDeg * RAD), halfM);
}

void Engine::setLines(const Line& a, const Line& b){ A_ = a; B_ = b; reset(); }

void Engine::setSplits(const double* v, int nv, const double* d, int nd){
  nSplitV_ = nv < MAX_SPLITS ? nv : MAX_SPLITS;
  nSplitD_ = nd < MAX_SPLITS ? nd : MAX_SPLITS;
  for(int i=0;i<nSplitV_;i++) splitV_[i] = v[i];
  for(int i=0;i<nSplitD_;i++) splitD_[i] = d[i];
}

void Engine::reset(){
  dsHavePrev_ = dsHaveCur_ = spHavePrev_ = spHaveCur_ = false;
  havePrev_ = false; cosL_ = 1;
  hasCross_[0] = hasCross_[1] = false;
  lapN_ = 0; bestLap_ = 0; lapArmed_ = false;
  nPend_ = 0; runN_ = 0; bestRun_ = 0;
  clean_ = 0;
}

void Engine::feed(const Point& r){
  // 1. même filtre que computeStats()
  if(!(r.fix >= 2 && fabs(r.lat) > 0.0001 && plausible(r))) return;
  stageDespike(r);
}

void Engine::flush(){ flushDespike(); flushSpeed(); }

// ---- 2. despike() en flux -------------------------------------------------
void Engine::stageDespike(const Point& p){
  if(!dsHavePrev_){ dsPrev_ = p; dsHavePrev_ = true; stageSpeed(p); return; }  // 1er point : conservé
  if(!dsHaveCur_){ dsCur_ = p; dsHaveCur_ = true; return; }
  // évaluer dsCur_ entre le dernier point CONSERVÉ et le suivant (p)
  bool drop = false;
  if(dist(dsPrev_, dsCur_) > MAX_STEP_M){
    if(dist(dsCur_, p) > MAX_STEP_M && dist(dsPrev_, p) < MAX_STEP_M) drop = true;
  }
  if(!drop){ dsPrev_ = dsCur_; stageSpeed(dsCur_); }
  dsCur_ = p;
}
void Engine::flushDespike(){
  if(dsHaveCur_){ stageSpeed(dsCur_); dsPrev_ = dsCur_; dsHaveCur_ = false; }
}

// ---- 3. despikeSpeed() en flux --------------------------------------------
void Engine::stageSpeed(const Point& p){
  if(!spHavePrev_){ spPrev_ = p; spHavePrev_ = true; process(p); return; }
  if(!spHaveCur_){ spCur_ = p; spHaveCur_ = true; return; }
  Point c = spCur_;
  const double dPrev = fabs(c.speed - spPrev_.speed);
  const double dNext = fabs(c.speed - p.speed);
  const double dSkip = fabs(p.speed - spPrev_.speed);
  if(dPrev > MAX_DV_KMH && dNext > MAX_DV_KMH && dSkip < MAX_DV_KMH)
    c.speed = (spPrev_.speed + p.speed) / 2;
  spPrev_ = c;
  process(c);
  spCur_ = p;
}
void Engine::flushSpeed(){
  if(spHaveCur_){ spPrev_ = spCur_; process(spCur_); spHaveCur_ = false; }
}

// ---- 4. franchissement ------------------------------------------------------
static inline bool segCross(double p1x, double p1y, double p2x, double p2y,
                            double p3x, double p3y, double p4x, double p4y, double& t){
  const double d = (p2x-p1x)*(p4y-p3y) - (p2y-p1y)*(p4x-p3x);
  if(fabs(d) < 1e-12) return false;
  t = ((p3x-p1x)*(p4y-p3y) - (p3y-p1y)*(p4x-p3x)) / d;
  const double u = ((p3x-p1x)*(p2y-p1y) - (p3y-p1y)*(p2x-p1x)) / d;
  return t >= 0 && t <= 1 && u >= 0 && u <= 1;
}

bool Engine::crossing(const Line& L, const Point& a, const Point& b, double& tOut) const {
  const double kx = cosL_ * 111320.0, ky = 110540.0;
  const double Ax = L.p1Lon*kx, Ay = L.p1Lat*ky, Bx = L.p2Lon*kx, By = L.p2Lat*ky;
  const double Cx = L.anchorLon*kx, Cy = L.anchorLat*ky;
  const double P1x = a.lon*kx, P1y = a.lat*ky, P2x = b.lon*kx, P2y = b.lat*ky;
  double t;
  if(!segCross(P1x, P1y, P2x, P2y, Ax, Ay, Bx, By, t)) return false;
  const double X = P1x + (P2x-P1x)*t, Y = P1y + (P2y-P1y)*t;
  if(hypot(X-Cx, Y-Cy) > L.halfM * ANCHOR_K) return false;
  const double vx = P2x-P1x, vy = P2y-P1y;
  double vn = hypot(vx, vy); if(vn == 0) vn = 1e-9;
  if((vx/vn)*L.dirX + (vy/vn)*L.dirY < DIR_MIN) return false;
  tOut = a.t + (b.t - a.t)*t;
  return true;
}

// ---- 5. chronos intermédiaires ---------------------------------------------
void Engine::splitsStep(Pending& P, const Point& p){
  const Point& q = P.last;
  for(int k=0;k<nSplitV_;k++){
    if(P.vHit[k]) continue;
    const double v = splitV_[k];
    if(p.speed >= v && q.speed < v){
      const double dv = p.speed - q.speed;
      const double f = dv > 0 ? (v - q.speed)/dv : 0;
      P.vHit[k] = true;
      Event e{EventKind::Split, 0, 0, (q.t + (p.t - q.t)*f) - P.t0, 0, 0, v};
      emit(e);
    }
  }
  double step = dist(q, p);
  if(step > MAX_STEP_M) step = 0;
  const double prevCum = P.cum, cum = P.cum + step;
  for(int k=0;k<nSplitD_;k++){
    if(P.dHit[k]) continue;
    const double d = splitD_[k];
    if(cum >= d && prevCum < d){
      const double dd = cum - prevCum;
      const double f = dd > 0 ? (d - prevCum)/dd : 0;
      P.dHit[k] = true;
      Event e{EventKind::Split, 0, 0, (q.t + (p.t - q.t)*f) - P.t0, 0, 1, d};
      emit(e);
    }
  }
  P.cum = cum;
  P.last = p;
}

void Engine::process(const Point& p){
  clean_++;
  if(!havePrev_){
    cosL_ = cos(p.lat * RAD);      // console : cos(pts[0].lat)
    prev_ = p; havePrev_ = true;
    return;
  }
  const bool drag = A_.set && B_.set;

  // Chronos intermédiaires des parcours en cours (paire prev_ → p).
  if(drag) for(int i=0;i<nPend_;i++) splitsStep(pend_[i], p);

  // Traversées de ce segment, triées par instant.
  struct C { uint8_t line; double t; } cs[2]; int nc = 0;
  double t;
  if(A_.set && crossing(A_, prev_, p, t)) cs[nc++] = {0, t};
  if(B_.set && crossing(B_, prev_, p, t)) cs[nc++] = {1, t};
  if(nc == 2 && cs[1].t < cs[0].t){ C x = cs[0]; cs[0] = cs[1]; cs[1] = x; }

  for(int k=0;k<nc;k++){
    const uint8_t li = cs[k].line; const double tc = cs[k].t;
    if(hasCross_[li] && tc - lastCross_[li] < DEDUP_S) continue;   // doublon
    hasCross_[li] = true; lastCross_[li] = tc;
    emit(Event{EventKind::Crossing, li, 0, tc, 0, 0, 0});

    if(!drag){
      // Circuit : une seule ligne (B si seule posée, sinon A).
      if(lapArmed_){
        const double lt = tc - lastLapCross_;
        lapN_++;
        if(lapN_ == 1 || lt < bestLap_) bestLap_ = lt;
        emit(Event{EventKind::Lap, li, lapN_, lt, bestLap_, 0, 0});
      }
      lapArmed_ = true; lastLapCross_ = tc;
    }else if(li == 0){
      // Départ : nouveau parcours en attente (le plus ancien est éliminé si
      // la file est pleine).
      if(nPend_ == MAX_PENDING){ memmove(pend_, pend_+1, sizeof(Pending)*(MAX_PENDING-1)); nPend_--; }
      Pending& P = pend_[nPend_++];
      memset(&P, 0, sizeof P);
      P.t0 = tc; P.active = true; P.last = p; P.cum = 0; P.n = 0;
    }else{
      // Arrivée : termine le plus ancien départ strictement antérieur.
      int j = 0;
      while(j < nPend_ && !(pend_[j].t0 < tc)) j++;
      if(j < nPend_){
        const double rt = tc - pend_[j].t0;
        runN_++;
        if(runN_ == 1 || rt < bestRun_) bestRun_ = rt;
        emit(Event{EventKind::Run, 1, runN_, rt, bestRun_, 0, 0});
        // Les départs plus anciens que celui-ci ne pourront plus jamais
        // être appariés (la console avance son index d'arrivée) : retirés.
        memmove(pend_, pend_ + j + 1, sizeof(Pending)*(nPend_ - j - 1));
        nPend_ -= j + 1;
      }
    }
  }
  prev_ = p;
}

} // namespace lap
