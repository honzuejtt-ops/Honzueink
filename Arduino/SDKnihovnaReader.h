#ifndef SDKNIHOVNA_READER_H
#define SDKNIHOVNA_READER_H

// ===================================================================
// SDKnihovnaReader.h — Modul pro práci se SD kartou
//
// Zajišťuje:
//   - Automatickou inicializaci SD karty při startu (pripravSD)
//   - Vytvoření adresářové struktury /eindata/ při prvním spuštění
//   - Naplnění souborů daty z PROGMEM headerů (viz sd_export_helper.h)
//   - Pomocné funkce pro čtení dat ze SD za běhu systému
//
// Použití v setup():
//   sdReady = pripravSD();
//
// SPI musí být inicializováno před voláním pripravSD().
// CS pin SD karty je SD_CS_PIN (GPIO9, sdílí SPI s displejem).
// ===================================================================

#include <SD.h>
#include "sd_export_helper.h"

// CS pin SD karty (sdílí SPI sběrnici s displejem)
#ifndef SD_CS_PIN
  #define SD_CS_PIN 9
#endif

// Kořenová složka datové struktury na SD kartě
#define EINDATA_ROOT "/eindata"

// -------------------------------------------------------------------
// Struktury pro archiv a dynamické knihy
// -------------------------------------------------------------------
struct ArchivDatum { char datum[12]; };
struct SDKniha    { char nazev[52]; };

// -------------------------------------------------------------------
// spocitejRadky()
// Spočítá počet řádků v souboru na SD kartě (počítá znaky '\n').
// Vrátí 0 pokud soubor neexistuje.
// Poznámka: soubory zapsané funkcemi exportXxxNaSD() vždy končí '\n',
//           takže výsledek odpovídá počtu záznamů.
// -------------------------------------------------------------------
int spocitejRadky(const char* cesta) {
  File f = SD.open(cesta);
  if (!f) return 0;
  int pocet = 0;
  // Počítáme znaky nového řádku přímo — bez alokace Stringu na každý řádek
  while (f.available()) {
    if (f.read() == '\n') pocet++;
  }
  f.close();
  return pocet;
}

// -------------------------------------------------------------------
// nactiRadekZSD()
// Načte konkrétní řádek ze souboru na SD kartě (číslování od 0).
// Vrátí prázdný String pokud soubor neexistuje nebo řádek chybí.
// -------------------------------------------------------------------
String nactiRadekZSD(const char* cesta, int radek) {
  File f = SD.open(cesta);
  if (!f) return "";
  int aktualniRadek = 0;
  while (f.available()) {
    String s = f.readStringUntil('\n');
    if (aktualniRadek == radek) {
      f.close();
      s.trim();
      return s;
    }
    aktualniRadek++;
  }
  f.close();
  return "";
}

// -------------------------------------------------------------------
// nactiNahodnyRadek()
// Načte náhodný řádek ze souboru na SD kartě.
// Používá algoritmus reservoir sampling — soubor se prochází jen jednou,
// bez načítání celého obsahu do paměti.
// Vrátí prázdný String pokud soubor neexistuje nebo je prázdný.
// -------------------------------------------------------------------
String nactiNahodnyRadek(const char* cesta) {
  File f = SD.open(cesta);
  if (!f) return "";
  String vybrany = "";
  int pocet = 0;
  while (f.available()) {
    String radek = f.readStringUntil('\n');
    radek.trim();
    if (radek.length() == 0) continue;
    pocet++;
    // S pravděpodobností 1/pocet nahradíme dosud vybraný řádek
    if (random(pocet) == 0) {
      vybrany = radek;
    }
  }
  f.close();
  return vybrany;
}

// -------------------------------------------------------------------
// existujeSD()
// Rychlá kontrola, zda je SD karta přítomná a složka /eindata/ existuje.
// Neotevírá žádný soubor — pouze testuje existenci kořenové složky.
// -------------------------------------------------------------------
bool existujeSD() {
  return SD.exists(EINDATA_ROOT);
}

// -------------------------------------------------------------------
// nactiCelySoubor()
// Načte celý obsah souboru ze SD karty do Stringu.
// Vrátí prázdný String pokud soubor neexistuje.
// -------------------------------------------------------------------
String nactiCelySoubor(const char* cesta) {
  File f = SD.open(cesta);
  if (!f) return "";
  String s = f.readString();
  f.close();
  return s;
}

// -------------------------------------------------------------------
// nactiArchivDatumy()
// Prochází složku /eindata/zpravy/archiv/ a vrátí počet nalezených
// datumových podsložek (formát YYYY-MM-DD).
// Výsledky se zapisují do pole archivDatumy[], max. maxCount položek.
// -------------------------------------------------------------------
int nactiArchivDatumy(String archivDatumy[], int maxCount) {
  int count = 0;
  if (!SD.exists("/eindata/zpravy/archiv")) return 0;
  File dir = SD.open("/eindata/zpravy/archiv");
  if (!dir || !dir.isDirectory()) { if (dir) dir.close(); return 0; }
  File entry = dir.openNextFile();
  while (entry && count < maxCount) {
    if (entry.isDirectory()) {
      const char* fname = entry.name();
      const char* lastSlash = strrchr(fname, '/');
      if (lastSlash) fname = lastSlash + 1;
      if (strlen(fname) >= 10) {
        archivDatumy[count] = String(fname);
        count++;
      }
    }
    entry.close();
    entry = dir.openNextFile();
  }
  if (entry) entry.close();
  dir.close();
  // Seřadit sestupně (nejnovější první) — jednoduchý bubble sort
  for (int i = 0; i < count - 1; i++) {
    for (int j = 0; j < count - i - 1; j++) {
      if (archivDatumy[j] < archivDatumy[j + 1]) {
        String tmp = archivDatumy[j];
        archivDatumy[j] = archivDatumy[j + 1];
        archivDatumy[j + 1] = tmp;
      }
    }
  }
  return count;
}

// -------------------------------------------------------------------
// nactiSeznamKnih()
// Prochází soubory /eindata/knihy/kniha_N.txt a načte jejich názvy
// z prvního řádku. Vrátí počet nalezených knih (max. maxCount).
// -------------------------------------------------------------------
int nactiSeznamKnih(String nazvy[], int maxCount) {
  int count = 0;
  for (int n = 1; n <= maxCount; n++) {
    String cesta = String("/eindata/knihy/kniha_") + n + ".txt";
    File f = SD.open(cesta.c_str());
    if (!f) break;
    String radek = f.readStringUntil('\n');
    f.close();
    radek.trim();
    if (radek.length() == 0) radek = "Kniha " + String(n);
    nazvy[count] = radek;
    count++;
  }
  return count;
}

// -------------------------------------------------------------------
// Interní pomocná funkce — vytvoří složku, pokud ještě neexistuje.
// -------------------------------------------------------------------
static bool _vytvorSlozku(const char* cesta) {
  if (!SD.exists(cesta)) {
    if (!SD.mkdir(cesta)) {
      Serial.print("SD: chyba mkdir ");
      Serial.println(cesta);
      return false;
    }
  }
  return true;
}

// -------------------------------------------------------------------
// pripravSD()
// Hlavní funkce volaná v setup() hned po inicializaci SPI.
//
// Postup:
//   1. Inicializuje SD kartu
//   2. Zkontroluje existenci složky /eindata/
//   3. Pokud neexistuje — vytvoří celou adresářovou strukturu
//      a naplní soubory daty z PROGMEM headerů
//   4. Vrátí true pokud vše proběhlo v pořádku, false při chybě
//
// Pokud /eindata/ již existuje, funkce skončí rychle bez zápisu.
// -------------------------------------------------------------------
bool pripravSD() {
  // Inicializace SD karty (SPI je již spuštěno v setup())
  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("SD: karta nenalezena nebo chyba inicializace");
    return false;
  }
  Serial.println("SD: karta OK");

  // Pokud /eindata/ již existuje, data jsou připravena — není třeba nic dělat
  if (SD.exists(EINDATA_ROOT)) {
    Serial.println("SD: /eindata/ existuje, data jsou připravena");
    return true;
  }

  // První spuštění — vytvoříme celou adresářovou strukturu
  Serial.println("SD: první spuštění, vytváříme strukturu /eindata/ ...");

  const char* slozky[] = {
    EINDATA_ROOT,
    "/eindata/cache",
    "/eindata/slovnik",
    "/eindata/generator",
    "/eindata/gamebook",
    "/eindata/knihy",
    "/eindata/fraze",
    "/eindata/kviz",
    "/eindata/hry",
    "/eindata/zpravy",
    "/eindata/zpravy/aktualni",
    "/eindata/zpravy/archiv"
  };
  const int pocetSlozek = sizeof(slozky) / sizeof(slozky[0]);
  for (int i = 0; i < pocetSlozek; i++) {
    if (!_vytvorSlozku(slozky[i])) return false;
  }

  // Export dat z PROGMEM headerů do souborů na SD kartě
  Serial.println("SD: exportujeme data z paměti na SD kartu ...");
  exportSlovnikNaSD();
  exportGeneratorNaSD();
  exportGamebookNaSD();
  exportKnihyNaSD();
  exportFrazeNaSD();
  exportKvizNaSD();
  exportHryNaSD();

  Serial.println("SD: inicializace dokončena!");
  return true;
}

#endif // SDKNIHOVNA_READER_H
