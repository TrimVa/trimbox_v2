#include "imu.h"
#include "../config.h"
#include <Arduino.h>
#include <Wire.h>

namespace imu {

// Registres communs LSM6DS3 / LSM6DS3TR-C / LSM6DSO
static constexpr uint8_t WHO_AM_I = 0x0F, CTRL1_XL = 0x10, CTRL2_G = 0x11,
                         CTRL3_C = 0x12, STATUS_REG = 0x1E, OUTX_L_G = 0x22;
static uint8_t s_addr = 0, s_who = 0;
static bool s_ok = false;
static int32_t s_sum[6]; static uint16_t s_n = 0;
static int16_t s_last[6];
static uint32_t s_total = 0, s_lastPoll = 0;
// Sensibilités : ±16 g → 0,488 mg/LSB ; ±2000 °/s → 70 m°/s/LSB.
static constexpr float MG_PER_LSB = 0.488f, CDPS_PER_LSB = 7.0f;

uint8_t address(){ return s_addr; }
uint8_t whoAmI(){ return s_who; }
uint32_t samples(){ return s_total; }

static bool wr(uint8_t reg, uint8_t v){
  Wire.beginTransmission(s_addr); Wire.write(reg); Wire.write(v);
  return Wire.endTransmission() == 0;
}
static bool rd(uint8_t reg, uint8_t* buf, uint8_t n){
  Wire.beginTransmission(s_addr); Wire.write(reg);
  if(Wire.endTransmission(false) != 0) return false;
  if(Wire.requestFrom((int)s_addr, (int)n) != n) return false;
  for(uint8_t i=0;i<n;i++) buf[i] = Wire.read();
  return true;
}

bool begin(){
  Wire.begin(PIN_IMU_SDA, PIN_IMU_SCL, 400000);
  for(uint8_t a : {(uint8_t)0x6A, (uint8_t)0x6B}){
    s_addr = a;
    uint8_t w = 0;
    if(rd(WHO_AM_I, &w, 1) && (w == 0x69 || w == 0x6A || w == 0x6C)){ s_who = w; break; }
    s_addr = 0;
  }
  if(!s_addr){ Serial.println("[imu] ABSENTE : vérifier SDA=8, SCL=9, 3,3 V, masse"); return false; }
  if(s_who == 0x6C)
    Serial.println("[imu] LSM6DSO détectée : échelle correcte pour un LSM6DSO, FAUSSE pour un LSM6DSO32");
  // CTRL1_XL : ODR 416 Hz (0110), FS ±16 g (01), filtre 400 Hz → 0x64
  // CTRL2_G  : ODR 416 Hz (0110), FS ±2000 °/s (11)         → 0x6C
  // CTRL3_C  : BDU (bloc cohérent) + incrément d'adresse    → 0x44
  s_ok = wr(CTRL3_C, 0x44) && wr(CTRL1_XL, 0x64) && wr(CTRL2_G, 0x6C);
  Serial.printf("[imu] 0x%02X à l'adresse 0x%02X, ±%d g : %s\n", s_who, s_addr, ACCEL_RANGE_G, s_ok ? "OK" : "ÉCHEC");
  return s_ok;
}

void poll(){
  if(!s_ok) return;
  const uint32_t now = micros();
  if(now - s_lastPoll < 2000) return;       // ~ 500 lectures/s au plus
  s_lastPoll = now;
  uint8_t st = 0;
  if(!rd(STATUS_REG, &st, 1) || !(st & 0x01)) return;   // XLDA : nouvel échantillon
  uint8_t b[12];
  if(!rd(OUTX_L_G, b, 12)) return;
  int16_t v[6];
  for(int i=0;i<6;i++) v[i] = (int16_t)(b[2*i] | (b[2*i+1] << 8));
  // ordre des registres : gyro x,y,z puis accél. x,y,z
  for(int i=0;i<6;i++){ s_sum[i] += v[i]; s_last[i] = v[i]; }
  s_n++; s_total++;
}

static int16_t pick(const float* src, int axis, int sign, float k){
  float x = src[axis] * sign * k;
  if(x > 32767) x = 32767; if(x < -32768) x = -32768;
  return (int16_t)lroundf(x);
}

rec::Imu take(){
  rec::Imu m{0,0,0,0,0,0};
  if(!s_ok) return m;
  float g[3], a[3];
  for(int i=0;i<3;i++){
    if(IMU_AVERAGE && s_n){ g[i] = (float)s_sum[i] / s_n; a[i] = (float)s_sum[3+i] / s_n; }
    else { g[i] = s_last[i]; a[i] = s_last[3+i]; }
  }
  m.ax = pick(a, AXIS_X_SRC, AXIS_X_SIGN, MG_PER_LSB);
  m.ay = pick(a, AXIS_Y_SRC, AXIS_Y_SIGN, MG_PER_LSB);
  m.az = pick(a, AXIS_Z_SRC, AXIS_Z_SIGN, MG_PER_LSB);
  m.gx = pick(g, AXIS_X_SRC, AXIS_X_SIGN, CDPS_PER_LSB);
  m.gy = pick(g, AXIS_Y_SRC, AXIS_Y_SIGN, CDPS_PER_LSB);
  m.gz = pick(g, AXIS_Z_SRC, AXIS_Z_SIGN, CDPS_PER_LSB);
  for(int i=0;i<6;i++) s_sum[i] = 0;
  s_n = 0;
  return m;
}

} // namespace imu
