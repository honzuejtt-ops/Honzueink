#include <Arduino.h>
#include <SPI.h>
#include <math.h>
#include <Preferences.h>
#include <GxEPD2_BW.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <qrcode_rm.h>

#include <time.h>

#include "TextLine.h"
#include "Gamebook.h"
#include "Knihy.h"
#include "Generator.h"
#include "Slovnik.h"
#include "Fraze.h"
#include "HryKviz.h" 
#include "HryWyr.h"  

// ===== WIFI A INTERNET =====
const char* ssid1 = "Barhon";
const char* pass1 = "Pizzue-96";
const char* ssid2 = "Bobik";
const char* pass2 = "honzuemanejfoun";

String urlPocasi = "https://raw.githubusercontent.com/honzuejtt-ops/Honzueink/main/pocasi.txt";
String urlZpravySvet = "https://raw.githubusercontent.com/honzuejtt-ops/Honzueink/main/zpravy_svet.txt";
String urlZpravyCR = "https://raw.githubusercontent.com/honzuejtt-ops/Honzueink/main/zpravy_cr.txt";
String urlTechAI = "https://raw.githubusercontent.com/honzuejtt-ops/Honzueink/main/zpravy_tech.txt";
String urlKurzy = "https://raw.githubusercontent.com/honzuejtt-ops/Honzueink/main/kurzy.txt";

String stazenaDataSvet = ""; String stazenaDataCR = ""; String stazenaDataTech = "";
String stazenaDataPocasi = ""; String stazenaDataKurzy = "";

int nepovedenePokusy = 0; bool casSynchronizovan = false; int lastSecHodiny = -1;

// PAMĚŤ PŘEŽÍVAJÍCÍ DEEP SLEEP
RTC_DATA_ATTR time_t rtc_posledniAktualizace = 0; 

struct Zprava { String titulek; String perex; String text; };
Zprava aktualniZpravy[10]; int pocetZprav = 0; int clanekMenuIndex = 0;

struct PocasiDen { String datum; int tMax; int tMin; String srazky; String ikona; int vitr; String vychod; String zapad; };
PocasiDen predpoved[7]; int pocasiDen = 0;

String kurzEur = "", kurzUsd = "", kurzBtc = "", kurzZlato = "", kurzStribro = "";

// ===== DISPLEJ A HW =====
GxEPD2_BW<GxEPD2_290_BS, GxEPD2_290_BS::HEIGHT> display(GxEPD2_290_BS(3, 4, 5, 6));
U8G2_FOR_ADAFRUIT_GFX u8g2Fonts;

#define BTN_DOWN 0
#define BTN_SELECT 21
#define Vext 18
#define LED_PIN 45
#define BAT_PIN 8

Preferences prefs;

// ===== FONTY A VZHLED (POUZE TYTO 3) =====
const uint8_t* fontyLub[][2] = { { u8g2_font_lubR08_te, u8g2_font_lubB08_te }, { u8g2_font_lubR10_te, u8g2_font_lubB10_te }, { u8g2_font_lubR12_te, u8g2_font_lubB12_te }, { u8g2_font_lubR14_te, u8g2_font_lubB14_te } };
const uint8_t* fontyNcen[][2] = { { u8g2_font_ncenR08_te, u8g2_font_ncenB08_te }, { u8g2_font_ncenR10_te, u8g2_font_ncenB10_te }, { u8g2_font_ncenR12_te, u8g2_font_ncenB12_te }, { u8g2_font_ncenR14_te, u8g2_font_ncenB14_te } };
const uint8_t* fontyHelv[][2] = { { u8g2_font_helvR08_te, u8g2_font_helvB08_te }, { u8g2_font_helvR10_te, u8g2_font_helvB10_te }, { u8g2_font_helvR12_te, u8g2_font_helvB12_te }, { u8g2_font_helvR14_te, u8g2_font_helvB14_te } };

int currentStyle = 0; int currentSize = 1; int currentBold = 1; bool reverseMode = false;
const char* styleNames[] = { "Elegantní", "Klasický", "Hravý" };
const char* sizeNames[] = { "Malá", "Střední", "Velká", "Extra" };
const char* boldNames[] = { "Normální", "Tučné" };

int getCharWidth() { int w[] = { 5, 7, 8, 9 }; return w[currentSize]; }
int getLineHeight() { int h[] = { 12, 15, 17, 20 }; return h[currentSize]; }
int getLinesPerPage() { return 96 / getLineHeight(); }
int getMenuItemHeight() { int h[] = { 18, 22, 25, 28 }; return h[currentSize]; }
int getVisibleMenuItems() { return (128 - 28) / getMenuItemHeight(); }

// DŮSLEDNÉ POUŽITÍ POUZE TVÝCH 3 FONTŮ PRO VŠECHNY VELIKOSTI:
const uint8_t* getBodyFont() { 
  switch (currentStyle) { 
    case 0: return fontyLub[currentSize][currentBold]; 
    case 1: return fontyNcen[currentSize][currentBold]; 
    case 2: return fontyHelv[currentSize][currentBold]; 
    default: return fontyLub[1][0]; 
  } 
}
const uint8_t* getTitleFont() { 
  switch (currentStyle) { 
    case 0: return fontyLub[1][1]; 
    case 1: return fontyNcen[1][1]; 
    case 2: return fontyHelv[1][1]; 
    default: return fontyLub[1][1]; 
  } 
}
const uint8_t* getSmallFont() { 
  switch (currentStyle) { 
    case 0: return fontyLub[0][0]; 
    case 1: return fontyNcen[0][0]; 
    case 2: return fontyHelv[0][0]; 
    default: return fontyLub[0][0]; 
  } 
}
const uint8_t* getBigFont() { 
  switch (currentStyle) { 
    case 0: return fontyLub[3][1]; 
    case 1: return fontyNcen[3][1]; 
    case 2: return fontyHelv[3][1]; 
    default: return fontyLub[3][1]; 
  } 
}

uint16_t bgColor() { return reverseMode ? GxEPD_BLACK : GxEPD_WHITE; }
uint16_t fgColor() { return reverseMode ? GxEPD_WHITE : GxEPD_BLACK; }

// ===== STAVY A APP STATE =====
enum AppState {
  STATE_MAIN_MENU, STATE_KNIHOVNA, STATE_KNIHOVNA_DETAIL, STATE_AKTUALITY,
  STATE_ZPRAVY_MENU, STATE_ZPRAVY_SEZNAM, STATE_ZPRAVY_TEXT, STATE_POCASI_UI, 
  STATE_KURZY, STATE_HODINY, STATE_TOOLBOX, STATE_SLOVNIK, 
  STATE_SLOVNIK_DETAIL, STATE_GENERATOR, STATE_GENERATOR_RESULT,
  STATE_NASTAVENI, STATE_NASTAVENI_VZHLED, STATE_NASTAVENI_BATERIE, STATE_NASTAVENI_O_ZARIZENI, STATE_LED_ON, 
  STATE_STOPKY, STATE_HRY, STATE_FLASKA, STATE_KOSTKY, STATE_KOSTKY_VYBER, 
  STATE_GAMEBOOK, STATE_ODHALOVACKA, STATE_ODHALOVACKA_DETAIL, STATE_FRAZE_MENU, STATE_FRAZE_DETAIL, STATE_UCENI,
  STATE_NASTAVENI_AKTUALIZACE, STATE_QR_MENU, STATE_QR_ZOBRAZ,
  STATE_KVIZ, STATE_KVIZ_ODPOVED, STATE_WYR
};

AppState appState = STATE_MAIN_MENU;
int menuIndex = 0, scrollOffset = 0, subMenuIndex = 0, subScrollOffset = 0, textScrollPage = 0, kostkyStran = 6;
bool stopkyRunning = false; unsigned long stopkyStart = 0, stopkyElapsed = 0, stopkyLastDraw = 0;
int gbNode = 0, gbTextPage = 0, knihaPozice[3] = { 0, 0, 0 };
int aktualniHraIdx = 0; 

// ===== BATERIE A STATUS =====
float getBatteryVoltage() { 
  int raw = analogRead(BAT_PIN);
  if (raw <= 100 || raw >= 4095) return 4.10; 
  return raw * (3.3 / 4095.0) * 4.9; 
}

int getBatteryPercentage() { 
  float v = getBatteryVoltage();
  if (v >= 4.10) return 100; 
  int pct = map(v * 100, 320, 410, 0, 100);
  return constrain(pct, 0, 100);
}

void nakresliStatusBar() {
  if (appState == STATE_HODINY || appState == STATE_ZPRAVY_TEXT || appState == STATE_ZPRAVY_SEZNAM || 
      appState == STATE_KNIHOVNA_DETAIL || appState == STATE_GAMEBOOK || appState == STATE_ODHALOVACKA || 
      appState == STATE_ODHALOVACKA_DETAIL || appState == STATE_KOSTKY || appState == STATE_POCASI_UI || 
      appState == STATE_STOPKY || appState == STATE_FRAZE_DETAIL || appState == STATE_UCENI || 
      appState == STATE_GENERATOR_RESULT || appState == STATE_FLASKA || appState == STATE_SLOVNIK_DETAIL ||
      appState == STATE_NASTAVENI_O_ZARIZENI || appState == STATE_QR_ZOBRAZ || appState == STATE_KVIZ_ODPOVED || appState == STATE_WYR || appState == STATE_KVIZ) {
    return;
  }

  int bx = 250; int by = 2; int bw = 20; int bh = 10;
  display.drawRect(bx, by, bw, bh, fgColor());
  display.fillRect(bx+bw, by+2, 2, 6, fgColor());
  
  int pct = getBatteryPercentage(); int bars = 0;
  if (pct >= 66) bars = 3; else if (pct >= 33) bars = 2; else if (pct >= 10) bars = 1;
  
  if (bars == 0) display.drawLine(bx, by, bx+bw, by+bh, fgColor());
  else for(int i=0; i<bars; i++) display.fillRect(bx + 2 + (i*6), by + 2, 4, bh - 4, fgColor());
  
  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 10)) {
    const char* dnyVTydnu[] = {"Ne", "Po", "Ut", "St", "Ct", "Pa", "So"};
    String den = dnyVTydnu[timeinfo.tm_wday];
    char timeStr[6]; sprintf(timeStr, "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    String casText = den + " " + String(timeinfo.tm_mday) + "." + String(timeinfo.tm_mon + 1) + ". " + String(timeStr);
    
    u8g2Fonts.setFontMode(1); u8g2Fonts.setFont(getSmallFont()); u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
    int tw = u8g2Fonts.getUTF8Width(casText.c_str());
    u8g2Fonts.setCursor(bx - tw - 8, 11); 
    u8g2Fonts.print(casText.c_str()); 
  }
}

// ===== MENU DATA =====
const int mainMenuCount = 7; String mainMenuItems[] = { "KNIHOVNA", "AKTUALITY", "TOOLBOX", "SLOVNÍK", "GENERÁTOR", "HRY", "NASTAVENÍ" };
const int aktualityCount = 4; String aktualityItems[] = { "Zprávy", "Světový čas", "Kurzy", "Počasí" };
const int zpravyMenuCount = 3; String zpravyMenuItems[] = { "Ze světa", "Z ČR", "Technologie a AI" };
const int knihovnaCount = 3; String knihovnaItems[] = { "Zaklínač 1", "Zaklínač 2", "Zaklínač 3" };
const int toolboxCount = 3; String toolboxItems[] = { "Dioda", "Stopky", "QR Kódy" }; bool ledState = false;

// DATA PRO QR KÓDY 
const int qrCount = 5; String qrItems[] = { "Platba na účet", "YouTube Kanál", "E-shop (Prsteny)", "Wi-Fi Hotspot", "Vizitka (Kontakt)" };
String qrData[] = {
  "SPD*1.0*ACC:1150279005/2700*AM:0.00*CC:CZK*MSG:Platba", 
  "https://www.youtube.com/@StudioSklep", 
  "https://www.jtatelier.cz/", 
  "WIFI:S:Bobik;T:WPA;P:honzuemanejfoun;;", 
  "BEGIN:VCARD\nVERSION:3.0\nN:Jan Těthal\nFN:Jan Těthal\nTEL:732213904\nORG:IČO 08602573\nEND:VCARD" 
};

const int slovnikCount = 4; String slovnikItems[] = { "Hledat CZ -> EN", "Hledat EN -> CZ", "Fráze", "Učení" }; int hledanySmer = 0; 
const int frazeMenuCount = 4; String frazeMenuItems[] = { "Nakupování", "Ubytování", "Zdraví", "Zábava" };
int uceniSmer = 0, uceniIdx = 0; bool uceniOdhaleno = false;
char hledanyText[12] = ""; int hledanyLen = 0, abcKurzor = 0, vysledekOffset = 0;
const char abeceda[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"; const int abcCount = 26;
const int generatorCount = 4; String generatorItems[] = { "Vtipy", "Žalmy", "Citáty", "Fakta" };
const int hryCount = 6; String hryItems[] = { "Flaška", "Kostky", "Gamebook", "Odhalovačka", "Kvíz", "Co bys radši?" };
const int kostkyCount = 4; String kostkyItems[] = { "6 stěn", "12 stěn", "24 stěn", "2x6 stěn" }; int kostkyStrany[] = { 6, 12, 24, 66 };

const int nastaveniCount = 5; String nastaveniItems[] = { "Vzhled displeje", "Auto-aktualizace", "Vynutit aktualizaci", "Baterie", "O zařízení" };
const int vzhledCount = 4; String vzhledItems[] = { "Font", "Velikost", "Tloušťka", "Reverz" };
const int intervalCount = 5; String intervalItems[] = {"Vypnuto", "Každé 3 hodiny", "Každých 5 hodin", "Každých 12 hodin", "Každých 24 hodin"};
unsigned long intervalyMs[] = {0, 10800000, 18000000, 43200000, 86400000};
int intervalIdx = 4; 

// ===== DATA ODHALOVAČKY =====
bool odhalenoPole[64]; unsigned long odhalovackaLastMs = 0; bool odhalovackaKonec = false; int odhalovackaSkore = 0;
const unsigned char img_slunce[] PROGMEM = {
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x01,0x80,0x00,0x00,0x00,0x00,
  0x00,0x00,0x03,0xc0,0x00,0x00,0x00,0x00, 0x00,0x00,0x03,0xc0,0x00,0x00,0x00,0x00,
  0x00,0x00,0x01,0x80,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
};
String odhalovackaOdpoved = "SLUNCE";

// ===== POMOCNÉ FUNKCE =====
void printCentered(String text, int y, const uint8_t* font) {
    u8g2Fonts.setFont(font); u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
    int tw = u8g2Fonts.getUTF8Width(text.c_str());
    u8g2Fonts.setCursor((display.width() - tw) / 2, y); u8g2Fonts.print(text.c_str());
}

int zalamejText(const char* buf, int len, TextLine* lines, int maxLines, int maxWidth) {
  int lineCount = 0, pos = 0, charW = getCharWidth();
  while (pos < len && lineCount < maxLines) {
    int lineStart = pos, lastSpace = -1, lineW = 0; bool zalomeno = false;
    while (pos < len && buf[pos] != '\n') {
      if (buf[pos] == ' ') lastSpace = pos;
      lineW += charW;
      if (lineW > maxWidth) {
        if (lastSpace > lineStart) { lines[lineCount].start = lineStart; lines[lineCount].len = lastSpace - lineStart; lineCount++; pos = lastSpace + 1; } 
        else { lines[lineCount].start = lineStart; lines[lineCount].len = pos - lineStart; lineCount++; }
        zalomeno = true; break;
      } pos++;
    }
    if (!zalomeno) { lines[lineCount].start = lineStart; lines[lineCount].len = pos - lineStart; lineCount++; if (pos < len && buf[pos] == '\n') pos++; }
  } return lineCount;
}

void nakresliLoadScreen(String text, int progress) {
  display.setPartialWindow(0, 0, display.width(), display.height());
  display.firstPage();
  do {
    display.fillScreen(bgColor());
    u8g2Fonts.setFontMode(1);
    
    u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
    u8g2Fonts.setFont(getBigFont()); // Nahrazeno pevné písmo
    int tw = u8g2Fonts.getUTF8Width("HONZUEINK");
    u8g2Fonts.setCursor((display.width() - tw) / 2, 40);
    u8g2Fonts.print("HONZUEINK");

    u8g2Fonts.setFont(getBodyFont()); // Nahrazeno pevné písmo
    tw = u8g2Fonts.getUTF8Width(text.c_str());
    u8g2Fonts.setCursor((display.width() - tw) / 2, 80);
    u8g2Fonts.print(text.c_str());

    display.drawRect(20, 100, 256, 12, fgColor());
    display.fillRect(20, 100, (256 * progress) / 100, 12, fgColor());
  } while (display.nextPage());
}

// ===== UKLÁDÁNÍ DAT DO PAMĚTI (PŘEŽIJE DEEP SLEEP) =====
void ulozZpravuDoCache(String klic, String data) {
  prefs.begin("cache", false);
  if (data.length() > 3800) data = data.substring(0, 3800); // Ořez pro jistotu
  prefs.putString(klic.c_str(), data);
  prefs.end();
}

void ulozStazenaData() {
  ulozZpravuDoCache("svet", stazenaDataSvet);
  ulozZpravuDoCache("cr", stazenaDataCR);
  ulozZpravuDoCache("tech", stazenaDataTech);
  ulozZpravuDoCache("poc", stazenaDataPocasi);
  ulozZpravuDoCache("kur", stazenaDataKurzy);
}

void nactiStazenaData() {
  prefs.begin("cache", true);
  stazenaDataSvet = prefs.getString("svet", "");
  stazenaDataCR = prefs.getString("cr", "");
  stazenaDataTech = prefs.getString("tech", "");
  stazenaDataPocasi = prefs.getString("poc", "");
  stazenaDataKurzy = prefs.getString("kur", "");
  prefs.end();

  if (stazenaDataPocasi != "") parsujPocasi(stazenaDataPocasi);
  if (stazenaDataKurzy != "") parsujKurzy(stazenaDataKurzy);
}


// ===== DEEP SLEEP FUNKCE =====
void jdiSpat() {
  struct tm timeinfo;
  String datumStr = "Dobrou noc";
  String svatekDnes = "";
  
  if (getLocalTime(&timeinfo, 150)) {
    const char* dnyVTydnu[] = {"Neděle", "Pondělí", "Úterý", "Středa", "Čtvrtek", "Pátek", "Sobota"};
    char dBuf[50]; 
    sprintf(dBuf, "%s %d. %d.", dnyVTydnu[timeinfo.tm_wday], timeinfo.tm_mday, timeinfo.tm_mon + 1);
    datumStr = String(dBuf);
    svatekDnes = "Svátek má " + getSvatek(timeinfo.tm_mday, timeinfo.tm_mon + 1);
  }

  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(bgColor());
    u8g2Fonts.setFontMode(1); u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
    
    // Horní část: Datum a Svátek
    u8g2Fonts.setFont(getTitleFont());
    int tw = u8g2Fonts.getUTF8Width(datumStr.c_str());
    u8g2Fonts.setCursor((display.width() - tw) / 2, 22); 
    u8g2Fonts.print(datumStr.c_str());
    
    u8g2Fonts.setFont(getSmallFont());
    tw = u8g2Fonts.getUTF8Width(svatekDnes.c_str());
    u8g2Fonts.setCursor((display.width() - tw) / 2, 38);
    u8g2Fonts.print(svatekDnes.c_str());
    
    display.drawFastHLine(0, 42, display.width(), fgColor());

    // Střed: Počasí (pokud máme data)
    if (stazenaDataPocasi != "") {
      nakresliIkonuPocasi(predpoved[0].ikona, 50, 75);
      u8g2Fonts.setFont(getBigFont());
      String temp = String(predpoved[0].tMax) + "° / " + String(predpoved[0].tMin) + "°C";
      u8g2Fonts.setCursor(110, 80); u8g2Fonts.print(temp.c_str());
    }

    // Spodek: Náhodný vtip/citát
    display.drawFastHLine(0, 100, display.width(), fgColor());
    u8g2Fonts.setFont(getSmallFont());
    String rTxt = String(vtipy[random(vtipyPocet)]);
    TextLine lines[2];
    int lCount = zalamejText(rTxt.c_str(), rTxt.length(), lines, 2, display.width() - 20);
    for(int i=0; i<lCount; i++) {
       char lBuf[85]; int len = lines[i].len; if(len>84) len=84;
       strncpy(lBuf, &rTxt.c_str()[lines[i].start], len); lBuf[len]='\0';
       u8g2Fonts.setCursor(10, 114 + (i*12)); u8g2Fonts.print(lBuf);
    }

  } while (display.nextPage());

  display.hibernate();
  esp_sleep_enable_ext0_wakeup((gpio_num_t)BTN_SELECT, 0); 
  esp_deep_sleep_start();
}

// ===== ZOBRAZOVÁNÍ QR KÓDŮ =====
void zobrazQR(const char* title, const char* dataStr) {
  QRCode qrcode;
  uint8_t qrcodeData[qrcode_getBufferSize(3)];
  qrcode_initText(&qrcode, qrcodeData, 3, 0, dataStr);

  int scale = 3;
  int offset_x = (display.width() - (qrcode.size * scale)) / 2;
  int offset_y = 28;

  display.setPartialWindow(0, 0, display.width(), display.height());
  display.firstPage();
  do {
    display.fillScreen(bgColor());
    u8g2Fonts.setFontMode(1);
    u8g2Fonts.setFont(getTitleFont()); u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
    u8g2Fonts.setCursor(5, 16); u8g2Fonts.print(title);
    display.drawFastHLine(0, 20, display.width(), fgColor());

    for (uint8_t y = 0; y < qrcode.size; y++) {
      for (uint8_t x = 0; x < qrcode.size; x++) {
        if (qrcode_getModule(&qrcode, x, y)) {
          display.fillRect(offset_x + x * scale, offset_y + y * scale, scale, scale, fgColor());
        }
      }
    }
    u8g2Fonts.setFont(getSmallFont()); u8g2Fonts.setCursor(5, 125); u8g2Fonts.print("Drž BTN21 = zpět");
  } while (display.nextPage());
}

// ===== ZOBRAZOVÁNÍ KVÍZU A WYR =====
void zobrazKviz() {
  String ot = String(kvizOtazky[aktualniHraIdx]);
  display.setPartialWindow(0, 0, display.width(), display.height());
  display.firstPage();
  do {
    display.fillScreen(bgColor()); nakresliStatusBar();
    u8g2Fonts.setFont(getTitleFont()); u8g2Fonts.setCursor(5, 16); u8g2Fonts.print("KVÍZ"); display.drawFastHLine(0, 20, display.width(), fgColor());
    u8g2Fonts.setFont(getBodyFont());
    TextLine lines[5]; int lCount = zalamejText(ot.c_str(), ot.length(), lines, 5, display.width() - 10);
    int y = 40;
    for(int i=0; i<lCount; i++){
      char lBuf[80]; int len = lines[i].len; if(len>79) len=79;
      strncpy(lBuf, &ot.c_str()[lines[i].start], len); lBuf[len]='\0';
      u8g2Fonts.setCursor(5, y); u8g2Fonts.print(lBuf); y+=getLineHeight();
    }
    u8g2Fonts.setFont(getSmallFont()); u8g2Fonts.setCursor(5, 125); u8g2Fonts.print("BTN21 = Odpověď | BTN0 = Další | Drž = zpět");
  } while (display.nextPage());
}

void zobrazKvizOdpoved() {
  String odp = String(kvizOdpovedi[aktualniHraIdx]);
  display.setPartialWindow(0, 0, display.width(), display.height());
  display.firstPage();
  do {
    display.fillScreen(bgColor());
    u8g2Fonts.setFont(getBigFont()); 
    int tw = u8g2Fonts.getUTF8Width(odp.c_str());
    u8g2Fonts.setCursor((display.width() - tw) / 2, 60); u8g2Fonts.print(odp.c_str());
    u8g2Fonts.setFont(getSmallFont()); u8g2Fonts.setCursor(5, 125); u8g2Fonts.print("BTN0 / BTN21 = Další otázka | Drž = zpět");
  } while (display.nextPage());
}

void zobrazWyr() {
  String ot = String(wyrOtazky[aktualniHraIdx]);
  display.setPartialWindow(0, 0, display.width(), display.height());
  display.firstPage();
  do {
    display.fillScreen(bgColor()); nakresliStatusBar();
    u8g2Fonts.setFont(getBodyFont());
    TextLine lines[6]; int lCount = zalamejText(ot.c_str(), ot.length(), lines, 6, display.width() - 10);
    int y = 30;
    for(int i=0; i<lCount; i++){
      char lBuf[80]; int len = lines[i].len; if(len>79) len=79;
      strncpy(lBuf, &ot.c_str()[lines[i].start], len); lBuf[len]='\0';
      int lw = u8g2Fonts.getUTF8Width(lBuf);
      u8g2Fonts.setCursor((display.width() - lw) / 2, y); u8g2Fonts.print(lBuf); y+=getLineHeight();
    }
    u8g2Fonts.setFont(getSmallFont()); u8g2Fonts.setCursor(5, 125); u8g2Fonts.print("BTN21 nebo BTN0 = Další | Drž = zpět");
  } while (display.nextPage());
}

// ===== DATABÁZE SVÁTKŮ =====
const char* svatkyMesice[] = {
  "Nový rok,Karina,Radmila,Diana,Dalimil,Tři králové,Vilma,Čestmír,Vladan,Břetislav,Bohdana,Pravoslav,Edita,Radovan,Alice,Ctirad,Drahoslav,Vladislav,Doubravka,Ilona,Běla,Slavomír,Zdeněk,Milena,Miloš,Zora,Ingrid,Otýlie,Zdislava,Robin,Marika",
  "Hynek,Nela,Blažej,Jarmila,Dobromila,Vanda,Veronika,Milada,Apolena,Mojmír,Božena,Slavěna,Věnceslav,Valentýn,Jiřina,Ljuba,Miloslava,Gizela,Patrik,Oldřich,Lenka,Petr,Svatopluk,Matěj,Liliana,Dorota,Alexandr,Lumír,Horymír",
  "Bedřich,Anežka,Kamil,Stela,Kazimír,Miroslav,Tomáš,Gabriela,Františka,Viktorie,Anděla,Řehoř,Růžena,Rút/Matylda,Ida,Elena/Herbert,Vlastimil,Eduard,Josef,Světlana,Radek,Leona,Ivona,Gabriel,Marián,Emanuel,Dita,Soňa,Taťána,Arnošt,Kvido",
  "Hugo,Erika,Richard,Ivana,Miroslava,Vendula,Heřman/Hermína,Ema,Dušan,Darja,Izabela,Julius,Aleš,Vincenc,Anastázie,Irena,Valérie,Rostislav,Marcela,Ilja,Alexej,Evžen,Vojtěch,Jiří,Marek,Oto,Jaroslav,Vlastislav,Robert,Blahoslav",
  "Svátek práce,Zikmund,Alexej,Květoslav,Klaudie,Radoslav,Stanislav,Státní svátek,Ctibor,Blanka,Svatava,Pankrác,Servác,Bonifác,Žofie,Přemysl,Aneta,Nataša,Ivo,Zbyšek,Monika,Emil,Vladimír,Jana,Viola,Filip,Valdemar,Vilém,Maxmilián,Ferdinand,Kamila",
  "Laura,Jarmil,Tamara,Dalibor,Dobroslav,Norbert,Iveta/Slavoj,Medard,Stanislava,Gita,Bruno,Antonie,Antonín,Roland,Vít,Zbyněk,Adolf,Milan,Leoš,Květa,Alois,Pavla,Zdeňka,Jan,Ivan,Adriana,Ladislav,Lubomír,Petr/Pavel,Šárka",
  "Jaroslava,Patricie,Radomír,Prokop,Cyril/Metoděj,Jan Hus,Bohuslava,Nora,Drahoslava,Libuše/Amálie,Olga,Bořek,Markéta,Karolína,Jindřich,Luboš,Martina,Drahomíra,Čeněk,Ilja,Vítězslav,Magdaléna,Libor,Kristýna,Jakub,Anna,Vroslav,Viktor,Marta,Bořivoj,Ignác",
  "Oskar,Gustav,Miluše,Dominik,Kristián,Oldřiška,Lada,Soběslav,Roman,Vavřinec,Zuzana,Klára,Alena,Alan,Hana,Jáchym,Petra,Helena,Ludvík,Bernard,Johana,Bohuslav,Sandra,Bartoloměj,Radim,Luděk,Otakar,Augustýn,Evelína,Vladěna,Pavlína",
  "Linda/Samuel,Adéla,Bronislav,Jindřiška,Boris,Boleslav,Regína,Mariana,Daniela,Irma,Denisa,Marie,Lubor,Radka,Jolana,Ludmila,Naděžda,Kryštof,Zita,Oleg,Matouš,Darina,Berta,Jaromír,Zlata,Andrea,Jonáš,Václav,Michal,Jeroným",
  "Igor,Olívie,Bohumil,František,Eliška,Hanuš,Justýna,Věra,Štefan,Marina,Andrej,Marcel,Renáta,Agáta,Tereza,Havel,Hedvika,Lukáš,Michaela,Vendelín,Brigita,Sabina,Teodor,Nina,Beáta,Erik,Šarlota,Státní svátek,Silvie,Tadeáš,Štěpánka",
  "Felix,Tobiáš,Hubert,Karel,Miriam,Liběna,Saskie,Bohumír,Bohdan,Evžen,Martin,Benedikt,Tibor,Sáva,Leopold,Otmar,Mahulena,Romana,Alžběta,Nikola,Albert,Cecílie,Klement,Emílie,Kateřina,Artur,Xenie,René,Zina,Ondřej",
  "Iva,Blanka,Svatoslav,Barbora,Jitka,Mikuláš,Ambrož/Benjamín,Květoslava,Vratislav,Julie,Dana,Simona,Lucie,Lýdie,Radana,Albína,Daniel,Miloslav,Ester,Dagmar,Natálie,Šimon,Vlasta,Adam/Eva,Boží hod,Štěpán,Žaneta,Bohumila,Judita,David,Silvestr"
};

String getSvatek(int d, int m) {
  if (m < 1 || m > 12) return "";
  String s = String(svatkyMesice[m - 1]); int count = 1; int lastPos = 0;
  for (int i = 0; i <= s.length(); i++) {
    if (i == s.length() || s[i] == ',') {
      if (count == d) return s.substring(lastPos, i);
      lastPos = i + 1; count++;
    }
  } return "";
}

// ===== PREFERENCES =====
void ulozPoziciKnihy(int idx, int page) { knihaPozice[idx] = page; prefs.begin("knihy", false); char key[8]; sprintf(key, "k%d", idx); prefs.putInt(key, page); prefs.end(); }
void nactiPoziceKnih() { prefs.begin("knihy", true); for (int i = 0; i < 3; i++) { char key[8]; sprintf(key, "k%d", i); knihaPozice[i] = prefs.getInt(key, 0); } prefs.end(); }
void ulozGBPozici(int node) { prefs.begin("gb", false); prefs.putInt("node", node); prefs.end(); }
int nactiGBPozici() { prefs.begin("gb", true); int n = prefs.getInt("node", 0); prefs.end(); return n; }

// ===== PARSOVÁNÍ DAT =====
void parsujZpravy(String raw) {
  pocetZprav = 0; int pos = 0;
  while (pocetZprav < 10) { 
    int tStart = raw.indexOf("|T|", pos); if (tStart == -1) break;
    int pStart = raw.indexOf("|P|", tStart); int xStart = raw.indexOf("|X|", pStart); int eStart = raw.indexOf("|E|", xStart);
    if (pStart == -1 || xStart == -1 || eStart == -1) break;
    aktualniZpravy[pocetZprav].titulek = raw.substring(tStart + 3, pStart);
    aktualniZpravy[pocetZprav].perex = raw.substring(pStart + 3, xStart);
    aktualniZpravy[pocetZprav].text = raw.substring(xStart + 3, eStart);
    pocetZprav++; pos = eStart + 3;
  }
  if (pocetZprav == 0) { aktualniZpravy[0].titulek = "Chyba dat"; aktualniZpravy[0].perex = "Žádná data nejsou v paměti."; aktualniZpravy[0].text = raw; pocetZprav = 1; }
}

void parsujPocasi(String raw) {
  int pos = 0;
  for (int i = 0; i < 7; i++) {
    int n1 = raw.indexOf('|', pos); int n2 = raw.indexOf('|', n1 + 1); int n3 = raw.indexOf('|', n2 + 1);
    int n4 = raw.indexOf('|', n3 + 1); int n5 = raw.indexOf('|', n4 + 1); int n6 = raw.indexOf('|', n5 + 1); int n7 = raw.indexOf('|', n6 + 1);
    int nEnd = raw.indexOf('\n', n7 + 1);
    if (n1 == -1 || nEnd == -1) break;
    predpoved[i].datum = raw.substring(pos, n1); predpoved[i].tMax = raw.substring(n1 + 1, n2).toInt();
    predpoved[i].tMin = raw.substring(n2 + 1, n3).toInt(); predpoved[i].srazky = raw.substring(n3 + 1, n4);
    predpoved[i].ikona = raw.substring(n4 + 1, n5); predpoved[i].vitr = raw.substring(n5 + 1, n6).toInt();
    predpoved[i].vychod = raw.substring(n6 + 1, n7); predpoved[i].zapad = raw.substring(n7 + 1, nEnd);
    pos = nEnd + 1;
  }
}

void parsujKurzy(String raw) {
  int n1 = raw.indexOf('|'); int n2 = raw.indexOf('|', n1 + 1);
  int n3 = raw.indexOf('|', n2 + 1); int n4 = raw.indexOf('|', n3 + 1);
  if (n1 > 0 && n4 > 0) {
    kurzEur = raw.substring(0, n1); kurzUsd = raw.substring(n1 + 1, n2); kurzBtc = raw.substring(n2 + 1, n3);
    kurzZlato = raw.substring(n3 + 1, n4); kurzStribro = raw.substring(n4 + 1); kurzStribro.trim();
  }
}

// ===== WI-FI A STAHOVÁNÍ =====
bool pripojWiFi() {
  if (WiFi.status() == WL_CONNECTED) return true;
  
  WiFi.disconnect(true, true); 
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);      
  WiFi.setSleep(false);        
  delay(200);

  Serial.println("Hledám síť: " + String(ssid1));
  WiFi.begin(ssid1, pass1);
  int pokusy = 0;
  while (WiFi.status() != WL_CONNECTED && pokusy < 35) { delay(500); pokusy++; }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Barhon selhal, zkouším Bobíka...");
    WiFi.begin(ssid2, pass2);
    pokusy = 0;
    while (WiFi.status() != WL_CONNECTED && pokusy < 35) { delay(500); pokusy++; }
  }

  if (WiFi.status() == WL_CONNECTED) {
    configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.nist.gov");
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 10000)) casSynchronizovan = true;
    return true;
  }
  return false;
}

String stahniTextZUrl(WiFiClientSecure& client, String nazev, String url) {
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS); 
  http.begin(client, url);
  http.setTimeout(12000); 
  http.addHeader("User-Agent", "ESP32-Honzueink");

  int httpCode = http.GET();
  String vysledek = "";

  if (httpCode == 200) { 
    vysledek = http.getString();
  } else {
    Serial.println("CHYBA GITHUB: " + String(httpCode) + " u souboru: " + nazev);
  }

  http.end();
  // Vracíme "" pokud to selže, abychom nepřepsali dobrá data chybou!
  return vysledek;
}

void aktualizovatDataNaPozadi(bool vynuceno) {
  bool casNaUpdate = false;
  time_t now = time(NULL);
  
  // Zkontrolujeme, jestli ESP už má z internetu reálný čas (epoch > 1.6 miliardy)
  if (intervalIdx > 0 && now > 1600000000) {
    unsigned long intervalSec = intervalyMs[intervalIdx] / 1000;
    if (rtc_posledniAktualizace == 0 || (now - rtc_posledniAktualizace >= intervalSec)) {
      casNaUpdate = true;
    }
  }

  if (vynuceno || casNaUpdate) {
    if (vynuceno || casNaUpdate) nakresliLoadScreen("Navazuji spojení...", 10);
    
    if (pripojWiFi()) {
      WiFiClientSecure secureClient;
      secureClient.setInsecure(); 
      bool asponNecoSeStahlo = false;
      
      nakresliLoadScreen("Stahuji zprávy ze světa...", 30);
      String tSvet = stahniTextZUrl(secureClient, "Svet", urlZpravySvet);
      // BEZPEČNÁ KONTROLA: Přepiš jen když se stáhl validní text (delší než 20 znaků)
      if (tSvet.length() > 20) { stazenaDataSvet = tSvet; asponNecoSeStahlo = true; }
      
      nakresliLoadScreen("Stahuji zprávy z ČR...", 45);
      String tCR = stahniTextZUrl(secureClient, "CR", urlZpravyCR);
      if (tCR.length() > 20) { stazenaDataCR = tCR; asponNecoSeStahlo = true; }
      
      nakresliLoadScreen("Stahuji Tech a AI...", 60);
      String tTech = stahniTextZUrl(secureClient, "Tech", urlTechAI);
      if (tTech.length() > 20) { stazenaDataTech = tTech; asponNecoSeStahlo = true; }
      
      nakresliLoadScreen("Stahuji Počasí...", 75);
      String tPoc = stahniTextZUrl(secureClient, "Pocasi", urlPocasi);
      if (tPoc.length() > 20) { stazenaDataPocasi = tPoc; asponNecoSeStahlo = true; }
      
      nakresliLoadScreen("Stahuji Kurzy...", 90);
      String tKur = stahniTextZUrl(secureClient, "Kurzy", urlKurzy);
      if (tKur.length() > 10) { stazenaDataKurzy = tKur; asponNecoSeStahlo = true; }
      
      if (asponNecoSeStahlo) {
        parsujPocasi(stazenaDataPocasi);
        nepovedenePokusy = 0;
        rtc_posledniAktualizace = time(NULL); 
        ulozStazenaData(); // Uložíme je do paměti, kterou nespálí Deep sleep
      }
      
      if (vynuceno || casNaUpdate) {
        nakresliLoadScreen("HOTOVO A ULOŽENO!", 100);
        delay(1000);
      }
    } else {
      if (vynuceno) {
        nakresliLoadScreen("Wi-Fi nenalezena!", 0);
        delay(2000);
      }
    }
  }
}

// ===== ZOBRAZOVÁNÍ MENU A DAT =====
void zobrazSubMenu(const char* title, String items[], int count, int selIndex, int scrOffset) {
  int visible = getVisibleMenuItems(); int itemH = getMenuItemHeight();
  display.setPartialWindow(0, 0, display.width(), display.height());
  display.firstPage();
  do {
    display.fillScreen(bgColor());
    u8g2Fonts.setFontMode(1);
    u8g2Fonts.setFont(getTitleFont()); u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
    u8g2Fonts.setCursor(10, 18); u8g2Fonts.print(title);
    nakresliStatusBar();
    display.drawFastHLine(0, 23, display.width(), fgColor());
    u8g2Fonts.setFont(getBodyFont());
    for (int i = 0; i < visible; i++) {
      int idx = i + scrOffset; if (idx >= count) break; int y = 28 + itemH + (i * itemH);
      if (idx == selIndex) { display.fillRect(0, y - itemH + 4, display.width(), itemH, fgColor()); u8g2Fonts.setForegroundColor(bgColor()); u8g2Fonts.setBackgroundColor(fgColor()); } 
      else { u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor()); }
      u8g2Fonts.setCursor(15, y); u8g2Fonts.print(items[idx].c_str());
    }
  } while (display.nextPage());
}

void zobrazBateriiMenu() {
  display.setPartialWindow(0, 0, display.width(), display.height());
  display.firstPage();
  do {
    display.fillScreen(bgColor());
    u8g2Fonts.setFontMode(1);
    u8g2Fonts.setFont(getTitleFont()); u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
    u8g2Fonts.setCursor(5, 18); u8g2Fonts.print("BATERIE");
    nakresliStatusBar();
    display.drawFastHLine(0, 23, display.width(), fgColor());
    
    int pct = getBatteryPercentage(); float v = getBatteryVoltage();
    u8g2Fonts.setFont(getBigFont()); u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
    u8g2Fonts.setCursor(10, 50); u8g2Fonts.print(String(pct) + " %");
    
    u8g2Fonts.setFont(getBodyFont());
    u8g2Fonts.setCursor(10, 75); u8g2Fonts.print("Napětí: " + String(v, 2) + " V");
    
    u8g2Fonts.setFont(getSmallFont());
    u8g2Fonts.setCursor(10, 95); u8g2Fonts.print("EASYLANDER, Li-pol 2500 mAh");
    u8g2Fonts.setCursor(10, 108); u8g2Fonts.print("3.7v, 9.25wh, Model: 603090");
    u8g2Fonts.setCursor(5, 125); u8g2Fonts.print("Drž BTN21 = zpět");
  } while (display.nextPage());
}

// === INFO O ZAŘÍZENÍ ===
void zobrazOZaRizeni() {
  display.setPartialWindow(0, 0, display.width(), display.height());
  display.firstPage();
  do {
    display.fillScreen(bgColor());
    u8g2Fonts.setFontMode(1);
    u8g2Fonts.setFont(getTitleFont()); u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
    u8g2Fonts.setCursor(5, 18); u8g2Fonts.print("INFO O ZAŘÍZENÍ");
    display.drawFastHLine(0, 23, display.width(), fgColor());

    u8g2Fonts.setFont(getSmallFont()); 
    int y = 38; int lh = 14; 

    u8g2Fonts.setCursor(5, y); u8g2Fonts.print("Model: Heltec Vision Master E290"); y += lh;
    u8g2Fonts.setCursor(5, y); u8g2Fonts.print("Displej: 2.9\" E-ink (296x128)"); y += lh;

    uint32_t freeRam = ESP.getFreeHeap() / 1024;
    uint32_t totalRam = ESP.getHeapSize() / 1024;
    String ramStr = "RAM: volná " + String(freeRam) + " KB / " + String(totalRam) + " KB";
    u8g2Fonts.setCursor(5, y); u8g2Fonts.print(ramStr.c_str()); y += lh;

    uint32_t flashSize = ESP.getFlashChipSize() / (1024 * 1024);
    uint32_t sketchSize = ESP.getSketchSize() / 1024;
    String flashStr = "Flash: " + String(flashSize) + " MB (Kód zabírá: " + String(sketchSize) + " KB)";
    u8g2Fonts.setCursor(5, y); u8g2Fonts.print(flashStr.c_str()); y += lh;

    uint32_t cpuFreq = ESP.getCpuFreqMHz();
    String cpuStr = "CPU: ESP32 procesor @ " + String(cpuFreq) + " MHz";
    u8g2Fonts.setCursor(5, y); u8g2Fonts.print(cpuStr.c_str()); y += lh;

    unsigned long upMin = millis() / 60000;
    String upStr = "Uptime: " + String(upMin) + " min";
    u8g2Fonts.setCursor(5, y); u8g2Fonts.print(upStr.c_str());

    display.drawFastHLine(0, 115, display.width(), fgColor());
    u8g2Fonts.setCursor(5, 127); u8g2Fonts.print("Drž BTN21 = zpět");
  } while (display.nextPage());
}

void drawDigit(int x, int y, int size, int d) {
  int t = size/5; if(t<1) t=1; int w = size; int h = size*2; bool seg[7] = {0};
  switch(d) {
    case 0: seg[0]=1; seg[1]=1; seg[2]=1; seg[3]=1; seg[4]=1; seg[5]=1; break;
    case 1: seg[1]=1; seg[2]=1; break; case 2: seg[0]=1; seg[1]=1; seg[6]=1; seg[4]=1; seg[3]=1; break;
    case 3: seg[0]=1; seg[1]=1; seg[6]=1; seg[2]=1; seg[3]=1; break; case 4: seg[5]=1; seg[6]=1; seg[1]=1; seg[2]=1; break;
    case 5: seg[0]=1; seg[5]=1; seg[6]=1; seg[2]=1; seg[3]=1; break; case 6: seg[0]=1; seg[5]=1; seg[6]=1; seg[4]=1; seg[3]=1; seg[2]=1; break;
    case 7: seg[0]=1; seg[1]=1; seg[2]=1; break; case 8: seg[0]=1; seg[1]=1; seg[2]=1; seg[3]=1; seg[4]=1; seg[5]=1; seg[6]=1; break;
    case 9: seg[0]=1; seg[1]=1; seg[2]=1; seg[3]=1; seg[5]=1; seg[6]=1; break;
  }
  uint16_t c = fgColor();
  if(seg[0]) display.fillRect(x, y, w, t, c); if(seg[1]) display.fillRect(x+w-t, y, t, h/2, c);
  if(seg[2]) display.fillRect(x+w-t, y+h/2, t, h/2, c); if(seg[3]) display.fillRect(x, y+h-t, w, t, c);
  if(seg[4]) display.fillRect(x, y+h/2, t, h/2, c); if(seg[5]) display.fillRect(x, y, t, h/2, c);
  if(seg[6]) display.fillRect(x, y+h/2-t/2, w, t, c);
}

void drawTime(int x, int y, int size, int h, int m, int s, bool showSec) {
  drawDigit(x, y, size, h/10); drawDigit(x+size+4, y, size, h%10);
  display.fillRect(x+size*2+8, y+size/2, size/5, size/5, fgColor()); display.fillRect(x+size*2+8, y+size*1.5, size/5, size/5, fgColor());
  drawDigit(x+size*2+16, y, size, m/10); drawDigit(x+size*3+20, y, size, m%10);
  if (showSec) { int ss = size/2; int sx = x+size*4+28; int sy = y + size; drawDigit(sx, sy, ss, s/10); drawDigit(sx+ss+4, sy, ss, s%10); }
}

void zobrazSvetovyCas() {
  struct tm timeinfo;
  bool gotTime = getLocalTime(&timeinfo, 10);
  display.setPartialWindow(0, 0, display.width(), display.height());
  display.firstPage();
  do {
    display.fillScreen(bgColor());
    u8g2Fonts.setFontMode(1);
    if (!gotTime) {
      u8g2Fonts.setFont(getTitleFont()); u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
      u8g2Fonts.setCursor(20, 70); u8g2Fonts.print("Čas není synchronizován");
    } else {
      int h = timeinfo.tm_hour; int m = timeinfo.tm_min; int s = timeinfo.tm_sec;
      int nyH = (h + 18) % 24; int bkkH = (h + 6) % 24; 
      char dateBuf[30]; strftime(dateBuf, sizeof(dateBuf), "Dnes je %d. %m. %Y", &timeinfo);
      String svatekJmeno = getSvatek(timeinfo.tm_mday, timeinfo.tm_mon + 1);

      u8g2Fonts.setFont(getTitleFont()); u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
      u8g2Fonts.setCursor(8, 15); u8g2Fonts.print("PRAHA");
      drawTime(8, 22, 16, h, m, s, true);
      
      u8g2Fonts.setFont(getSmallFont()); 
      u8g2Fonts.setCursor(140, 15); u8g2Fonts.print("NEW YORK");
      drawTime(140, 22, 8, nyH, m, 0, false);
      
      display.drawFastVLine(210, 5, 50, fgColor());
      
      u8g2Fonts.setCursor(220, 15); u8g2Fonts.print("BANGKOK");
      drawTime(220, 22, 8, bkkH, m, 0, false);

      display.drawFastHLine(0, 65, display.width(), fgColor());
      printCentered(String(dateBuf), 85, getTitleFont());
      
      u8g2Fonts.setFont(getBodyFont()); int w1 = u8g2Fonts.getUTF8Width("Svátek má: ");
      u8g2Fonts.setFont(getTitleFont()); int w2 = u8g2Fonts.getUTF8Width(svatekJmeno.c_str());
      int startX = (display.width() - (w1 + w2)) / 2;
      u8g2Fonts.setFont(getBodyFont()); u8g2Fonts.setCursor(startX, 105); u8g2Fonts.print("Svátek má: ");
      u8g2Fonts.setFont(getTitleFont()); u8g2Fonts.setCursor(startX + w1, 105); u8g2Fonts.print(svatekJmeno.c_str());
      
      u8g2Fonts.setFont(getSmallFont()); u8g2Fonts.setCursor(5, 125); u8g2Fonts.print("Drž BTN21 = zpět");
      nakresliStatusBar();
    }
  } while (display.nextPage());
}

void zobrazKurzyUI() {
  display.setPartialWindow(0, 0, display.width(), display.height());
  display.firstPage();
  do {
    display.fillScreen(bgColor());
    u8g2Fonts.setFontMode(1);
    u8g2Fonts.setFont(getTitleFont()); u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
    u8g2Fonts.setCursor(5, 18); u8g2Fonts.print("FINANČNÍ KURZY (CZK)");
    nakresliStatusBar();
    display.drawFastHLine(0, 23, display.width(), fgColor());

    u8g2Fonts.setFont(getBodyFont());
    u8g2Fonts.setCursor(10, 45); u8g2Fonts.print("1 EUR = " + kurzEur + " Kč");
    u8g2Fonts.setCursor(10, 65); u8g2Fonts.print("1 USD = " + kurzUsd + " Kč");
    u8g2Fonts.setCursor(10, 85); u8g2Fonts.print("1 BTC = " + kurzBtc + " Kč");

    u8g2Fonts.setCursor(160, 45); u8g2Fonts.print("Zlato (1g):");
    u8g2Fonts.setFont(getTitleFont()); u8g2Fonts.setCursor(160, 62); u8g2Fonts.print(kurzZlato + " Kč");
    u8g2Fonts.setFont(getBodyFont()); u8g2Fonts.setCursor(160, 85); u8g2Fonts.print("Stříbro (1g):");
    u8g2Fonts.setFont(getTitleFont()); u8g2Fonts.setCursor(160, 102); u8g2Fonts.print(kurzStribro + " Kč");

    u8g2Fonts.setFont(getSmallFont()); u8g2Fonts.setCursor(5, 125); u8g2Fonts.print("Drž BTN21 = zpět");
  } while (display.nextPage());
}

void nakresliIkonuPocasi(String typ, int x, int y) {
  uint16_t f = fgColor(); uint16_t b = bgColor();
  if (typ == "SLUNCE") {
    display.drawCircle(x, y, 14, f); display.drawCircle(x, y, 15, f);
    display.drawLine(x, y - 19, x, y - 24, f); display.drawLine(x, y + 19, x, y + 24, f);
    display.drawLine(x - 19, y, x - 24, y, f); display.drawLine(x + 19, y, x + 24, y, f);
    display.drawLine(x - 13, y - 13, x - 17, y - 17, f); display.drawLine(x + 13, y + 13, x + 17, y + 17, f);
    display.drawLine(x - 13, y + 13, x - 17, y + 17, f); display.drawLine(x + 13, y - 13, x + 17, y - 17, f);
  } else if (typ == "MRAKY" || typ == "DEST" || typ == "BOURKA" || typ == "SNIH" || typ == "OBLACNO") {
    if (typ == "OBLACNO") { display.drawCircle(x + 15, y - 10, 10, f); display.drawLine(x + 28, y - 10, x + 32, y - 10, f); display.drawLine(x + 15, y - 23, x + 15, y - 27, f); }
    display.fillCircle(x - 12, y + 5, 12, f); display.fillCircle(x + 12, y + 5, 12, f); display.fillCircle(x, y - 2, 14, f); display.fillRect(x - 12, y + 5, 24, 13, f);
    display.fillCircle(x - 12, y + 5, 10, b); display.fillCircle(x + 12, y + 5, 10, b); display.fillCircle(x, y - 2, 12, b); display.fillRect(x - 12, y + 5, 24, 11, b);
    if (typ == "DEST") { display.drawLine(x - 10, y + 22, x - 15, y + 32, f); display.drawLine(x, y + 22, x - 5, y + 32, f); display.drawLine(x + 10, y + 22, x + 5, y + 32, f); } 
    else if (typ == "BOURKA") { display.drawLine(x, y + 20, x - 5, y + 28, f); display.drawLine(x - 5, y + 28, x + 2, y + 28, f); display.drawLine(x + 2, y + 28, x - 4, y + 38, f); } 
    else if (typ == "SNIH") { display.drawPixel(x - 10, y + 24, f); display.drawPixel(x - 10, y + 28, f); display.drawPixel(x, y + 22, f); display.drawPixel(x, y + 27, f); display.drawPixel(x + 10, y + 24, f); display.drawPixel(x + 10, y + 28, f); }
  }
}

void zobrazPocasiDen(int denIdx) {
  display.setPartialWindow(0, 0, display.width(), display.height());
  display.firstPage();
  do {
    display.fillScreen(bgColor());
    u8g2Fonts.setFontMode(1);
    u8g2Fonts.setFont(getTitleFont()); u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
    
    String denZkratka = "";
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 10)) {
      const char* dnyVTydnu[] = {"Ne", "Po", "Ut", "St", "Ct", "Pa", "So"};
      int hledanyDen = (timeinfo.tm_wday + denIdx) % 7;
      denZkratka = String(dnyVTydnu[hledanyDen]) + " ";
    }
    
    String nadpis = "POČASÍ: " + denZkratka + predpoved[denIdx].datum + " (" + String(denIdx + 1) + "/7)";
    u8g2Fonts.setCursor(10, 18); u8g2Fonts.print(nadpis.c_str());
    
    nakresliStatusBar();
    display.drawFastHLine(0, 23, display.width(), fgColor());

    nakresliIkonuPocasi(predpoved[denIdx].ikona, 240, 50);

    u8g2Fonts.setFont(getBigFont()); u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
    String textMax = "Den: " + String(predpoved[denIdx].tMax) + " °C";
    u8g2Fonts.setCursor(10, 48); u8g2Fonts.print(textMax.c_str());
    
    u8g2Fonts.setFont(getTitleFont()); u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
    String textMin = "Noc: " + String(predpoved[denIdx].tMin) + " °C";
    u8g2Fonts.setCursor(10, 68); u8g2Fonts.print(textMin.c_str());
    
    u8g2Fonts.setFont(getSmallFont()); u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
    String detaily = "Srážky: " + predpoved[denIdx].srazky + " mm | Vítr: " + String(predpoved[denIdx].vitr) + " m/s";
    u8g2Fonts.setCursor(10, 85); u8g2Fonts.print(detaily.c_str());

    String astroData = "Slunce: Východ " + predpoved[denIdx].vychod + " | Západ " + predpoved[denIdx].zapad;
    u8g2Fonts.setCursor(10, 102); u8g2Fonts.print(astroData.c_str());

    display.drawFastHLine(0, 112, display.width(), fgColor());
    u8g2Fonts.setCursor(5, 125); u8g2Fonts.print("BTN0=další den | Drž BTN21=zpět");
  } while (display.nextPage());
}

void zobrazSeznamZprav() {
  display.setPartialWindow(0, 0, display.width(), display.height());
  display.firstPage();
  do {
    display.fillScreen(bgColor());
    u8g2Fonts.setFontMode(1);
    u8g2Fonts.setFont(getTitleFont()); u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
    String t = "VÝBĚR (" + String(clanekMenuIndex + 1) + "/" + String(pocetZprav) + ")";
    u8g2Fonts.setCursor(5, 18); u8g2Fonts.print(t.c_str());
    nakresliStatusBar();
    display.drawFastHLine(0, 23, display.width(), fgColor());

    int y = 38; String tit = aktualniZpravy[clanekMenuIndex].titulek;
    TextLine tLines[3]; int maxW = display.width() - 10;
    int tCount = zalamejText(tit.c_str(), tit.length(), tLines, 3, maxW);
    for (int i = 0; i < tCount; i++) {
      char tBuf[80]; int l = tLines[i].len; if (l > 79) l = 79;
      strncpy(tBuf, &tit[tLines[i].start], l); tBuf[l] = '\0';
      u8g2Fonts.setCursor(5, y); u8g2Fonts.print(tBuf); y += 14;
    }

    u8g2Fonts.setFont(getSmallFont());
    y += 4; String per = aktualniZpravy[clanekMenuIndex].perex;
    TextLine pLines[4]; int pCount = zalamejText(per.c_str(), per.length(), pLines, 4, maxW);
    for (int i = 0; i < pCount; i++) {
      if (y > 105) break; 
      char pBuf[80]; int l = pLines[i].len; if (l > 79) l = 79;
      strncpy(pBuf, &per[pLines[i].start], l); pBuf[l] = '\0';
      u8g2Fonts.setCursor(5, y); u8g2Fonts.print(pBuf); y += 12; 
    }
    display.drawFastHLine(0, 112, display.width(), fgColor());
    u8g2Fonts.setCursor(5, 125); u8g2Fonts.print("BTN0=další | BTN21=číst | Drž=zpět");
  } while (display.nextPage());
}

void zobrazText(const char* title, const char* text) {
  char buf[3500]; strncpy_P(buf, text, sizeof(buf) - 1); buf[sizeof(buf) - 1] = '\0';
  int len = strlen(buf); int maxWidth = display.width() - 10;
  int lineHeight = getLineHeight(); int linesPerPage = getLinesPerPage();
  TextLine lines[200]; int lineCount = zalamejText(buf, len, lines, 200, maxWidth);
  int totalPages = (lineCount + linesPerPage - 1) / linesPerPage; if (totalPages < 1) totalPages = 1;
  if (textScrollPage >= totalPages) textScrollPage = 0;
  int startLine = textScrollPage * linesPerPage;
  display.setPartialWindow(0, 0, display.width(), display.height());
  display.firstPage();
  do {
    display.fillScreen(bgColor());
    u8g2Fonts.setFontMode(1);
    u8g2Fonts.setFont(getTitleFont()); u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
    u8g2Fonts.setCursor(5, 16); u8g2Fonts.print(title);
    nakresliStatusBar();
    display.drawFastHLine(0, 20, display.width(), fgColor());
    u8g2Fonts.setFont(getBodyFont());
    int y = 36;
    for (int i = 0; i < linesPerPage; i++) {
      int idx = startLine + i; if (idx >= lineCount) break;
      char lineBuf[80]; int l = lines[idx].len; if (l > 79) l = 79;
      strncpy(lineBuf, &buf[lines[idx].start], l); lineBuf[l] = '\0';
      u8g2Fonts.setCursor(5, y); u8g2Fonts.print(lineBuf); y += lineHeight;
    }
    u8g2Fonts.setFont(getSmallFont());
    String info = String(textScrollPage + 1) + "/" + String(totalPages);
    u8g2Fonts.setCursor(240, 125); u8g2Fonts.print(info.c_str());
  } while (display.nextPage());
}

void drawFlaskaFrame(float angle, const char* msg) {
  int cx = 148, cy = 64, r = 50; float rad = angle * PI / 180.0; float cosA = cos(rad); float sinA = sin(rad);
  int bodyX[] = {-25, 10, 10, -25}; int bodyY[] = {-12, -12, 12, 12};
  int neckX[] = {10, 30, 30, 10}; int neckY[] = {-5, -5, 5, 5};
  int labelX[] = {-8, 0, 0, -8}; int labelY[] = {-12, -12, 12, 12}; 
  display.setPartialWindow(0, 0, display.width(), display.height());
  display.firstPage();
  do {
    display.fillScreen(bgColor()); u8g2Fonts.setFontMode(1);
    display.drawCircle(cx, cy, r, fgColor()); display.drawCircle(cx, cy, r + 1, fgColor());
    int rbx[4], rby[4]; for(int i=0; i<4; i++) { rbx[i] = cx + (int)(bodyX[i]*cosA - bodyY[i]*sinA); rby[i] = cy + (int)(bodyX[i]*sinA + bodyY[i]*cosA); }
    display.fillTriangle(rbx[0], rby[0], rbx[1], rby[1], rbx[2], rby[2], fgColor()); display.fillTriangle(rbx[0], rby[0], rbx[2], rby[2], rbx[3], rby[3], fgColor());
    int rlx[4], rly[4]; for(int i=0; i<4; i++) { rlx[i] = cx + (int)(labelX[i]*cosA - labelY[i]*sinA); rly[i] = cy + (int)(labelX[i]*sinA + labelY[i]*cosA); }
    display.fillTriangle(rlx[0], rly[0], rlx[1], rly[1], rlx[2], rly[2], bgColor()); display.fillTriangle(rlx[0], rly[0], rlx[2], rly[2], rlx[3], rly[3], bgColor());
    int rnx[4], rny[4]; for(int i=0; i<4; i++) { rnx[i] = cx + (int)(neckX[i]*cosA - neckY[i]*sinA); rny[i] = cy + (int)(neckX[i]*sinA + neckY[i]*cosA); }
    display.fillTriangle(rnx[0], rny[0], rnx[1], rny[1], rnx[2], rny[2], fgColor()); display.fillTriangle(rnx[0], rny[0], rnx[2], rny[2], rnx[3], rny[3], fgColor()); display.drawLine(rnx[1], rny[1], rnx[2], rny[2], bgColor());
    if (msg) { u8g2Fonts.setFont(getSmallFont()); u8g2Fonts.setCursor(5, 12); u8g2Fonts.print(msg); }
  } while (display.nextPage());
}

void hrajFlasku() {
  float uhel = random(0, 360); drawFlaskaFrame(uhel, "Točím..."); delay(500); drawFlaskaFrame(uhel + 90, "Točím..."); delay(500); drawFlaskaFrame(uhel + 180, "Točím..."); delay(500); drawFlaskaFrame(random(0, 360), "Stiskni BTN21=znovu | Drž BTN0=zpět");
}

void drawDots(int bx, int by, int boxSize, int hodnota) {
  int cx = bx + boxSize / 2, cy = by + boxSize / 2, off = boxSize / 4, r = boxSize / 16; if (r < 3) r = 3;
  int tlx = cx - off, tly = cy - off, trx = cx + off, try_ = cy - off, mlx = cx - off, mly = cy, mrx = cx + off, mry = cy, blx = cx - off, bly = cy + off, brx = cx + off, bry = cy + off;
  switch (hodnota) {
    case 1: display.fillCircle(cx, cy, r, fgColor()); break;
    case 2: display.fillCircle(trx, try_, r, fgColor()); display.fillCircle(blx, bly, r, fgColor()); break;
    case 3: display.fillCircle(trx, try_, r, fgColor()); display.fillCircle(cx, cy, r, fgColor()); display.fillCircle(blx, bly, r, fgColor()); break;
    case 4: display.fillCircle(tlx, tly, r, fgColor()); display.fillCircle(trx, try_, r, fgColor()); display.fillCircle(blx, bly, r, fgColor()); display.fillCircle(brx, bry, r, fgColor()); break;
    case 5: display.fillCircle(tlx, tly, r, fgColor()); display.fillCircle(trx, try_, r, fgColor()); display.fillCircle(cx, cy, r, fgColor()); display.fillCircle(blx, bly, r, fgColor()); display.fillCircle(brx, bry, r, fgColor()); break;
    case 6: display.fillCircle(tlx, tly, r, fgColor()); display.fillCircle(trx, try_, r, fgColor()); display.fillCircle(mlx, mly, r, fgColor()); display.fillCircle(mrx, mry, r, fgColor()); display.fillCircle(blx, bly, r, fgColor()); display.fillCircle(brx, bry, r, fgColor()); break;
  }
}

void drawPolygon(int cx, int cy, int radius, int sides) {
  for (int i = 0; i < sides; i++) {
    float a1 = i * (2 * PI / sides) - (PI / 2); float a2 = (i + 1) * (2 * PI / sides) - (PI / 2);
    display.drawLine(cx + radius * cos(a1), cy + radius * sin(a1), cx + radius * cos(a2), cy + radius * sin(a2), fgColor());
    display.drawLine(cx + (radius-1) * cos(a1), cy + (radius-1) * sin(a1), cx + (radius-1) * cos(a2), cy + (radius-1) * sin(a2), fgColor());
  }
}

void nakresliJednuKostku(int bx, int by, int boxSize, int cislo, int strany, bool finale) {
  int cx = bx + boxSize/2; int cy = by + boxSize/2; int r = boxSize/2;
  if (strany == 12) drawPolygon(cx, cy, r, 6);
  else if (strany == 24) drawPolygon(cx, cy, r, 8);
  else { display.drawRoundRect(bx, by, boxSize, boxSize, 8, fgColor()); display.drawRoundRect(bx + 1, by + 1, boxSize - 2, boxSize - 2, 7, fgColor()); }
  
  if (strany == 6) { drawDots(bx, by, boxSize, cislo); } 
  else {
    u8g2Fonts.setFont(getTitleFont()); u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
    String s = String(cislo); int tw = s.length() * (finale ? 14 : 10);
    u8g2Fonts.setCursor(cx - tw / 2, cy + (finale ? 8 : 6)); u8g2Fonts.print(s.c_str());
  }
}

void drawKostkaFrame(int c1, int c2, int strany, const char* msg, bool finale) {
  display.setPartialWindow(0, 0, display.width(), display.height());
  display.firstPage();
  do {
    display.fillScreen(bgColor()); 
    u8g2Fonts.setFontMode(1);
    nakresliStatusBar();
    
    if (strany == 66) { 
      int boxSize = 55; int by = (128 - boxSize) / 2;
      nakresliJednuKostku(45, by, boxSize, c1, 6, finale); 
      nakresliJednuKostku(115, by, boxSize, c2, 6, finale);
      u8g2Fonts.setFont(getBodyFont()); u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor()); 
      u8g2Fonts.setCursor(185, 64); u8g2Fonts.print("Součet:"); 
      u8g2Fonts.setFont(getBigFont()); u8g2Fonts.setCursor(200, 95); u8g2Fonts.print(String(c1+c2).c_str());
    } else { 
      int boxSize = 80; int by = (128 - boxSize) / 2;
      nakresliJednuKostku(30, by, boxSize, c1, strany, finale);
      u8g2Fonts.setFont(getTitleFont()); u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor()); 
      String hozenoStr = "Hozeno: " + String(c1); u8g2Fonts.setCursor(140, 70); u8g2Fonts.print(hozenoStr.c_str());
      u8g2Fonts.setFont(getSmallFont()); String kostkaStr = "Kostka: D" + String(strany); u8g2Fonts.setCursor(140, 90); u8g2Fonts.print(kostkaStr.c_str());
    }
    if (msg) { u8g2Fonts.setFont(getSmallFont()); u8g2Fonts.setCursor(5, 124); u8g2Fonts.print(msg); }
  } while (display.nextPage());
}
void zobrazKostkyIdle() { drawKostkaFrame(1, 1, kostkyStran, "Drž BTN0=třást | BTN21=hodit", true); }
void hrajKostku(int strany) {
  int maxCislo = (strany == 66) ? 6 : strany;
  int c1 = random(1, maxCislo + 1); int c2 = (strany == 66) ? random(1, 7) : 0;
  drawKostkaFrame(random(1, maxCislo+1), random(1, maxCislo+1), strany, "Míchám...", false);
  delay(600); drawKostkaFrame(c1, c2, strany, "Drž BTN0=třást | BTN21=hodit", true);
}

void drawStopky(unsigned long ms, bool running) {
  unsigned long totalSec = ms / 1000; int min = totalSec / 60; int sec = totalSec % 60; int desetiny = (ms % 1000) / 100;
  int cx = 215, cy = 64, r = 45; float angle = (sec / 60.0) * 360.0 - 90.0; float rad = angle * PI / 180.0;
  int hx = cx + (int)(36 * cos(rad)); int hy = cy + (int)(36 * sin(rad));
  
  display.setPartialWindow(0, 0, display.width(), display.height());
  display.firstPage();
  do {
    display.fillScreen(bgColor()); nakresliStatusBar();
    
    display.drawCircle(cx, cy, r, fgColor()); display.drawCircle(cx, cy, r + 1, fgColor()); display.fillCircle(cx, cy, 3, fgColor());
    for (int i = 0; i < 12; i++) {
      float a = (i * 30.0 - 90.0) * PI / 180.0; int x1 = cx + (int)(41 * cos(a)), y1 = cy + (int)(41 * sin(a)); int x2 = cx + (int)(45 * cos(a)), y2 = cy + (int)(45 * sin(a)); display.drawLine(x1, y1, x2, y2, fgColor());
    }
    display.drawLine(cx, cy, hx, hy, fgColor()); display.drawLine(cx + 1, cy, hx + 1, hy, fgColor()); display.drawLine(cx, cy + 1, hx, hy + 1, fgColor());
    
    u8g2Fonts.setFontMode(0); 
    u8g2Fonts.setForegroundColor(fgColor()); 
    u8g2Fonts.setBackgroundColor(bgColor());
    
    char timeBuf[12]; sprintf(timeBuf, "%02d:%02d", min, sec);
    u8g2Fonts.setFont(getBigFont()); // Použije tvůj nastavený velký font
    u8g2Fonts.setCursor(10, 52); 
    u8g2Fonts.print(timeBuf);
    
    sprintf(timeBuf, ".%d", desetiny); 
    u8g2Fonts.setFont(getTitleFont()); 
    u8g2Fonts.setCursor(95, 52); 
    u8g2Fonts.print(timeBuf);
    
    u8g2Fonts.setFont(getTitleFont()); u8g2Fonts.setCursor(10, 80); u8g2Fonts.print(running ? "BĚŽÍ" : "STOP");
    
    u8g2Fonts.setFontMode(1); 
    u8g2Fonts.setFont(getSmallFont()); u8g2Fonts.setCursor(5, 105); u8g2Fonts.print("BTN21 = start/stop"); u8g2Fonts.setCursor(5, 117); u8g2Fonts.print("BTN0 = reset"); u8g2Fonts.setCursor(5, 127); u8g2Fonts.print("Drž BTN21 = zpět");
  } while (display.nextPage());
}

void zobrazGBNode() {
  GBNode node; memcpy_P(&node, &gamebook[gbNode], sizeof(GBNode));
  char buf[600]; strncpy_P(buf, (const char*)node.text, sizeof(buf) - 1); buf[sizeof(buf) - 1] = '\0';
  char popisA[60], popisB[60]; strncpy_P(popisA, (const char*)node.popisA, sizeof(popisA) - 1); popisA[sizeof(popisA) - 1] = '\0'; strncpy_P(popisB, (const char*)node.popisB, sizeof(popisB) - 1); popisB[sizeof(popisB) - 1] = '\0';
  int volbaA, volbaB; memcpy_P(&volbaA, &gamebook[gbNode].volbaA, sizeof(int)); memcpy_P(&volbaB, &gamebook[gbNode].volbaB, sizeof(int));
  bool konec = (volbaA == -1 && volbaB == -1);
  int maxWidth = display.width() - 10; int lineHeight = getLineHeight(); int textAreaHeight = konec ? 90 : 70;
  int linesPerPage = textAreaHeight / lineHeight; TextLine lines[80];
  int lineCount = zalamejText(buf, strlen(buf), lines, 80, maxWidth);
  int totalPages = (lineCount + linesPerPage - 1) / linesPerPage; if (totalPages < 1) totalPages = 1;
  if (gbTextPage >= totalPages) gbTextPage = totalPages - 1;
  int startLine = gbTextPage * linesPerPage; bool lastPage = (gbTextPage >= totalPages - 1);
  display.setPartialWindow(0, 0, display.width(), display.height());
  display.firstPage();
  do {
    display.fillScreen(bgColor()); u8g2Fonts.setFontMode(1); nakresliStatusBar();
    u8g2Fonts.setFont(getBodyFont()); u8g2Fonts.setCursor(5, 16); u8g2Fonts.print("ZAKLÍNAČ #" + String(gbNode));
    String stranky = String(gbTextPage + 1) + "/" + String(totalPages); u8g2Fonts.setCursor(220, 16); u8g2Fonts.print(stranky.c_str());
    display.drawFastHLine(0, 20, display.width(), fgColor());
    int y = 34;
    for (int i = 0; i < linesPerPage; i++) {
      int idx = startLine + i; if (idx >= lineCount) break; char lineBuf[80]; int l = lines[idx].len; if (l > 79) l = 79;
      strncpy(lineBuf, &buf[lines[idx].start], l); lineBuf[l] = '\0'; u8g2Fonts.setCursor(5, y); u8g2Fonts.print(lineBuf); y += lineHeight;
    }
    if (lastPage && !konec) {
      display.drawFastHLine(0, 98, display.width(), fgColor()); u8g2Fonts.setFont(getSmallFont());
      String textA = String("BTN21: ") + String(popisA); String textB = String("Drž BTN0: ") + String(popisB);
      u8g2Fonts.setCursor(5, 110); u8g2Fonts.print(textA.c_str()); u8g2Fonts.setCursor(5, 124); u8g2Fonts.print(textB.c_str());
    } else if (lastPage && konec) {
      display.drawFastHLine(0, 105, display.width(), fgColor()); u8g2Fonts.setFont(getBodyFont()); u8g2Fonts.setCursor(5, 118); u8g2Fonts.print("KONEC! BTN21=znovu | Drž=zpět");
    } else { u8g2Fonts.setFont(getSmallFont()); u8g2Fonts.setCursor(5, 124); u8g2Fonts.print("BTN0 = další stránka"); }
  } while (display.nextPage());
}

void drawScaledBitmap(int x, int y, const unsigned char *bitmap, int w, int h, uint16_t color, int scale, bool masked) {
  int byteWidth = (w + 7) / 8;
  for (int j = 0; j < h; j++) {
    for (int i = 0; i < w; i++) {
      int blockIdx = (j / 8) * 8 + (i / 8); 
      if (masked && !odhalenoPole[blockIdx]) continue; 
      if (pgm_read_byte(bitmap + j * byteWidth + i / 8) & (128 >> (i & 7))) {
        display.fillRect(x + i * scale, y + j * scale, scale, scale, color);
      }
    }
  }
}

void zobrazOdhalovacku() {
  display.setPartialWindow(0, 0, display.width(), display.height());
  display.firstPage();
  do {
    display.fillScreen(bgColor()); u8g2Fonts.setFontMode(1); nakresliStatusBar();
    u8g2Fonts.setFont(getTitleFont()); u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
    u8g2Fonts.setCursor(5, 16);
    String tit = "ODHALOVAČKA (Skóre: " + String(odhalovackaSkore) + ")";
    u8g2Fonts.print(tit.c_str());
    display.drawFastHLine(0, 20, display.width(), fgColor());

    int imgX = (display.width() - 128) / 2; int imgY = 25; 
    
    drawScaledBitmap(imgX, imgY, img_slunce, 64, 64, fgColor(), 2, true);

    u8g2Fonts.setFont(getSmallFont());
    u8g2Fonts.setCursor(5, 124); u8g2Fonts.print("BTN21 = Vím to! | BTN0 = Zpět");
  } while (display.nextPage());
}

void zobrazOdhalovackuDetail() {
  display.setPartialWindow(0, 0, display.width(), display.height());
  display.firstPage();
  do {
    display.fillScreen(bgColor()); u8g2Fonts.setFontMode(1); nakresliStatusBar();
    drawScaledBitmap(10, 0, img_slunce, 64, 64, fgColor(), 2, false); 
    
    u8g2Fonts.setFont(getBigFont());
    u8g2Fonts.setForegroundColor(fgColor());
    u8g2Fonts.setCursor(145, 64);
    u8g2Fonts.print(odhalovackaOdpoved.c_str());

    u8g2Fonts.setFont(getSmallFont());
    u8g2Fonts.setCursor(145, 120); u8g2Fonts.print("Drž BTN21=zpět");
  } while (display.nextPage());
}

// ===== SLOVNÍK A GENERÁTOR =====
String odstranDiakritiku(String s) {
  String out = "";
  for (int i = 0; i < s.length(); i++) {
    unsigned char c = s[i];
    if (c < 128) { out += (char)c; } 
    else if (c == 0xC3 || c == 0xC4 || c == 0xC5) {
      if (i + 1 < s.length()) {
        unsigned char next = s[i+1];
        if (c == 0xC3) {
          if (next == 0xA1) out += 'a'; else if (next == 0xA9) out += 'e'; else if (next == 0xAD) out += 'i';
          else if (next == 0xB3) out += 'o'; else if (next == 0xBA) out += 'u'; else if (next == 0xBD) out += 'y';
        } else if (c == 0xC4) {
          if (next == 0x8D) out += 'c'; else if (next == 0x8F) out += 'd'; else if (next == 0x9B) out += 'e';
        } else if (c == 0xC5) {
          if (next == 0x88) out += 'n'; else if (next == 0x99) out += 'r'; else if (next == 0xA1) out += 's';
          else if (next == 0xA5) out += 't'; else if (next == 0xAF) out += 'u'; else if (next == 0xBE) out += 'z';
        } i++;
      }
    }
  } return out;
}

void zobrazHledani() {
  int shody[10]; int shodCount = 0;
  for (int i = 0; i < slovnikDataCount && shodCount < 10; i++) {
    SlovnikSlovo s; memcpy_P(&s, &slovnikData[i], sizeof(SlovnikSlovo));
    char zdroj[50]; if (hledanySmer == 0) strncpy_P(zdroj, (const char*)s.cz, sizeof(zdroj) - 1); else strncpy_P(zdroj, (const char*)s.en, sizeof(zdroj) - 1);
    zdroj[sizeof(zdroj) - 1] = '\0';
    String cistyZdroj = odstranDiakritiku(String(zdroj)); cistyZdroj.toLowerCase(); String hledaneStr = String(hledanyText); hledaneStr.toLowerCase();
    if (hledanyLen > 0 && cistyZdroj.startsWith(hledaneStr)) { shody[shodCount] = i; shodCount++; }
  }
  display.setPartialWindow(0, 0, display.width(), display.height());
  display.firstPage();
  do {
    display.fillScreen(bgColor()); u8g2Fonts.setFontMode(1); nakresliStatusBar();
    u8g2Fonts.setFont(getBodyFont()); u8g2Fonts.setCursor(5, 16); u8g2Fonts.print(hledanySmer == 0 ? "HLEDAT (CZ): " : "HLEDAT (EN): ");
    u8g2Fonts.print(hledanyText); u8g2Fonts.print("_"); display.drawFastHLine(0, 20, display.width(), fgColor());
    u8g2Fonts.setFont(getSmallFont());
    for (int i = 0; i < abcCount; i++) {
      int col = i % 13; int row = i / 13; int x = 5 + col * 22; int y = 32 + row * 14;
      if (i == abcKurzor) { display.fillRect(x - 2, y - 10, 18, 13, fgColor()); u8g2Fonts.setForegroundColor(bgColor()); u8g2Fonts.setBackgroundColor(fgColor()); } 
      else { u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor()); }
      char ch[2] = { abeceda[i], '\0' }; u8g2Fonts.setCursor(x, y); u8g2Fonts.print(ch);
    }
    display.drawFastHLine(0, 60, display.width(), fgColor()); u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
    if (hledanyLen == 0) { u8g2Fonts.setCursor(5, 74); u8g2Fonts.print("Zadej písmena..."); } 
    else if (shodCount == 0) { u8g2Fonts.setCursor(5, 74); u8g2Fonts.print("Nic nenalezeno."); } 
    else {
      int y = 74;
      for (int i = 0; i < shodCount; i++) {
        int idx = shody[i]; SlovnikSlovo s; memcpy_P(&s, &slovnikData[idx], sizeof(SlovnikSlovo));
        char cz[30], en[30]; strncpy_P(cz, (const char*)s.cz, sizeof(cz) - 1); cz[sizeof(cz) - 1] = '\0'; strncpy_P(en, (const char*)s.en, sizeof(en) - 1); en[sizeof(en) - 1] = '\0';
        u8g2Fonts.setCursor(5, y); String preklad = (hledanySmer == 0) ? (String(cz) + " = " + String(en)) : (String(en) + " = " + String(cz)); u8g2Fonts.print(preklad.c_str()); y += 10;
      }
    }
    u8g2Fonts.setFont(getSmallFont()); u8g2Fonts.setCursor(5, 125); u8g2Fonts.print("BTN0=písmeno BTN21=potvrď Drž=zpět");
  } while (display.nextPage());
}

void zobrazFraze(const char* title, const Fraze* data, int count) {
  int startIdx = textScrollPage * 4; int totalPages = (count + 3) / 4; if (textScrollPage >= totalPages) textScrollPage = 0;
  display.setPartialWindow(0, 0, display.width(), display.height());
  display.firstPage();
  do {
    display.fillScreen(bgColor()); u8g2Fonts.setFontMode(1); nakresliStatusBar();
    u8g2Fonts.setFont(getBodyFont()); u8g2Fonts.setCursor(5, 16); u8g2Fonts.print(title); 
    String stranky = String(textScrollPage + 1) + "/" + String(totalPages); u8g2Fonts.setCursor(220, 16); u8g2Fonts.print(stranky.c_str());
    display.drawFastHLine(0, 20, display.width(), fgColor());
    int y = 32;
    for (int i = 0; i < 4 && (startIdx + i) < count; i++) {
      Fraze f; memcpy_P(&f, &data[startIdx + i], sizeof(Fraze));
      char cz[60], en[60]; strncpy_P(cz, (const char*)f.cz, sizeof(cz) - 1); cz[sizeof(cz) - 1] = '\0'; strncpy_P(en, (const char*)f.en, sizeof(en) - 1); en[sizeof(en) - 1] = '\0';
      u8g2Fonts.setFont(getBodyFont()); u8g2Fonts.setCursor(5, y); u8g2Fonts.print(cz); y += 11;
      u8g2Fonts.setFont(getSmallFont()); u8g2Fonts.setCursor(15, y); u8g2Fonts.print(en); y += 14;
    }
  } while (display.nextPage());
}

void zobrazUceni() {
  SlovnikSlovo s; memcpy_P(&s, &slovnikData[uceniIdx], sizeof(SlovnikSlovo));
  char cz[30], en[30]; strncpy_P(cz, (const char*)s.cz, sizeof(cz) - 1); cz[sizeof(cz) - 1] = '\0'; strncpy_P(en, (const char*)s.en, sizeof(en) - 1); en[sizeof(en) - 1] = '\0';
  const char* otazka = (uceniSmer == 0) ? cz : en; const char* odpoved = (uceniSmer == 0) ? en : cz; const char* smerText = (uceniSmer == 0) ? "CZ -> EN" : "EN -> CZ";
  
  display.setPartialWindow(0, 0, display.width(), display.height());
  display.firstPage();
  do {
    display.fillScreen(bgColor()); nakresliStatusBar();
    u8g2Fonts.setFontMode(0); 
    u8g2Fonts.setForegroundColor(fgColor()); 
    u8g2Fonts.setBackgroundColor(bgColor());
    
    u8g2Fonts.setFont(getBodyFont()); u8g2Fonts.setCursor(5, 16); u8g2Fonts.print("UČENÍ"); 
    u8g2Fonts.setCursor(200, 16); u8g2Fonts.print(smerText); 
    display.drawFastHLine(0, 20, display.width(), fgColor());
    
    u8g2Fonts.setFont(getBigFont()); 
    u8g2Fonts.setCursor(15, 50); u8g2Fonts.print(otazka); 
    display.drawFastHLine(10, 60, display.width() - 20, fgColor());
    
    u8g2Fonts.setCursor(15, 95); 
    if (uceniOdhaleno) u8g2Fonts.print(odpoved); else u8g2Fonts.print("? ? ?");
    
    u8g2Fonts.setFontMode(1); 
    u8g2Fonts.setFont(getSmallFont());
    if (uceniOdhaleno) { u8g2Fonts.setCursor(5, 124); u8g2Fonts.print("BTN21 = další slovo | BTN0 = změna směru | Drž = zpět"); } 
    else { u8g2Fonts.setCursor(5, 124); u8g2Fonts.print("BTN21 = odhalit | BTN0 = změna směru | Drž = zpět"); }
  } while (display.nextPage());
}

void zobrazGeneratorResult(const char* title, const char* text) {
  int maxWidth = display.width() - 10; 
  char buf[500]; strncpy(buf, text, sizeof(buf) - 1); buf[sizeof(buf) - 1] = '\0';
  
  TextLine lines[50]; 
  int lineCount = zalamejText(buf, strlen(buf), lines, 50, maxWidth);

  int lineHeight = 16; 
  int linesPerPage = 100 / lineHeight;

  display.setPartialWindow(0, 0, display.width(), display.height());
  display.firstPage();
  do {
    display.fillScreen(bgColor()); u8g2Fonts.setFontMode(1); nakresliStatusBar(); 
    
    u8g2Fonts.setFont(getTitleFont()); u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
    u8g2Fonts.setCursor(10, 16); u8g2Fonts.print(title); 
    display.drawFastHLine(0, 20, display.width(), fgColor());
    
    u8g2Fonts.setFont(getBodyFont()); 
    int totalH = lineCount * lineHeight;
    int startY = 25 + (103 - totalH) / 2 + (lineHeight / 2); 
    if (startY < 40) startY = 40; 
    int y = startY;

    for (int i = 0; i < linesPerPage && i < lineCount; i++) {
      char lineBuf[80]; int l = lines[i].len; if (l > 79) l = 79; 
      strncpy(lineBuf, &buf[lines[i].start], l); lineBuf[l] = '\0';
      char* p = lineBuf; while(*p == ' ') p++; 
      
      int tw = u8g2Fonts.getUTF8Width(p);
      int cx = (display.width() - tw) / 2; 
      
      u8g2Fonts.setCursor(cx, y); 
      u8g2Fonts.print(p); 
      y += lineHeight;
    }
  } while (display.nextPage());
}

void zobrazLedStav() {
  display.setPartialWindow(0, 0, display.width(), display.height());
  display.firstPage();
  do {
    display.fillScreen(bgColor()); u8g2Fonts.setFontMode(1); nakresliStatusBar();
    u8g2Fonts.setFont(getTitleFont()); u8g2Fonts.setCursor(10, 18); u8g2Fonts.print("DIODA"); display.drawFastHLine(0, 23, display.width(), fgColor());
    u8g2Fonts.setFont(getBodyFont()); u8g2Fonts.setCursor(10, 55); u8g2Fonts.print(ledState ? "LED: ZAPNUTO" : "LED: VYPNUTO");
    u8g2Fonts.setCursor(10, 80); u8g2Fonts.print("Klikni = přepnout"); u8g2Fonts.setCursor(10, 105); u8g2Fonts.print("Drž = zpět");
  } while (display.nextPage());
}

// ==== TLAČÍTKA A NÁVRATY ====
bool longPress() { unsigned long start = millis(); while (digitalRead(BTN_SELECT) == LOW) { if (millis() - start > 500) { while (digitalRead(BTN_SELECT) == LOW) delay(10); return true; } delay(10); } return false; }
bool longPressBTN0() { unsigned long start = millis(); while (digitalRead(BTN_DOWN) == LOW) { if (millis() - start > 500) { while (digitalRead(BTN_DOWN) == LOW) delay(10); return true; } delay(10); } return false; }
void menuDown(int& idx, int& scr, int count) { int visible = getVisibleMenuItems(); idx = (idx + 1) % count; if (idx >= scr + visible) scr++; if (idx == 0) scr = 0; }

void goBack() {
  switch (appState) {
    case STATE_KNIHOVNA_DETAIL: ulozPoziciKnihy(subMenuIndex, textScrollPage); appState = STATE_KNIHOVNA; zobrazSubMenu("KNIHOVNA", knihovnaItems, knihovnaCount, subMenuIndex, subScrollOffset); break;
    case STATE_KNIHOVNA: appState = STATE_MAIN_MENU; zobrazSubMenu("HLAVNÍ MENU", mainMenuItems, mainMenuCount, menuIndex, scrollOffset); break;
    
    case STATE_ZPRAVY_TEXT: appState = STATE_ZPRAVY_SEZNAM; zobrazSeznamZprav(); break;
    case STATE_ZPRAVY_SEZNAM: clanekMenuIndex = 0; appState = STATE_ZPRAVY_MENU; zobrazSubMenu("ZPRÁVY", zpravyMenuItems, zpravyMenuCount, subMenuIndex, subScrollOffset); break;
    case STATE_ZPRAVY_MENU: appState = STATE_AKTUALITY; subMenuIndex = 0; subScrollOffset = 0; zobrazSubMenu("AKTUALITY", aktualityItems, aktualityCount, subMenuIndex, subScrollOffset); break;
    case STATE_POCASI_UI: pocasiDen = 0; appState = STATE_AKTUALITY; zobrazSubMenu("AKTUALITY", aktualityItems, aktualityCount, subMenuIndex, subScrollOffset); break;
    case STATE_HODINY: case STATE_KURZY: appState = STATE_AKTUALITY; zobrazSubMenu("AKTUALITY", aktualityItems, aktualityCount, subMenuIndex, subScrollOffset); break;
    case STATE_AKTUALITY: appState = STATE_MAIN_MENU; zobrazSubMenu("HLAVNÍ MENU", mainMenuItems, mainMenuCount, menuIndex, scrollOffset); break;

    case STATE_FRAZE_DETAIL: appState = STATE_FRAZE_MENU; zobrazSubMenu("FRÁZE", frazeMenuItems, frazeMenuCount, subMenuIndex, subScrollOffset); break;
    case STATE_FRAZE_MENU: appState = STATE_SLOVNIK; subMenuIndex = 2; subScrollOffset = 0; zobrazSubMenu("SLOVNÍK", slovnikItems, slovnikCount, subMenuIndex, subScrollOffset); break;
    case STATE_UCENI: appState = STATE_SLOVNIK; subMenuIndex = 3; subScrollOffset = 0; zobrazSubMenu("SLOVNÍK", slovnikItems, slovnikCount, subMenuIndex, subScrollOffset); break;
    case STATE_SLOVNIK_DETAIL: appState = STATE_SLOVNIK; zobrazSubMenu("SLOVNÍK", slovnikItems, slovnikCount, subMenuIndex, subScrollOffset); break;
    case STATE_SLOVNIK: appState = STATE_MAIN_MENU; zobrazSubMenu("HLAVNÍ MENU", mainMenuItems, mainMenuCount, menuIndex, scrollOffset); break;
    
    case STATE_QR_ZOBRAZ: appState = STATE_QR_MENU; zobrazSubMenu("QR KÓDY", qrItems, qrCount, subMenuIndex, subScrollOffset); break;
    case STATE_QR_MENU: appState = STATE_TOOLBOX; subMenuIndex = 2; subScrollOffset = 0; zobrazSubMenu("TOOLBOX", toolboxItems, toolboxCount, subMenuIndex, subScrollOffset); break;
    case STATE_LED_ON: ledState = false; digitalWrite(LED_PIN, LOW); appState = STATE_TOOLBOX; zobrazSubMenu("TOOLBOX", toolboxItems, toolboxCount, subMenuIndex, subScrollOffset); break;
    case STATE_STOPKY: stopkyRunning = false; stopkyElapsed = 0; appState = STATE_TOOLBOX; zobrazSubMenu("TOOLBOX", toolboxItems, toolboxCount, subMenuIndex, subScrollOffset); break;
    case STATE_TOOLBOX: appState = STATE_MAIN_MENU; zobrazSubMenu("HLAVNÍ MENU", mainMenuItems, mainMenuCount, menuIndex, scrollOffset); break;
    
    case STATE_KVIZ: case STATE_KVIZ_ODPOVED: case STATE_WYR: case STATE_FLASKA: case STATE_KOSTKY: case STATE_ODHALOVACKA: case STATE_ODHALOVACKA_DETAIL: appState = STATE_HRY; zobrazSubMenu("HRY", hryItems, hryCount, subMenuIndex, subScrollOffset); break;
    case STATE_KOSTKY_VYBER: appState = STATE_HRY; subMenuIndex = 1; subScrollOffset = 0; zobrazSubMenu("HRY", hryItems, hryCount, subMenuIndex, subScrollOffset); break;
    case STATE_GAMEBOOK: ulozGBPozici(gbNode); appState = STATE_HRY; subMenuIndex = 2; subScrollOffset = 0; zobrazSubMenu("HRY", hryItems, hryCount, subMenuIndex, subScrollOffset); break;
    case STATE_HRY: appState = STATE_MAIN_MENU; zobrazSubMenu("HLAVNÍ MENU", mainMenuItems, mainMenuCount, menuIndex, scrollOffset); break;
    
    case STATE_GENERATOR_RESULT: appState = STATE_GENERATOR; zobrazSubMenu("GENERÁTOR", generatorItems, generatorCount, subMenuIndex, subScrollOffset); break;
    case STATE_GENERATOR: appState = STATE_MAIN_MENU; zobrazSubMenu("HLAVNÍ MENU", mainMenuItems, mainMenuCount, menuIndex, scrollOffset); break;
    
    case STATE_NASTAVENI_O_ZARIZENI: appState = STATE_NASTAVENI; subMenuIndex = 4; zobrazSubMenu("NASTAVENÍ", nastaveniItems, nastaveniCount, subMenuIndex, subScrollOffset); break;
    case STATE_NASTAVENI_BATERIE: appState = STATE_NASTAVENI; subMenuIndex = 3; zobrazSubMenu("NASTAVENÍ", nastaveniItems, nastaveniCount, subMenuIndex, subScrollOffset); break;
    case STATE_NASTAVENI_AKTUALIZACE: appState = STATE_NASTAVENI; subMenuIndex = 1; zobrazSubMenu("NASTAVENÍ", nastaveniItems, nastaveniCount, subMenuIndex, subScrollOffset); break;
    case STATE_NASTAVENI_VZHLED: appState = STATE_NASTAVENI; subMenuIndex = 0; zobrazSubMenu("NASTAVENÍ", nastaveniItems, nastaveniCount, subMenuIndex, subScrollOffset); break;
    case STATE_NASTAVENI: appState = STATE_MAIN_MENU; zobrazSubMenu("HLAVNÍ MENU", mainMenuItems, mainMenuCount, menuIndex, scrollOffset); break;

    default: appState = STATE_MAIN_MENU; zobrazSubMenu("HLAVNÍ MENU", mainMenuItems, mainMenuCount, menuIndex, scrollOffset); break;
  }
}

// ===== SETUP =====
void setup() {
  Serial.begin(115200);
  randomSeed(analogRead(0) + millis());
  pinMode(Vext, OUTPUT); digitalWrite(Vext, HIGH); delay(100);
  pinMode(LED_PIN, OUTPUT); digitalWrite(LED_PIN, LOW);

  SPI.begin(2, -1, 1, 3); 
  display.init(); 
  display.setRotation(1);
  
  display.setFullWindow();
  display.firstPage();
  do { display.fillScreen(GxEPD_WHITE); } while (display.nextPage());

  u8g2Fonts.begin(display); 
  u8g2Fonts.setFontMode(1);
  pinMode(BTN_DOWN, INPUT_PULLUP); pinMode(BTN_SELECT, INPUT_PULLUP);
  
  nactiPoziceKnih(); gbNode = nactiGBPozici();
  
  prefs.begin("nastaveni", true);
  intervalIdx = prefs.getInt("updInt", 4);
  prefs.end();

  // 1. ZKUSÍME NAČÍST DATA Z ULOŽENÉ PAMĚTI
  nactiStazenaData();
  
  // 2. DETEKCE PROBUZENÍ Z DEEP SLEEPU
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0) {
    if (stazenaDataSvet.length() < 20) {
      aktualizovatDataNaPozadi(true); 
    }
  } else {
    aktualizovatDataNaPozadi(true);
  }
  
  zobrazSubMenu("HLAVNÍ MENU", mainMenuItems, mainMenuCount, menuIndex, scrollOffset);
}

// ===== LOOP =====
void loop() {
  aktualizovatDataNaPozadi(false);

  if (appState == STATE_ODHALOVACKA && !odhalovackaKonec) {
    if (millis() - odhalovackaLastMs > 150) { 
      int nepouzitych = 0;
      for(int i=0; i<64; i++) if(!odhalenoPole[i]) nepouzitych++;
      if (nepouzitych > 0) {
        int r = random(nepouzitych); int c = 0;
        for(int i=0; i<64; i++) {
          if(!odhalenoPole[i]) { if(c == r) { odhalenoPole[i] = true; break; } c++; }
        }
        zobrazOdhalovacku();
      } else { odhalovackaKonec = true; } 
      odhalovackaLastMs = millis();
    }
  }

  if (appState == STATE_HODINY) {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 10)) {
      if (timeinfo.tm_sec != lastSecHodiny) {
        lastSecHodiny = timeinfo.tm_sec;
        zobrazSvetovyCas();
      }
    }
  }

  // ===== BTN_DOWN (GPIO 0) =====
  if (digitalRead(BTN_DOWN) == LOW) {
    delay(10); 
    if (digitalRead(BTN_DOWN) == LOW) {

      if (appState == STATE_MAIN_MENU && longPressBTN0()) {
        jdiSpat();
        return;
      }

      if (appState == STATE_KOSTKY) {
        bool zatraseno = false;
        while (digitalRead(BTN_DOWN) == LOW) {
          if(!zatraseno) { 
            int rndMax = (kostkyStran == 66) ? 6 : kostkyStran;
            drawKostkaFrame(random(1, rndMax + 1), random(1, 7), kostkyStran, "Třesu...", false);
            zatraseno = true;
          }
          delay(80); 
          int rndMax = (kostkyStran == 66) ? 6 : kostkyStran;
          drawKostkaFrame(random(1, rndMax + 1), random(1, 7), kostkyStran, "Třesu...", false);
        }
        hrajKostku(kostkyStran); return;
      }
      if (appState == STATE_GAMEBOOK) {
        if (longPressBTN0()) {
          int volbaB; memcpy_P(&volbaB, &gamebook[gbNode].volbaB, sizeof(int));
          if (volbaB >= 0) { gbNode = volbaB; gbTextPage = 0; ulozGBPozici(gbNode); zobrazGBNode(); }
        } else { gbTextPage++; zobrazGBNode(); }
        return;
      }
      if (appState == STATE_POCASI_UI) {
        if (longPressBTN0()) { goBack(); return; }
        pocasiDen = (pocasiDen + 1) % 7; zobrazPocasiDen(pocasiDen);
        while (digitalRead(BTN_DOWN) == LOW) delay(10); return;
      }

      if (longPressBTN0()) { goBack(); return; }

      switch (appState) {
        case STATE_MAIN_MENU: menuDown(menuIndex, scrollOffset, mainMenuCount); zobrazSubMenu("HLAVNÍ MENU", mainMenuItems, mainMenuCount, menuIndex, scrollOffset); break;
        case STATE_KNIHOVNA: menuDown(subMenuIndex, subScrollOffset, knihovnaCount); zobrazSubMenu("KNIHOVNA", knihovnaItems, knihovnaCount, subMenuIndex, subScrollOffset); break;
        case STATE_KNIHOVNA_DETAIL: textScrollPage++; ulozPoziciKnihy(subMenuIndex, textScrollPage); zobrazText(knihovnaItems[subMenuIndex].c_str(), knihy[subMenuIndex]); break;
        case STATE_AKTUALITY: menuDown(subMenuIndex, subScrollOffset, aktualityCount); zobrazSubMenu("AKTUALITY", aktualityItems, aktualityCount, subMenuIndex, subScrollOffset); break;
        case STATE_ZPRAVY_MENU: menuDown(subMenuIndex, subScrollOffset, zpravyMenuCount); zobrazSubMenu("ZPRÁVY", zpravyMenuItems, zpravyMenuCount, subMenuIndex, subScrollOffset); break;
        case STATE_ZPRAVY_SEZNAM: clanekMenuIndex = (clanekMenuIndex + 1) % pocetZprav; zobrazSeznamZprav(); break;
        case STATE_ZPRAVY_TEXT: textScrollPage++; zobrazText(aktualniZpravy[clanekMenuIndex].titulek.c_str(), aktualniZpravy[clanekMenuIndex].text.c_str()); break;
        case STATE_TOOLBOX: menuDown(subMenuIndex, subScrollOffset, toolboxCount); zobrazSubMenu("TOOLBOX", toolboxItems, toolboxCount, subMenuIndex, subScrollOffset); break;
        case STATE_QR_MENU: menuDown(subMenuIndex, subScrollOffset, qrCount); zobrazSubMenu("QR KÓDY", qrItems, qrCount, subMenuIndex, subScrollOffset); break;
        
        case STATE_SLOVNIK: menuDown(subMenuIndex, subScrollOffset, slovnikCount); zobrazSubMenu("SLOVNÍK", slovnikItems, slovnikCount, subMenuIndex, subScrollOffset); break;
        case STATE_SLOVNIK_DETAIL: abcKurzor = (abcKurzor + 1) % abcCount; zobrazHledani(); break;
        case STATE_FRAZE_MENU: menuDown(subMenuIndex, subScrollOffset, frazeMenuCount); zobrazSubMenu("FRÁZE", frazeMenuItems, frazeMenuCount, subMenuIndex, subScrollOffset); break;
        case STATE_FRAZE_DETAIL:
          textScrollPage++;
          if (subMenuIndex == 0) zobrazFraze("NAKUPOVÁNÍ", frazeNakup, frazeNakupCount);
          else if (subMenuIndex == 1) zobrazFraze("UBYTOVÁNÍ", frazeUbyt, frazeUbytCount);
          else if (subMenuIndex == 2) zobrazFraze("ZDRAVÍ", frazeZdravi, frazeZdraviCount);
          else if (subMenuIndex == 3) zobrazFraze("ZÁBAVA", frazeZabava, frazeZabavaCount);
          break;
        case STATE_UCENI: uceniSmer = 1 - uceniSmer; zobrazUceni(); break;
        case STATE_GENERATOR: menuDown(subMenuIndex, subScrollOffset, generatorCount); zobrazSubMenu("GENERÁTOR", generatorItems, generatorCount, subMenuIndex, subScrollOffset); break;
        case STATE_HRY: menuDown(subMenuIndex, subScrollOffset, hryCount); zobrazSubMenu("HRY", hryItems, hryCount, subMenuIndex, subScrollOffset); break;
        case STATE_KOSTKY_VYBER: menuDown(subMenuIndex, subScrollOffset, kostkyCount); zobrazSubMenu("KOSTKY", kostkyItems, kostkyCount, subMenuIndex, subScrollOffset); break;
        
        case STATE_KVIZ: aktualniHraIdx = random(kvizPocet); zobrazKviz(); break;
        case STATE_KVIZ_ODPOVED: appState = STATE_KVIZ; aktualniHraIdx = random(kvizPocet); zobrazKviz(); break;
        case STATE_WYR: aktualniHraIdx = random(wyrPocet); zobrazWyr(); break;
        
        case STATE_NASTAVENI: menuDown(subMenuIndex, subScrollOffset, nastaveniCount); zobrazSubMenu("NASTAVENÍ", nastaveniItems, nastaveniCount, subMenuIndex, subScrollOffset); break;
        case STATE_NASTAVENI_VZHLED: menuDown(subMenuIndex, subScrollOffset, vzhledCount); zobrazSubMenu("VZHLED", vzhledItems, vzhledCount, subMenuIndex, subScrollOffset); break;
        case STATE_NASTAVENI_AKTUALIZACE: menuDown(subMenuIndex, subScrollOffset, intervalCount); zobrazSubMenu("AKTUALIZACE", intervalItems, intervalCount, subMenuIndex, subScrollOffset); break;
        
        case STATE_ODHALOVACKA: case STATE_ODHALOVACKA_DETAIL: goBack(); break;
        
        default: break;
      }
      while (digitalRead(BTN_DOWN) == LOW) delay(10);
    }
  }

  // ===== BTN_SELECT (GPIO 21) =====
  if (digitalRead(BTN_SELECT) == LOW) {
    delay(10); 
    if (digitalRead(BTN_SELECT) == LOW) {
      if (longPress()) { goBack(); return; }

      switch (appState) {
        case STATE_MAIN_MENU:
          subMenuIndex = 0; subScrollOffset = 0; textScrollPage = 0;
          if (menuIndex == 0) { appState = STATE_KNIHOVNA; zobrazSubMenu("KNIHOVNA", knihovnaItems, knihovnaCount, 0, 0); }
          else if (menuIndex == 1) { appState = STATE_AKTUALITY; zobrazSubMenu("AKTUALITY", aktualityItems, aktualityCount, 0, 0); }
          else if (menuIndex == 2) { appState = STATE_TOOLBOX; zobrazSubMenu("TOOLBOX", toolboxItems, toolboxCount, 0, 0); }
          else if (menuIndex == 3) { appState = STATE_SLOVNIK; zobrazSubMenu("SLOVNÍK", slovnikItems, slovnikCount, 0, 0); }
          else if (menuIndex == 4) { appState = STATE_GENERATOR; zobrazSubMenu("GENERÁTOR", generatorItems, generatorCount, 0, 0); }
          else if (menuIndex == 5) { appState = STATE_HRY; zobrazSubMenu("HRY", hryItems, hryCount, 0, 0); }
          else if (menuIndex == 6) { appState = STATE_NASTAVENI; zobrazSubMenu("NASTAVENÍ", nastaveniItems, nastaveniCount, 0, 0); }
          break;

        case STATE_KNIHOVNA: textScrollPage = knihaPozice[subMenuIndex]; appState = STATE_KNIHOVNA_DETAIL; zobrazText(knihovnaItems[subMenuIndex].c_str(), knihy[subMenuIndex]); break;

        case STATE_AKTUALITY:
          if (subMenuIndex == 0) { appState = STATE_ZPRAVY_MENU; subMenuIndex = 0; subScrollOffset = 0; zobrazSubMenu("ZPRÁVY", zpravyMenuItems, zpravyMenuCount, 0, 0); }
          else if (subMenuIndex == 1) { appState = STATE_HODINY; lastSecHodiny = -1; zobrazSvetovyCas(); }
          else if (subMenuIndex == 2) { appState = STATE_KURZY; parsujKurzy(stazenaDataKurzy); zobrazKurzyUI(); }
          else if (subMenuIndex == 3) { appState = STATE_POCASI_UI; pocasiDen = 0; zobrazPocasiDen(pocasiDen); }
          break;

        case STATE_ZPRAVY_MENU:
          appState = STATE_ZPRAVY_SEZNAM; clanekMenuIndex = 0;
          if (subMenuIndex == 0) parsujZpravy(stazenaDataSvet); else if (subMenuIndex == 1) parsujZpravy(stazenaDataCR); else if (subMenuIndex == 2) parsujZpravy(stazenaDataTech);
          zobrazSeznamZprav();
          break;

        case STATE_ZPRAVY_SEZNAM: appState = STATE_ZPRAVY_TEXT; textScrollPage = 0; zobrazText(aktualniZpravy[clanekMenuIndex].titulek.c_str(), aktualniZpravy[clanekMenuIndex].text.c_str()); break;

        case STATE_TOOLBOX:
          if (subMenuIndex == 0) { appState = STATE_LED_ON; zobrazLedStav(); }
          else if (subMenuIndex == 1) { appState = STATE_STOPKY; stopkyRunning = false; stopkyElapsed = 0; stopkyLastDraw = 0; drawStopky(0, false); }
          else if (subMenuIndex == 2) { appState = STATE_QR_MENU; subMenuIndex = 0; subScrollOffset = 0; zobrazSubMenu("QR KÓDY", qrItems, qrCount, 0, 0); }
          break;

        case STATE_QR_MENU: appState = STATE_QR_ZOBRAZ; zobrazQR(qrItems[subMenuIndex].c_str(), qrData[subMenuIndex].c_str()); break;

        case STATE_LED_ON: ledState = !ledState; digitalWrite(LED_PIN, ledState ? HIGH : LOW); zobrazLedStav(); break;
        case STATE_STOPKY:
          if (stopkyRunning) { stopkyElapsed += millis() - stopkyStart; stopkyRunning = false; drawStopky(stopkyElapsed, false); }
          else { stopkyStart = millis(); stopkyRunning = true; }
          break;

        case STATE_SLOVNIK:
          textScrollPage = 0;
          if (subMenuIndex == 0 || subMenuIndex == 1) {
            appState = STATE_SLOVNIK_DETAIL; hledanySmer = subMenuIndex; hledanyLen = 0; hledanyText[0] = '\0'; abcKurzor = 0; vysledekOffset = 0; zobrazHledani();
          } else if (subMenuIndex == 2) {
            appState = STATE_FRAZE_MENU; subMenuIndex = 0; subScrollOffset = 0; zobrazSubMenu("FRÁZE", frazeMenuItems, frazeMenuCount, 0, 0);
          } else if (subMenuIndex == 3) {
            appState = STATE_UCENI; uceniIdx = random(slovnikDataCount); uceniOdhaleno = false; zobrazUceni();
          }
          break;
        case STATE_SLOVNIK_DETAIL:
          if (hledanyLen < 11) { hledanyText[hledanyLen] = abeceda[abcKurzor] + 32; hledanyLen++; hledanyText[hledanyLen] = '\0'; vysledekOffset = 0; }
          zobrazHledani();
          break;
        case STATE_FRAZE_MENU:
          textScrollPage = 0; appState = STATE_FRAZE_DETAIL;
          if (subMenuIndex == 0) zobrazFraze("NAKUPOVÁNÍ", frazeNakup, frazeNakupCount);
          else if (subMenuIndex == 1) zobrazFraze("UBYTOVÁNÍ", frazeUbyt, frazeUbytCount);
          else if (subMenuIndex == 2) zobrazFraze("ZDRAVÍ", frazeZdravi, frazeZdraviCount);
          else if (subMenuIndex == 3) zobrazFraze("ZÁBAVA", frazeZabava, frazeZabavaCount);
          break;
        case STATE_FRAZE_DETAIL:
          textScrollPage++;
          if (subMenuIndex == 0) zobrazFraze("NAKUPOVÁNÍ", frazeNakup, frazeNakupCount);
          else if (subMenuIndex == 1) zobrazFraze("UBYTOVÁNÍ", frazeUbyt, frazeUbytCount);
          else if (subMenuIndex == 2) zobrazFraze("ZDRAVÍ", frazeZdravi, frazeZdraviCount);
          else if (subMenuIndex == 3) zobrazFraze("ZÁBAVA", frazeZabava, frazeZabavaCount);
          break;
        case STATE_UCENI:
          if (uceniOdhaleno) { uceniIdx = random(slovnikDataCount); uceniOdhaleno = false; } else { uceniOdhaleno = true; }
          zobrazUceni();
          break;

       case STATE_GENERATOR:
         appState = STATE_GENERATOR_RESULT;
         if (subMenuIndex == 0) zobrazGeneratorResult("VTIP", vtipy[random(vtipyPocet)]);
         else if (subMenuIndex == 1) zobrazGeneratorResult("ŽALM", zalmy[random(zalmyPocet)]);
         else if (subMenuIndex == 2) zobrazGeneratorResult("CITÁT", citaty[random(citatyPocet)]);
         else if (subMenuIndex == 3) zobrazGeneratorResult("FAKT", fakty[random(faktyPocet)]); 
         break;
        case STATE_GENERATOR_RESULT:
          if (subMenuIndex == 0) zobrazGeneratorResult("VTIP", vtipy[random(vtipyPocet)]);
          else if (subMenuIndex == 1) zobrazGeneratorResult("ŽALM", zalmy[random(zalmyPocet)]);
          else if (subMenuIndex == 2) zobrazGeneratorResult("CITÁT", citaty[random(citatyPocet)]);
          else if (subMenuIndex == 3) zobrazGeneratorResult("FAKT", fakty[random(faktyPocet)]); 
          break;

        case STATE_HRY:
          if (subMenuIndex == 0) { appState = STATE_FLASKA; hrajFlasku(); }
          else if (subMenuIndex == 1) { appState = STATE_KOSTKY_VYBER; subMenuIndex = 0; subScrollOffset = 0; zobrazSubMenu("KOSTKY", kostkyItems, kostkyCount, 0, 0); }
          else if (subMenuIndex == 2) { appState = STATE_GAMEBOOK; gbNode = nactiGBPozici(); gbTextPage = 0; zobrazGBNode(); }
          else if (subMenuIndex == 3) { appState = STATE_ODHALOVACKA; odhalovackaKonec = false; for(int i=0; i<64; i++) odhalenoPole[i] = false; zobrazOdhalovacku(); }
          else if (subMenuIndex == 4) { appState = STATE_KVIZ; aktualniHraIdx = random(kvizPocet); zobrazKviz(); }
          else if (subMenuIndex == 5) { appState = STATE_WYR; aktualniHraIdx = random(wyrPocet); zobrazWyr(); }
          break;
        case STATE_FLASKA: hrajFlasku(); break;
        case STATE_KOSTKY_VYBER: kostkyStran = kostkyStrany[subMenuIndex]; appState = STATE_KOSTKY; zobrazKostkyIdle(); break;
        case STATE_KOSTKY: hrajKostku(kostkyStran); break;
        case STATE_GAMEBOOK:
          {
            int volbaA; memcpy_P(&volbaA, &gamebook[gbNode].volbaA, sizeof(int));
            if (volbaA == -1) { gbNode = 0; gbTextPage = 0; ulozGBPozici(0); zobrazGBNode(); }
            else { gbNode = volbaA; gbTextPage = 0; ulozGBPozici(gbNode); zobrazGBNode(); }
            break;
          }
        case STATE_ODHALOVACKA: odhalovackaKonec = true; appState = STATE_ODHALOVACKA_DETAIL; zobrazOdhalovackuDetail(); break;
        case STATE_ODHALOVACKA_DETAIL: appState = STATE_HRY; zobrazSubMenu("HRY", hryItems, hryCount, subMenuIndex, subScrollOffset); break;
        
        case STATE_KVIZ: appState = STATE_KVIZ_ODPOVED; zobrazKvizOdpoved(); break;
        case STATE_KVIZ_ODPOVED: appState = STATE_KVIZ; aktualniHraIdx = random(kvizPocet); zobrazKviz(); break;
        case STATE_WYR: aktualniHraIdx = random(wyrPocet); zobrazWyr(); break;

        case STATE_NASTAVENI:
          if (subMenuIndex == 0) { appState = STATE_NASTAVENI_VZHLED; subMenuIndex = 0; subScrollOffset = 0; zobrazSubMenu("VZHLED", vzhledItems, vzhledCount, 0, 0); }
          else if (subMenuIndex == 1) { appState = STATE_NASTAVENI_AKTUALIZACE; subMenuIndex = intervalIdx; subScrollOffset = 0; zobrazSubMenu("AKTUALIZACE", intervalItems, intervalCount, subMenuIndex, subScrollOffset); }
          else if (subMenuIndex == 2) { aktualizovatDataNaPozadi(true); zobrazSubMenu("NASTAVENÍ", nastaveniItems, nastaveniCount, subMenuIndex, subScrollOffset); }
          else if (subMenuIndex == 3) { appState = STATE_NASTAVENI_BATERIE; zobrazBateriiMenu(); }
          else if (subMenuIndex == 4) { appState = STATE_NASTAVENI_O_ZARIZENI; zobrazOZaRizeni(); }
          break;

        case STATE_NASTAVENI_VZHLED:
          if (subMenuIndex == 0) { currentStyle = (currentStyle + 1) % 3; delay(100); zobrazSubMenu("VZHLED", vzhledItems, vzhledCount, subMenuIndex, subScrollOffset); }
          else if (subMenuIndex == 1) { currentSize = (currentSize + 1) % 4; delay(100); zobrazSubMenu("VZHLED", vzhledItems, vzhledCount, subMenuIndex, subScrollOffset); }
          else if (subMenuIndex == 2) { currentBold = (currentBold + 1) % 2; delay(100); zobrazSubMenu("VZHLED", vzhledItems, vzhledCount, subMenuIndex, subScrollOffset); }
          else if (subMenuIndex == 3) { reverseMode = !reverseMode; delay(100); zobrazSubMenu("VZHLED", vzhledItems, vzhledCount, subMenuIndex, subScrollOffset); }
          break;
          
        case STATE_NASTAVENI_AKTUALIZACE:
          intervalIdx = subMenuIndex; 
          prefs.begin("nastaveni", false); prefs.putInt("updInt", intervalIdx); prefs.end(); 
          goBack(); 
          break;

        default: break;
      }
      while (digitalRead(BTN_SELECT) == LOW) delay(10);
    }
  }

  // ===== STOPKY UPDATE =====
  if (appState == STATE_STOPKY && stopkyRunning) {
    unsigned long now = millis();
    unsigned long currentMs = stopkyElapsed + (now - stopkyStart);
    unsigned long currentSec = currentMs / 1000;
    unsigned long lastSec = stopkyLastDraw / 1000;
    if (currentSec != lastSec) {
      drawStopky(currentMs, true);
      stopkyLastDraw = currentMs;
    }
  }
}
