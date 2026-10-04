#include "linearm.h"
#include <math.h>

namespace linearm {

static constexpr double K = 1e-7 * M_PI / 180.0;   // deg×1e7 → rad
static constexpr double R = 6371000.0;

double Armer::distM(int32_t lat1, int32_t lon1, int32_t lat2, int32_t lon2){
  const double dN = (double)(lat2 - lat1) * K * R;
  const double dE = (double)(lon2 - lon1) * K * R * cos(lat1 * K);
  return sqrt(dN * dN + dE * dE);
}

double Armer::bearingDeg(int32_t lat1, int32_t lon1, int32_t lat2, int32_t lon2){
  const double dN = (double)(lat2 - lat1) * K * R;
  const double dE = (double)(lon2 - lon1) * K * R * cos(lat1 * K);
  double b = atan2(dE, dN) * 180.0 / M_PI;
  if(b < 0) b += 360.0;
  return b;
}

void Armer::offset(int32_t& lat, int32_t& lon, double headingDeg, double m){
  const double h = headingDeg * M_PI / 180.0;
  const double dN = m * cos(h), dE = m * sin(h);
  lat += (int32_t)llround(dN / (K * R));
  lon += (int32_t)llround(dE / (K * R * cos(lat * K)));
}

void Armer::arm(Which w, int32_t lat, int32_t lon, uint32_t now){
  active_ = true; moving_ = false;
  which_ = w; aLat_ = lat; aLon_ = lon;
  armedAt_ = now; movedAt_ = 0;
}

Result Armer::update(uint32_t now, bool fix, int32_t lat, int32_t lon, int32_t gSpeed, int32_t headMot){
  Result r; r.which = which_;
  if(!active_) return r;
  const double d = fix ? distM(aLat_, aLon_, lat, lon) : 0.0;
  if(!moving_){
    if(now - armedAt_ > s_.startWindowMs){ active_ = false; r.kind = Result::Timeout; return r; }
    if(fix && (gSpeed >= s_.moveMms || d >= s_.moveDistM)){
      moving_ = true; movedAt_ = now; r.kind = Result::Moving;
    }
    return r;
  }
  if(now - movedAt_ > s_.placeWindowMs){ active_ = false; r.kind = Result::Timeout; return r; }
  if(!fix) return r;
  double head = -1;
  if(d >= s_.headDistM && gSpeed >= s_.headSpeedMms){
    head = headMot / 1e5; r.fromDoppler = true;
  }else if(d >= s_.bearingDistM){
    head = bearingDeg(aLat_, aLon_, lat, lon);
  }
  if(head < 0) return r;
  while(head >= 360.0) head -= 360.0;
  while(head < 0) head += 360.0;
  r.lat = aLat_; r.lon = aLon_;
  offset(r.lat, r.lon, head, s_.offsetM);
  r.headingE5 = (int32_t)llround(head * 1e5);
  r.kind = Result::Placed;
  active_ = false;
  return r;
}

} // namespace linearm
