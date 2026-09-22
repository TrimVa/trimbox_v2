#include "radio.h"
#include "../config.h"
#include <Arduino.h>
#include <string.h>

namespace radio {

static HardwareSerial& S = CRSF_SERIAL;
static crsf::Parser s_parser;
static linecmd::Decoder s_cmd;
static Stats s_st = {};
static uint8_t s_ch = CRSF_LINE_CHANNEL_DEFAULT;

static rec::Pvt s_gps; static bool s_haveGps = false;
static uint32_t s_lastGpsTx = 0, s_lastStatusTx = 0, s_lastTx = 0, s_lastRc = 0;
static uint32_t s_rateT0 = 0, s_rateN = 0;
static char s_status[16] = "S STOP";

// File d'événements texte, traitée UN PAR UN (voir CRSF_EVENT_HOLD_MS).
struct Ev { char text[16]; uint32_t until, next; };
static constexpr uint8_t EV_MAX = 8;
static Ev s_ev[EV_MAX]; static uint8_t s_nEv = 0;

const Stats& stats(){ return s_st; }
void setLineChannel(uint8_t ch){ if(ch >= 1 && ch <= 16) s_ch = ch; }

void begin(uint8_t lineChannel){
  setLineChannel(lineChannel);
  S.setRxBufferSize(2048);
  S.begin(crsf::BAUD, SERIAL_8N1, PIN_CRSF_RX, PIN_CRSF_TX);
}

void setGps(const rec::Pvt& p){ s_gps = p; s_haveGps = true; }

void event(const char* text){
  if(s_nEv == EV_MAX){ memmove(s_ev, s_ev + 1, sizeof(Ev) * (EV_MAX - 1)); s_nEv--; }
  Ev& e = s_ev[s_nEv++];
  strncpy(e.text, text, 15); e.text[15] = 0;
  e.until = 0; e.next = 0;
}

void status(const char* text){ strncpy(s_status, text, 15); s_status[15] = 0; }

// Une trame de télémétrie au plus, juste après une trame de voies reçue :
// le récepteur est alors prêt, et le débit reste borné par celui des voies.
static void sendTelemetry(uint32_t now){
  if(now - s_lastTx < CRSF_MIN_GAP_MS) return;
  uint8_t f[crsf::MAX_FRAME]; size_t n = 0;

  // 1. événement en tête de file : tenu seul pendant CRSF_EVENT_HOLD_MS
  if(s_nEv){
    Ev& e = s_ev[0];
    if(!e.until) e.until = now + CRSF_EVENT_HOLD_MS;
    if((int32_t)(now - e.until) >= 0){
      memmove(s_ev, s_ev + 1, sizeof(Ev) * (s_nEv - 1)); s_nEv--;
    }else if((int32_t)(now - e.next) >= 0){
      n = crsf::buildFlightMode(f, e.text);
      e.next = now + CRSF_EVENT_SPACING_MS;
    }
  }
  // 2. position GPS à 5 Hz
  if(!n && s_haveGps && now - s_lastGpsTx >= CRSF_GPS_PERIOD_MS){
    const rec::Pvt& p = s_gps;
    const bool fix = p.fixType >= 3 && p.gnssFixOK();
    n = crsf::buildGps(f, fix ? p.lat : 0, fix ? p.lon : 0, fix && p.gSpeed > 0 ? (uint32_t)p.gSpeed : 0,
                       p.headMot, p.hMSL, p.numSV);
    s_lastGpsTx = now;
  }
  // 3. rappel d'état à 1 Hz (si aucun événement n'attend)
  if(!n && s_nEv == 0 && now - s_lastStatusTx >= CRSF_STATUS_PERIOD_MS){
    n = crsf::buildFlightMode(f, s_status);
    s_lastStatusTx = now;
  }
  if(n){ S.write(f, n); s_lastTx = now; s_st.txFrames++; }
}

linecmd::Output poll(){
  linecmd::Output out;
  const uint32_t now = millis();
  int budget = 256;
  while(S.available() && budget--){
    if(!s_parser.feed((uint8_t)S.read())) continue;
    const uint8_t t = s_parser.type();
    if(t == crsf::T_RC_CHANNELS){
      uint16_t ch[16];
      if(crsf::decodeChannels(s_parser.payload(), s_parser.payloadLen(), ch)){
        s_st.rcFrames++; s_rateN++; s_lastRc = now;
        const linecmd::Output o = s_cmd.update(crsf::channelPercent(ch[s_ch - 1]), now);
        if(o.cmd != linecmd::Cmd::None) out = o;
        sendTelemetry(now);
      }
    }else if(t == crsf::T_LINK_STATS){
      s_st.haveLink = crsf::decodeLinkStats(s_parser.payload(), s_parser.payloadLen(), s_st.link);
    }
  }
  s_st.badCrc = s_parser.badCrc();
  s_cmd.tick(now);
  s_st.linkUp = s_lastRc && now - s_lastRc < 500;
  if(now - s_rateT0 >= 2000){
    s_st.rcRateHz = s_rateN * 1000.0f / (now - s_rateT0);
    s_rateT0 = now; s_rateN = 0;
  }
  return out;
}

} // namespace radio
