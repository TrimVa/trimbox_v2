#include "recorder.h"
#include "records.h"

namespace recd {

bool Recorder::start(uint32_t now){
  if(st_ != STOPPED) return false;
  st_ = RECORDING;
  everFix_ = false; still_ = false;
  lastFixMs_ = now;
  return true;
}

bool Recorder::stop(){
  if(st_ == STOPPED) return false;
  st_ = STOPPED;
  return true;
}

Result Recorder::epoch(uint32_t now, bool fix, int32_t gSpeed){
  Result r;
  if(st_ == STOPPED) return r;
  const uint8_t f = s_.flags;
  if(fix){ lastFixMs_ = now; everFix_ = true; }
  const bool slow = gSpeed < (int32_t)s_.statSpeed;

  if(st_ == RECORDING){
    // Pause à l'arrêt : uniquement avec un fix valide (sinon la vitesse
    // n'a pas de sens).
    if((f & F_STATIONARY) && fix && slow){
      if(!still_){ still_ = true; stillSinceMs_ = now; }
      else if(now - stillSinceMs_ >= (uint32_t)s_.statInterval * 1000u){
        st_ = PAUSED; pausedSinceMs_ = now; pauseReason_ = rec::REASON_STATIONARY;
        r.changed = r.storeChange = true; r.reason = pauseReason_;
        still_ = false;
        return r;
      }
    }else if(fix){
      still_ = false;
    }
    // Pause sans signal.
    if((f & F_NOFIX) && !fix && now - lastFixMs_ >= (uint32_t)s_.noFixInterval * 1000u){
      st_ = PAUSED; pausedSinceMs_ = now; pauseReason_ = rec::REASON_NOFIX;
      r.changed = r.storeChange = true; r.reason = pauseReason_;
      return r;
    }
    r.store = !(f & F_WAIT_FIX) || everFix_;
    return r;
  }

  // PAUSED
  const bool resume = fix && (pauseReason_ != rec::REASON_STATIONARY || !slow);
  if(resume){
    // La reprise est NOTIFIÉE mais PAS stockée (v1 §4.6).
    st_ = RECORDING; still_ = false;
    r.changed = true; r.storeChange = false;
    r.reason = pauseReason_ == rec::REASON_STATIONARY ? rec::REASON_MOVING : rec::REASON_FIX;
    r.store = true;
    return r;
  }
  if((f & F_AUTOOFF) && now - pausedSinceMs_ >= (uint32_t)s_.autoOffInterval * 1000u){
    if(!((f & F_WAIT_DATA) && stored_ == 0)) r.powerOff = true;
  }
  return r;
}

} // namespace recd
