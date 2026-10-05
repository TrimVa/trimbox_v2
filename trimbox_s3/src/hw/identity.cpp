#include "identity.h"
#include "../config.h"
#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

namespace identity {

static const char* NS = "trimbox";

void load(Ident& id){
  strncpy(id.name, DEVICE_NICKNAME, sizeof id.name); id.name[sizeof id.name - 1] = 0;
  strncpy(id.pass, WIFI_PASS, sizeof id.pass);       id.pass[sizeof id.pass - 1] = 0;
  Preferences p;
  if(p.begin(NS, true)){                  // lecture seule ; absent au premier démarrage
    char b[ident::PASS_MAX + 1];
    if(p.getString("name", b, sizeof b) && ident::validName(b)) strcpy(id.name, b);
    if(p.getString("pass", b, sizeof b) && ident::validPass(b)) strcpy(id.pass, b);
    p.end();
  }
  id.passCustom = strcmp(id.pass, WIFI_PASS) != 0;
}

bool save(const ident::Change& c){
  Preferences p;
  if(!p.begin(NS, false)) return false;
  bool ok = true;
  if(c.mask & ident::F_NAME) ok = ok && p.putString("name", c.name) == strlen(c.name);
  if(c.mask & ident::F_PASS) ok = ok && p.putString("pass", c.pass) == strlen(c.pass);
  p.end();
  return ok;
}

bool reset(){
  Preferences p;
  if(!p.begin(NS, false)) return false;
  const bool ok = p.clear();
  p.end();
  return ok;
}

} // namespace identity
