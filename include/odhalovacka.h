#ifndef ODHALOVACKA_H
#define ODHALOVACKA_H

// ===== ODHALOVAČKA =====
// Každá hádanka = jeden obrázek 8×8 pixelů + textová odpověď.
// Obrázek je 8 bajtů: každý bajt = jeden řádek (8 pixelů), bit 1 = černý pixel.
// Formát přidání nové hádanky (stačí zkopírovat blok níže):
//   1) deklaruj statickou bitmapu:  static const unsigned char odhal_XXX[] PROGMEM = { 8 bajtů };
//   2) přidej řádek do pole odhalovackaData:  { odhal_XXX, "ODPOVED" },
// Pozn.: odpověď piš velkými písmeny BEZ diakritiky (kvůli fontu displeje).

struct OdhalObrazek {
  const unsigned char* bitmap;  // 8 bajtů (8×8)
  const char* odpoved;          // text odpovědi
};

// --- Bitmapy (8×8) ---
// SRDCE
// . # # . . # # .
// # # # # # # # #
// # # # # # # # #
// # # # # # # # #
// . # # # # # # .
// . . # # # # . .
// . . . # # . . .
// . . . . . . . .
static const unsigned char odhal_srdce[] PROGMEM = {
  0x66, 0xFF, 0xFF, 0xFF, 0x7E, 0x3C, 0x18, 0x00
};

// SLUNCE
// # . . . . . . #
// . . # # # # . .
// . # # # # # # .
// . # # # # # # .
// . # # # # # # .
// . # # # # # # .
// . . # # # # . .
// # . . . . . . #
static const unsigned char odhal_slunce[] PROGMEM = {
  0x81, 0x3C, 0x7E, 0x7E, 0x7E, 0x7E, 0x3C, 0x81
};

// MESIC
// . . . # # # # .
// . . # # # # . .
// . . # # # # . .
// . . # # # # . .
// . . # # # # . .
// . . # # # # # .
// . . . . . . . .
// . . . . . . . .
static const unsigned char odhal_mesic[] PROGMEM = {
  0x1E, 0x3C, 0x3C, 0x3C, 0x3C, 0x3E, 0x00, 0x00
};

// DOMEK
// . . # # # # . .
// . # # # # # # .
// # # # # # # # #
// # # # # # # # #
// # # # # # # # #
// # # . . . . # #
// # # . . . . # #
// # # # # # # # #
static const unsigned char odhal_domek[] PROGMEM = {
  0x3C, 0x7E, 0xFF, 0xFF, 0xFF, 0xC3, 0xC3, 0xFF
};

// STROM
// . . # # # # . .
// . # # # # # # .
// # # # # # # # #
// # # # # # # # #
// . . # # # # . .
// . . # # # # . .
// . . . # # . . .
// . . . # # . . .
static const unsigned char odhal_strom[] PROGMEM = {
  0x3C, 0x7E, 0xFF, 0xFF, 0x3C, 0x3C, 0x18, 0x18
};

// SNEHULAK
// . . # # # # . .
// . # # # # # # .
// . # # # # # # .
// . # # # # # # .
// # # # # # # # #
// # # # # # # # #
// . # # # # # # .
// . . . . . . . .
static const unsigned char odhal_snehulak[] PROGMEM = {
  0x3C, 0x7E, 0x7E, 0x7E, 0xFF, 0xFF, 0x7E, 0x00
};

// LOD
// . . . # . . . .
// . . . # . . . .
// . . # # # . . .
// . # # # # # . .
// # # # # # # # #
// . # # # # # # .
// . . # # # # . .
// . . . . . . . .
static const unsigned char odhal_lod[] PROGMEM = {
  0x08, 0x08, 0x38, 0x7C, 0xFF, 0x7E, 0x3C, 0x00
};

// RYBA
// . . . . . . # .
// . . . . . # # #
// . . . # # # # #
// . # # # # # # #
// . # # # # # # #
// . . . # # # # #
// . . . . . # # #
// . . . . . . # .
static const unsigned char odhal_ryba[] PROGMEM = {
  0x02, 0x07, 0x1F, 0x7F, 0x7F, 0x1F, 0x07, 0x02
};

// HRNEK
// # # # # # # # #
// # # # # # # # #
// # # # # # # # #
// # # # # # # # #
// . # # # # # # .
// . # # # # # # .
// . . # # # # . .
// . . . . . . . .
static const unsigned char odhal_hrnek[] PROGMEM = {
  0xFF, 0xFF, 0xFF, 0xFF, 0x7E, 0x7E, 0x3C, 0x00
};

// KVETINA
// . . . # . . . .
// . # # # # # . .
// # # # # # # # .
// . # # # # # . .
// . . # # # . . .
// . . # # # . . .
// . . . # . . . .
// . . . # . . . .
static const unsigned char odhal_kvetina[] PROGMEM = {
  0x08, 0x7C, 0xFE, 0x7C, 0x38, 0x38, 0x10, 0x10
};

// AUTO
// . . . . . . . .
// . # # # # # # .
// # # # # # # # #
// # # # # # # # #
// # # # # # # # #
// # # # # # # # #
// . # . . . . # .
// . # # # # # # .
static const unsigned char odhal_auto[] PROGMEM = {
  0x00, 0x7E, 0xFF, 0xFF, 0xFF, 0xFF, 0x42, 0x7E
};

// HVEZDA
// . . . # # . . .
// . . . # # . . .
// # # # # # # # #
// . # # # # # # .
// . . # # # # . .
// . . # # # # . .
// . . . # # . . .
// . . . . . . . .
static const unsigned char odhal_hvezda[] PROGMEM = {
  0x18, 0x18, 0xFF, 0x7E, 0x3C, 0x3C, 0x18, 0x00
};

// --- Seznam hádanek ---
const OdhalObrazek odhalovackaData[] PROGMEM = {
  { odhal_slunce,   "SLUNCE"   },
  { odhal_srdce,    "SRDCE"    },
  { odhal_mesic,    "MESIC"    },
  { odhal_domek,    "DOMEK"    },
  { odhal_strom,    "STROM"    },
  { odhal_snehulak, "SNEHULAK" },
  { odhal_lod,      "LOD"      },
  { odhal_ryba,     "RYBA"     },
  { odhal_hrnek,    "HRNEK"    },
  { odhal_kvetina,  "KVETINA"  },
  { odhal_auto,     "AUTO"     },
  { odhal_hvezda,   "HVEZDA"   },
};

#define ODHALOVACKA_POCET (sizeof(odhalovackaData) / sizeof(odhalovackaData[0]))

#endif // ODHALOVACKA_H
