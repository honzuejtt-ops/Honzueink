#ifndef SD_EXPORT_HELPER_H
#define SD_EXPORT_HELPER_H

// ===================================================================
// sd_export_helper.h — Export PROGMEM dat na SD kartu
//
// Obsahuje pomocné funkce, které projdou stávající PROGMEM pole
// a zapíší je jako textové soubory do složky /eindata/ na SD kartě.
// Voláno automaticky z pripravSD() při prvním spuštění s prázdnou SD.
// ===================================================================

#include <SD.h>
#include "slovnik.h"
#include "generator.h"
#include "gamebook.h"
#include "knihy.h"
#include "fraze.h"
#include "HryKviz.h"
#include "HryWyr.h"

// -------------------------------------------------------------------
// Interní pomocná funkce — odstraní soubor (pokud existuje) a otevře ho
// čistě pro zápis, aby FILE_WRITE nezačínal v append módu za starými daty.
// -------------------------------------------------------------------
static File _sdZapisStart(const char* cesta) {
  if (SD.exists(cesta)) SD.remove(cesta);
  return SD.open(cesta, FILE_WRITE);
}

// -------------------------------------------------------------------
// exportSlovnikNaSD()
// Zapíše slovníkové páry CZ|EN do /eindata/slovnik/slovnik.txt
// Formát: jeden záznam na řádek, odděleno znakem |
// -------------------------------------------------------------------
void exportSlovnikNaSD() {
  File f = _sdZapisStart("/eindata/slovnik/slovnik.txt");
  if (!f) {
    Serial.println("SD export: chyba otevření slovnik.txt");
    return;
  }
  for (int i = 0; i < slovnikDataCount; i++) {
    SlovnikSlovo s;
    memcpy_P(&s, &slovnikData[i], sizeof(SlovnikSlovo));
    f.print(s.cz);
    f.print('|');
    f.println(s.en);
  }
  f.close();
  Serial.print("SD export: slovnik OK (");
  Serial.print(slovnikDataCount);
  Serial.println(" slov)");
}

// -------------------------------------------------------------------
// exportGeneratorNaSD()
// Zapíše vtipy, žalmy, citáty a fakty do /eindata/generator/
// Formát: jeden záznam na řádek (plain text)
// -------------------------------------------------------------------
void exportGeneratorNaSD() {
  File f;

  // Vtipy
  f = _sdZapisStart("/eindata/generator/vtipy.txt");
  if (f) {
    for (int i = 0; i < vtipyPocet; i++) f.println(vtipy[i]);
    f.close();
    Serial.print("SD export: vtipy OK (");
    Serial.print(vtipyPocet);
    Serial.println(")");
  } else {
    Serial.println("SD export: chyba vtipy.txt");
  }

  // Žalmy
  f = _sdZapisStart("/eindata/generator/zalmy.txt");
  if (f) {
    for (int i = 0; i < zalmyPocet; i++) f.println(zalmy[i]);
    f.close();
    Serial.print("SD export: žalmy OK (");
    Serial.print(zalmyPocet);
    Serial.println(")");
  } else {
    Serial.println("SD export: chyba zalmy.txt");
  }

  // Citáty
  f = _sdZapisStart("/eindata/generator/citaty.txt");
  if (f) {
    for (int i = 0; i < citatyPocet; i++) f.println(citaty[i]);
    f.close();
    Serial.print("SD export: citáty OK (");
    Serial.print(citatyPocet);
    Serial.println(")");
  } else {
    Serial.println("SD export: chyba citaty.txt");
  }

  // Fakty
  f = _sdZapisStart("/eindata/generator/fakty.txt");
  if (f) {
    for (int i = 0; i < faktyPocet; i++) f.println(fakty[i]);
    f.close();
    Serial.print("SD export: fakty OK (");
    Serial.print(faktyPocet);
    Serial.println(")");
  } else {
    Serial.println("SD export: chyba fakty.txt");
  }
}

// -------------------------------------------------------------------
// exportGamebookNaSD()
// Zapíše gamebook uzly do /eindata/gamebook/gamebook.txt
// Formát: text|volbaA|volbaB|popisA|popisB (jeden uzel na řádek)
// -------------------------------------------------------------------
void exportGamebookNaSD() {
  File f = _sdZapisStart("/eindata/gamebook/gamebook.txt");
  if (!f) {
    Serial.println("SD export: chyba otevření gamebook.txt");
    return;
  }
  for (int i = 0; i < gamebookCount; i++) {
    GBNode node;
    memcpy_P(&node, &gamebook[i], sizeof(GBNode));
    f.print(node.text);
    f.print('|');
    f.print(node.volbaA);
    f.print('|');
    f.print(node.volbaB);
    f.print('|');
    f.print(node.popisA ? node.popisA : "");
    f.print('|');
    f.println(node.popisB ? node.popisB : "");
  }
  f.close();
  Serial.print("SD export: gamebook OK (");
  Serial.print(gamebookCount);
  Serial.println(" uzlů)");
}

// -------------------------------------------------------------------
// exportKnihyNaSD()
// Zapíše každou knihu jako samostatný soubor do /eindata/knihy/
// Formát: surový text z PROGMEM (bez změn)
// -------------------------------------------------------------------
void exportKnihyNaSD() {
  const char* soubory[] = {
    "/eindata/knihy/kniha_1.txt",
    "/eindata/knihy/kniha_2.txt",
    "/eindata/knihy/kniha_3.txt"
  };
  const char* zdroje[] = { kniha1, kniha2, kniha3 };
  const int pocetKnih = 3;

  for (int k = 0; k < pocetKnih; k++) {
    File f = _sdZapisStart(soubory[k]);
    if (!f) {
      Serial.print("SD export: chyba knihy ");
      Serial.println(k + 1);
      continue;
    }
    // Čteme PROGMEM text znak po znaku a zapisujeme na SD
    const char* ptr = zdroje[k];
    char c;
    while ((c = pgm_read_byte(ptr++)) != '\0') {
      f.write(c);
    }
    f.close();
    Serial.print("SD export: kniha ");
    Serial.print(k + 1);
    Serial.println(" OK");
  }
}

// -------------------------------------------------------------------
// exportFrazeNaSD()
// Zapíše fráze (nakup, ubyt, zdravi, zabava) do /eindata/fraze/
// Formát: cz|en (jeden pár na řádek)
// -------------------------------------------------------------------
void exportFrazeNaSD() {
  const Fraze* pole[] = { frazeNakup, frazeUbyt, frazeZdravi, frazeZabava };
  const int pocty[]   = { frazeNakupCount, frazeUbytCount, frazeZdraviCount, frazeZabavaCount };
  const char* soubory[] = {
    "/eindata/fraze/nakup.txt",
    "/eindata/fraze/ubyt.txt",
    "/eindata/fraze/zdravi.txt",
    "/eindata/fraze/zabava.txt"
  };
  const char* nazvy[] = { "nakup", "ubyt", "zdravi", "zabava" };

  for (int k = 0; k < 4; k++) {
    File f = _sdZapisStart(soubory[k]);
    if (!f) {
      Serial.print("SD export: chyba fráze ");
      Serial.println(nazvy[k]);
      continue;
    }
    for (int i = 0; i < pocty[k]; i++) {
      Fraze fraze;
      memcpy_P(&fraze, &pole[k][i], sizeof(Fraze));
      f.print(fraze.cz);
      f.print('|');
      f.println(fraze.en);
    }
    f.close();
    Serial.print("SD export: fráze ");
    Serial.print(nazvy[k]);
    Serial.print(" OK (");
    Serial.print(pocty[k]);
    Serial.println(")");
  }
}

// -------------------------------------------------------------------
// exportKvizNaSD()
// Zapíše kvíz otázky do /eindata/kviz/otazky.txt
// Formát: otazka|odpoved|kategorieIdx|obtiznost (jeden záznam na řádek)
// -------------------------------------------------------------------
void exportKvizNaSD() {
  File f = _sdZapisStart("/eindata/kviz/otazky.txt");
  if (!f) {
    Serial.println("SD export: chyba otevření kviz/otazky.txt");
    return;
  }
  for (int i = 0; i < kvizPocet; i++) {
    f.print(kvizOtazky[i]);
    f.print('|');
    f.print(kvizOdpovedi[i]);
    f.print('|');
    f.print(kvizKategoriePole[i]);
    f.print('|');
    f.println(kvizObtiznostPole[i]);
  }
  f.close();
  Serial.print("SD export: kvíz OK (");
  Serial.print(kvizPocet);
  Serial.println(" otázek)");
}

// -------------------------------------------------------------------
// exportHryNaSD()
// Zapíše Would You Rather otázky do /eindata/hry/wyr.txt
// Formát: otazka|moznostA|moznostB|procento (jeden záznam na řádek)
// -------------------------------------------------------------------
void exportHryNaSD() {
  File f = _sdZapisStart("/eindata/hry/wyr.txt");
  if (!f) {
    Serial.println("SD export: chyba otevření hry/wyr.txt");
    return;
  }
  for (int i = 0; i < wyrPocet; i++) {
    f.print(wyrOtazky[i]);
    f.print('|');
    f.print(wyrMoznostiA[i]);
    f.print('|');
    f.print(wyrMoznostiB[i]);
    f.print('|');
    f.println(wyrProcenta[i]);
  }
  f.close();
  Serial.print("SD export: hry WYR OK (");
  Serial.print(wyrPocet);
  Serial.println(" otázek)");
}

#endif // SD_EXPORT_HELPER_H
