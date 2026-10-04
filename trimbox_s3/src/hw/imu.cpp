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

static uint32_t s_nextTry = 0;
static uint8_t s_tries = 0;

// Recherche et réglage. Sonde d'abord à 100 kHz (tolérant aux câbles longs
// et aux résistances de tirage faibles), puis passe à 400 kHz.
static bool probe(bool verbose){
  Wire.end();
  pinMode(PIN_IMU_SDA, INPUT_PULLUP); pinMode(PIN_IMU_SCL, INPUT_PULLUP);
  Wire.begin(PIN_IMU_SDA, PIN_IMU_SCL, 100000);
  Wire.setTimeOut(20);
  s_addr = 0; s_who = 0;
  uint8_t seen = 0, seenWho = 0;
  for(uint8_t a : {(uint8_t)0x6A, (uint8_t)0x6B}){
    s_addr = a;
    uint8_t w = 0;
    if(rd(WHO_AM_I, &w, 1)){
      seen = a; seenWho = w;
      // 0x69 LSM6DS3, 0x6A LSM6DS3TR-C, 0x6B LSM6DSR/ISM330, 0x6C LSM6DSO
      if(w == 0x69 || w == 0x6A || w == 0x6B || w == 0x6C){ s_who = w; break; }
    }
    s_addr = 0;
  }
  if(!s_addr){
    if(verbose){
      if(seen) Serial.printf("[imu] appareil à 0x%02X mais identifiant 0x%02X inconnu : puce non LSM6 ?\n", seen, seenWho);
      else Serial.println("[imu] ABSENTE : vérifier SDA=8, SCL=9, 3,3 V, masse (nouvel essai toutes les 2 s)");
    }
    return false;
  }
  if(s_who == 0x6C && verbose)
    Serial.println("[imu] LSM6DSO détectée : échelle correcte pour un LSM6DSO, FAUSSE pour un LSM6DSO32");
  // Redémarrage logiciel de la puce (SW_RESET), puis réglage :
  // CTRL1_XL : ODR 416 Hz (0110), FS ±16 g (01), filtre 400 Hz → 0x64
  // CTRL2_G  : ODR 416 Hz (0110), FS ±2000 °/s (11)         → 0x6C
  // CTRL3_C  : BDU (bloc cohérent) + incrément d'adresse    → 0x44
  wr(CTRL3_C, 0x01);
  delay(20);
  s_ok = wr(CTRL3_C, 0x44) && wr(CTRL1_XL, 0x64) && wr(CTRL2_G, 0x6C);
  // Relecture : les registres doivent contenir ce qu'on y a écrit.
  uint8_t c1 = 0, c2 = 0;
  s_ok = s_ok && rd(CTRL1_XL, &c1, 1) && rd(CTRL2_G, &c2, 1) && c1 == 0x64 && c2 == 0x6C;
  Wire.setClock(400000);
  Serial.printf("[imu] 0x%02X à l'adresse 0x%02X, ±%d g : %s\n", s_who, s_addr, ACCEL_RANGE_G, s_ok ? "OK" : "ÉCHEC du réglage");
  return s_ok;
}

bool begin(){
  for(int i = 0; i < 5 && !s_ok; i++){
    if(probe(i == 4)) break;
    delay(100);                  // la puce peut démarrer après l'ESP32
  }
  s_nextTry = millis() + 2000;
  return s_ok;
}

bool ok(){ return s_ok; }

void poll(){
  if(!s_ok){
    // Centrale absente au démarrage (branchée ou alimentée plus tard) :
    // nouvel essai toutes les 2 s, message une fois sur 15.
    if((int32_t)(millis() - s_nextTry) >= 0){
      s_nextTry = millis() + 2000;
      probe(++s_tries % 15 == 1);
    }
    return;
  }
  const uint32_t now = micros();
  if(now - s_lastPoll < 2000) return;       // ~ 500 lectures/s au plus
  s_lastPoll = now;
  uint8_t st = 0;
  if(!rd(STATUS_REG, &st, 1)){
    static uint16_t fails = 0;
    if(++fails >= 200){ fails = 0; s_ok = false; Serial.println("[imu] liaison perdue : nouvelle recherche"); }
    return;
  }
  if(!(st & 0x01)) return;                               // XLDA : nouvel échantillon
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
