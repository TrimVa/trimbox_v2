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

bool Hold::apply(Sample& x){
  if(!x.fix){ held_ = false; slow_ = 0; return false; }
  const bool calm = imuCalm(x, s_);

  if(held_){
    const bool leave = x.gSpeed >= s_.exitMms || !calm ||
                       distM(aLat_, aLon_, x.lat, x.lon) > s_.maxDriftM;
    if(leave){ held_ = false; slow_ = 0; return false; }
  }else{
    slow_ = (x.gSpeed < s_.enterMms && calm) ? (slow_ < 255 ? slow_ + 1 : 255) : 0;
    if(slow_ < s_.enterEpochs) return false;
    held_ = true; n_ = 0; sLat_ = sLon_ = sH_ = sE_ = 0;
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
