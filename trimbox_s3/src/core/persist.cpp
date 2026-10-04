#include "persist.h"
#include "bytes.h"
#include "checksums.h"
#include <string.h>

namespace persist {

void encode(const RecConfig& c, uint8_t o[REC_BYTES]){
  memset(o, 0, REC_BYTES);
  put_le32(o+0, REC_MAGIC); o[4] = REC_VERSION;
  o[5] = c.enabled; o[6] = c.dataRate; o[7] = c.flags;
  put_le16(o+8, c.statSpeed); put_le16(o+10, c.statInterval);
  put_le16(o+12, c.noFixInterval); put_le16(o+14, c.autoOffInterval);
  o[16] = c.gnssDynModel; o[17] = c.gnss3dSpeed; o[18] = c.gnssMinAcc; o[19] = 0;
  put_le32(o+20, c.seq);
  put_le32(o+24, crc32_ieee(o, 24));
}

bool decode(const uint8_t i[REC_BYTES], RecConfig& c){
  if(get_le32(i) != REC_MAGIC || i[4] != REC_VERSION) return false;
  if(get_le32(i+24) != crc32_ieee(i, 24)) return false;
  c.enabled = i[5]; c.dataRate = i[6]; c.flags = i[7];
  c.statSpeed = get_le16(i+8); c.statInterval = get_le16(i+10);
  c.noFixInterval = get_le16(i+12); c.autoOffInterval = get_le16(i+14);
  c.gnssDynModel = i[16]; c.gnss3dSpeed = i[17]; c.gnssMinAcc = i[18];
  c.seq = get_le32(i+20);
  return true;
}

void encodeLinesPayload(const LineConfig& c, uint8_t o[28]){
  o[0] = LINE_VERSION; o[1] = c.mode; o[2] = c.crsfChannel; o[3] = 0;
  put_le32(o+4,  (uint32_t)c.startLat);  put_le32(o+8,  (uint32_t)c.startLon);
  put_le32(o+12, (uint32_t)c.startHeading);
  put_le32(o+16, (uint32_t)c.finishLat); put_le32(o+20, (uint32_t)c.finishLon);
  put_le32(o+24, (uint32_t)c.finishHeading);
}
void decodeLinesPayload(const uint8_t i[28], LineConfig& c){
  c.mode = i[1]; c.crsfChannel = (i[2] >= 1 && i[2] <= 16) ? i[2] : 8;
  c.startLat  = get_le32s(i+4);  c.startLon  = get_le32s(i+8);  c.startHeading  = get_le32s(i+12);
  c.finishLat = get_le32s(i+16); c.finishLon = get_le32s(i+20); c.finishHeading = get_le32s(i+24);
}

void encode(const LineConfig& c, uint8_t o[LINE_BYTES]){
  memset(o, 0, LINE_BYTES);
  put_le32(o, LINE_MAGIC);
  encodeLinesPayload(c, o+4);
  put_le32(o+32, c.seq);
  put_le32(o+36, crc32_ieee(o, 36));
}
bool decode(const uint8_t i[LINE_BYTES], LineConfig& c){
  if(get_le32(i) != LINE_MAGIC || i[4] != LINE_VERSION) return false;
  if(get_le32(i+36) != crc32_ieee(i, 36)) return false;
  decodeLinesPayload(i+4, c);
  c.seq = get_le32(i+32);
  return true;
}

int newest(bool aValid, uint32_t aSeq, bool bValid, uint32_t bSeq){
  if(aValid && bValid) return (int32_t)(bSeq - aSeq) > 0 ? 1 : 0;
  if(aValid) return 0;
  if(bValid) return 1;
  return -1;
}

} // namespace persist
