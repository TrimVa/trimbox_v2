#include "recorder.h"
#include "records.h"

namespace recd {

bool Recorder::start(uint32_t now){
  if(st_ != STOPPED) return false;
  st_ = RECORDING;
  everFix_ = false; still_ = false; fast_ = 0;
  lastFixMs_ = now;
  return true;
}

bool Recorder::stop(){
  if(st_ == STOPPED) return false;
  st_ = STOPPED;
  fast_ = 0; still_ = false;
  return true;
}

Result Recorder::epoch(uint32_t now, bool fix, int32_t gSpeed){
  Result r;
  const uint8_t f = s_.flags;
  const bool slowNow = gSpeed < (int32_t)s_.statSpeed;

  // ARRÊTÉ : avec F_AUTO, la voiture qui roule démarre l'enregistrement.
  // Il faut un fix (sans lui, la vitesse ne veut rien dire) et plusieurs
  // époques de suite au-dessus du seuil : une pointe de bruit ne suffit pas.
  if(st_ == STOPPED){
    if(!(f & F_AUTO)) return r;
    if(fix && !slowNow){
      if(fast_ < 255) fast_++;
      if(fast_ >= s_.autoStartEpochs){
        st_ = RECORDING;
        everFix_ = true; still_ = false; fast_ = 0;
        lastFixMs_ = now;
        r.changed = r.storeChange = true;
        r.reason = rec::REASON_MOVING;
        r.store = true;
      }
    }else{
      fast_ = 0;
    }
    return r;
  }
  fast_ = 0;
  if(fix){ lastFixMs_ = now; everFix_ = true; }
  const bool slow = slowNow;

  if(st_ == RECORDING){
    // Pause à l'arrêt : uniquement avec un fix valide (sinon la vitesse
    // n'a pas de sens).
    if((f & F_STATIONARY) && fix && slow){
      if(!still_){ still_ = true; stillSinceMs_ = now; }
      else if(now - stillSinceMs_ >= (uint32_t)s_.statInterval * 1000u){
        // Avec F_AUTO, l'arrêt prolongé FERME la session : le roulage suivant
        // en ouvre une nouvelle. Sans elle, l'ancienne mise en pause.
        if(f & F_AUTO){
          st_ = STOPPED; pauseReason_ = rec::REASON_STATIONARY;
        }else{
          st_ = PAUSED; pausedSinceMs_ = now; pauseReason_ = rec::REASON_STATIONARY;
        }
        r.changed = r.storeChange = true; r.reason = rec::REASON_STATIONARY;
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
