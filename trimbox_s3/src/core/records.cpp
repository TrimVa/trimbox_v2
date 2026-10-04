#include "records.h"
#include "bytes.h"
#include <string.h>
#include <math.h>

namespace rec {

bool decodeNavPvt(const uint8_t* p, size_t len, Pvt& o){
  if(len < 84) return false;
  o.iTOW = get_le32(p+0);
  o.year = get_le16(p+4); o.month = p[6]; o.day = p[7];
  o.hour = p[8]; o.min = p[9]; o.sec = p[10]; o.valid = p[11];
  o.tAcc = get_le32(p+12); o.nano = get_le32s(p+16);
  o.fixType = p[20]; o.flags = p[21]; o.flags2 = p[22]; o.numSV = p[23];
  o.lon = get_le32s(p+24); o.lat = get_le32s(p+28);
  o.height = get_le32s(p+32); o.hMSL = get_le32s(p+36);
  o.hAcc = get_le32(p+40); o.vAcc = get_le32(p+44);
  o.velN = get_le32s(p+48); o.velE = get_le32s(p+52); o.velD = get_le32s(p+56);
  o.gSpeed = get_le32s(p+60); o.headMot = get_le32s(p+64);
  o.sAcc = get_le32(p+68); o.headAcc = get_le32(p+72);
  o.pDOP = get_le16(p+76); o.flags3 = p[78];
  return true;
}

void buildData(uint8_t o[PAYLOAD], const Pvt& p, const Imu& m, uint8_t battery, bool speed3d){
  memset(o, 0, PAYLOAD);
  put_le32(o+0, p.iTOW);
  put_le16(o+4, p.year); o[6]=p.month; o[7]=p.day; o[8]=p.hour; o[9]=p.min; o[10]=p.sec;
  o[11] = p.valid;
  put_le32(o+12, p.tAcc); put_le32(o+16, (uint32_t)p.nano);
  o[20] = p.fixType; o[21] = p.flags; o[22] = p.flags2; o[23] = p.numSV;
  put_le32(o+24, (uint32_t)p.lon); put_le32(o+28, (uint32_t)p.lat);
  put_le32(o+32, (uint32_t)p.height); put_le32(o+36, (uint32_t)p.hMSL);
  put_le32(o+40, p.hAcc); put_le32(o+44, p.vAcc);
  int32_t speed = p.gSpeed;
  if(speed3d){
    const double v = sqrt((double)p.gSpeed*p.gSpeed + (double)p.velD*p.velD);
    speed = (int32_t)(v + 0.5);
  }
  put_le32(o+48, (uint32_t)speed);
  put_le32(o+52, (uint32_t)p.headMot);
  put_le32(o+56, p.sAcc); put_le32(o+60, p.headAcc);
  put_le16(o+64, p.pDOP);
  o[66] = p.flags3 & 0x01;          // bit 0 : lat/lon/altitude invalides
  o[67] = battery;
  put_le16(o+68, (uint16_t)m.ax); put_le16(o+70, (uint16_t)m.ay); put_le16(o+72, (uint16_t)m.az);
  put_le16(o+74, (uint16_t)m.gx); put_le16(o+76, (uint16_t)m.gy); put_le16(o+78, (uint16_t)m.gz);
}

bool validData(const uint8_t d[PAYLOAD]){
  const uint8_t fixType = d[20], numSV = d[23];
  const int32_t lon = get_le32s(d+24), lat = get_le32s(d+28), gSpeed = get_le32s(d+48);
  if(fixType > 5) return false;
  if(numSV > 60) return false;
  if(lat < -900000000 || lat > 900000000) return false;
  if(lon < -1800000000 || lon > 1800000000) return false;
  if(gSpeed < 0 || gSpeed > 140000000) return false;
  return true;
}

void buildState(uint8_t o[12], uint8_t state, uint8_t reason, uint32_t iTOW, uint32_t count){
  memset(o, 0, 12);
  o[0] = state; o[1] = reason;
  put_le32(o+4, iTOW); put_le32(o+8, count);
}

double distM(double lat1, double lon1, double lat2, double lon2){
  // NE PAS « optimiser » : les écarts sont en degrés, 111320 et 110540 sont
  // des mètres PAR DEGRÉ. Convertir en radians avant diviserait tout par ~57.
  const double rad = M_PI / 180.0;
  const double dx = (lon2 - lon1) * 111320.0 * cos((lat1 + lat2) / 2.0 * rad);
  const double dy = (lat2 - lat1) * 110540.0;
  return sqrt(dx*dx + dy*dy);
}

} // namespace rec
