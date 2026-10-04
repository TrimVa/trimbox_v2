#include "linecmd.h"

namespace linecmd {

void Decoder::tick(uint32_t now){
  if(armed_ && now - last_ > TIMEOUT_MS){ armed_ = false; st_ = Wait; }
}

Output Decoder::update(int v, uint32_t now){
  Output o;
  last_ = now;
  const bool neutral = v >= -NEUTRAL && v <= NEUTRAL;
  if(!armed_){
    if(neutral){ armed_ = true; st_ = Neutral; }
    return o;
  }
  switch(st_){
    case Wait:
      if(neutral) st_ = Neutral;
      break;
    case Neutral:
      if(v > ACTIVE){ st_ = High; since_ = now; fired_ = false; }
      else if(v < -ACTIVE){ st_ = Low; since_ = now; fired_ = false; }
      break;
    case High:
      if(v <= NEUTRAL){ st_ = neutral ? Neutral : Wait; break; }
      if(!fired_ && now - since_ >= HOLD_MS){ fired_ = true; o.cmd = Cmd::PoseStart; o.atMs = since_; }
      break;
    case Low:
      if(v >= -NEUTRAL){
        const uint32_t held = now - since_;
        if(!fired_ && held >= HOLD_MS){ o.cmd = Cmd::PoseFinish; o.atMs = since_; }
        st_ = neutral ? Neutral : Wait;
        break;
      }
      if(!fired_ && now - since_ >= CLEAR_MS){ fired_ = true; o.cmd = Cmd::Clear; o.atMs = now; }
      break;
  }
  return o;
}

} // namespace linecmd
