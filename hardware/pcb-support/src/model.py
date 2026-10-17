# ============================================================================
#  PCB support TrimBox — modèle unique (circuit, placement, routage).
#  Tout le reste (schéma KiCad, PCB KiCad, Gerber, BOM/CPL, contrôles, rendus)
#  est déduit de ce fichier.
#
#  Repère : face COMPOSANTS du PCB support (face avant, F.Cu), qui regarde vers
#  le bas une fois monté. La carte ESP32 est posée sur l'autre face (B.Cu) :
#  vue d'ici, c'est sa face arrière, sérigraphie « IOxx » lisible.
#  Origine : coin haut-gauche du contour de la carte ESP32 (antenne en haut,
#  USB-C en bas) ; x vers la droite, y vers le bas ; millimètres.
# ============================================================================
from footprints import load, esp_header, xform

# ---------------------------------------------------------------- géométrie
# [À MESURER] valeurs estimées d'après la photo ; à remplacer par les cotes
# relevées au pied à coulisse.
GEO = dict(
    W=23.4, H=32.6,          # contour de la carte ESP32 (sans la prise USB-C)
    Y0=5.1,                  # axe de la 1re rangée de trous depuis le bord haut
    PITCH=2.54,
    XL_OUT=1.54, XL_IN=4.08,     # colonnes gauche (vue de dessous)
    XR_IN=19.32, XR_OUT=21.86,   # colonnes droite (vue de dessous)
    PAD=1.7, DRILL=1.0,
    YCUT=3.6,                # le PCB support s'arrête ici : rien sous l'antenne
)

# Correspondance trou ↔ sérigraphie (vue de dessous). Hypothèse A : sur chaque
# rangée, les deux noms sérigraphiés se lisent dans l'ordre des trous de gauche
# à droite. [À CONFIRMER] par le test BOOT ↔ trou extérieur (IO0).
MAPPING = dict(
    right={  # rangée : (colonne intérieure, colonne extérieure)
        1: ('EN', 'IO0'), 2: ('IO8', 'IO7'), 3: ('IO10', 'IO9'), 4: ('IO12', 'IO11'),
        5: ('IO14', 'IO13'), 6: ('IO16', 'IO15'), 7: ('IO18', 'IO17'), 8: ('IO26', 'IO21'),
        9: ('3V3', '3V3'), 10: ('GND', 'GND'), 11: ('VIN', 'VIN'),
    },
    left={   # rangée : (colonne extérieure, colonne intérieure) — tenue mécanique seulement
        1: ('IO3', 'IO1'), 2: ('IO2', 'IO4'), 11: ('IO47', 'IO48'),
    },
)

# ---------------------------------------------------------------- composants
class Comp:
    def __init__(self, ref, value, fp, x, y, rot, nets, lcsc='', mpn='', descr='', dnp=False, sym=None):
        self.ref, self.value, self.fpname = ref, value, fp
        self.x, self.y, self.rot = x, y, rot
        self.nets, self.lcsc, self.mpn, self.descr, self.dnp = nets, lcsc, mpn, descr, dnp
        self.sym = sym
        self.fp = None

    def pads_abs(self):
        """[(pad, x, y, angle)] en coordonnées carte."""
        out = []
        for p in self.fp.pads:
            x, y = xform(self.x, self.y, self.rot, p.x, p.y)
            out.append((p, x, y, (self.rot + p.rot) % 360))
        return out


ESP_NETS = {'VIN': '+5V', '3V3': '+3V3', 'GND': 'GND', 'IO8': 'SDA', 'IO9': 'SCL',
            'IO7': 'IMU_INT1', 'IO16': 'ELRS_TX', 'IO15': 'ELRS_RX', 'IO18': 'GPS_TX', 'IO17': 'GPS_RX'}

COMPS = [
    # --- carte ESP32 (non fournie par JLCPCB : picots soudés à la main)
    Comp('U3', 'ESP32-S3 Mini Nano N16R8', None, 0, 0, 0, ESP_NETS, dnp=True,
         descr='Carte NiceMCU ESP32-S3_MINI_Nano N16R8, picots 2,54 mm', sym='ESP'),
    # --- connecteurs (sortie des câbles par le bord gauche)
    # J1 : 1 masse, 2 + BEC, 3 → RX du récepteur (IO15), 4 ← TX du récepteur (IO16), 5 masse
    Comp('J1', 'RX / BEC', 'JST_SH_SM05B-SRSS-TB_1x05-1MP_P1.00mm_Horizontal', 3.1, 15.5, 270,
         {'1': 'GND', '2': 'VBEC', '3': 'ELRS_RX', '4': 'ELRS_TX', '5': 'GND', 'MP': 'GND'},
         'C136657', 'SM05B-SRSS-TB(LF)(SN)', 'JST SH 1,0 mm 5 broches, sortie latérale', sym='CONN5'),
    # J2 : 1 masse, 2 + 5 V, 3 ← TX du GPS (IO18), 4 → RX du GPS (IO17) — ordre G V T R du M100 Mini
    Comp('J2', 'GPS', 'JST_SH_SM04B-SRSS-TB_1x04-1MP_P1.00mm_Horizontal', 3.1, 25.0, 270,
         {'1': 'GND', '2': '+5V', '3': 'GPS_TX', '4': 'GPS_RX', 'MP': 'GND'},
         'C160404', 'SM04B-SRSS-TB(LF)(SN)', 'JST SH 1,0 mm 4 broches, sortie latérale', sym='CONN4'),
    # --- alimentation 5 V (TPS563201, 4,5-17 V → 5,07 V)
    Comp('U1', 'TPS563201', 'SOT-23-6', 12.2, 25.4, 90,
         {'1': 'GND', '2': 'SW', '3': 'VBEC', '4': 'FB', '5': 'VBEC', '6': 'BST'},
         'C116592', 'TPS563201DDCR', 'Abaisseur synchrone 3 A, 4,5-17 V', sym='TPS'),
    Comp('L1', '3,3 µH', 'L_Taiyo-Yuden_NR-30xx', 12.2, 29.6, 90, {'1': 'V5_BUCK', '2': 'SW'},
         'C167749', 'FNR3015S3R3MT', 'Inductance 3,3 µH 1,6 A 3×3 mm', sym='L'),
    Comp('C1', '22 µF 25 V', 'C_0805_2012Metric', 15.0, 26.6, 90, {'1': 'GND', '2': 'VBEC'},
         'C45783', 'CL21A226MAQNNNE', 'Entrée régulateur (≈ 10 µF effectifs sous 8,4 V)', sym='C'),
    Comp('C2', '100 nF 50 V', 'C_0603_1608Metric', 16.9, 26.6, 90, {'1': 'GND', '2': 'VBEC'},
         'C14663', 'CC0603KRX7R9BB104', 'Entrée régulateur (HF)', sym='C'),
    Comp('C3', '100 nF', 'C_0402_1005Metric', 9.9, 25.6, 90, {'1': 'SW', '2': 'BST'},
         'C1525', 'CL05B104KO5NNNC', 'Amorçage (VBST-SW)', sym='C'),
    Comp('R1', '56 kΩ 1 %', 'R_0402_1005Metric', 16.3, 23.4, 0, {'1': 'FB', '2': 'V5_BUCK'},
         'C25796', '0402WGF5602TCE', 'Pont de contre-réaction (haut)', sym='R'),
    Comp('R2', '10 kΩ 1 %', 'R_0402_1005Metric', 14.6, 22.9, 90, {'1': 'GND', '2': 'FB'},
         'C25744', '0402WGF1002TCE', 'Pont de contre-réaction (bas)', sym='R'),
    Comp('C4', '22 µF 25 V', 'C_0805_2012Metric', 9.4, 30.0, 90, {'1': 'GND', '2': 'V5_BUCK'},
         'C45783', 'CL21A226MAQNNNE', 'Sortie régulateur', sym='C'),
    Comp('C5', '22 µF 25 V', 'C_0805_2012Metric', 16.2, 29.4, 0, {'1': 'V5_BUCK', '2': 'GND'},
         'C45783', 'CL21A226MAQNNNE', 'Sortie régulateur', sym='C'),
    Comp('D1', 'B5819W', 'D_SOD-123', 16.4, 31.65, 180, {'1': '+5V', '2': 'V5_BUCK'},
         'C8598', 'B5819W', 'Schottky : empêche l’USB d’alimenter le BEC', sym='D'),
    Comp('C8', '10 µF 25 V', 'C_0805_2012Metric', 6.8, 30.6, 90, {'1': '+5V', '2': 'GND'},
         'C15850', 'CL21A106KAYNNNE', '5 V du GPS et de la carte', sym='C'),
    # --- centrale inertielle
    Comp('U2', 'LSM6DS3TR-C', 'LGA-14_3x2.5mm_P0.5mm_LayoutBorder3x4y', 12.0, 8.6, 270,
         {'1': 'GND', '2': 'GND', '3': 'GND', '4': 'IMU_INT1', '5': '+3V3', '6': 'GND', '7': 'GND',
          '8': '+3V3', '12': '+3V3', '13': 'SCL', '14': 'SDA'},
         'C967633', 'LSM6DS3TR-C', 'Centrale inertielle ±16 g / ±2000 °/s, I2C 0x6A', sym='IMU'),
    Comp('C6', '100 nF', 'C_0402_1005Metric', 9.6, 9.9, 90, {'1': 'GND', '2': '+3V3'},
         'C1525', 'CL05B104KO5NNNC', 'Découplage VDDIO', sym='C'),
    Comp('C7', '100 nF', 'C_0402_1005Metric', 11.2, 11.3, 0, {'1': '+3V3', '2': 'GND'},
         'C1525', 'CL05B104KO5NNNC', 'Découplage VDD', sym='C'),
    Comp('R3', '4,7 kΩ', 'R_0402_1005Metric', 16.0, 12.0, 0, {'1': '+3V3', '2': 'SDA'},
         'C25900', '0402WGF4701TCE', 'Tirage I2C SDA', sym='R'),
    Comp('R4', '4,7 kΩ', 'R_0402_1005Metric', 16.0, 13.2, 0, {'1': '+3V3', '2': 'SCL'},
         'C25900', '0402WGF4701TCE', 'Tirage I2C SCL', sym='R'),
]


def build():
    for c in COMPS:
        c.fp = esp_header(GEO, MAPPING) if c.ref == 'U3' else load(c.fpname)
        if c.ref == 'U3':
            c.fpname = c.fp.name
        for n in c.nets:
            assert any(p.num == n for p in c.fp.pads), (c.ref, n)
    return COMPS


def nets():
    s = set()
    for c in COMPS:
        s.update(c.nets.values())
    return sorted(s, key=lambda n: (n != 'GND', n))


# Routage (rempli plus loin) : pistes (couche, largeur, réseau, [(x, y), …]) et vias.
TRACKS = []
VIAS = []
