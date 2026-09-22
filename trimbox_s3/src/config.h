// ============================================================================
//  TrimBox DIY S3 — configuration de compilation
//  Cahier des charges v2. Les valeurs numériques ne sont PAS indicatives
//  (v1, « Comment utiliser ce document ») : ne pas les changer sans raison.
// ============================================================================
#pragma once

// ---------------------------------------------------------------- identité
#define DEVICE_NICKNAME   "Buggy 1"          // ≤ 16 caractères (v1 §9.4)
#define BRAND             "TrimBox DIY S3"
#define DEVICE_NAME       "TrimBox " DEVICE_NICKNAME
#define FIRMWARE_VER      "2.0-a4"
#define HARDWARE_VER      "ESP32-S3-DevKitC-1 N16R8"
#define MANUFACTURER      "TrimBox DIY"
#define BUILD_STAMP       __DATE__ " " __TIME__

// ---------------------------------------------------------------- brochage
// v2 §2.3. INTERDITES : 35-37 (PSRAM octale), 19-20 (USB), 0/3/45/46 en
// sortie (configuration au démarrage). ADC : uniquement l'ADC1 (GPIO 1-10).
#define PIN_GNSS_RX       18    // ← TX du module GNSS
#define PIN_GNSS_TX       17    // → RX du module GNSS
#define PIN_GPS_EN        10    // commande d'alimentation du GNSS
#define GPS_EN_ACTIVE     HIGH  // niveau qui ALLUME le GNSS (selon le montage)
#define PIN_CRSF_RX       16    // ← TX du récepteur (sortie série du ER5C-i)
#define PIN_CRSF_TX       15    // → RX du récepteur
#define PIN_XBUS_RX        5    // ESC (réservé, décodeur après captures)
#define PIN_XBUS_TX        6
#define PIN_IMU_SDA        8
#define PIN_IMU_SCL        9
#define PIN_IMU_INT1       7
#define PIN_BUTTON         0    // bouton BOOT (actif à l'état bas)
#define PIN_LED_RGB       48    // 48 sur DevKitC-1 v1.0, 38 sur v1.1

// ---------------------------------------------------------------- UART
#ifndef TRIMBOX_QEMU
  #define GNSS_SERIAL Serial1     // UART1
  #define CRSF_SERIAL Serial2     // UART2
#else
  // Variante émulateur (tests/qemu) : QEMU n'émule que UART0 et UART1.
  // Le GNSS partage UART0 avec le journal, le CRSF prend UART1.
  #define GNSS_SERIAL Serial
  #define CRSF_SERIAL Serial1
#endif

// ---------------------------------------------------------------- GNSS
#define GNSS_BAUD         115200
#define GNSS_DYN_MODEL    7     // Airborne 2g (v1 §4.8)
#define MAX_NAVIGATION_RATE 25

// ---------------------------------------------------------------- IMU
#define ACCEL_RANGE_G     16    // ±16 g OBLIGATOIRE (v1 §9.11)
// 1 = moyenne des échantillons IMU (416 Hz) sur la période GNSS : filtre
//     anti-repliement, les vibrations du châssis ne « tombent » plus au
//     hasard sur l'échantillon retenu.
// 0 = dernier échantillon lu (comportement le plus proche d'une lecture
//     ponctuelle).
// [À VALIDER] : comparer la détection du temps en l'air (console,
// MIN_AIR_SAMPLES = 3, seuil 0,35 g) sur un même roulage dans les deux modes.
#define IMU_AVERAGE       1
// Correspondance des axes selon le montage : AXIS_X_SRC = 0/1/2 pour x/y/z
// du capteur, AXIS_X_SIGN = ±1. Par défaut : capteur à plat, x vers l'avant.
#define AXIS_X_SRC 0
#define AXIS_X_SIGN 1
#define AXIS_Y_SRC 1
#define AXIS_Y_SIGN 1
#define AXIS_Z_SRC 2
#define AXIS_Z_SIGN 1

// ---------------------------------------------------------------- CRSF
#define CRSF_LINE_CHANNEL_DEFAULT 8     // voie de pose de ligne (1..16)
#define CRSF_GPS_PERIOD_MS        200   // 5 Hz
#define CRSF_STATUS_PERIOD_MS     1000  // rappel de l'état, 1 Hz
// Messages texte (chrono, accusés) : ExpressLRS ne garde que le DERNIER
// message « mode de vol » en attente d'envoi radio. Deux messages envoyés
// coup sur coup = le premier perdu. Chaque message est donc tenu seul
// pendant CRSF_EVENT_HOLD_MS (renvoyé toutes les CRSF_EVENT_SPACING_MS),
// puis le suivant de la file prend sa place.
#define CRSF_EVENT_HOLD_MS        700
#define CRSF_EVENT_SPACING_MS     150
#define CRSF_MIN_GAP_MS           10    // au plus une trame toutes les 10 ms
#define LINE_MIN_SPEED_MMS        2000  // pose refusée sous 2 m/s (cap non fiable)

// ---------------------------------------------------------------- Wi-Fi
// Point d'accès de la console embarquée (v2 §7). Il s'allume de lui-même
// quand la voiture est arrêtée depuis WIFI_AUTO_ON_S secondes, et se coupe
// dès qu'elle roule (radio 2,4 GHz : v2 §10.2).
#define WIFI_PASS         "trimbox-rc"   // 8 caractères minimum — À PERSONNALISER
#define WIFI_AUTO_DEFAULT 1              // 1 : automatique dès le démarrage ; 0 : jamais seul
#define WIFI_AUTO_ON_S    30             // arrêt continu avant activation
#define WIFI_STILL_MMS    1389           // « à l'arrêt » : sous 5 km/h (ou sans fix)
#define WIFI_MOVE_MMS     2000           // « roule » : au-dessus de 7,2 km/h…
#define WIFI_MOVE_EPOCHS  3              // … pendant 3 solutions GNSS de suite
#define WIFI_CHANNEL      6
#define WIFI_TX_POWER     WIFI_POWER_8_5dBm
#define BUTTON_LONG_MS    3000           // appui long sur BOOT : Wi-Fi marche / arrêt

// ---------------------------------------------------------------- mise à jour
#define OTA_VALIDATE_AFTER_S 15          // fonctionnement avant de confirmer un nouveau firmware

// ---------------------------------------------------------------- sécurité
#define AUTO_RESUME_RECORDING 0   // v1 §4.5 : jamais de reprise après coupure
#define MIN_UPTIME_BEFORE_SLEEP_S 120

// ---------------------------------------------------------------- mémoire
#define TXQ_SIZE          4096
#define LIVE_RESERVE      (88 + 256)  // v1 §4.2
