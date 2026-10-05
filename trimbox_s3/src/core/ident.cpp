#include "ident.h"
#include <string.h>
#include <stdio.h>

namespace ident {

static bool nameChar(char c){
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
         c == ' ' || c == '-' || c == '_' || c == '.';
}

bool validName(const char* s){
  if(!s) return false;
  const size_t n = strlen(s);
  if(n < 1 || n > NAME_MAX) return false;
  if(s[0] == ' ' || s[n - 1] == ' ') return false;
  for(size_t i = 0; i < n; i++){
    if(!nameChar(s[i])) return false;
    if(s[i] == ' ' && i > 0 && s[i - 1] == ' ') return false;
  }
  return true;
}

bool validPass(const char* s){
  if(!s) return false;
  const size_t n = strlen(s);
  if(n < PASS_MIN || n > PASS_MAX) return false;
  for(size_t i = 0; i < n; i++) if((uint8_t)s[i] < 0x20 || (uint8_t)s[i] > 0x7E) return false;
  return true;
}

void ssidFor(const char* name, char out[33]){
  snprintf(out, 33, "TrimBox-%s", name);
  for(char* c = out; *c; c++) if(*c == ' ') *c = '-';
}

void bleNameFor(const char* name, char out[32]){
  snprintf(out, 32, "TrimBox %s", name);
}

// Copie un champ de taille fixe complété de zéros ; faux s'il n'est pas
// terminé proprement (octet non nul après le premier zéro).
static bool field(const uint8_t* p, size_t n, char* out){
  size_t len = 0;
  while(len < n && p[len]) len++;
  for(size_t i = len; i < n; i++) if(p[i]) return false;
  memcpy(out, p, len); out[len] = 0;
  return true;
}

bool decodeSet(const uint8_t* p, size_t len, Change& c){
  c = Change();
  if(!p || len != SET_BYTES) return false;
  const uint8_t m = p[0];
  if(m == 0 || (m & ~(F_NAME | F_PASS))) return false;
  if(!field(p + 1, NAME_MAX, c.name)) return false;
  if(!field(p + 1 + NAME_MAX, PASS_MAX, c.pass)) return false;
  if((m & F_NAME) && !validName(c.name)) return false;
  if((m & F_PASS) && !validPass(c.pass)) return false;
  if(!(m & F_NAME)) c.name[0] = 0;
  if(!(m & F_PASS)) c.pass[0] = 0;
  c.mask = m;
  return true;
}

void encodeSet(const Change& c, uint8_t out[SET_BYTES]){
  memset(out, 0, SET_BYTES);
  out[0] = c.mask;
  memcpy(out + 1, c.name, strnlen(c.name, NAME_MAX));
  memcpy(out + 1 + NAME_MAX, c.pass, strnlen(c.pass, PASS_MAX));
}

void encodeGet(const char* name, bool passCustom, uint8_t out[GET_BYTES]){
  memset(out, 0, GET_BYTES);
  memcpy(out, name, strnlen(name, NAME_MAX));
  out[NAME_MAX] = passCustom ? G_PASS_CUSTOM : 0;
}

} // namespace ident
