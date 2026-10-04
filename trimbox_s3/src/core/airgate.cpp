#include "airgate.h"

namespace airgate {

void Motion::epoch(bool fix, int32_t gSpeedMms){
  const bool movingEpoch = fix && gSpeedMms >= moveMms;
  moveCnt = movingEpoch ? (moveCnt < 255 ? moveCnt + 1 : 255) : 0;
  moving = moveCnt >= moveEpochs;
  still = !(fix && gSpeedMms >= stillMms);
}

Act Gate::step(uint32_t now, bool stale, bool moving, bool still, bool isOn, bool autoOn){
  if(!stale && moving){                       // roule : coupure IMMÉDIATE, sans condition
    since_ = 0;
    return isOn ? Act::Off : Act::None;
  }
  if(stale || still){ if(!since_) since_ = now ? now : 1; }
  else since_ = 0;                            // entre 5 et 7,2 km/h : on attend
  if(autoOn && !isOn && since_ && now - since_ >= onAfter_) return Act::On;
  return Act::None;
}

} // namespace airgate
