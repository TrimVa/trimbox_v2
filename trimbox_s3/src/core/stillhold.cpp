#include "stillhold.h"
#include <math.h>

namespace still {

bool Hold::imuCalm(const Sample& x, const Settings& s){
  if(!x.imuOk) return true;                         // pas d'IMU : on s'en remet au GNSS
  const double a = sqrt((double)x.ax * x.ax + (double)x.ay * x.ay + (double)x.az * x.az);
  const double g = sqrt((double)x.gx * x.gx + (double)x.gy * x.gy + (double)x.gz * x.gz);
  return fabs(a - 1000.0) <= s.accelTolMg && g <= s.gyroTolCdps;
}

double Hold::distM(int32_t lat1, int32_t lon1, int32_t lat2, int32_t lon2){
  const double k = 1e-7 * M_PI / 180.0;
  const double dN = (lat2 - lat1) * k * 6371000.0;
  const double dE = (lon2 - lon1) * k * 6371000.0 * cos(lat1 * k);
  return sqrt(dN * dN + dE * dE);
}

static int32_t clampThr(int32_t base, uint32_t acc, int k, int32_t cap){
  int64_t t = (int64_t)acc * k;
  if(t < base) t = base;
  if(t > cap) t = cap;
  return (int32_t)(t < base ? base : t);
}
int32_t Hold::enterThr(const Sample& x, const Settings& s){ return clampThr(s.enterMms, x.sAcc, 2, s.enterMaxMms); }
int32_t Hold::exitThr(const Sample& x, const Settings& s){ return clampThr(s.exitMms, x.sAcc, 3, s.exitMaxMms); }

bool Hold::apply(Sample& x){
  if(!x.fix){ release(); return false; }

  const bool useImu = x.imuOk && x.act.ok;
  // Vitesse GNSS significative : au-dessus du seuil de sortie (relevé par sAcc)
  const bool gnssFast = x.gSpeed >= exitThr(x, s_);
  // Éloignement : 4 m, ou 3 × hAcc si le GNSS annonce moins bien
  double drift = s_.maxDriftM;
  if(x.hAcc && x.hAcc * 3e-3 > drift) drift = x.hAcc * 3e-3;

  if(useImu){
    const bool quiet = x.act.accStdMg <= s_.quietAccMg && x.act.gyroStdCdps <= s_.quietGyroCdps;
    const bool moving = x.act.accStdMg > s_.moveAccMg || x.act.gyroStdCdps > s_.moveGyroCdps;
    if(quiet){ if(!quiet_){ quiet_ = true; quietSince_ = x.tMs; } }
    else quiet_ = false;
    busy_ = moving ? (busy_ < 255 ? busy_ + 1 : 255) : 0;
    // garde-fou : le GNSS annonce longtemps une vraie vitesse
    const bool over = x.gSpeed >= s_.overrideMms && x.gSpeed >= (int64_t)x.sAcc * 3;
    if(over){ if(!fast_){ fast_ = true; fastSince_ = x.tMs; } }
    else fast_ = false;
    const bool overLong = fast_ && x.tMs - fastSince_ >= s_.overrideMs;

    if(held_){
      const bool leave = busy_ >= s_.exitImuEpochs || (moving && gnssFast) || overLong ||
                         distM(aLat_, aLon_, x.lat, x.lon) > drift;
      if(leave){ release(); return false; }
    }else{
      if(!(quiet_ && x.tMs - quietSince_ >= s_.enterImuMs) || over) return false;
      held_ = true; n_ = 0; sLat_ = sLon_ = sH_ = sE_ = 0;
    }
  }else{
    const bool calm = imuCalm(x, s_);
    if(held_){
      const bool leave = gnssFast || !calm || distM(aLat_, aLon_, x.lat, x.lon) > drift;
      if(leave){ release(); return false; }
    }else{
      slow_ = (x.gSpeed < enterThr(x, s_) && calm) ? (slow_ < 255 ? slow_ + 1 : 255) : 0;
      if(slow_ < s_.enterEpochs) return false;
      held_ = true; n_ = 0; sLat_ = sLon_ = sH_ = sE_ = 0;
    }
  }

  // Ancrage : moyenne glissante des premières positions à l'arrêt, puis figé.
  if(n_ < s_.anchorAvg){
    n_++;
    sLat_ += x.lat; sLon_ += x.lon; sH_ += x.hMSL; sE_ += x.height;
    aLat_ = (int32_t)llround(sLat_ / n_); aLon_ = (int32_t)llround(sLon_ / n_);
    aH_   = (int32_t)llround(sH_ / n_);   aE_   = (int32_t)llround(sE_ / n_);
  }
  x.lat = aLat_; x.lon = aLon_; x.hMSL = aH_; x.height = aE_;
  x.gSpeed = 0;
  return true;
}

} // namespace still
