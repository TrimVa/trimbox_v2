// ============================================================================
//  TrimBox — firmware de TEST de la centrale inertielle (IMU)
//
//  Croquis autonome pour ESP32-S3, à flasher à la place du firmware TrimBox
//  le temps du diagnostic. Moniteur série à 115200 bauds.
//
//  1. état électrique des lignes SDA / SCL (résistances de tirage, court-circuit)
//  2. recherche de tout appareil I2C sur SDA = GPIO 8, SCL = GPIO 9,
//     puis avec SDA et SCL inversés (erreur de câblage fréquente)
//  3. identification de la puce (LSM6DS3, LSM6DS3TR-C, LSM6DSO, QMI8658,
//     MPU-6050/6500, BMI160…) : les modules vendus « LSM6DS3 » ne portent
//     pas toujours cette puce
//  4. si c'est une LSM6 : réglage identique au firmware et affichage continu
//     des accélérations (g) et rotations (°/s), 5 fois par seconde
//
//  Touches : s = relancer le diagnostic, p = pause/reprise de l'affichage
// ============================================================================
#include <Arduino.h>
#include <Wire.h>

static const int SDA_PIN = 8, SCL_PIN = 9;     // câblage TrimBox (config.h)
static int g_sda = SDA_PIN, g_scl = SCL_PIN;
static uint8_t g_addr = 0, g_who = 0;
static bool g_lsm = false, g_pause = false;

static bool rd(uint8_t addr, uint8_t reg, uint8_t* b, uint8_t n){
  Wire.beginTransmission(addr); Wire.write(reg);
  if(Wire.endTransmission(false) != 0) return false;
  if(Wire.requestFrom((int)addr, (int)n) != n) return false;
  for(uint8_t i = 0; i < n; i++) b[i] = Wire.read();
  return true;
}
static bool wr(uint8_t addr, uint8_t reg, uint8_t v){
  Wire.beginTransmission(addr); Wire.write(reg); Wire.write(v);
  return Wire.endTransmission() == 0;
}

// Niveau des lignes au repos, sans aucune résistance interne : doit être HAUT
// (le module porte normalement ses propres résistances de tirage).
static void lignes(){
  Wire.end();
  pinMode(SDA_PIN, INPUT); pinMode(SCL_PIN, INPUT);
  delay(5);
  const int sda = digitalRead(SDA_PIN), scl = digitalRead(SCL_PIN);
  Serial.printf("  lignes au repos : SDA (GPIO %d) = %s, SCL (GPIO %d) = %s\n",
                SDA_PIN, sda ? "HAUT" : "BAS", SCL_PIN, scl ? "HAUT" : "BAS");
  if(!sda || !scl)
    Serial.println("  !! ligne au BAS : pas de résistance de tirage sur le module, fil à la masse,\n"
                   "     ou module non alimenté (vérifier 3V3 et GND). Le test continue avec les\n"
                   "     résistances internes de l'ESP32 (faibles, mais suffisantes à 100 kHz).");
}

static int scan(int sda, int scl, uint8_t* found, int max){
  Wire.end();
  pinMode(sda, INPUT_PULLUP); pinMode(scl, INPUT_PULLUP);
  Wire.begin(sda, scl, 100000);
  int n = 0;
  for(uint8_t a = 0x08; a < 0x78; a++){
    Wire.beginTransmission(a);
    if(Wire.endTransmission() == 0 && n < max) found[n++] = a;
  }
  return n;
}

static const char* nomPuce(uint8_t addr, uint8_t& who){
  uint8_t v = 0;
  if(addr == 0x6A || addr == 0x6B){
    if(rd(addr, 0x0F, &v, 1)){
      who = v;
      switch(v){
        case 0x69: return "LSM6DS3 / LSM6DS33 (ST)";
        case 0x6A: return "LSM6DS3TR-C (ST)";
        case 0x6C: return "LSM6DSO / LSM6DSOX / LSM6DSO32 (ST)";
        case 0x6B: return "LSM6DSR / ISM330DHCX (ST)";
      }
    }
    if(rd(addr, 0x00, &v, 1) && v == 0x05){ who = v; return "QMI8658 (QST) — PAS une LSM6 : pilote différent"; }
  }
  if(addr == 0x68 || addr == 0x69){
    if(rd(addr, 0x75, &v, 1)){
      who = v;
      switch(v){
        case 0x68: return "MPU-6050 (InvenSense) — PAS une LSM6";
        case 0x70: return "MPU-6500 — PAS une LSM6";
        case 0x71: return "MPU-9250 — PAS une LSM6";
        case 0x98: return "ICM-20689 — PAS une LSM6";
        case 0x12: return "ICM-20602 — PAS une LSM6";
      }
    }
    if(rd(addr, 0x00, &v, 1)){
      who = v;
      if(v == 0xD1) return "BMI160 (Bosch) — PAS une LSM6";
      if(v == 0x24) return "BMI270 (Bosch) — PAS une LSM6";
      if(v == 0xEA) return "ICM-20948 — PAS une LSM6";
    }
  }
  who = v;
  return "inconnue";
}

static void diagnostic(){
  g_lsm = false; g_addr = 0;
  Serial.println("\n=========== TEST IMU — TrimBox ===========");
  Serial.println("1) état électrique");
  lignes();

  Serial.printf("2) recherche I2C (SDA = GPIO %d, SCL = GPIO %d)\n", SDA_PIN, SCL_PIN);
  uint8_t f[16];
  int n = scan(SDA_PIN, SCL_PIN, f, 16);
  g_sda = SDA_PIN; g_scl = SCL_PIN;
  if(!n){
    Serial.println("  aucun appareil. Essai avec SDA et SCL inversés…");
    n = scan(SCL_PIN, SDA_PIN, f, 16);
    if(n){
      g_sda = SCL_PIN; g_scl = SDA_PIN;
      Serial.printf("  !! trouvé en INVERSANT : SDA du module est sur GPIO %d, SCL sur GPIO %d.\n"
                    "     → croiser les deux fils (SDA module → GPIO 8, SCL module → GPIO 9).\n", g_sda, g_scl);
    }
  }
  if(!n){
    Serial.println("  !! AUCUN appareil I2C.\n"
                   "     Vérifier : VCC module → 3V3 (pas 5V), GND commun, SDA → GPIO 8, SCL → GPIO 9,\n"
                   "     broche CS du module reliée à 3V3 (sinon la puce peut rester en mode SPI),\n"
                   "     soudures, et que le module n'est pas mort (5 V appliqué par erreur ?).");
    return;
  }
  for(int i = 0; i < n; i++){
    uint8_t who = 0;
    const char* nom = nomPuce(f[i], who);
    Serial.printf("  appareil à l'adresse 0x%02X — identifiant 0x%02X — %s\n", f[i], who, nom);
    if(!g_lsm && (f[i] == 0x6A || f[i] == 0x6B) && (who == 0x69 || who == 0x6A || who == 0x6C || who == 0x6B)){
      g_lsm = true; g_addr = f[i]; g_who = who;
    }
  }
  if(!g_lsm){
    Serial.println("  !! aucune puce LSM6 : le firmware TrimBox ne sait pas lire cette centrale.\n"
                   "     Envoie ces lignes : on adaptera le pilote à la puce trouvée.");
    return;
  }
  Serial.println("3) réglage (identique au firmware : 416 Hz, ±16 g, ±2000 °/s)");
  Wire.setClock(400000);
  const bool ok = wr(g_addr, 0x12, 0x44) && wr(g_addr, 0x10, 0x64) && wr(g_addr, 0x11, 0x6C);
  Serial.printf("  écriture des registres : %s\n", ok ? "OK" : "ÉCHEC");
  if(g_who == 0x6C) Serial.println("  note : si c'est une LSM6DSO32, l'échelle réelle est ±32 g (valeurs ×2).");
  Serial.println("4) mesures — module posé à plat, composants vers le haut : |a| ≈ 1,00 g, az ≈ +1,00 g");
  Serial.println("       ax      ay      az   |a| (g)  |    gx      gy      gz (°/s)  | temp");
  delay(100);
}

void setup(){
  Serial.begin(115200);
  delay(1500);
  diagnostic();
}

void loop(){
  if(Serial.available()){
    const char c = Serial.read();
    if(c == 's') diagnostic();
    if(c == 'p') g_pause = !g_pause;
  }
  static uint32_t t = 0;
  if(!g_lsm || g_pause || millis() - t < 200) return;
  t = millis();
  uint8_t st = 0, b[14];
  if(!rd(g_addr, 0x1E, &st, 1)){ Serial.println("  !! lecture impossible : liaison I2C perdue"); return; }
  if(!(st & 0x01)){ Serial.printf("  !! pas de nouvel échantillon (STATUS = 0x%02X) : puce pas démarrée ?\n", st); return; }
  if(!rd(g_addr, 0x20, b, 14)){ Serial.println("  !! lecture des mesures impossible"); return; }
  int16_t v[7];
  for(int i = 0; i < 7; i++) v[i] = (int16_t)(b[2*i] | (b[2*i + 1] << 8));
  const float temp = 25.0f + v[0] / (g_who == 0x69 ? 16.0f : 256.0f);
  float g[3], a[3];
  for(int i = 0; i < 3; i++){ g[i] = v[1 + i] * 0.070f; a[i] = v[4 + i] * 0.000488f; }
  const float n = sqrtf(a[0]*a[0] + a[1]*a[1] + a[2]*a[2]);
  Serial.printf("  %+6.2f  %+6.2f  %+6.2f   %5.2f    | %+7.1f %+7.1f %+7.1f       | %4.1f °C%s\n",
                a[0], a[1], a[2], n, g[0], g[1], g[2], temp,
                (n < 0.8f || n > 1.2f) ? "   <- |a| anormal à l'arrêt" : "");
}
