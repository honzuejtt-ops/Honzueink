
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
// Markerový soubor pro detekci prvního spuštění (pokud neexistuje, exportují se PROGMEM data)
#define SD_MARKER_FILE "/eindata/slovnik/slovnik.txt"

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
static bool _jeReadmeSoubor(const char* fname) {
  if (!fname) return false;
  const char* p = "README";
  for (int i = 0; p[i] != '\0'; i++) {
    char c = fname[i];
    if (c == '\0') return false;
    if (c >= 'a' && c <= 'z') c = c - ('a' - 'A');
    if (c != p[i]) return false;
  }
  return true;
}

static bool _jeDatovyTxtSoubor(const char* fname) {
  if (!fname) return false;
  int n = strlen(fname);
  if (n < 4) return false;
  char a = fname[n - 4], b = fname[n - 3], c = fname[n - 2], d = fname[n - 1];
  bool txt = (a == '.' && (b == 't' || b == 'T') && (c == 'x' || c == 'X') && (d == 't' || d == 'T'));
  return txt && !_jeReadmeSoubor(fname);
}

int nactiSeznamKnihSD(String nazvy[], String cesty[], int maxCount) {
  int count = 0;
  if (!SD.exists("/eindata/knihy")) return 0;
  File dir = SD.open("/eindata/knihy");
  if (!dir || !dir.isDirectory()) { if (dir) dir.close(); return 0; }
  File entry = dir.openNextFile();
  while (entry && count < maxCount) {
    if (!entry.isDirectory()) {
      const char* full = entry.name();
      const char* lastSlash = strrchr(full, '/');
      const char* fname = lastSlash ? (lastSlash + 1) : full;
      if (_jeDatovyTxtSoubor(fname)) {
        String cesta = String("/eindata/knihy/") + fname;
        String radek = "";
        File f = SD.open(cesta.c_str());
        if (f) {
          radek = f.readStringUntil('\n');
          f.close();
        }
        radek.trim();
        if (radek.length() == 0) radek = String(fname);
        nazvy[count] = radek;
        cesty[count] = cesta;
        count++;
      }
    }
    entry.close();
    entry = dir.openNextFile();
  }
  if (entry) entry.close();
  dir.close();
  return count;
}

int nactiSeznamKnih(String nazvy[], int maxCount) {
  String tmpCesty[20];
  if (maxCount > 20) maxCount = 20;
  return nactiSeznamKnihSD(nazvy, tmpCesty, maxCount);
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
// zajistiSDSlozky()
// Ověří a vytvoří všechny potřebné podsložky /eindata/.
// Volá se vždy (nejen při prvním spuštění), aby opravila chybějící složky.
// Vrátí true pokud je vše OK.
// -------------------------------------------------------------------
static bool zajistiSDSlozky() {
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
  return true;
}

// -------------------------------------------------------------------
// zajistiSlovnikJazyky()
// Vytvoří jazykové soubory slovníku, pokud chybí.
// Formát řádku: LEVA|PRAVA
// -------------------------------------------------------------------
static void _zapisPokudNeexistuje(const char* cesta, const char* obsah) {
  if (SD.exists(cesta)) return;
  File f = SD.open(cesta, FILE_WRITE);
  if (!f) return;
  f.print(obsah);
  f.close();
}

static void _zapisVzdy(const char* cesta, const char* obsah) {
  if (SD.exists(cesta)) SD.remove(cesta);
  File f = SD.open(cesta, FILE_WRITE);
  if (!f) return;
  f.print(obsah);
  f.close();
}

static void zajistiSlovnikJazyky() {
  _zapisPokudNeexistuje("/eindata/slovnik/cz_en.txt",
    "ahoj|hello\n"
    "dekuji|thank you\n"
    "prosim|please\n");
  _zapisPokudNeexistuje("/eindata/slovnik/cz_it.txt",
    "ahoj|ciao\n"
    "dekuji|grazie\n"
    "prosim|per favore\n");
  _zapisPokudNeexistuje("/eindata/slovnik/cz_de.txt",
    "ahoj|hallo\n"
    "dekuji|danke\n"
    "prosim|bitte\n");
  _zapisPokudNeexistuje("/eindata/slovnik/cz_es.txt",
    "ahoj|hola\n"
    "dekuji|gracias\n"
    "prosim|por favor\n");
  _zapisPokudNeexistuje("/eindata/slovnik/cz_fr.txt",
    "ahoj|bonjour\n"
    "dekuji|merci\n"
    "prosim|s'il vous plait\n");
  _zapisPokudNeexistuje("/eindata/slovnik/cz_sv.txt",
    "ahoj|hej\n"
    "dekuji|tack\n"
    "prosim|snalla\n");
  _zapisPokudNeexistuje("/eindata/slovnik/cz_uk.txt",
    "ahoj|privit\n"
    "dekuji|dyakuyu\n"
    "prosim|bud laska\n");
}

static void zajistiNavodyVDataSlozkach() {
  _zapisVzdy("/eindata/README_FORMAT.txt",
    "eINK data slozky (struzny navod)\n"
    "- Knihy: /eindata/knihy\n"
    "- Gamebook: /eindata/gamebook\n"
    "- Generator: /eindata/generator\n"
    "- Kviz: /eindata/kviz\n"
    "- Hry (WYR): /eindata/hry\n"
    "- Slovnik: /eindata/slovnik\n"
    "V jednotlivych slozkach najdes README_FORMAT.txt s detaily.\n");

  _zapisVzdy("/eindata/knihy/README_FORMAT.txt",
    "KNIHY - format\n"
    "Soubor: libovolny_nazev.txt\n"
    "1. radek = nazev knihy (zobrazi se v menu)\n"
    "2.+ radek = text knihy\n"
    "README*.txt se do knihovny nezarazuje.\n");

  _zapisVzdy("/eindata/gamebook/README_FORMAT.txt",
    "GAMEBOOK - format\n"
    "Doporuceny format souboru:\n"
    "1. radek = nazev gamebooku (zobrazi se v menu)\n"
    "Dalsi radky = uzly ve formatu:\n"
    "text|volbaA|volbaB|popisA|popisB\n"
    "Priklad:\n"
    "Stojis na krizovatce.|1|2|Jit doleva|Jit doprava\n"
    "Konec.|-1|-1||\n"
    "Pozn.: Pokud 1. radek vypada jako uzel, bere se jako uzel (kompatibilita).\n");

  _zapisVzdy("/eindata/generator/README_FORMAT.txt",
    "GENERATOR - format\n"
    "Kazdy radek = 1 polozka.\n"
    "Doporucene soubory: vtipy.txt, zalmy.txt, citaty.txt, fakty.txt\n"
    "Muzes upravovat radky nebo vkladat cele TXT soubory.\n");

  _zapisVzdy("/eindata/kviz/README_FORMAT.txt",
    "KVIZ - format\n"
    "Kazdy radek:\n"
    "otazka|odpoved|kategorie|obtiznost\n"
    "kategorie: 0..5 (Kultura, Veda, Vseobecny, Osobnosti, Sport, Priroda)\n"
    "obtiznost: 100 / 200 / 300\n"
    "Priklad:\n"
    "Hlavni mesto Italie?|Rim|2|100\n"
    "Muzes doplnovat nove radky i cele soubory.\n");

  _zapisVzdy("/eindata/hry/README_FORMAT.txt",
    "HRY - WYR format\n"
    "Soubor: wyr.txt (nebo jiny .txt)\n"
    "Kazdy radek:\n"
    "otazka|moznostA|moznostB|procentaA\n"
    "Priklad:\n"
    "Co bys radsi?|Kavu|Caj|55\n"
    "procentaA je 0..100.\n");

  _zapisVzdy("/eindata/slovnik/README_FORMAT.txt",
    "SLOVNIK - format\n"
    "Kazdy radek:\n"
    "cz|preklad\n"
    "Priklad:\n"
    "ahoj|hello\n"
    "Doporucene soubory: cz_en.txt, cz_de.txt, cz_it.txt, cz_es.txt, cz_fr.txt, cz_sv.txt, cz_uk.txt\n");
}

// -------------------------------------------------------------------
// pripravSD()
// Hlavní funkce volaná v setup() hned po inicializaci SPI.
//
// Postup:
//   1. Inicializuje SD kartu
//   2. Vždy ověří a vytvoří chybějící podsložky /eindata/
//   3. Pokud chybí markerový soubor (slovnik.txt) — naplní SD
//      daty z PROGMEM headerů (první spuštění)
//   4. Vrátí true pokud vše proběhlo v pořádku, false při chybě
// -------------------------------------------------------------------
bool pripravSD() {
  // Inicializace SD karty (SPI je již spuštěno v setup())
  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("SD: karta nenalezena nebo chyba inicializace");
    return false;
  }
  Serial.println("SD: karta OK");

  // Vždy ověříme a vytvoříme chybějící podsložky
  // (_vytvorSlozku přeskočí existující, vytvoří chybějící)
  if (!zajistiSDSlozky()) return false;
  zajistiSlovnikJazyky();
  zajistiNavodyVDataSlozkach();

  // Pokud /eindata/ už měla obsah, neexportujeme PROGMEM data znovu.
  // Kontrolujeme markerový soubor — pokud chybí, je to první spuštění.
  if (SD.exists(SD_MARKER_FILE)) {
    Serial.println("SD: /eindata/ existuje, data jsou připravena");
    return true;
  }

  // První spuštění — naplníme soubory daty z PROGMEM headerů
  Serial.println("SD: první spuštění, exportujeme data z paměti na SD kartu ...");
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
