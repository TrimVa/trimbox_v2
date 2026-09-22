#include "ubx.h"
#include "checksums.h"
#include "bytes.h"
#include <string.h>

namespace ubx {

size_t build(uint8_t* out, uint8_t cls, uint8_t id, const uint8_t* payload, uint16_t len){
  out[0] = SYNC1; out[1] = SYNC2; out[2] = cls; out[3] = id;
  put_le16(out+4, len);
  if(len) memcpy(out+6, payload, len);
  Fletcher8 ck; ck.add(out+2, 4 + len);
  out[6+len] = ck.a; out[7+len] = ck.b;
  return len + 8;
}

ValSet::ValSet(){
  payload_[0] = 0x00;   // version
  payload_[1] = 0x01;   // couches : RAM
  payload_[2] = 0; payload_[3] = 0;
  len_ = 4;
}

void ValSet::add(uint32_t key, uint32_t value){
  static const uint8_t sizes[8] = {0, 1, 1, 2, 4, 8, 0, 0};
  const uint8_t n = sizes[(key >> 28) & 0x07];
  if(!n || n > 4 || len_ + 4 + n > sizeof(payload_)) return;
  put_le32(payload_ + len_, key); len_ += 4;
  for(uint8_t i=0;i<n;i++) payload_[len_++] = (uint8_t)(value >> (8*i));
  nkeys_++;
}

size_t ValSet::finish(uint8_t* out, size_t cap){
  if(cap < len_ + 8) return 0;
  return build(out, CLS_CFG, ID_CFG_VALSET, payload_, (uint16_t)len_);
}

bool Parser::feed(uint8_t b){
  switch(st_){
    case S1:  if(b == SYNC1) st_ = S2; break;
    case S2:  st_ = (b == SYNC2) ? CLS : (b == SYNC1 ? S2 : S1); break;
    case CLS: cls_ = b; ckA_ = b; ckB_ = b; st_ = ID; break;
    case ID:  id_ = b; ckA_ += b; ckB_ += ckA_; st_ = L1; break;
    case L1:  len_ = b; ckA_ += b; ckB_ += ckA_; st_ = L2; break;
    case L2:
      len_ |= (uint16_t)b << 8; ckA_ += b; ckB_ += ckA_;
      pos_ = 0;
      if(len_ > sizeof(buf_)){ st_ = S1; bad_++; }
      else st_ = len_ ? PAY : CKA;
      break;
    case PAY:
      buf_[pos_++] = b; ckA_ += b; ckB_ += ckA_;
      if(pos_ >= len_) st_ = CKA;
      break;
    case CKA: rxA_ = b; st_ = CKB; break;
    case CKB:
      st_ = S1;
      if(rxA_ == ckA_ && b == ckB_) return true;
      bad_++;
      break;
  }
  return false;
}

uint16_t measPeriodMs(uint8_t dataRate){
  switch(dataRate){
    case 0: return 40;    // 25 Hz
    case 1: return 100;   // 10 Hz
    case 2: return 200;   //  5 Hz
    case 3: return 1000;  //  1 Hz
    case 4: return 50;    // 20 Hz
    default: return 40;
  }
}

} // namespace ubx
