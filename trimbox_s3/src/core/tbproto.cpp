#include "tbproto.h"
#include "checksums.h"
#include <string.h>

namespace tb {

size_t build(uint8_t* out, uint8_t cls, uint8_t id, const uint8_t* payload, uint16_t len){
  out[0] = SYNC1; out[1] = SYNC2; out[2] = cls; out[3] = id;
  out[4] = (uint8_t)len; out[5] = (uint8_t)(len >> 8);
  if(len) memcpy(out + HEADER, payload, len);
  Fletcher8 ck; ck.add(out + 2, 4 + len);
  out[HEADER + len] = ck.a; out[HEADER + len + 1] = ck.b;
  return len + OVERHEAD;
}

bool Parser::lengthPlausible(uint8_t cls, uint8_t id, uint16_t len) const {
  if(len > MAX_PAYLOAD) return false;
  if(!strict_ || cls != CLS) return true;
  switch(id){           // longueurs admises pour une COMMANDE reçue
    case ID_STATUS: case ID_ERASE: case ID_BUILD:  return len == 0;
    case ID_DOWNLOAD:  return len == 0 || len == 1;
    case ID_CONFIG:    return len == 0 || len == 12;
    case ID_GNSSCFG:   return len == 0 || len == 3;
    case ID_UNLOCK:    return len == 4;
    case ID_LINES:     return len == 0 || len == 28;
    case ID_WIFI:      return len == 0 || len == 1;
    default:           return len <= 64;   // inconnu : sera refusé par NACK
  }
}

size_t Parser::push(const uint8_t* data, size_t n){
  size_t room = sizeof(buf_) - n_;
  if(n > room){            // ne devrait pas arriver : on repart proprement
    n_ = 0; bad_++;
    room = sizeof(buf_);
    if(n > room) n = room;
  }
  memcpy(buf_ + n_, data, n);
  n_ += n;
  return n;
}

bool Parser::next(Frame& f){
  size_t i = 0;
  bool found = false;
  while(true){
    while(i + 1 < n_ && !(buf_[i] == SYNC1 && buf_[i+1] == SYNC2)) i++;
    if(i + HEADER > n_) break;
    const uint8_t cls = buf_[i+2], id = buf_[i+3];
    const uint16_t len = (uint16_t)(buf_[i+4] | (buf_[i+5] << 8));
    if(!lengthPlausible(cls, id, len)){ i += 2; bad_++; continue; }
    if(i + len + OVERHEAD > n_) break;               // trame incomplète
    Fletcher8 ck; ck.add(buf_ + i + 2, 4 + len);
    if(ck.a == buf_[i + HEADER + len] && ck.b == buf_[i + HEADER + len + 1]){
      f.cls = cls; f.id = id; f.len = len;
      memcpy(f.payload, buf_ + i + HEADER, len);
      i += len + OVERHEAD;
      found = true;
      break;
    }
    // Somme fausse : la longueur annoncée n'est PAS fiable → +2 seulement.
    bad_++;
    i += 2;
  }
  // Conserve le reste non traité en tête de tampon.
  if(i > n_) i = n_;
  memmove(buf_, buf_ + i, n_ - i);
  n_ -= i;
  return found;
}

} // namespace tb
