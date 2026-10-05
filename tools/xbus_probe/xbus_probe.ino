// ============================================================================
//  TrimBox — sonde de la prise secondaire d'un variateur (« X-Bus »)
//
//  Croquis AUTONOME pour ESP32-S3 : il ne fait rien d'autre qu'interroger la
//  prise de données du variateur et afficher tout ce qui en revient.
//  À flasher à la place du firmware TrimBox le temps des essais.
//
//  CÂBLAGE (prise secondaire du variateur, 3 fils)
//    − (noir/marron)  ──────────────── GND de l'ESP32   ← indispensable
//    + (rouge)        ──── NE PAS BRANCHER sur l'ESP32 (le variateur
//                          l'alimente déjà ; l'ESP32 est alimenté en USB)
//    signal (orange)  ──┬── 1 kΩ ───── GPIO 5  (émission)
//                       └── voir ci-dessous ─ GPIO 4  (réception)
//
//    Réception : si le fil signal est à 3,3 V au repos, relie-le directement
//    à GPIO 4. S'il est à 5 V, passe par un pont diviseur : 10 kΩ entre le
//    fil et GPIO 4, puis 20 kΩ entre GPIO 4 et GND. Les entrées de l'ESP32
//    ne supportent PAS le 5 V.
//    Mesure la tension au repos AVANT de brancher quoi que ce soit.
//
//  UTILISATION : ouvrir le moniteur série à 115200, puis taper une lettre.
//    h : poignée de main SRXL2 (115 200 puis 400 000 bauds), toutes cibles
//    c : SRXL2 — poignée de main puis trames de commande répétées
//    e : écoute passive, en balayant les vitesses usuelles
//    a : mesure de la vitesse d'après la plus courte impulsion reçue
//    b : envoi d'un « break » puis écoute (réveille certains bus)
//    ? : rappel du menu
// ============================================================================
#include <Arduino.h>

static const int PIN_TX = 5;      // vers le fil signal, par 1 kΩ
static const int PIN_RX = 4;      // depuis le fil signal (pont si 5 V)

HardwareSerial BUS(1);

// Un seul fil : tout ce que l'on émet revient dans notre propre entrée. On
// garde la trace des octets envoyés pour ne pas les prendre pour une réponse.
static uint8_t s_echo[256];
static int s_echoN = 0;
static void noteSent(const uint8_t* p, int n){
  for(int i = 0; i < n && s_echoN < (int)sizeof s_echo; i++) s_echo[s_echoN++] = p[i];
}
static bool isEcho(uint8_t b){
  if(!s_echoN) return false;
  if(s_echo[0] != b){ s_echoN = 0; return false; }   // désynchronisé : on abandonne le filtre
  memmove(s_echo, s_echo + 1, --s_echoN);
  return true;
}
static void sendBus(const uint8_t* p, int n){
  while(BUS.available()) BUS.read();
  s_echoN = 0;
  noteSent(p, n);
  BUS.write(p, n);
  BUS.flush();
}

static const uint32_t BAUDS[] = {500000, 400000, 250000, 115200, 1000000, 460800, 100000, 57600, 38400, 19200, 9600};
static const uint8_t  DESTS[] = {0x00, 0xFF, 0x40, 0x41, 0x42, 0x43, 0x21, 0x30, 0x10, 0x60, 0xB0};

// ---------------------------------------------------------------- utilitaires
static uint16_t crc16(const uint8_t* p, uint8_t n){      // CCITT, poly 0x1021, init 0
  uint16_t c = 0;
  while(n--){
    c ^= (uint16_t)(*p++) << 8;
    for(int i = 0; i < 8; i++) c = (c & 0x8000) ? (uint16_t)((c << 1) ^ 0x1021) : (uint16_t)(c << 1);
  }
  return c;
}

static void openBus(uint32_t baud){
  BUS.end();
  BUS.begin(baud, SERIAL_8N1, PIN_RX, PIN_TX);
  BUS.setRxBufferSize(1024);
  delay(5);
  while(BUS.available()) BUS.read();       // on jette l'écho et les restes
}

// Affiche ce qui arrive pendant `ms`. Renvoie le nombre d'octets reçus.
static int listen(uint32_t ms, const char* tag){
  const uint32_t t0 = millis();
  int n = 0;
  uint8_t line = 0;
  while(millis() - t0 < ms){
    while(BUS.available()){
      const uint8_t b = (uint8_t)BUS.read();
      if(isEcho(b)) continue;                 // notre propre émission
      if(!n) Serial.printf("  %s REPONSE :", tag);
      if(line == 16){ Serial.printf("\n            "); line = 0; }
      Serial.printf(" %02X", b);
      n++; line++;
      if(n >= 512){ Serial.printf(" …"); ms = 0; break; }
    }
    delay(1);
  }
  if(n) Serial.printf("\n  → %d octet(s)\n", n);
  return n;
}

// ---------------------------------------------------------------- SRXL2
// Trame : A6 | type | longueur | charge utile | CRC (2 octets, poids fort
// d'abord). La poignée de main fait 14 octets au total.
static void srxlHandshake(uint8_t dest, uint8_t baudCode){
  uint8_t f[14];
  f[0] = 0xA6; f[1] = 0x21; f[2] = 14;
  f[3] = 0x30;              // nous : « contrôleur de vol »
  f[4] = dest;
  f[5] = 0x10;              // priorité
  f[6] = baudCode;          // 0 = 115200, 1 = 400000
  f[7] = 0x00;              // pas de télémétrie à relayer
  const uint32_t uid = 0x54424F58;          // « TBOX »
  f[8] = uid & 0xFF; f[9] = (uid >> 8) & 0xFF; f[10] = (uid >> 16) & 0xFF; f[11] = (uid >> 24) & 0xFF;
  const uint16_t c = crc16(f, 12);
  f[12] = c >> 8; f[13] = c & 0xFF;
  sendBus(f, sizeof f);
}

// Trame de commande SRXL2 (type 0xCD) : le variateur n'envoie sa télémétrie
// qu'une fois la liaison établie et alimentée en trames de commande.
static void srxlControl(uint16_t throttle){
  uint8_t f[22];
  f[0] = 0xA6; f[1] = 0xCD; f[2] = 22;
  f[3] = 0x00;              // commande : données de voies
  f[4] = 0xFF;              // demande de réponse (« reply ID » diffusé)
  f[5] = 0x00;              // RSSI
  f[6] = 0x00; f[7] = 0x00; // compteur de trames perdues
  const uint32_t mask = 0x0000000F;          // 4 voies
  f[8] = mask & 0xFF; f[9] = (mask >> 8) & 0xFF; f[10] = (mask >> 16) & 0xFF; f[11] = (mask >> 24) & 0xFF;
  for(int i = 0; i < 4; i++){                // 4 voies sur 16 bits
    const uint16_t v = (i == 0) ? throttle : 0x8000;
    f[12 + 2 * i] = v & 0xFF; f[13 + 2 * i] = v >> 8;
  }
  const uint16_t c = crc16(f, 20);
  f[20] = c >> 8; f[21] = c & 0xFF;
  sendBus(f, sizeof f);
}

static void cmdHandshake(){
  for(uint8_t bi = 0; bi < 2; bi++){
    const uint32_t baud = bi ? 400000 : 115200;
    Serial.printf("\n== poignée de main SRXL2 à %lu bauds\n", (unsigned long)baud);
    openBus(baud);
    for(uint8_t d : DESTS){
      Serial.printf("  → cible 0x%02X\n", d);
      srxlHandshake(d, bi);
      delay(2);
      if(listen(60, "") == 0) Serial.println("    (pas de reponse)");
    }
  }
  Serial.println("== fin\n");
}

static void cmdControl(){
  Serial.println("\n== liaison SRXL2 puis trames de commande (5 s)");
  openBus(115200);
  for(uint8_t d : DESTS){ srxlHandshake(d, 0); delay(10); }
  srxlHandshake(0xFF, 0);                    // diffusion : fin de négociation
  const uint32_t t0 = millis();
  int total = 0;
  while(millis() - t0 < 5000){
    srxlControl(0x8000);                     // gaz au neutre
    total += listen(20, "bus");
  }
  if(total) Serial.printf("== fin : %d octet(s) VENANT DU VARIATEUR\n\n", total);
  else      Serial.println("== fin : rien d'autre que notre propre echo — le variateur n'a pas repondu\n");
}

static void cmdEcoute(){
  Serial.println("\n== écoute passive");
  for(uint32_t b : BAUDS){
    openBus(b);
    Serial.printf("  %lu bauds…\n", (unsigned long)b);
    if(listen(1500, "bus") == 0) Serial.println("    (rien)");
  }
  Serial.println("== fin\n");
}

// Mesure des impulsions : la plus courte donne la vitesse, mais seule la
// RÉPARTITION des durées dit si c'est une vraie liaison série (largeurs
// multiples d'un même bit) ou du bruit (largeurs quelconques).
static const int MAXE = 4000;
static volatile uint32_t s_us[MAXE];
static volatile int s_n = 0;
static volatile uint32_t s_last = 0, s_minUs = 0xFFFFFFFF, s_edges = 0;
static void IRAM_ATTR onEdge(){
  const uint32_t now = micros();
  const uint32_t d = now - s_last;
  s_last = now;
  if(s_edges && d > 1 && d < s_minUs) s_minUs = d;
  s_edges = s_edges + 1;
  const int i = s_n;
  if(i < MAXE){ s_us[i] = d; s_n = i + 1; }
}

static void mesure(uint32_t ms, bool detail){
  BUS.end();
  pinMode(PIN_RX, INPUT);
  s_minUs = 0xFFFFFFFF; s_edges = 0; s_n = 0; s_last = micros();
  attachInterrupt(PIN_RX, onEdge, CHANGE);
  delay(ms);
  detachInterrupt(PIN_RX);
  if(s_edges < 2){ Serial.println("  aucun front : la ligne ne bouge pas\n"); return; }
  Serial.printf("  %lu fronts en %lu ms (%lu /s), plus courte impulsion %lu us -> ~%lu bauds\n",
                (unsigned long)s_edges, (unsigned long)ms, (unsigned long)(s_edges * 1000UL / ms),
                (unsigned long)s_minUs, (unsigned long)(1000000UL / s_minUs));
  // Répartition : une liaison série ne donne que des multiples du temps bit.
  static const uint32_t B[] = {2, 3, 4, 6, 8, 12, 16, 24, 40, 80, 200, 1000, 10000, 0xFFFFFFFF};
  int cnt[15] = {0};
  const int n = s_n;
  for(int i = 1; i < n; i++){
    for(int k = 0; k < 14; k++) if(s_us[i] <= B[k]){ cnt[k]++; break; }
  }
  Serial.println("  repartition des durees (us) :");
  uint32_t lo = 0;
  for(int k = 0; k < 14; k++){
    if(cnt[k]) Serial.printf("    %6lu - %-6lu : %d\n", (unsigned long)lo,
                             (unsigned long)(B[k] == 0xFFFFFFFF ? 999999 : B[k]), cnt[k]);
    lo = B[k];
  }
  if(detail){
    Serial.printf("  %d premieres durees (us), dans l'ordre :\n   ", n < 200 ? n : 200);
    for(int i = 1; i < n && i < 200; i++){
      Serial.printf(" %lu", (unsigned long)s_us[i]);
      if(i % 20 == 0) Serial.printf("\n   ");
    }
    Serial.println();
  }
  Serial.println();
}

static void cmdAuto(){
  Serial.println("\n== mesure des impulsions (10 s) — fais parler le variateur maintenant");
  mesure(10000, false);
}

static void cmdRaw(){
  Serial.println("\n== releve detaille (2 s)");
  mesure(2000, true);
}

static void cmdFlottant(){
  Serial.println("\n== test du fil debranche : DEBRANCHE le fil signal de GPIO 4, garde la masse,");
  Serial.println("   puis attends 5 s. Si des fronts apparaissent quand meme, ce n'est que du bruit.");
  delay(5000);
  mesure(3000, false);
}

// Le fil arrive-t-il vraiment jusqu'au variateur ? On tire la ligne au bas,
// on relâche, et on regarde si quelque chose la remonte : seule une
// résistance de tirage côté variateur peut le faire.
static void cmdLiaison(){
  Serial.println("\n== test de liaison (tirage de la ligne)");
  BUS.end();
  pinMode(PIN_RX, INPUT);
  // niveau au repos
  int haut = 0;
  for(int i = 0; i < 1000; i++){ haut += digitalRead(PIN_RX); delayMicroseconds(200); }
  Serial.printf("  au repos : %d %% du temps en haut\n", haut / 10);
  // on force au bas, puis on relâche
  pinMode(PIN_TX, OUTPUT); digitalWrite(PIN_TX, LOW);
  delay(2);
  const int bas = digitalRead(PIN_RX);
  pinMode(PIN_TX, INPUT);
  uint32_t t0 = micros(), dt = 0;
  while(micros() - t0 < 5000){ if(digitalRead(PIN_RX)){ dt = micros() - t0; break; } }
  Serial.printf("  pendant le tirage : %s\n", bas ? "reste en HAUT (fil coupe cote emission ?)" : "passe au BAS (bon)");
  if(dt) Serial.printf("  remonte en %lu us apres relachement : il y a bien un tirage cote variateur\n\n", (unsigned long)dt);
  else   Serial.println("  ne remonte pas : rien ne tire la ligne — le fil n'atteint probablement pas le variateur\n");
  openBus(115200);
}

static void cmdBreak(){
  Serial.println("\n== break puis écoute");
  for(uint32_t b : (const uint32_t[]){115200, 400000}){
    openBus(b);
    pinMode(PIN_TX, OUTPUT);
    digitalWrite(PIN_TX, LOW); delay(5); digitalWrite(PIN_TX, HIGH);   // break long
    openBus(b);
    Serial.printf("  %lu bauds…\n", (unsigned long)b);
    if(listen(500, "bus") == 0) Serial.println("    (rien)");
  }
  Serial.println("== fin\n");
}

static void menu(){
  Serial.println("\n--- sonde X-Bus TrimBox ---");
  Serial.println("  h : poignée de main SRXL2 (toutes cibles, 2 vitesses)");
  Serial.println("  c : liaison SRXL2 + trames de commande pendant 5 s");
  Serial.println("  e : écoute passive, toutes vitesses");
  Serial.println("  a : mesure des impulsions (10 s) + repartition des durees");
  Serial.println("  r : releve detaille des 200 premieres durees (2 s)");
  Serial.println("  f : test du fil debranche (distingue le bruit d'une vraie liaison)");
  Serial.println("  b : break puis écoute");
  Serial.println("  p : test de liaison (le fil atteint-il le variateur ?)");
  Serial.println("  ? : ce menu\n");
}

void setup(){
  Serial.begin(115200);
  delay(1500);
  Serial.println("\nSonde de la prise de données du variateur — ESP32-S3");
  Serial.printf("TX GPIO %d (par 1 kΩ), RX GPIO %d, masse commune obligatoire\n", PIN_TX, PIN_RX);
  openBus(115200);
  menu();
}

void loop(){
  if(Serial.available()){
    switch(Serial.read()){
      case 'h': cmdHandshake(); break;
      case 'c': cmdControl();   break;
      case 'e': cmdEcoute();    break;
      case 'a': cmdAuto();      break;
      case 'r': cmdRaw();       break;
      case 'f': cmdFlottant();  break;
      case 'b': cmdBreak();     break;
      case 'p': cmdLiaison();   break;
      case '?': menu();         break;
      default: break;
    }
  }
  // Hors commande, on affiche quand même tout ce qui passerait.
  listen(50, "bus");
}
