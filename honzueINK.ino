#include <Arduino.h>
struct ArchivClanek;
#include <SPI.h>
#include <SD.h>
#include <math.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <GxEPD2_BW.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <ESPmDNS.h>      // honzueink.local pro web upload
#include <ArduinoJson.h>
#include <esp_timer.h>   // blikání LED během WiFi/stahování (běží i při blokujícím kódu)
#include <qrcode_rm.h>
#include "driver/i2s_std.h"
#include <Audio.h>   // MP3/AAC/FLAC/OGG dekoder (knihovna ESP32-audioI2S)
#include "mbedtls/sha256.h" // SHA-256 pro "těžbu bitcoinu" (TOOLBOX)

// USB Mass Storage (SD karta jako USB disk) — jen když je USB v OTG režimu.
// POZOR: USB-OTG a USB CDC (sériový port) sdílí stejné piny D+/D-, proto musí být
// "USB CDC On Boot" = Disabled. Když je CDC zapnuté, obsadí USB hned při startu
// a na PC se objeví "šedivý" disk, který hlásí "vložte jednotku".
#if defined(ARDUINO_USB_MODE) && (ARDUINO_USB_MODE == 0) && defined(SOC_USB_OTG_SUPPORTED) && SOC_USB_OTG_SUPPORTED
#include "USB.h"
#include "USBMSC.h"
#define USB_SD_HW_OK 1
#else
#define USB_SD_HW_OK 0
#endif

#ifndef ARDUINO_USB_CDC_ON_BOOT
#define ARDUINO_USB_CDC_ON_BOOT 0
#endif

#if USB_SD_HW_OK && !ARDUINO_USB_CDC_ON_BOOT
#define USB_SD_MOZNE 1
#else
#define USB_SD_MOZNE 0
#endif

#include <time.h>

#include "TextLine.h"
#include "gamebook.h"
#include "odhalovacka.h"
#include "knihy.h"
#include "generator.h"
#include "slovnik.h"
#include "fraze.h"
#include "HryKviz.h" 
#include "HryWyr.h"  
#include "SDKnihovnaReader.h"

// ===== SD KARTA — STAV =====
bool sdReady = false; // true pokud SD karta funguje a /eindata/ je připravena
bool littlefsReady = false; // true pokud je interni LittleFS uspesne pripojen
static const char* TZ_PRAGUE = "CET-1CEST,M3.5.0/2,M10.5.0/3";
const bool LOCAL_ARCHIVE_ON_DEVICE = true; // Archiv se ukládá lokálně na SD do /eindata/zpravy/archiv/<datum>/

// ===== WIFI A INTERNET =====
const char* ssid1 = "Barhon";
const char* pass1 = "Pizzue-96";
const char* ssid2 = "Korbel";
const char* pass2 = "honzuemanejfoun";

const String urlBase = "https://raw.githubusercontent.com/honzuejtt-ops/Honzueink/main/";
String urlZpravySvet = urlBase + "eindata/zpravy/aktualni/zpravy_svet.txt";
String urlZpravyCR   = urlBase + "eindata/zpravy/aktualni/zpravy_cr.txt";
String urlTechAI     = urlBase + "eindata/zpravy/aktualni/zpravy_tech.txt";
String urlZpravyPozitivni = urlBase + "eindata/zpravy/aktualni/zpravy_pozitivni.txt";
String urlPocasi     = urlBase + "eindata/cache/pocasi.txt";
String urlKurzy      = urlBase + "eindata/cache/kurzy.txt";
String urlHoroskop   = urlBase + "eindata/cache/horoskop.txt";
String urlKurzyHistorie = urlBase + "eindata/cache/kurzy_historie.txt";

String urlZpravySvetIdx = urlBase + "eindata/zpravy/aktualni/zpravy_svet.idx";
String urlZpravyCRIdx   = urlBase + "eindata/zpravy/aktualni/zpravy_cr.idx";
String urlZpravyTechIdx = urlBase + "eindata/zpravy/aktualni/zpravy_tech.idx";
String urlZpravyPozitivniIdx = urlBase + "eindata/zpravy/aktualni/zpravy_pozitivni.idx";
String urlZpravyDetail  = urlBase + "eindata/zpravy/aktualni/detail/";

String stazenaDataSvet = ""; String stazenaDataCR = ""; String stazenaDataTech = ""; String stazenaDataPozitivni = "";
String stazenaDataPocasi = ""; String stazenaDataKurzy = "";
String stazenaDataHoroskop = ""; String stazenaDataKurzyHistorie = "";

bool casSynchronizovan = false; int lastSecHodiny = -1;

// Globální sdílený TLS klient
WiFiClientSecure sharedClient;
bool sharedClientInitialized = false;
WebServer webServer(80);
bool webUploadServerReady = false;
bool webUploadMode = false;
bool webUploadApMode = false;
bool webUploadNeedsRefresh = false;
String webUploadLastMsg = "";
String webUploadStaIp = "";
String webUploadApIp = "";
String webUploadSsid = "";

const char* uploadApSsid = "HonzueINK-Upload";
const char* uploadApPass = "honzue1234";

void initSharedClient() {
  if (!sharedClientInitialized) {
    sharedClient.setInsecure();
    sharedClientInitialized = true;
  }
}

// PAMĚŤ PŘEŽÍVAJÍCÍ DEEP SLEEP
RTC_DATA_ATTR time_t rtc_posledniAktualizace = 0;
RTC_DATA_ATTR time_t rtc_posledniPokusAktualizace = 0;     // proti opakování při chybě
RTC_DATA_ATTR int    rtc_posledniPlanovanaAktualizace = 0; // YYYYMMDD

// Navigační stav pro obnovu po probuzení z deep sleep
RTC_DATA_ATTR bool rtc_bylDeepSleep    = false;
RTC_DATA_ATTR int  rtc_appState        = 0;  // přetypujeme na AppState
RTC_DATA_ATTR int  rtc_menuIndex       = 0;
RTC_DATA_ATTR int  rtc_scrollOffset    = 0;
RTC_DATA_ATTR int  rtc_subMenuIndex    = 0;
RTC_DATA_ATTR int  rtc_subScrollOffset = 0;
RTC_DATA_ATTR int  rtc_clanekMenuIndex = 0;
RTC_DATA_ATTR int  rtc_textScrollPage  = 0;
RTC_DATA_ATTR int  rtc_pocasiDen       = 0;
RTC_DATA_ATTR int  rtc_horoskopIdx     = 0;
RTC_DATA_ATTR int  rtc_grafIdx         = 0;

// ID článku = 12znakový hash z indexu (text se dotahuje až při otevření)
struct Zprava { String uid; String titulek; String datum; String perex; String text; String id; };
Zprava aktualniZpravy[20]; int pocetZprav = 0; int clanekMenuIndex = 0;

struct PocasiDen { String datum; int tMax; int tMin; String srazky; String ikona; int vitr; String vychod; String zapad; };
PocasiDen predpoved[7]; int pocasiDen = 0;

String kurzEur = "", kurzUsd = "", kurzBtc = "", kurzZlato = "", kurzStribro = "";
String kurzNafta = "", kurzBenzin = "";

// ===== DISPLEJ A HW =====
GxEPD2_BW<GxEPD2_290_BS, GxEPD2_290_BS::HEIGHT> display(GxEPD2_290_BS(3, 4, 5, 6));
U8G2_FOR_ADAFRUIT_GFX u8g2Fonts;

#define BTN_DOWN 0
#define BTN_SELECT 21
#define Vext 18
#define LED_PIN 45
#define BAT_PIN 7      // GPIO7 = BAT_ADC na Heltec Vision Master E290
#define ADC_CTRL 46    // GPIO46 = ADC_CTRL, povoluje bateriový dělič (HIGH = zapnuto)

// Blikání LED při práci s WiFi (stahování dat, nahrávání souborů).
// Řízeno časovačem, protože stahování je blokující a loop() během něj neběží.
// Místo blikání LED pomalu "dýchá" přes PWM, aby to nebylo rušivé.
volatile bool ledBlikam = false;
volatile unsigned long webLedAktivitaMs = 0;
extern bool ledState;

static void ledCasovacCb(void*) {
  static bool pripojeno = false;
  static uint16_t faze = 0;
  bool dychat = ledBlikam || webUploadMode;
  if (!dychat) {
    if (pripojeno) {
      ledcWrite(LED_PIN, 0);
      ledcDetach(LED_PIN);
      pinMode(LED_PIN, OUTPUT);
      digitalWrite(LED_PIN, ledState ? HIGH : LOW);   // vrátit stav nástroje Dioda
      pripojeno = false;
    }
    return;
  }
  if (!pripojeno) {
    ledcAttachChannel(LED_PIN, 2000, 8, 4);           // PWM pro plynulý jas
    faze = 0;
    pripojeno = true;
  }
  // pomalé dýchání: 0 → max → 0, perioda ~2,4 s, max ~24 % jasu
  if (++faze >= 160) faze = 0;
  uint16_t p = (faze < 80) ? faze : (159 - faze);
  ledcWrite(LED_PIN, (uint32_t)(p * 60) / 79);
}

// SD karta — sdílí SPI sběrnici s displejem (MOSI=1, SCK=2)
// Zapoj: SD.MISO → GPIO8, SD.CS → GPIO9, SD.MOSI → GPIO1, SD.CLK → GPIO2
#define SD_MISO_PIN 8
#define SD_CS_PIN   9

// Kapacitní touch zóny (ESP32-S3: touch je jen na GPIO1–14).
// Směrový kříž (D-pad): GPIO10=NAHORU, GPIO11=DOLU, GPIO12=DOLEVA, GPIO14=DOPRAVA.
#define TOUCH_PIN_COUNT 4
static const uint8_t TOUCH_PINY[TOUCH_PIN_COUNT]  = { 10, 11, 12, 14 };
static const char*   TOUCH_NAZVY[TOUCH_PIN_COUNT] = { "NAHORU", "DOLU", "DOLEVA", "DOPRAVA" };

// ===== PŘÍDAVNÉ PERIFERIE =====
// Rotační encoder (A/B + tlačítko SW):
#define ENC_A_PIN   13
#define ENC_B_PIN   40
#define ENC_SW_PIN  47
// Kolik kvadratuřnich přechodů = jedno mechanické cvaknutí (detent).
#define ENC_KROKY_NA_CVAK 4
// Mikrofon INMP441 (I2S): datový pin SD. SCK/WS sdílí s DAC (GPIO42/41).
#define MIC_SD_PIN  43
// Pasivní buzzery (LEDC/PWM). BUZZER2 uvolněn z DAC_PWR (GPIO48).
#define BUZZER1_PIN 44
#define BUZZER2_PIN 48

// Dopředné deklarace testů přídavných periferií (kvůli goBack před definicemi)
void micStop();
bool micStart(uint16_t dmaDesc = 6, uint16_t dmaFrame = 240);
void buzzerStop();
void zahrajUspesnyTon();
void prehrajWav(const char* cesta);
void opicakStop();
// Jízdní řády
void nactiJrSeznam();
void zobrazJrDetail();
void zobrazJrZprava(const char* text);
// Kalendář
void zobrazKalTyden();
void zobrazKalMesic();
void zobrazKalDetail();
void zobrazKalZprava(const char* text);
// Recepty
void zobrazReceptDetail();

Preferences prefs;

// ===== FONTY A VZHLED (POUZE TYTO 3) =====
const uint8_t* fontyLub[][2] = { { u8g2_font_lubR08_te, u8g2_font_lubB08_te }, { u8g2_font_lubR10_te, u8g2_font_lubB10_te }, { u8g2_font_lubR12_te, u8g2_font_lubB12_te }, { u8g2_font_lubR14_te, u8g2_font_lubB14_te } };
const uint8_t* fontyNcen[][2] = { { u8g2_font_ncenR08_te, u8g2_font_ncenB08_te }, { u8g2_font_ncenR10_te, u8g2_font_ncenB10_te }, { u8g2_font_ncenR12_te, u8g2_font_ncenB12_te }, { u8g2_font_ncenR14_te, u8g2_font_ncenB14_te } };
const uint8_t* fontyHelv[][2] = { { u8g2_font_helvR08_te, u8g2_font_helvB08_te }, { u8g2_font_helvR10_te, u8g2_font_helvB10_te }, { u8g2_font_helvR12_te, u8g2_font_helvB12_te }, { u8g2_font_helvR14_te, u8g2_font_helvB14_te } };

// DODATEČNÝ STYL (česká diakritika, tzn. fonty s příponou _te)
// Strojopis — monospace (terminálový vzhled). Pozor: tato řada nemá tučnou
// variantu s češtinou, proto se „Tučné" u tohoto stylu nijak neprojeví.
const uint8_t* fontyMono[][2] = { { u8g2_font_6x12_te, u8g2_font_6x12_te }, { u8g2_font_7x13_te, u8g2_font_7x13_te }, { u8g2_font_9x15_te, u8g2_font_9x15_te }, { u8g2_font_10x20_te, u8g2_font_10x20_te } };

int currentStyle = 0; int currentSize = 1; int currentBold = 1; bool reverseMode = false;
bool napovedaZapnuta = true; // zobrazovat nápovědy ve spodní části displeje (Nastavení → Vzhled)
const char* styleNames[] = { "Elegantní", "Klasický", "Hravý", "Strojopis" };
const char* sizeNames[] = { "Malá", "Střední", "Velká" };
const char* boldNames[] = { "Normální", "Tučné" };

int getCharWidth() {
  // Strojopis je monospace → šířka znaku je pevná a musí odpovídat fontu,
  // jinak by se delší řádky nevešly na obrazovku (text by se ořezával).
  if (currentStyle == 3) { int w[] = { 6, 7, 9, 10 }; return w[currentSize]; }
  int w[] = { 5, 7, 8, 9 }; return w[currentSize];
}
int getLineHeight() { int h[] = { 12, 15, 17, 20 }; return h[currentSize]; }
int getLinesPerPage() { return 96 / getLineHeight(); }
int getMenuItemHeight() { int h[] = { 18, 22, 25, 28 }; return h[currentSize]; }
int getVisibleMenuItems() { return (128 - 28) / getMenuItemHeight(); }

// DŮSLEDNÉ POUŽITÍ STYLŮ PRO VŠECHNY VELIKOSTI:
const uint8_t* getBodyFont() { 
  switch (currentStyle) { 
    case 0: return fontyLub[currentSize][currentBold]; 
    case 1: return fontyNcen[currentSize][currentBold]; 
    case 2: return fontyHelv[currentSize][currentBold]; 
    case 3: return fontyMono[currentSize][currentBold]; 
    default: return fontyLub[1][0]; 
  } 
}
const uint8_t* getBodyBoldFont() {
  switch (currentStyle) {
    case 0: return fontyLub[currentSize][1];
    case 1: return fontyNcen[currentSize][1];
    case 2: return fontyHelv[currentSize][1];
    case 3: return fontyMono[currentSize][1];
    default: return fontyLub[1][1];
  }
}
const uint8_t* getTitleFont() { 
  switch (currentStyle) { 
    case 0: return fontyLub[1][1]; 
    case 1: return fontyNcen[1][1]; 
    case 2: return fontyHelv[1][1]; 
    case 3: return fontyMono[1][1]; 
    default: return fontyLub[1][1]; 
  } 
}
const uint8_t* getSmallFont() { 
  switch (currentStyle) { 
    case 0: return fontyLub[0][0]; 
    case 1: return fontyNcen[0][0]; 
    case 2: return fontyHelv[0][0]; 
    case 3: return fontyMono[0][0]; 
    default: return fontyLub[0][0]; 
  } 
}
const uint8_t* getSmallBoldFont() {
  switch (currentStyle) {
    case 0: return fontyLub[0][1];
    case 1: return fontyNcen[0][1];
    case 2: return fontyHelv[0][1];
    case 3: return fontyMono[0][1];
    default: return fontyLub[0][1];
  }
}
const uint8_t* getBigFont() { 
  switch (currentStyle) { 
    case 0: return fontyLub[3][1]; 
    case 1: return fontyNcen[3][1]; 
    case 2: return fontyHelv[3][1]; 
    case 3: return fontyMono[3][1]; 
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
  STATE_TEZBA, STATE_TOUCH_TEST,
  STATE_GAMEBOOK_MENU, STATE_GAMEBOOK, STATE_ODHALOVACKA, STATE_ODHALOVACKA_DETAIL, STATE_FRAZE_MENU, STATE_FRAZE_DETAIL, STATE_UCENI, STATE_SLOVNIK_JAZYK,
  STATE_NASTAVENI_AKTUALIZACE, STATE_NASTAVENI_AUTOUSPANI, STATE_QR_MENU, STATE_QR_ZOBRAZ,
  STATE_KVIZ, STATE_KVIZ_ODPOVED, STATE_WYR,
  // Nové stavy (přidávají se na konec aby se neposunovaly indexy)
  STATE_KVIZ_KATEGORIE, STATE_KVIZ_OBTIZNOST,
  STATE_WYR_VYSLEDEK,
  STATE_HOROSKOP_MENU, STATE_HOROSKOP_DETAIL,
  STATE_KURZY_GRAF,
  STATE_SD_BROWSER,
  // Nové stavy pro archiv zpráv, SD prohlížeč textu/obrázků
  STATE_ARCHIV_DATUMY, STATE_ARCHIV_ZPRAVY_MENU, STATE_ARCHIV_HLEDANI,
  STATE_SD_TEXT_VIEW, STATE_SD_BMP_VIEW,
  STATE_WEB_UPLOAD,
  STATE_NASTAVENI_WIFI,
  STATE_WIFI_SEZNAM,
  STATE_WIFI_HESLO,
  STATE_WIFI_POTVRDIT,
  STATE_HUDBA,
  STATE_USB_SD,
  STATE_ENCODER_TEST,
  STATE_MIC_TEST, STATE_BUZZER_TEST,
  // Diktafon (přidáno na konec, aby se neposunuly indexy stavů)
  STATE_DIKTAFON_MENU, STATE_DIKTAFON_SEZNAM, STATE_DIKTAFON_NAHRAVANI,
  STATE_OPICAK,
  STATE_JR_SEZNAM, STATE_JR_DETAIL,
  // Kalendář (Google iCal data z GitHubu)
  STATE_KALENDAR_MENU, STATE_KALENDAR_TYDEN, STATE_KALENDAR_MESIC, STATE_KALENDAR_DEN,
  // Recepty
  STATE_RECEPTY_SEZNAM, STATE_RECEPT_DETAIL,
  // Časovač / budík a kalkulačka
  STATE_CASOVAC, STATE_KALKULACKA
};

AppState appState = STATE_MAIN_MENU;
int menuIndex = 0, scrollOffset = 0, subMenuIndex = 0, subScrollOffset = 0, textScrollPage = 0, kostkyStran = 6;
bool stopkyRunning = false; unsigned long stopkyStart = 0, stopkyElapsed = 0, stopkyLastDraw = 0;

// ===== "TĚŽBA BITCOINU" (jen pro zábavu) =====
bool     tezbaBezi = false;              // běží těžba
unsigned long tezbaNonce = 0;            // aktuální nonce
unsigned long tezbaHashu = 0;            // celkem spočítaných hashů
unsigned long tezbaHashMs = 0;           // čas strávený samotným hashováním (ms)
unsigned long tezbaBloku = 0;            // "nalezených" bloků
unsigned long tezbaStartMs = 0;          // start celé session
unsigned long tezbaPosledniDraw = 0;     // čas posledního překreslení
int      tezbaNejNuly = -1;              // nejvíc vedoucích hex nul (-1 = ještě nic)
int      tezbaCilNuly = 5;               // cíl (5 hex nul = 20 bitů, pro zábavu)
uint8_t  tezbaHeader[80];                // 80bajtová hlava bloku (poslední 4 B = nonce)
uint8_t  tezbaNejHash[32];               // nejlepší nalezený hash

// --- reálná těžba (Stratum sólo klient) ---
bool     tezbaReal = false;              // true = reálný režim (máme config)
String   tpPool = "", tpWorker = "";
uint16_t tpPort = 3333;
WiFiClient tpClient;
String   tpRx = "";
bool     tpPrijhlasen = false;
String   tpExtranonce1 = ""; int tpEn2Size = 4;
String   tpJobId = "", tpPrevhash = "", tpCoinb1 = "", tpCoinb2 = "", tpVersion = "", tpNbits = "", tpNtime = "";
String   tpMerkle[16]; int tpMerkleCount = 0;
uint8_t  tpTarget[32];
int      tpMsgId = 2, tpNejNuly = -1;
uint32_t tpNonce = 0;
unsigned long tpBloku = 0, tpPosledniRx = 0, tpPosledniRetry = 0;
String   tpStav = "Nepripojeno";
uint8_t  tpNejHash[32];

// --- test kapacitního touch pinu (4 zóny = D-pad) ---
uint32_t touchBaseA[TOUCH_PIN_COUNT] = {0};
uint32_t touchNowA[TOUCH_PIN_COUNT]  = {0};
uint32_t touchPrahA[TOUCH_PIN_COUNT] = {0};
long     touchPeakA[TOUCH_PIN_COUNT] = {0};   // největší skok od posledního vykreslení
bool     touchDotyA[TOUCH_PIN_COUNT] = {false};
unsigned long touchLast = 0;                  // čas posledního vykreslení
unsigned long touchLastPoll = 0;              // čas posledního vzorkování
int refreshMode = 1; // 0=Střední (partial + občasné plné čištění), 1=Rychlý (vždy partial, výchozí)
int refreshDrawCounter = 0;
unsigned long lastUserActivityMs = 0;

// ===== CÍLENÉ PŘEKRESLENÍ (menší okno = rychlejší partial refresh) =====
// Volající může před vykreslením nastavit menší oblast, která se má obnovit.
// Když oblast není nastavená (0x0), obnoví se celá obrazovka.
// Oblast je jednorázová — beginUiFrame() ji po použití zruší.
static int uiRegX = 0, uiRegY = 0, uiRegW = 0, uiRegH = 0;
// Příznak: právě proběhl posun kurzoru v menu (menuDown) — umožní obnovit jen 2 řádky.
static bool gPosunKurzoru = false;
// První vykreslení po probuzení z deep sleep musí být přes celou obrazovku —
// řadič displeje nemá po hibernaci platnou paměť starého obrazu.
static bool gPrvniRamPoProbuzeni = false;

void zrusOblastPrekresleni() { uiRegX = uiRegY = uiRegW = uiRegH = 0; }

void nastavOblastPrekresleni(int x, int y, int w, int h) {
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  if (x + w > display.width())  w = display.width()  - x;
  if (y + h > display.height()) h = display.height() - y;
  if (w <= 0 || h <= 0) { zrusOblastPrekresleni(); return; }
  uiRegX = x; uiRegY = y; uiRegW = w; uiRegH = h;
}

void beginUiFrame(bool partialPreferred = true) {
  // První snímek po probuzení: vynutit celou obrazovku (jinak by partial
  // překreslení mohlo nechat mimo okno neplatná data řadiče).
  if (gPrvniRamPoProbuzeni) {
    gPrvniRamPoProbuzeni = false;
    zrusOblastPrekresleni();
    gPosunKurzoru = false;
    display.setFullWindow();
    display.firstPage();
    return;
  }
  bool useFull = true;
  if (refreshMode == 0) {
    // Střední režim: většinou partial, jen zřídka plné čištění (proti duchům).
    // Vzácně = každý 100. snímek; full refresh totiž blikne a na pár dalších
    // snímků po sobě zanechá slabé duchy, takže častěji škodí.
    if (partialPreferred) {
      refreshDrawCounter++;
      useFull = (refreshDrawCounter % 100 == 0);
    } else {
      useFull = true;
    }
  } else {
    // Rychlý režim: vždy partial (maximální odezva).
    useFull = false;
  }

  if (useFull) display.setFullWindow();
  else if (uiRegW > 0 && uiRegH > 0) display.setPartialWindow(uiRegX, uiRegY, uiRegW, uiRegH);
  else display.setPartialWindow(0, 0, display.width(), display.height());
  zrusOblastPrekresleni(); // oblast je jednorázová
  gPosunKurzoru = false;   // bezpečnostní dočištění příznaku
  display.firstPage();
}
int gbNode = 0, gbTextPage = 0, buchaPozice[15] = {};
int aktualniHraIdx = 0;
int kvizKatIdx = 0; int kvizObtIdx = 0; int kvizKatScrollOffset = 0; int grafMenuIndex = 0; int horoskopMenuIndex = 0; int horoskopScrollPage = 0;

// ===== SD PROHLÍŽEČ =====
String sdCurrentPath = "/";
int sdBrowserIndex = 0;
int sdBrowserScrollOffset = 0;
struct SdEntry { char name[30]; bool isDir; uint32_t size; };
SdEntry sdEntries[50];
int sdEntryCount = 0;

// ===== ARCHIV ZPRÁV =====
String archivDatumy[30];
int    archivDatumCount        = 0;
int    archivDatumIndex        = 0;
int    archivDatumScrollOffset = 0;
int    archivZpravySubIndex    = 0;
int    archivZpravyScrollOffset = 0;
String archivAktualniDatum     = "";
bool   zpravyZArchivu          = false;
int    archivStranka           = 0;   // aktuální stránka při prohlížení archivu (20 článků)
int    archivCelkemClanku      = 0;   // celkový počet článků v otevřeném archivním souboru
String archivOtevrenySoubor    = "";  // cesta k právě otevřenému archivnímu souboru

// ===== STRÁNKOVÁNÍ AKTUALIT (bez limitu 20 článků) =====
// Data rubriky jsou v RAM (stazenaDataSvet…), do políčka aktualniZpravy[20]
// se načítá vždy jen jedna stránka — stejně jako archiv. Ukazatel míří na
// globální String rubriky, takže data zůstávají v paměti.
static const String* aktualniRawZdroj = nullptr;
int    aktualniStranka         = 0;   // aktuální stránka seznamu aktualit
int    aktualniCelkemClanku    = 0;   // celkový počet článků v rubrice
static String sdZpravyRaw      = "";  // data otevřená ze SD v |T| formátu (musí přežít stránkování)

// ===== HLEDÁNÍ V ARCHIVU (podle klíčových slov v titulku) =====
// Znaky klávesnice (bez diakritiky — při hledání se diakritika ignoruje).
// '<' = smazat znak, '>' = vyhledat.
const char archivHledaniZnaky[] = "abcdefghijklmnopqrstuvwxyz0123456789 <>.!?-";
const int  archivHledaniZnakuPocet = sizeof(archivHledaniZnaky) - 1;
char  hledaniBuf[24] = "";
int   hledaniLen = 0;
int   hledaniKurzor = 0;
bool  zpravyZHledani = false; // true = zobrazené výsledky pocházejí z hledání
int   hledaniStranka = 0;      // aktuální stránka výsledků hledání (20 článků)
int   hledaniCelkem = 0;       // celkový počet nalezených článků

// ===== SD TEXT / BMP PROHLÍŽEČ =====
String sdTextViewBuffer = "";
String sdTextViewTitle  = "";

// ===== SD KVÍZ — aktuální otázka načtená ze SD =====
String sdKvizOtazka = "", sdKvizOdpoved = "", sdKvizKatLabel = "";
bool kvizZSD = false;
String aktivniKvizSoubor = "/eindata/kviz/otazky.txt";

// ===== SD WYR — aktuální otázka načtená ze SD =====
String sdWyrOtazka = "", sdWyrMozA = "", sdWyrMozB = "";
int sdWyrProcenta = 50;
bool wyrZSD = false;
String aktivniWyrSoubor = "/eindata/hry/wyr.txt";

// ===== SD GAMEBOOK — aktuálně načtený uzel ze SD =====
String gbTextSD = "", gbPopisASD = "", gbPopisBSD = "";
int gbVolbaASD = -1, gbVolbaBSD = -1;
bool gbZSD = false;
String aktivniGamebookSoubor = "/eindata/gamebook/gamebook.txt";
bool aktivniGamebookMaTitulek = false;
String dynamicGamebookItems[15];
String dynamicGamebookFiles[15];
int dynamicGamebookCount = 0;

// ===== BATERIE A STATUS =====
// Cache pro čtení baterie — měříme max 1× za 60 sekund (čtení trvá ~50 ms)
static float cachedBatVoltage = -1.0;
static unsigned long lastBatReadMs = 0;

float getBatteryVoltage() {
  // Vrátit cache pokud je čerstvá (< 60 s)
  if (cachedBatVoltage >= 0.0 && millis() - lastBatReadMs < 60000UL) return cachedBatVoltage;

  // Zapnout bateriový dělič přes ADC_CTRL (GPIO46, HIGH = zapnuto)
  digitalWrite(ADC_CTRL, HIGH);
  delay(20);  // Stabilizace napětí na děliči

  // Průměr z 16 kalibrovaných čtení (analogReadMilliVolts = factory eFuse kalibrace)
  long sum = 0;
  for (int i = 0; i < 16; i++) {
    sum += analogReadMilliVolts(BAT_PIN);
    delay(2);
  }
  digitalWrite(ADC_CTRL, LOW);  // Vypnout dělič — úspora energie

  float mvAvg = (float)sum / 16.0f;
  if (mvAvg < 100.0f) {
    cachedBatVoltage = 0.0f;
  } else {
    // Dělič R1=390kΩ, R2=100kΩ → poměr (390+100)/100 = 4.9
    cachedBatVoltage = (mvAvg / 1000.0f) * 4.9f;
  }
  lastBatReadMs = millis();
  return cachedBatVoltage;
}

int getBatteryPercentage() {
  static int lastPct = -2;  // -2 = neinicializováno
  float v = getBatteryVoltage();
  if (v <= 0.1f) { lastPct = -1; return -1; }  // neznámý stav / baterie nepřipojena

  int pct;
  // Kalibrace pro tvoji sestavu: 4.04 V bereme jako 100 %.
  if (v >= 4.04f) pct = 100;
  else if (v <= 3.20f) pct = 0;
  // Nelineární mapování pro Li-pol baterii:
  else if (v >= 3.95f) pct = 80 + (int)((v - 3.95f) / 0.09f * 20); // 3.95..4.04 => 80..100
  else if (v >= 3.80f) pct = 50 + (int)((v - 3.80f) / 0.20f * 30);
  else if (v >= 3.60f) pct = 20 + (int)((v - 3.60f) / 0.20f * 30);
  else pct = (int)((v - 3.20f) / 0.40f * 20);

  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;

  // Hystereze ±2 % — zabrání blikání ikony při malých výkyvech napětí
  if (lastPct >= 0 && abs(pct - lastPct) <= 2) pct = lastPct;
  lastPct = pct;
  return pct;
}

// Malá ikona WiFi do stavové lišty (kreslená ručně — 3 oblouky + tečka).
// Kreslí se jen když je WiFi připojená. x,y = levý horní roh (~13x9 px).
void nakresliWifiIkona(int x, int y) {
  if (WiFi.status() != WL_CONNECTED) return;
  int cx = x + 6, cy = y + 8; // střed spodní tečky
  const int r[3] = { 6, 4, 2 };
  for (int i = 0; i < 3; i++) {
    for (int a = 205; a <= 335; a += 7) {
      float rad = a * PI / 180.0f;
      display.drawPixel(cx + (int)lroundf(r[i] * cosf(rad)), cy + (int)lroundf(r[i] * sinf(rad)), fgColor());
    }
  }
  display.fillCircle(cx, cy, 1, fgColor());
}

void nakresliStatusBar() {
  if (appState == STATE_HODINY || appState == STATE_ZPRAVY_TEXT || appState == STATE_ZPRAVY_SEZNAM || 
      appState == STATE_KNIHOVNA_DETAIL || appState == STATE_GAMEBOOK || appState == STATE_ODHALOVACKA || 
      appState == STATE_ODHALOVACKA_DETAIL || appState == STATE_KOSTKY || appState == STATE_POCASI_UI || 
      appState == STATE_STOPKY || appState == STATE_FRAZE_DETAIL || appState == STATE_UCENI || 
      appState == STATE_GENERATOR_RESULT || appState == STATE_FLASKA || appState == STATE_SLOVNIK_DETAIL ||
      appState == STATE_NASTAVENI_O_ZARIZENI || appState == STATE_QR_ZOBRAZ || appState == STATE_KVIZ_ODPOVED ||
      appState == STATE_WYR || appState == STATE_KVIZ || appState == STATE_WYR_VYSLEDEK ||
      appState == STATE_HOROSKOP_DETAIL || appState == STATE_KURZY_GRAF ||
      appState == STATE_SD_TEXT_VIEW || appState == STATE_SD_BMP_VIEW ||
      appState == STATE_WEB_UPLOAD) {
    return;
  }

  int bx = 250; int by = 2; int bw = 20; int bh = 10;
  display.drawRect(bx, by, bw, bh, fgColor());
  display.fillRect(bx+bw, by+2, 2, 6, fgColor());
  
  int pct = getBatteryPercentage(); int bars = 0;
  if (pct < 0) {
    // Neznámý stav — nakresli otazník
    u8g2Fonts.setFontMode(1); u8g2Fonts.setFont(getSmallFont()); u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
    u8g2Fonts.setCursor(bx + 5, by + 9); u8g2Fonts.print("?");
  } else {
    // 4 úrovně: 75%+ = 4 bary, 50%+ = 3, 25%+ = 2, 10%+ = 1, <10% = kritická (křížek)
    if (pct >= 75) bars = 4; else if (pct >= 50) bars = 3; else if (pct >= 25) bars = 2; else if (pct >= 10) bars = 1;
    if (bars == 0) {
      // Kritická baterie — zobraz diagonální čáru
      display.drawLine(bx + 1, by + 1, bx + bw - 2, by + bh - 2, fgColor());
    } else {
      // Každý bar: šířka 3 px, mezera 1 px → 4*(3+1)=16 px + 2 px okraj = 18 px (vejde do bw=20)
      for (int i = 0; i < bars; i++) display.fillRect(bx + 2 + (i * 4), by + 2, 3, bh - 4, fgColor());
    }
  }
  
  struct tm timeinfo;
  int casLevy = bx - 6; // levý okraj obsahu vpravo (vlevo od něj přijde ikona WiFi)
  if (getLocalTime(&timeinfo, 10)) {
    const char* dnyVTydnu[] = {"Ne", "Po", "Ut", "St", "Ct", "Pa", "So"};
    String den = dnyVTydnu[timeinfo.tm_wday];
    char timeStr[6]; sprintf(timeStr, "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    String casText = den + " " + String(timeinfo.tm_mday) + "." + String(timeinfo.tm_mon + 1) + ". " + String(timeStr);
    
    u8g2Fonts.setFontMode(1); u8g2Fonts.setFont(getSmallFont()); u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
    int tw = u8g2Fonts.getUTF8Width(casText.c_str());
    casLevy = bx - tw - 8;
    u8g2Fonts.setCursor(casLevy, 11); 
    u8g2Fonts.print(casText.c_str()); 
  }
  nakresliWifiIkona(casLevy - 15, 1);
}

// ===== MENU DATA =====
const int mainMenuCount = 9; String mainMenuItems[] = { "KNIHOVNA", "AKTUALITY", "HUDBA", "TOOLBOX", "SLOVNÍK", "GENERÁTOR", "HRY", "SD KARTA", "NASTAVENÍ" };
const int aktualityCount = 7; String aktualityItems[] = { "Zprávy", "Světový čas", "Kurzy", "Počasí", "Horoskop", "Spoje", "Kalendář" };
const int zpravyMenuCount = 5; String zpravyMenuItems[] = { "Ze světa", "Z ČR", "Technologie a AI", "Pozitivní zprávy", "Archiv" };
// Dynamická knihovna — naplněna při vstupu do KNIHOVNA
String dynamicKnihovnaItems[15];
String dynamicKnihovnaFiles[15];
int    dynamicKnihovnaCount = 0;
const int toolboxCount = 13; String toolboxItems[] = { "Diktafon", "Opičák", "Dioda", "Stopky", "QR Kódy", "Těžba Bitcoinu", "Touch test", "Encoder test", "Mikrofon", "Buzery", "Recepty", "Časovač", "Kalkulačka" }; bool ledState = false;
// Diktafon — podmenu a seznam nahrávek
const int diktafonMenuCount = 2; String diktafonMenuItems[] = { "Nahrát", "Přehrát" };
String diktafonItems[40];
String diktafonCesty[40];
int    diktafonCount = 0;

// Jízdní řády (offline data z GTFS PID na SD v /eindata/jr)
#define JR_DIR "/eindata/jr"
String jrItems[12];
String jrFiles[12];
String jrLabels[12];
int    jrCount = 0;
String jrAktSoubor = "";
String jrAktLabel = "";
bool   jrRealtimeChci = false;    // uživatel stiskl BTN0 = načíst realtime
bool   jrWifiZapnuto  = false;    // WiFi zapnutá kvůli realtime

// Google kalendář (data z iCal přes GitHub Actions v /eindata/kalendar)
#define KAL_DIR "/eindata/kalendar"
const int kalendarMenuCount = 2; String kalendarMenuItems[] = { "Týden", "Měsíc" };
String kalObsah = "";             // celý soubor událostí
int    kalDenIdx = 0;             // vybraný den 0..6 v týdenním pohledu
int    kalMesic = 0, kalRok = 0;  // zobrazený měsíc v měsíčním pohledu
int    kalMesicDen = 1;           // vybraný den v měsíční mřížce
bool   kalDetailZMesice = false;  // detail otevřený z měsíční mřížky
int    kalDetailOffset = 0;       // offset dne od dneška pro detail
int    kalStranka = 0;            // stránka v detailu dne

// Recepty (/eindata/recepty/*.txt)
#define REC_DIR "/eindata/recepty"
String receptyItems[40];
String receptyFiles[40];
int    receptyCount = 0;
int    receptyPorce = 4;          // aktuální počet porcí
int    receptyZaklad = 4;         // základní počet porcí z receptu
int    receptStranka = 0;
String receptNazev = "", receptSuroviny = "", receptPostup = "";

// DATA PRO QR KÓDY 
const int qrCount = 5; String qrItems[] = { "Platba na účet", "YouTube Kanál", "E-shop (Prsteny)", "Wi-Fi Hotspot", "Vizitka (Kontakt)" };
String qrData[] = {
  "SPD*1.0*ACC:1150279005/2700*AM:0.00*CC:CZK*MSG:Platba", 
  "https://www.youtube.com/@StudioSklep", 
  "https://www.jtatelier.cz/", 
  "WIFI:S:Bobik;T:WPA;P:honzuemanejfoun;;", 
  "BEGIN:VCARD\nVERSION:3.0\nN:Jan Těthal\nFN:Jan Těthal\nTEL:732213904\nORG:IČO 08602573\nEND:VCARD" 
};

const int slovnikCount = 3; String slovnikItems[] = { "Hledat", "Fráze", "Učení" }; int hledanySmer = 0; 
const int frazeMenuCount = 4; String frazeMenuItems[] = { "Nakupování", "Ubytování", "Zdraví", "Zábava" };
int uceniSmer = 0, uceniIdx = 0; bool uceniOdhaleno = false;
const int slovnikLangCount = 7;
String slovnikLangNazvy[slovnikLangCount] = { "Anglictina", "Italstina", "Nemcina", "Spanelstina", "Francouzstina", "Svedstina", "Ukrajinstina" };
String slovnikLangSoubory[slovnikLangCount] = { "cz_en.txt", "cz_it.txt", "cz_de.txt", "cz_es.txt", "cz_fr.txt", "cz_sv.txt", "cz_uk.txt" };
int slovnikLangIdx = 0;
char hledanyText[12] = ""; int hledanyLen = 0, abcKurzor = 0;
const char abeceda[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"; const int abcCount = 26;
const int generatorCount = 4; String generatorItems[] = { "Vtipy", "Žalmy", "Citáty", "Fakta" };
const int hryCount = 6; String hryItems[] = { "Flaška", "Kostky", "Gamebook", "Odhalovačka", "Kvíz", "Co bys radši?" };
const int kostkyCount = 4; String kostkyItems[] = { "6 stěn", "12 stěn", "24 stěn", "2x6 stěn" }; int kostkyStrany[] = { 6, 12, 24, 66 };

// Kvíz kategorie a obtížnost
String kvizKategorieItems[] = { "Kultura", "Věda", "Všeobecný", "Osobnosti", "Sport", "Příroda", "Vše (All-in)" };
String kvizObtiznostItems[] = { "Za 100 bodů", "Za 200 bodů", "Za 300 bodů" };

// Horoskop
const int horoskopCount = 12;
String horoskopItems[] = { "Beran", "Býk", "Blíženci", "Rak", "Lev", "Panna", "Váhy", "Štír", "Střelec", "Kozoroh", "Vodnář", "Ryby" };
String horoskopy[12];

// Graf kurzů: 0=EUR, 1=USD, 2=BTC, 3=Zlato, 4=Stříbro
const char* grafNazvy[] = { "EUR/CZK", "USD/CZK", "BTC/CZK", "Zlato CZK/g", "Stribro CZK/g" };
const int grafCount = 5;

const int nastaveniCount = 7; String nastaveniItems[] = { "Aktualizace", "WiFi", "Vzhled displeje", "Auto uspání", "Baterie", "O zařízení", "USB SD karta" };

// ===== WIFI MENU (sub-položky) =====
String wifiMenuItems[] = { "Hledat sítě", "Vypnout WiFi" };
const int wifiMenuCount = 2;

// ===== KLAVESNICE PRO WIFI HESLO =====
// 16 znaků na řádek, 3 řádky. Poslední dva znaky jsou speciální:
//   '<' = smazat poslední znak, '>' = potvrdit a připojit
const char wifiKlavesniceZnaky[] = "abcdefghijklmnopqrstuvwxyz0123456789-_.@!#$*+=<>";
const int wifiKlavesZnakuPocet = sizeof(wifiKlavesniceZnaky) - 1; // 48 znaků
const int wifiKlavesSloupcu = 16;

String wifiHesloSsid = "";   // SSID, pro které zadáváme heslo
char   wifiHesloBuf[64] = "";
int    wifiHesloLen = 0;
int    wifiKlavesKurzor = 0;
String wifiHesloStatus = "";
// Hranice dlouhého stisku v klávesnici hesla (ms)
const unsigned long WIFI_KB_DLOUHY_MS = 450;
const int vzhledCount = 6; String vzhledItems[] = { "Velikost", "Tloušťka", "Font", "Refresh", "Reverz", "Nápověda: ZAP" };
const int aktualizaceCount = 2; String aktualizaceItems[] = { "Vynutit nyní", "Web nahrávání" };
const int autoSleepCount = 7; String autoSleepItems[] = { "Po 1 minutě", "Po 5 minutách", "Po 10 minutách", "Po 15 minutách", "Po 30 minutách", "Po 1 hodině", "Nikdy" };
unsigned long autoSleepMs[] = { 60000UL, 300000UL, 600000UL, 900000UL, 1800000UL, 3600000UL, 0UL };
int autoSleepIdx = 6; // default: nikdy

// ===== DATA PRO WiFi SKEN =====
String  wifiSitSSID[15];
int32_t wifiSitRSSI[15];
uint8_t wifiSitEnc[15];
int     wifiPocetSiti = 0;
int     wifiSitIndex = 0;
bool    wifiSkenProbiha = false;
unsigned long wifiSkenZacatek = 0;

// ===== DATA ODHALOVAČKY =====
bool odhalenoPole[64]; unsigned long odhalovackaLastMs = 0; bool odhalovackaKonec = false; int odhalovackaSkore = 0;
int odhalovackaIndex = 0; // index aktuální hádanky v odhalovackaData[]

// ===== POMOCNÉ FUNKCE =====
void printCentered(String text, int y, const uint8_t* font) {
    u8g2Fonts.setFont(font); u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
    int tw = u8g2Fonts.getUTF8Width(text.c_str());
    u8g2Fonts.setCursor((display.width() - tw) / 2, y); u8g2Fonts.print(text.c_str());
}

// Vypíše řádek nápovědy — pokud je nápověda v nastavení vypnutá, nic nevykreslí.
// Používá se pro ovládací nápovědy ve spodní části displeje (gamebook je vyjímka).
void napoveda(int x, int y, const char* text) {
  if (!napovedaZapnuta) return;
  u8g2Fonts.setCursor(x, y);
  u8g2Fonts.print(text);
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
  beginUiFrame(true);
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
void zobrazSDInfo(bool cteckaOk) {
  uint8_t cardType = cteckaOk ? SD.cardType() : CARD_NONE;

  Serial.println("=== SD KARTA ===");
  if (!cteckaOk) {
    Serial.println("SD: ctecka NENALEZENA nebo karta chybi!");
  } else {
    Serial.print("SD: karta nalezena, typ: ");
    if      (cardType == CARD_MMC)  Serial.println("MMC");
    else if (cardType == CARD_SD)   Serial.println("SDSC");
    else if (cardType == CARD_SDHC) Serial.println("SDHC");
    else                             Serial.println("NEZNAMY");
    Serial.print("Velikost: ");
    Serial.print((unsigned long)(SD.cardSize() / (1024ULL * 1024ULL)));
    Serial.println(" MB");
  }

  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(bgColor());
    u8g2Fonts.setFontMode(1);
    u8g2Fonts.setForegroundColor(fgColor());
    u8g2Fonts.setBackgroundColor(bgColor());

    u8g2Fonts.setFont(getTitleFont());
    int tw = u8g2Fonts.getUTF8Width("SD KARTA");
    u8g2Fonts.setCursor((display.width() - tw) / 2, 20);
    u8g2Fonts.print("SD KARTA");

    u8g2Fonts.setFont(getBodyFont());

    if (!cteckaOk) {
      tw = u8g2Fonts.getUTF8Width("Ctecka: NENALEZENA");
      u8g2Fonts.setCursor((display.width() - tw) / 2, 50);
      u8g2Fonts.print("Ctecka: NENALEZENA");
      tw = u8g2Fonts.getUTF8Width("Zkontroluj zapojeni");
      u8g2Fonts.setCursor((display.width() - tw) / 2, 70);
      u8g2Fonts.print("Zkontroluj zapojeni");
      char sdHint[32];
      snprintf(sdHint, sizeof(sdHint), "MISO=GPIO%d  CS=GPIO%d", SD_MISO_PIN, SD_CS_PIN);
      tw = u8g2Fonts.getUTF8Width(sdHint);
      u8g2Fonts.setCursor((display.width() - tw) / 2, 90);
      u8g2Fonts.print(sdHint);
    } else {
      tw = u8g2Fonts.getUTF8Width("Ctecka: OK");
      u8g2Fonts.setCursor((display.width() - tw) / 2, 50);
      u8g2Fonts.print("Ctecka: OK");

      String typStr = "Typ: ";
      if      (cardType == CARD_MMC)  typStr += "MMC";
      else if (cardType == CARD_SD)   typStr += "SDSC";
      else if (cardType == CARD_SDHC) typStr += "SDHC";
      else                             typStr += "neznamy";
      tw = u8g2Fonts.getUTF8Width(typStr.c_str());
      u8g2Fonts.setCursor((display.width() - tw) / 2, 70);
      u8g2Fonts.print(typStr.c_str());

      String sizStr = "Velikost: " + String((unsigned long)(SD.cardSize() / (1024ULL * 1024ULL))) + " MB";
      tw = u8g2Fonts.getUTF8Width(sizStr.c_str());
      u8g2Fonts.setCursor((display.width() - tw) / 2, 90);
      u8g2Fonts.print(sizStr.c_str());
    }

    u8g2Fonts.setFont(getSmallFont());
    tw = u8g2Fonts.getUTF8Width("Pokracuj stiskem tlacitka...");
    u8g2Fonts.setCursor((display.width() - tw) / 2, 120);
    u8g2Fonts.print("Pokracuj stiskem tlacitka...");
  } while (display.nextPage());

  // Počkáme na stisk libovolného tlačítka
  while (digitalRead(BTN_DOWN) == HIGH && digitalRead(BTN_SELECT) == HIGH) {
    delay(50);
  }
  delay(200); // debounce
}

// ===== UKLÁDÁNÍ DAT DO PAMĚTI (PŘEŽIJE DEEP SLEEP) =====
void ulozZpravuDoCache(const String& klic, const String& data) {
  // Ořízne data na max 20000 znaků (hledá konec poslední zprávy |E|)
  int maxLen = data.length();
  if (maxLen > 20000) {
    int lastE = data.lastIndexOf("|E|", 20000);
    maxLen = (lastE > 0) ? lastE + 3 : 20000;
  }
  if (sdReady) {
    // SD je primární úložiště — přepíšeme soubor čistě
    String path = "/eindata/cache/" + klic + ".dat";
    if (SD.exists(path.c_str())) SD.remove(path.c_str());
    File f = SD.open(path.c_str(), FILE_WRITE);
    if (f) {
      // Zapisujeme po částech místo celého Stringu — šetříme RAM
      const int CHUNK = 2048;
      for (int pos = 0; pos < maxLen; pos += CHUNK) {
        int len = min(CHUNK, maxLen - pos);
        f.write((const uint8_t*)data.c_str() + pos, len);
      }
      f.close();
      return;
    }
  }
  // Fallback: LittleFS (pokud SD není dostupná)
  if (!littlefsReady) return;
  String path = "/" + klic + ".dat";
  File f = LittleFS.open(path, "w");
  if (f) {
    const int CHUNK = 2048;
    for (int pos = 0; pos < maxLen; pos += CHUNK) {
      int len = min(CHUNK, maxLen - pos);
      f.write((const uint8_t*)data.c_str() + pos, len);
    }
    f.close();
  }
  else { Serial.println("LittleFS: nelze zapsat " + path); }
}

void ulozStazenaData() {
  ulozZpravuDoCache("svet", stazenaDataSvet);
  ulozZpravuDoCache("cr", stazenaDataCR);
  ulozZpravuDoCache("tech", stazenaDataTech);
  ulozZpravuDoCache("poz", stazenaDataPozitivni);
  ulozZpravuDoCache("poc", stazenaDataPocasi);
  ulozZpravuDoCache("kur", stazenaDataKurzy);
  ulozZpravuDoCache("horo", stazenaDataHoroskop);
  ulozZpravuDoCache("khist", stazenaDataKurzyHistorie);
}

String nactiSouborZFS(const char* path) {
  if (!littlefsReady) return "";
  if (!LittleFS.exists(path)) return "";
  File f = LittleFS.open(path, "r");
  if (!f) return "";
  String s = f.readString();
  f.close();
  return s;
}

// Načte celý textový soubor ze SD karty (jízdní řády, klíč apod.).
static String nactiTextZeSD(const String& cesta) {
  if (!sdReady) return "";
  File f = SD.open(cesta.c_str());
  if (!f) return "";
  String s = f.readString();
  f.close();
  return s;
}

// Načte datový soubor cache — SD primárně, LittleFS jako záloha
String nactiSouborZCache(const char* klic) {
  if (sdReady) {
    String sdPath = String("/eindata/cache/") + klic + ".dat";
    File f = SD.open(sdPath.c_str());
    if (f) { String s = f.readString(); f.close(); if (s.length() > 0) return s; }
  }
  String lfPath = String("/") + klic + ".dat";
  return nactiSouborZFS(lfPath.c_str());
}

// Načte zprávy ze SD aktualni — primární zdroj
String nactiSouborZAktualni(const char* soubor) {
  if (!sdReady) return "";
  String cesta = String("/eindata/zpravy/aktualni/") + soubor;
  File f = SD.open(cesta.c_str());
  if (f) { String s = f.readString(); f.close(); return s; }
  return "";
}

void nactiStazenaData() {
  // Zprávy — SD aktualni má přednost před cache
  stazenaDataSvet  = nactiSouborZAktualni("zpravy_svet.txt");
  if (stazenaDataSvet.length() == 0)  stazenaDataSvet  = nactiSouborZCache("svet");
  stazenaDataCR    = nactiSouborZAktualni("zpravy_cr.txt");
  if (stazenaDataCR.length() == 0)    stazenaDataCR    = nactiSouborZCache("cr");
  stazenaDataTech  = nactiSouborZAktualni("zpravy_tech.txt");
  if (stazenaDataTech.length() == 0)  stazenaDataTech  = nactiSouborZCache("tech");
  stazenaDataPozitivni = nactiSouborZAktualni("zpravy_pozitivni.txt");
  if (stazenaDataPozitivni.length() == 0) stazenaDataPozitivni = nactiSouborZCache("poz");

  // Cache data (počasí, kurzy, horoskopy)
  stazenaDataPocasi        = nactiSouborZCache("poc");
  stazenaDataKurzy         = nactiSouborZCache("kur");
  stazenaDataHoroskop      = nactiSouborZCache("horo");
  stazenaDataKurzyHistorie = nactiSouborZCache("khist");

  if (stazenaDataPocasi    != "") parsujPocasi(stazenaDataPocasi);
  if (stazenaDataKurzy     != "") parsujKurzy(stazenaDataKurzy);
  // Seznam zpráv nepřenačítáme, když ho uživatel právě čte — jinak by se
  // mu pod rukama změnil obsah i počet článků.
  if (stazenaDataSvet      != "" && appState != STATE_ZPRAVY_TEXT && appState != STATE_ZPRAVY_SEZNAM)
    parsujZpravy(stazenaDataSvet);
  if (stazenaDataHoroskop  != "") parsujHoroskopy(stazenaDataHoroskop);
}


// ===== NÁHODNÝ VÝBĚR BEZ OPAKOVÁNÍ =====
// "Shuffle bag": vrací indexy 0..pocet-1 v náhodném pořadí tak, že se každá
// položka objeví právě jednou. Teprve po vyčerpání celé řady se zamíchá znovu.
// Řady se drží pro jednotlivé zdroje (soubor / kategorii), aby se neovlivňovaly.
struct NahodnaRada {
  uint32_t klic;
  uint16_t pocet;
  uint16_t dalsi;
  uint16_t* poradi;
};
#define NAHODNYCH_RAD 32
static NahodnaRada nahodneRady[NAHODNYCH_RAD];
static int nahodnaRadPocet = 0;

static uint32_t nahodnyKlic(const char* s, int sul) {
  uint32_t h = 2166136261u;
  while (*s) { h ^= (uint8_t)(*s++); h *= 16777619u; }
  h ^= (uint32_t)sul * 2654435761u;
  return h;
}

static void _zamichejRadu(int i) {
  NahodnaRada* r = &nahodneRady[i];
  for (uint16_t k = 0; k < r->pocet; k++) r->poradi[k] = k;
  for (uint16_t k = r->pocet; k > 1; k--) {
    uint16_t j = (uint16_t)random(k);
    uint16_t t = r->poradi[k - 1]; r->poradi[k - 1] = r->poradi[j]; r->poradi[j] = t;
  }
  r->dalsi = 0;
}

static bool _alokujRadu(int i, uint16_t pocet) {
  NahodnaRada* r = &nahodneRady[i];
  if (r->poradi) { free(r->poradi); r->poradi = nullptr; }
  r->pocet = 0;
  if (pocet == 0) return false;
  r->poradi = (uint16_t*)malloc((size_t)pocet * sizeof(uint16_t));
  if (!r->poradi) return false;
  r->pocet = pocet;
  _zamichejRadu(i);
  return true;
}

static uint16_t nahodnyIndex(uint32_t klic, int pocet) {
  if (pocet <= 0) return 0;
  if (pocet > 65535) pocet = 65535;
  int ri = -1;
  for (int i = 0; i < nahodnaRadPocet; i++) {
    if (nahodneRady[i].klic == klic) { ri = i; break; }
  }
  if (ri < 0) {
    if (nahodnaRadPocet >= NAHODNYCH_RAD) {      // nejstarší radu uvolníme
      free(nahodneRady[0].poradi);
      for (int i = 1; i < NAHODNYCH_RAD; i++) nahodneRady[i - 1] = nahodneRady[i];
      nahodnaRadPocet = NAHODNYCH_RAD - 1;
    }
    ri = nahodnaRadPocet++;
    nahodneRady[ri].klic = klic;
    nahodneRady[ri].pocet = 0;
    nahodneRady[ri].dalsi = 0;
    nahodneRady[ri].poradi = nullptr;
    if (!_alokujRadu(ri, (uint16_t)pocet)) return (uint16_t)random(pocet);
  }
  // změnil se počet položek (např. uživatel upravil soubor) → nová řada
  if (nahodneRady[ri].pocet != (uint16_t)pocet && !_alokujRadu(ri, (uint16_t)pocet))
    return (uint16_t)random(pocet);
  if (nahodneRady[ri].dalsi >= nahodneRady[ri].pocet) _zamichejRadu(ri);
  return nahodneRady[ri].poradi[nahodneRady[ri].dalsi++];
}

// Počet neprázdných řádků v textovém souboru na SD.
static int spocitejRadkySD(const char* cesta) {
  File f = SD.open(cesta);
  if (!f) return 0;
  int n = 0;
  while (f.available()) {
    String r = f.readStringUntil('\n');
    r.trim();
    if (r.length() > 0) n++;
  }
  f.close();
  return n;
}

// Načte řádek podle pořadí (0 = první neprázdný).
static String nactiRadekPodleIndexu(const char* cesta, int idx) {
  if (idx < 0) return "";
  File f = SD.open(cesta);
  if (!f) return "";
  String out = "";
  int n = 0;
  while (f.available()) {
    String r = f.readStringUntil('\n');
    r.trim();
    if (r.length() == 0) continue;
    if (n == idx) { out = r; break; }
    n++;
  }
  f.close();
  return out;
}


// ===== GENERÁTOR — NAČTENÍ S FALLBACKEM NA SD =====
// Pokud je SD dostupná a soubor existuje, načte náhodný řádek z SD (uživatel může obsah upravit).
// Jinak vrátí položku z PROGMEM pole (zabudovaná data).
static String _nahodnyTextGeneratoru(const char* sdCesta, const char** pole, int pocet) {
  if (sdReady) {
    int n = spocitejRadkySD(sdCesta);
    if (n > 0) {
      String s = nactiRadekPodleIndexu(sdCesta, nahodnyIndex(nahodnyKlic(sdCesta, 1), n));
      if (s.length() > 0) return s;
    }
  }
  if (pocet <= 0) return "";
  return String(pole[nahodnyIndex(nahodnyKlic(sdCesta, 2), pocet)]);
}

static bool _endsWithTxt(const char* s) {
  if (!s) return false;
  int n = strlen(s);
  if (n < 4) return false;
  char a = s[n - 4], b = s[n - 3], c = s[n - 2], d = s[n - 1];
  return (a == '.' && (b == 't' || b == 'T') && (c == 'x' || c == 'X') && (d == 't' || d == 'T'));
}

static bool _isReadmeTxt(const char* s) {
  if (!s) return false;
  // case-insensitive prefix "README"
  const char* p = "README";
  for (int i = 0; p[i] != '\0'; i++) {
    char c = s[i];
    if (c == '\0') return false;
    if (c >= 'a' && c <= 'z') c = c - ('a' - 'A');
    if (c != p[i]) return false;
  }
  return true;
}

static bool _isDataTxtFile(const char* baseName) {
  return _endsWithTxt(baseName) && !_isReadmeTxt(baseName);
}

static int _countCharInString(const String& s, char c) {
  int cnt = 0;
  for (int i = 0; i < (int)s.length(); i++) if (s[i] == c) cnt++;
  return cnt;
}

// ===== KONVERZE TEXTU ZE SD DO UTF-8 =====
// Soubory vytvořené ve Windows bývají v CP1250 (Windows-1250), ne v UTF-8.
// Tady je poznáme a převedeme, aby se česká diakritika zobrazila správně.
static bool _jeValidniUtf8(const String& s) {
  int i = 0, n = s.length();
  while (i < n) {
    uint8_t c = (uint8_t)s[i];
    int d;
    if (c < 0x80) { i++; continue; }
    else if ((c & 0xE0) == 0xC0) d = 1;
    else if ((c & 0xF0) == 0xE0) d = 2;
    else if ((c & 0xF8) == 0xF0) d = 3;
    else return false;
    if (i + d >= n) return false;
    for (int k = 1; k <= d; k++)
      if (((uint8_t)s[i + k] & 0xC0) != 0x80) return false;
    i += d + 1;
  }
  return true;
}

// CP1250 (0x80–0xFF) → Unicode
static const uint16_t _cp1250Mapa[128] = {
  0x20AC, 0, 0x201A, 0, 0x201E, 0x2026, 0x2020, 0x2021,
  0, 0x2030, 0x0160, 0x2039, 0x015A, 0x0164, 0x017D, 0x0179,
  0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
  0, 0x2122, 0x0161, 0x203A, 0x015B, 0x0165, 0x017E, 0x017A,
  0x00A0, 0x02C7, 0x02D8, 0x0141, 0x00A4, 0x0104, 0x00A6, 0x00A7,
  0x00A8, 0x00A9, 0x015E, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x017B,
  0x00B0, 0x00B1, 0x02DB, 0x0142, 0x00B4, 0x00B5, 0x00B6, 0x00B7,
  0x00B8, 0x0105, 0x015F, 0x00BB, 0x013D, 0x02DD, 0x013E, 0x017C,
  0x0154, 0x00C1, 0x00C2, 0x0102, 0x00C4, 0x0139, 0x0106, 0x00C7,
  0x010C, 0x00C9, 0x0118, 0x00CB, 0x011A, 0x00CD, 0x00CE, 0x010E,
  0x0110, 0x0143, 0x0147, 0x00D3, 0x00D4, 0x0150, 0x00D6, 0x00D7,
  0x0158, 0x016E, 0x00DA, 0x0170, 0x00DC, 0x00DD, 0x0162, 0x00DF,
  0x0155, 0x00E1, 0x00E2, 0x0103, 0x00E4, 0x013A, 0x0107, 0x00E7,
  0x010D, 0x00E9, 0x0119, 0x00EB, 0x011B, 0x00ED, 0x00EE, 0x010F,
  0x0111, 0x0144, 0x0148, 0x00F3, 0x00F4, 0x0151, 0x00F6, 0x00F7,
  0x0159, 0x016F, 0x00FA, 0x0171, 0x00FC, 0x00FD, 0x0163, 0x02D9
};

static void _pripojUtf8(String& out, uint16_t cp) {
  if (cp == 0) { out += '?'; return; }
  if (cp < 0x80) { out += (char)cp; }
  else if (cp < 0x800) {
    out += (char)(0xC0 | (cp >> 6));
    out += (char)(0x80 | (cp & 0x3F));
  } else {
    out += (char)(0xE0 | (cp >> 12));
    out += (char)(0x80 | ((cp >> 6) & 0x3F));
    out += (char)(0x80 | (cp & 0x3F));
  }
}

static String _textNaUtf8(String s) {
  // UTF-8 BOM na začátku souboru/řádku zahodíme
  if (s.length() >= 3 && (uint8_t)s[0] == 0xEF && (uint8_t)s[1] == 0xBB && (uint8_t)s[2] == 0xBF)
    s = s.substring(3);
  if (_jeValidniUtf8(s)) return s;      // už je UTF-8
  String out; out.reserve(s.length() * 2);
  for (int i = 0; i < (int)s.length(); i++) {
    uint8_t c = (uint8_t)s[i];
    if (c < 0x80) out += (char)c;
    else _pripojUtf8(out, _cp1250Mapa[c - 0x80]);
  }
  return out;
}

static bool _gamebookHasTitleLine(const char* path) {
  if (!sdReady) return false;
  File f = SD.open(path);
  if (!f) return false;
  String first = f.readStringUntil('\n');
  f.close();
  first.trim();
  if (first.length() == 0) return false;
  // Uzel gamebooku má obvykle 4 oddělovače '|'. Pokud je to méně, bereme to jako titulek.
  return _countCharInString(first, '|') < 4;
}

static String _firstTxtInDir(const char* dirPath, const char* preferredFallback) {
  if (!sdReady) return String(preferredFallback);
  File dir = SD.open(dirPath);
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return String(preferredFallback);
  }
  String vybrany = "";
  File entry = dir.openNextFile();
  while (entry) {
    if (!entry.isDirectory()) {
      const char* full = entry.name();
      const char* base = strrchr(full, '/');
      base = base ? (base + 1) : full;
      if (_isDataTxtFile(base)) {
        vybrany = String(dirPath);
        if (!vybrany.endsWith("/")) vybrany += "/";
        vybrany += base;
        entry.close();
        break;
      }
    }
    entry.close();
    entry = dir.openNextFile();
  }
  if (entry) entry.close();
  dir.close();
  if (vybrany.length() == 0) return String(preferredFallback);
  return vybrany;
}

static void obnovAktivniDatoveSoubory() {
  aktivniGamebookSoubor = _firstTxtInDir("/eindata/gamebook", "/eindata/gamebook/gamebook.txt");
  aktivniGamebookMaTitulek = _gamebookHasTitleLine(aktivniGamebookSoubor.c_str());
  aktivniKvizSoubor = _firstTxtInDir("/eindata/kviz", "/eindata/kviz/otazky.txt");
  // Preferujeme explicitně wyr.txt, ale pokud není, vezmeme první TXT ve složce.
  if (sdReady && SD.exists("/eindata/hry/wyr.txt")) aktivniWyrSoubor = "/eindata/hry/wyr.txt";
  else aktivniWyrSoubor = _firstTxtInDir("/eindata/hry", "/eindata/hry/wyr.txt");
}

static void nactiSeznamGamebooku() {
  dynamicGamebookCount = 0;
  if (sdReady) {
    File dir = SD.open("/eindata/gamebook");
    if (dir && dir.isDirectory()) {
      File entry = dir.openNextFile();
      while (entry && dynamicGamebookCount < 15) {
        if (!entry.isDirectory()) {
          const char* full = entry.name();
          const char* base = strrchr(full, '/');
          base = base ? (base + 1) : full;
          if (_isDataTxtFile(base)) {
            String path = "/eindata/gamebook/";
            path += base;
            String title = "";
            File gf = SD.open(path.c_str());
            if (gf) {
              title = gf.readStringUntil('\n');
              gf.close();
              title.trim();
            }
            if (title.length() == 0 || _countCharInString(title, '|') >= 4) {
              title = String(base);
            } else {
              title = _textNaUtf8(title);
            }
            dynamicGamebookFiles[dynamicGamebookCount] = path;
            dynamicGamebookItems[dynamicGamebookCount] = title;
            dynamicGamebookCount++;
          }
        }
        entry.close();
        entry = dir.openNextFile();
      }
      if (entry) entry.close();
      dir.close();
    } else if (dir) {
      dir.close();
    }
  }

  if (dynamicGamebookCount == 0) {
    dynamicGamebookFiles[0] = "/eindata/gamebook/gamebook.txt";
    dynamicGamebookItems[0] = "Zaklinac";
    dynamicGamebookCount = 1;
  }
}

static String _generatorPathForCategory(const char* fallbackPath, const char* key) {
  if (!sdReady) return String(fallbackPath);
  File dir = SD.open("/eindata/generator");
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return String(fallbackPath);
  }
  String vybrany = "";
  File entry = dir.openNextFile();
  while (entry) {
    if (!entry.isDirectory()) {
      const char* full = entry.name();
      const char* base = strrchr(full, '/');
      base = base ? (base + 1) : full;
      if (_endsWithTxt(base)) {
        String nm = String(base);
        nm.toLowerCase();
        if (nm.indexOf(key) >= 0) {
          vybrany = "/eindata/generator/";
          vybrany += base;
          entry.close();
          break;
        }
      }
    }
    entry.close();
    entry = dir.openNextFile();
  }
  if (entry) entry.close();
  dir.close();
  if (vybrany.length() == 0) return String(fallbackPath);
  return vybrany;
}

static String _basenameOnly(const String& in) {
  int p1 = in.lastIndexOf('/');
  int p2 = in.lastIndexOf('\\');
  int p = (p1 > p2) ? p1 : p2;
  return (p >= 0) ? in.substring(p + 1) : in;
}

static bool _isSafeUploadName(const String& name) {
  if (name.length() < 1 || name.length() > 100) return false;
  if (name.indexOf("..") >= 0) return false;
  for (int i = 0; i < (int)name.length(); i++) {
    uint8_t c = (uint8_t)name[i];
    if (c < 0x20 || c == 0x7F) return false; // ridici znaky
    if (c == '/' || c == '\\' || c == ':' || c == '*' ||
        c == '?' || c == '"' || c == '<' || c == '>' || c == '|') return false; // neplatne v nazvu souboru
  }
  return true; // povolujeme i mezery a ceske znaky (UTF-8)
}

static bool _hasExt(const String& s, const char* ext) {
  if (s.length() < 4) return false;
  String low = s; low.toLowerCase();
  return low.endsWith(ext);
}

static bool _isAudioName(const String& s) {
  return _hasExt(s, ".wav") || _hasExt(s, ".mp3") || _hasExt(s, ".m4a") ||
         _hasExt(s, ".ogg") || _hasExt(s, ".flac");
}

// Povolene pripony podle typu slozky (hudba = audio, ostatni = .txt)
static bool _extAllowedForKind(const String& kind, const String& name) {
  if (kind == "hudba") return _isAudioName(name);
  return _hasExt(name, ".txt");
}

static String _urlEncode(const String& in) {
  static const char* hex = "0123456789ABCDEF";
  String out;
  for (int i = 0; i < (int)in.length(); i++) {
    uint8_t c = (uint8_t)in[i];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
        c == '-' || c == '_' || c == '.' || c == '~') out += (char)c;
    else { out += '%'; out += hex[c >> 4]; out += hex[c & 0x0F]; }
  }
  return out;
}

static String _uploadDirFromKind(const String& kind) {
  if (kind == "knihy") return "/eindata/knihy/";
  if (kind == "gamebook") return "/eindata/gamebook/";
  if (kind == "slovnik") return "/eindata/slovnik/";
  if (kind == "generator") return "/eindata/generator/";
  if (kind == "kviz") return "/eindata/kviz/";
  if (kind == "hry" || kind == "wyr") return "/eindata/hry/";
  if (kind == "recepty") return "/eindata/recepty/";
  if (kind == "hudba") return "/hudba/";
  return "";
}

static String _htmlEscape(const String& in) {
  String out = in;
  out.replace("&", "&amp;");
  out.replace("<", "&lt;");
  out.replace(">", "&gt;");
  out.replace("\"", "&quot;");
  return out;
}

// ===== MODERNÍ WEBOVÉ ROZHRANÍ (společné CSS) =====
static const char* _webCss() {
  return
    ":root{--bg:#0f1115;--card:#171a21;--line:#262b35;--txt:#e8ecf1;--mut:#8a94a6;"
    "--acc:#4f8cff;--ok:#31c48d;--err:#f05252}"
    "*{box-sizing:border-box}"
    "body{margin:0;padding:28px 16px;background:radial-gradient(1200px 600px at 50% -220px,#1b2436,#0f1115 62%);"
    "color:var(--txt);font:15px/1.55 system-ui,-apple-system,'Segoe UI',Roboto,sans-serif}"
    ".wrap{max-width:780px;margin:0 auto}"
    "h1{font-size:26px;margin:0 0 4px;letter-spacing:-.4px}"
    "h1 span{color:var(--acc)}"
    ".sub{color:var(--mut);margin:0 0 18px;font-size:14px}"
    ".chips{display:flex;gap:8px;flex-wrap:wrap;margin-bottom:18px}"
    ".chip{background:var(--card);border:1px solid var(--line);border-radius:999px;padding:5px 12px;"
    "font-size:12px;color:var(--mut)}"
    ".chip b{color:var(--txt);font-weight:600}"
    ".card{background:var(--card);border:1px solid var(--line);border-radius:16px;padding:18px;margin-bottom:14px}"
    ".card h2{font-size:15px;margin:0 0 14px;font-weight:600}"
    "label{display:block;font-size:11px;color:var(--mut);margin:0 0 6px;"
    "text-transform:uppercase;letter-spacing:.07em}"
    "select,input[type=text]{width:100%;background:#0f131a;color:var(--txt);border:1px solid var(--line);"
    "border-radius:10px;padding:11px 12px;font:inherit;outline:none}"
    "select:focus,input[type=text]:focus{border-color:var(--acc)}"
    ".row{display:grid;grid-template-columns:1fr 1fr;gap:12px}"
    ".drop{margin:14px 0 10px;border:2px dashed #303746;border-radius:14px;padding:26px 16px;text-align:center;"
    "color:var(--mut);cursor:pointer;transition:.18s}"
    ".drop:hover,.drop.on{border-color:var(--acc);color:var(--txt);background:#141c2b}"
    ".drop b{color:var(--txt)}"
    ".drop .file{display:none;margin-top:8px;color:var(--ok);font-weight:600;font-size:13px;word-break:break-all}"
    "input[type=file]{display:none}"
    "button{width:100%;border:0;border-radius:10px;padding:13px;margin-top:6px;"
    "background:linear-gradient(180deg,#5b95ff,#3f7bf0);color:#fff;font:600 15px/1 sans-serif;cursor:pointer}"
    "button:hover{filter:brightness(1.08)}"
    "button:disabled{opacity:.55;cursor:default}"
    ".danger{background:#2a1719;color:#ff9191;border:1px solid #4a2226}"
    ".grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(132px,1fr));gap:8px}"
    ".grid a{display:block;text-align:center;padding:10px 8px;border:1px solid var(--line);border-radius:10px;"
    "color:var(--txt);text-decoration:none;font-size:13px;background:#12161d}"
    ".grid a:hover{border-color:var(--acc)}"
    "a{color:var(--acc)}"
    ".foot{color:var(--mut);font-size:12px;text-align:center;margin-top:22px;line-height:1.6}"
    ".ok{color:var(--ok)}.err{color:var(--err)}"
    "details.card summary{cursor:pointer;font-weight:600;font-size:15px;list-style:none}"
    "details.card summary::-webkit-details-marker{display:none}"
    "details.card summary:after{content:'+';float:right;color:var(--mut)}"
    "details[open].card summary:after{content:'\\2013'}"
    "table{width:100%;border-collapse:collapse;font-size:14px}"
    "th{text-align:left;color:var(--mut);font-size:11px;text-transform:uppercase;letter-spacing:.07em;"
    "padding:0 8px 8px;border-bottom:1px solid var(--line);font-weight:600}"
    "td{padding:9px 8px;border-bottom:1px solid #1e222b;vertical-align:middle}"
    "tr:last-child td{border-bottom:0}"
    "td.acts{text-align:right;white-space:nowrap}"
    "td.acts a{display:inline-block;padding:3px 8px;border-radius:7px;text-decoration:none;font-size:13px;margin-left:4px}"
    "td.acts a:hover{background:#1d2432}"
    ".acts .del{color:#ff9191}"
    "pre{white-space:pre-wrap;word-break:break-word;background:#0f131a;border:1px solid var(--line);"
    "border-radius:12px;padding:16px;font:13px/1.6 ui-monospace,Consolas,monospace;margin:0;max-height:70vh;overflow:auto}"
    ".note{background:#141c2b;border:1px solid #24314a;border-radius:12px;padding:12px 14px;"
    "font-size:13px;color:#a9b6cc;margin-top:14px}"
    "@media(max-width:560px){.row{grid-template-columns:1fr}body{padding:20px 12px}}";
}

static String _webPageStart(const String& title) {
  String h = "<!doctype html><html lang='cs'><head><meta charset='utf-8'>"
             "<meta name='viewport' content='width=device-width,initial-scale=1'>"
             "<title>" + _htmlEscape(title) + " · HonzueINK</title><style>";
  h += _webCss();
  h += "</style></head><body><div class='wrap'>";
  return h;
}

static String _webKindOptions() {
  return
    "<option value='knihy'>Knihy</option>"
    "<option value='gamebook'>Gamebook</option>"
    "<option value='slovnik'>Slovník</option>"
    "<option value='generator'>Generátor</option>"
    "<option value='kviz'>Kvíz</option>"
    "<option value='hry'>WYR / Hry</option>"
    "<option value='recepty'>Recepty</option>"
    "<option value='hudba'>Hudba (WAV/MP3)</option>";
}

static String _listFilesHtmlForKind(const String& kind) {
  String dirPath = _uploadDirFromKind(kind);
  if (dirPath.length() == 0) return "<p class='err'>Neplatný typ složky.</p>";
  if (!sdReady) return "<p class='err'>SD není dostupná.</p>";
  File dir = SD.open(dirPath.c_str());
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return "<p class='err'>Složka neexistuje: " + _htmlEscape(dirPath) + "</p>";
  }
  String k = kind; if (k.length() == 0) k = "knihy";
  String html = _webPageStart("Soubory");
  html += "<h1>Soubory <span>" + _htmlEscape(k) + "</span></h1>";
  html += "<p class='sub'>" + _htmlEscape(dirPath) + "</p>";
  html += "<div class='card'><table><tr><th>Soubor</th><th>Velikost</th><th></th></tr>";
  int cnt = 0;
  File entry = dir.openNextFile();
  while (entry && cnt < 200) {
    if (!entry.isDirectory()) {
      const char* full = entry.name();
      const char* base = strrchr(full, '/');
      base = base ? (base + 1) : full;
      String name = String(base);
      String escName = _htmlEscape(name);
      String urlName = _urlEncode(name);
      html += "<tr><td>" + escName + "</td><td>" + String((unsigned long)entry.size()) + " B</td><td class='acts'>";
      if (_hasExt(name, ".txt")) html += "<a href='/view?kind=" + k + "&filename=" + urlName + "'>Otevřít</a>";
      html += "<a class='del' href='/delete-file?kind=" + k + "&filename=" + urlName +
              "' onclick=\"return confirm('Opravdu smazat tento soubor?');\">Smazat</a>";
      html += "</td></tr>";
      cnt++;
    }
    entry.close();
    entry = dir.openNextFile();
  }
  if (entry) entry.close();
  dir.close();
  if (cnt == 0) html += "<tr><td colspan='3' style='color:#8a94a6'>Složka je prázdná.</td></tr>";
  html += "</table></div>";
  html += "<p><a href='/'>← Zpět na nahrávání</a></p>";
  html += "</div></body></html>";
  return html;
}

static void obnovDynamickyObsahPoUploadu() {
  obnovAktivniDatoveSoubory();
  nactiSeznamKnih();
  nactiSeznamGamebooku();
}

static void _sendWebUploadHome() {
  bool staOk = (WiFi.status() == WL_CONNECTED) && !webUploadApMode;
  String mode = staOk ? "STA" : "AP";
  // Živá IP z driveru — cache webUploadStaIp může po obnově DHCP zastarát
  String ip = staOk ? WiFi.localIP().toString() : webUploadApIp;
  if (ip.length() == 0 || ip == "0.0.0.0") ip = WiFi.softAPIP().toString();

  String chips = "";
  if (staOk && webUploadSsid.length() > 0) {
    chips += "<span class='chip'>WiFi <b>" + _htmlEscape(webUploadSsid) + "</b></span>";
    if (webUploadApIp.length() > 0)
      chips += "<span class='chip'>AP záloha <b>http://" + webUploadApIp + "/</b></span>";
  }

  String html = _webPageStart("Web upload");
  html +=
    "<h1>Honzue<span>INK</span></h1>"
    "<p class='sub'>Nahrávání souborů do ePaperu přes Wi-Fi</p>"
    "<div class='chips'>"
    "<span class='chip'>Režim <b>" + mode + "</b></span>"
    "<span class='chip'>Adresa <b>http://" + ip + "/</b></span>"
    "<span class='chip'>mDNS <b>honzueink.local</b></span>"
    "<span class='chip'>SD <b class='" + String(sdReady ? "ok'>OK" : "err'>chyba") + "</b></span>" +
    chips +
    "</div>";

  html +=
    "<div class='card'>"
    "<h2>Nahrát soubor</h2>"
    "<form id='upf' method='POST' action='/upload' enctype='multipart/form-data'>"
    "<div class='row'>"
    "<div><label>Typ</label><select name='kind'>" + _webKindOptions() + "</select></div>"
    "<div><label>Název souboru (nepovinné)</label>"
    "<input type='text' name='filename' placeholder='např. palacinky.txt'></div>"
    "</div>"
    "<div class='drop' id='drop'><b>Přetáhni soubor sem</b> nebo klikni pro výběr"
    "<div class='file' id='fname'></div></div>"
    "<input type='file' id='file' name='file' "
    "accept='.txt,text/plain,.wav,.mp3,.m4a,.ogg,.flac,audio/*' required>"
    "<button type='submit' id='upbtn'>Nahrát soubor</button>"
    "</form>"
    "<div class='note'>Povolené typy: <b>.txt</b> (texty, recepty, hry, knihy) a "
    "<b>.wav / .mp3 / .m4a / .ogg / .flac</b> (hudba).</div>"
    "</div>";

  html +=
    "<details class='card'><summary>Smazat soubor</summary>"
    "<form id='delf' method='POST' action='/delete' style='margin-top:16px'>"
    "<div class='row'>"
    "<div><label>Typ</label><select name='kind'>" + _webKindOptions() + "</select></div>"
    "<div><label>Název souboru</label>"
    "<input type='text' name='filename' placeholder='palacinky.txt' required></div>"
    "</div>"
    "<button class='danger' type='submit'>Smazat soubor</button>"
    "</form>"
    "</details>";

  html +=
    "<div class='card'><h2>Obsah na SD kartě</h2><div class='grid'>"
    "<a href='/list?kind=knihy'>Knihy</a>"
    "<a href='/list?kind=gamebook'>Gamebook</a>"
    "<a href='/list?kind=slovnik'>Slovník</a>"
    "<a href='/list?kind=generator'>Generátor</a>"
    "<a href='/list?kind=kviz'>Kvíz</a>"
    "<a href='/list?kind=hry'>WYR / Hry</a>"
    "<a href='/list?kind=recepty'>Recepty</a>"
    "<a href='/list?kind=hudba'>Hudba</a>"
    "</div></div>";

  html +=
    "<div class='card'><h2>Ukázky formátů</h2><div class='grid'>"
    "<a href='/example?kind=knihy'>Knihy</a>"
    "<a href='/example?kind=gamebook'>Gamebook</a>"
    "<a href='/example?kind=slovnik'>Slovník</a>"
    "<a href='/example?kind=generator'>Generátor</a>"
    "<a href='/example?kind=kviz'>Kvíz</a>"
    "<a href='/example?kind=hry'>Hry</a>"
    "<a href='/example?kind=recepty'>Recepty</a>"
    "</div>"
    "<p class='sub' style='margin:12px 0 0'>Ukázka se otevře jako prostý text – můžeš ji uložit a upravit.</p>"
    "<p style='margin:10px 0 0'><a href='/status'>Stav zařízení</a> · "
    "<a href='/reload'>Obnovit index</a></p></div>";

  html +=
    "<div class='foot'>Pokud se stránka v domácí Wi-Fi nenačte, připoj se přímo k Wi-Fi ePaperu (režim AP).<br>"
    "Některé routery blokují vzájemnou komunikaci zařízení v síti.</div>";

  html +=
    "<script>"
    "var f=document.getElementById('file'),d=document.getElementById('drop'),nm=document.getElementById('fname');"
    "function showFile(){if(f.files&&f.files.length){nm.textContent=f.files[0].name;nm.style.display='block';}}"
    "f.addEventListener('change',showFile);"
    "d.addEventListener('click',function(){f.click();});"
    "['dragenter','dragover'].forEach(function(e){d.addEventListener(e,function(ev){ev.preventDefault();d.classList.add('on');});});"
    "['dragleave','drop'].forEach(function(e){d.addEventListener(e,function(ev){ev.preventDefault();d.classList.remove('on');});});"
    "['dragover','drop'].forEach(function(e){document.addEventListener(e,function(ev){ev.preventDefault();});});"
    "d.addEventListener('drop',function(ev){if(ev.dataTransfer.files.length){f.files=ev.dataTransfer.files;showFile();}});"
    "document.getElementById('upf').addEventListener('submit',function(){"
    "var k=this.kind.value||'knihy';var n=(this.filename.value||'').trim();"
    "this.action='/upload?kind='+encodeURIComponent(k)+'&filename='+encodeURIComponent(n);"
    "var b=document.getElementById('upbtn');b.disabled=true;b.textContent='Nahrávám…';"
    "});"
    "document.getElementById('delf').addEventListener('submit',function(e){"
    "var k=this.kind.value||'knihy';var n=(this.filename.value||'').trim();"
    "this.action='/delete?kind='+encodeURIComponent(k)+'&filename='+encodeURIComponent(n);"
    "if(!confirm('Opravdu smazat '+n+'?')) e.preventDefault();"
    "});"
    "</script>"
    "</div></body></html>";
  webServer.send(200, "text/html; charset=utf-8", html);
}

static void startWebUploadServer() {
  if (webUploadServerReady) return;

  webServer.on("/", HTTP_GET, []() { _sendWebUploadHome(); });
  webServer.on("/status", HTTP_GET, []() {
    time_t now = time(NULL);
    struct tm t;
    bool okTime = getLocalTime(&t, 20);
    if (webServer.hasArg("json")) {
      String js = "{";
      js += "\"sdReady\":" + String(sdReady ? "true" : "false");
      js += ",\"wifiConnected\":" + String((WiFi.status() == WL_CONNECTED) ? "true" : "false");
      js += ",\"staIp\":\"" + WiFi.localIP().toString() + "\"";
      js += ",\"apIp\":\"" + WiFi.softAPIP().toString() + "\"";
      js += ",\"ssid\":\"" + WiFi.SSID() + "\"";
      js += ",\"rssi\":" + String(WiFi.RSSI());
      js += ",\"epoch\":" + String((long)now);
      js += ",\"timeOk\":" + String(okTime ? "true" : "false");
      js += "}";
      webServer.send(200, "application/json", js);
      return;
    }
    bool wifiOk = (WiFi.status() == WL_CONNECTED);
    String html = _webPageStart("Stav zařízení");
    html += "<h1>Stav <span>zařízení</span></h1>";
    html += "<p class='sub'>Přehled bez nutnosti cokoli restartovat</p><div class='chips'>";
    html += "<span class='chip'>SD <b class='" + String(sdReady ? "ok'>OK" : "err'>chyba") + "</b></span>";
    html += "<span class='chip'>Wi-Fi <b>" + String(wifiOk ? "připojeno" : "nepřipojeno") + "</b></span>";
    if (wifiOk) html += "<span class='chip'>Síť <b>" + _htmlEscape(WiFi.SSID()) + "</b></span>";
    html += "<span class='chip'>STA IP <b>" + WiFi.localIP().toString() + "</b></span>";
    html += "<span class='chip'>AP IP <b>" + WiFi.softAPIP().toString() + "</b></span>";
    if (wifiOk) html += "<span class='chip'>Signál <b>" + String(WiFi.RSSI()) + " dBm</b></span>";
    html += "</div><div class='card'><table><tr><th>Položka</th><th>Hodnota</th></tr>";
    html += "<tr><td>Čas</td><td>" + String(okTime ? "synchronizován" : "nesynchronizován") + "</td></tr>";
    if (okTime) {
      char b[32];
      strftime(b, sizeof(b), "%d.%m.%Y %H:%M:%S", &t);
      html += "<tr><td>Datum a čas</td><td>" + String(b) + "</td></tr>";
    }
    html += "</table></div>";
    html += "<p><a href='/'>← Zpět na nahrávání</a> · <a href='/status?json=1'>JSON</a></p>";
    html += "</div></body></html>";
    webServer.send(200, "text/html; charset=utf-8", html);
  });
  webServer.on("/reload", HTTP_GET, []() {
    obnovDynamickyObsahPoUploadu();
    webUploadLastMsg = "Index obsahu obnoven.";
    webServer.sendHeader("Location", "/");
    webServer.send(303, "text/plain", "");
  });
  webServer.on("/list", HTTP_GET, []() {
    String kind = webServer.arg("kind");
    if (kind.length() == 0) kind = "knihy";
    webServer.send(200, "text/html; charset=utf-8", _listFilesHtmlForKind(kind));
  });
  webServer.on("/view", HTTP_GET, []() {
    String kind = webServer.arg("kind");
    if (kind.length() == 0) kind = "knihy";
    String dir = _uploadDirFromKind(kind);
    if (dir.length() == 0) { webServer.send(400, "text/plain; charset=utf-8", "Neplatny typ slozky."); return; }
    String fileName = _basenameOnly(webServer.arg("filename"));
    if (!_isSafeUploadName(fileName) || !_hasExt(fileName, ".txt")) {
      webServer.send(400, "text/plain; charset=utf-8", "Neplatne jmeno souboru.");
      return;
    }
    String path = dir + fileName;
    String txt = nactiCelySoubor(path.c_str());
    if (txt.length() == 0 && !SD.exists(path.c_str())) {
      webServer.send(404, "text/plain; charset=utf-8", "Soubor nenalezen.");
      return;
    }
    String body = _webPageStart("Náhled souboru");
    body += "<h1>" + _htmlEscape(fileName) + "</h1>";
    body += "<p class='sub'>" + _htmlEscape(path) + "</p>";
    body += "<div class='card'><pre>" + _htmlEscape(txt) + "</pre></div>";
    body += "<p><a href='/list?kind=" + kind + "'>← Zpět na výpis</a> · <a href='/'>Domů</a></p>";
    body += "</div></body></html>";
    webServer.send(200, "text/html; charset=utf-8", body);
  });
  webServer.on("/delete-file", HTTP_GET, []() {
    String kind = webServer.arg("kind");
    if (kind.length() == 0) kind = "knihy";
    String dir = _uploadDirFromKind(kind);
    if (dir.length() == 0) { webServer.send(400, "text/plain; charset=utf-8", "Neplatny typ slozky."); return; }
    String fileName = _basenameOnly(webServer.arg("filename"));
    if (!_isSafeUploadName(fileName) || !_extAllowedForKind(kind, fileName) || _isReadmeTxt(fileName.c_str())) {
      webServer.send(400, "text/plain; charset=utf-8", "Neplatny soubor pro smazani.");
      return;
    }
    String path = dir + fileName;
    if (!SD.exists(path.c_str())) { webServer.send(404, "text/plain; charset=utf-8", "Soubor neexistuje."); return; }
    if (!SD.remove(path.c_str())) { webServer.send(500, "text/plain; charset=utf-8", "Mazani selhalo."); return; }
    obnovDynamickyObsahPoUploadu();
    webUploadLastMsg = "OK: smazano " + path;
    webUploadNeedsRefresh = true;
    String body = _webPageStart("Smazáno");
    body += "<h1>Smazáno</h1><p class='sub'>" + _htmlEscape(path) + "</p>";
    body += "<div class='card'><p class='ok'>Soubor byl úspěšně odstraněn.</p>";
    body += "<p style='margin:12px 0 0'><a href='/list?kind=" + kind + "'>← Zpět na výpis</a> · <a href='/'>Domů</a></p></div>";
    body += "</div></body></html>";
    webServer.send(200, "text/html; charset=utf-8", body);
  });
  webServer.on("/delete", HTTP_POST, []() {
    if (!sdReady) {
      webServer.send(500, "text/plain; charset=utf-8", "Chyba: SD neni dostupna.");
      return;
    }
    String kind = webServer.arg("kind");
    if (kind.length() == 0) kind = "knihy";
    String dir = _uploadDirFromKind(kind);
    if (dir.length() == 0) {
      webServer.send(400, "text/plain; charset=utf-8", "Chyba: neplatny typ slozky.");
      return;
    }
    String fileName = _basenameOnly(webServer.arg("filename"));
    if (!_isSafeUploadName(fileName) || !_extAllowedForKind(kind, fileName)) {
      webServer.send(400, "text/plain; charset=utf-8", "Chyba: neplatne jmeno souboru.");
      return;
    }
    if (_isReadmeTxt(fileName.c_str())) {
      webServer.send(403, "text/plain; charset=utf-8", "README soubory nelze mazat pres web.");
      return;
    }
    String path = dir + fileName;
    if (!SD.exists(path.c_str())) {
      webServer.send(404, "text/plain; charset=utf-8", "Soubor neexistuje: " + path);
      return;
    }
    if (!SD.remove(path.c_str())) {
      webServer.send(500, "text/plain; charset=utf-8", "Mazani selhalo: " + path);
      return;
    }
    obnovDynamickyObsahPoUploadu();
    webUploadLastMsg = "OK: smazano " + path;
    webUploadNeedsRefresh = true;
    String html = _webPageStart("Smazáno");
    html += "<h1>Smazáno</h1><p class='sub'>" + _htmlEscape(path) + "</p>";
    html += "<div class='card'><p class='ok'>Soubor byl úspěšně odstraněn.</p>";
    html += "<p style='margin:12px 0 0'><a href='/'>← Zpět na nahrávání</a></p></div>";
    html += "</div></body></html>";
    webServer.send(200, "text/html; charset=utf-8", html);
  });
  webServer.on("/example", HTTP_GET, []() {
    String kind = webServer.arg("kind");
    String path = "/eindata/README_FORMAT.txt";
    if (kind == "knihy") path = "/eindata/knihy/README_FORMAT.txt";
    else if (kind == "gamebook") path = "/eindata/gamebook/README_FORMAT.txt";
    else if (kind == "slovnik") path = "/eindata/slovnik/README_FORMAT.txt";
    else if (kind == "generator") path = "/eindata/generator/README_FORMAT.txt";
    else if (kind == "kviz") path = "/eindata/kviz/README_FORMAT.txt";
    else if (kind == "hry" || kind == "wyr") path = "/eindata/hry/README_FORMAT.txt";
    if (kind == "recepty") {
      String txt =
        "Palačinky\n"
        "Porce: 4\n"
        "250 g hladké mouky\n"
        "1/2 l mléka\n"
        "2 vejce\n"
        "1 lžíce cukru\n"
        "špetka soli\n"
        "olej na smažení\n"
        "\n"
        "POSTUP\n"
        "1. Mouku, mléko, vejce, cukr a sůl vyšlehej v hladké těsto.\n"
        "2. Nech těsto 15 minut odpočinout.\n"
        "3. Na rozpálené pánvi smaž tenké palačinky po obou stranách.\n"
        "\n"
        "Pozn.: první řádek = název, řádek 'Porce: N' = základ pro přepočet,\n"
        "suroviny každá na vlastní řádek (množství na začátku se přepočte),\n"
        "prázdný řádek odděluje suroviny od postupu.";
      webServer.send(200, "text/plain; charset=utf-8", txt);
      return;
    }
    String txt = nactiCelySoubor(path.c_str());
    if (txt.length() == 0) {
      webServer.send(404, "text/plain; charset=utf-8", "README_FORMAT nenalezen: " + path);
      return;
    }
    webServer.send(200, "text/plain; charset=utf-8", txt);
  });

  webServer.on(
    "/upload", HTTP_POST,
    []() {
      String msg = webUploadLastMsg.length() ? webUploadLastMsg : "Upload OK";
      bool ok = msg.startsWith("OK");
      String html = _webPageStart(ok ? "Nahráno" : "Chyba");
      html += "<h1>" + String(ok ? "Nahráno" : "Chyba") + "</h1>";
      html += "<p class='sub'>" + _htmlEscape(msg) + "</p>";
      html += "<div class='card'><p class='" + String(ok ? "ok" : "err") + "'>" +
              String(ok ? "Soubor je uložen v zařízení." : "Soubor se nepodařilo nahrát.") + "</p>";
      html += "<p style='margin:12px 0 0'><a href='/'>← Zpět na nahrávání</a></p></div>";
      html += "</div></body></html>";
      webServer.send(200, "text/html; charset=utf-8", html);
      webUploadNeedsRefresh = true;
    },
    []() {
      static File upFile;
      static String finalPath = "";
      static String tmpPath = "";
      static bool failed = false;

      HTTPUpload& upload = webServer.upload();
      if (upload.status == UPLOAD_FILE_START) {
        webLedAktivitaMs = millis();      // LED bliká během přenosu
        failed = false;
        webUploadLastMsg = "";
        if (!sdReady) {
          failed = true;
          webUploadLastMsg = "Chyba: SD neni dostupna.";
          return;
        }

        String kind = webServer.arg("kind");
        if (kind.length() == 0) kind = "knihy";
        String dir = _uploadDirFromKind(kind);
        if (dir.length() == 0) {
          failed = true;
          webUploadLastMsg = "Chyba: neplatny typ slozky.";
          Serial.println("WEB UPLOAD: invalid kind");
          return;
        }

        String fileName = webServer.arg("filename");
        if (fileName.length() == 0) fileName = upload.filename;
        fileName = _basenameOnly(fileName);
        if (!_isSafeUploadName(fileName) || !_extAllowedForKind(kind, fileName)) {
          failed = true;
          webUploadLastMsg = "Chyba: neplatne jmeno/typ souboru.";
          Serial.println("WEB UPLOAD: invalid filename");
          return;
        }

        // Slozku pripadne vytvorime (napr. /hudba)
        if (!SD.exists(dir.c_str())) {
          String d = dir; while (d.endsWith("/")) d = d.substring(0, d.length() - 1);
          if (!SD.mkdir(d.c_str())) { failed = true; webUploadLastMsg = "Chyba: nelze vytvorit slozku " + d; return; }
        }

        finalPath = dir + fileName;
        tmpPath = finalPath + ".tmp";
        Serial.println("WEB UPLOAD START -> " + finalPath);
        if (SD.exists(tmpPath.c_str())) SD.remove(tmpPath.c_str());
        upFile = SD.open(tmpPath.c_str(), FILE_WRITE);
        if (!upFile) {
          failed = true;
          webUploadLastMsg = "Chyba: nelze otevrit docasny soubor.";
          Serial.println("WEB UPLOAD: cannot open tmp");
          return;
        }
      } else if (upload.status == UPLOAD_FILE_WRITE) {
        webLedAktivitaMs = millis();
        if (!failed && upFile) {
          size_t wr = upFile.write(upload.buf, upload.currentSize);
          if (wr != upload.currentSize) {
            failed = true;
            webUploadLastMsg = "Chyba: zapis na SD selhal.";
            upFile.close();
          }
        }
      } else if (upload.status == UPLOAD_FILE_END) {
        if (!failed && upFile) {
          upFile.close();
          if (SD.exists(finalPath.c_str())) SD.remove(finalPath.c_str());
          if (!SD.rename(tmpPath.c_str(), finalPath.c_str())) {
            failed = true;
            webUploadLastMsg = "Chyba: nelze prejmenovat temp soubor.";
            Serial.println("WEB UPLOAD: rename failed");
          } else {
            obnovDynamickyObsahPoUploadu();
            webUploadLastMsg = "OK: nahrano do " + finalPath;
            Serial.println("WEB UPLOAD OK -> " + finalPath);
          }
        }
      } else if (upload.status == UPLOAD_FILE_ABORTED) {
        failed = true;
        if (upFile) upFile.close();
        if (tmpPath.length() && SD.exists(tmpPath.c_str())) SD.remove(tmpPath.c_str());
        webUploadLastMsg = "Upload prerusen.";
      }
    }
  );

  webServer.begin();
  webUploadServerReady = true;

  // mDNS — aby šlo v prohlížeči zadat honzueink.local místo IP adresy
  MDNS.end();
  if (MDNS.begin("honzueink")) {
    MDNS.addService("http", "tcp", 80);
    Serial.println("mDNS: http://honzueink.local/");
  }
}

static void stopWebUploadServer() {
  if (!webUploadServerReady) return;
  webServer.stop();
  MDNS.end();
  webUploadServerReady = false;
}

static void zobrazWebUpload() {
  bool staOk = (WiFi.status() == WL_CONNECTED) && !webUploadApMode;
  String mode = staOk ? "STA" : "AP";
  // Živá IP z driveru — cache webUploadStaIp může po obnově DHCP zastarát
  String ip = staOk ? WiFi.localIP().toString() : webUploadApIp;
  if (ip.length() == 0 || ip == "0.0.0.0") ip = WiFi.softAPIP().toString();
  String line1 = "Web upload (" + mode + ")";
  String line2 = "http://" + ip + "/";
  String ssid = webUploadSsid;
  if (ssid.length() > 24) ssid = ssid.substring(0, 24) + "…";
  String wifiLine = staOk ? ("WiFi: " + ssid + "  " + String(WiFi.RSSI()) + " dBm")
                          : String("WiFi: -- (jen AP)");
  beginUiFrame(true);
  do {
    display.fillScreen(bgColor());
    u8g2Fonts.setFontMode(1); u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
    printCentered(line1, 24, getTitleFont());
    printCentered(line2, 44, getSmallBoldFont());
    printCentered(wifiLine, 58, getSmallFont());
    printCentered("nebo honzueink.local", 72, getSmallFont());
    printCentered("AP zaloha: 192.168.4.1", 86, getSmallFont());
    printCentered("BTN21=STA/AP | BTN0=obnovit", 104, getSmallFont());
    printCentered("Drz BTN21 = konec", 118, getSmallFont());
  } while (display.nextPage());
}

static void startWebUploadApOnly() {
  WiFi.disconnect(true, true);
  delay(120);
  WiFi.mode(WIFI_AP);
  delay(120);
  bool apOk = WiFi.softAP(uploadApSsid, uploadApPass);
  webUploadApMode = apOk;
  webUploadApIp = WiFi.softAPIP().toString();
  webUploadStaIp = "";
  webUploadSsid = "";
  if (!apOk) webUploadLastMsg = "AP mode se nepodarilo spustit.";
  else webUploadLastMsg = "AP aktivni: " + webUploadApIp;
}

static bool trySwitchWebUploadToSta() {
  if (webUploadApMode) {
    WiFi.softAPdisconnect(true);
    webUploadApMode = false;
    delay(120);
  }
  // Čistý start: pripojWiFi() vrací true hned, když už je připojené.
  // Bez odpojení by zařízení zůstalo na předchozí síti (typicky domácí Wi-Fi).
  WiFi.disconnect(false, true);
  delay(150);

  bool ok = pripojWiFi();
  bool staOk = ok && (WiFi.status() == WL_CONNECTED);
  if (staOk) {
    webUploadStaIp = WiFi.localIP().toString();
    webUploadSsid = WiFi.SSID();
  } else {
    webUploadStaIp = "";
    webUploadSsid = "";
  }

  // Záložní AP běží i při úspěšném STA — upload jde vždy (192.168.4.1).
  WiFi.mode(WIFI_AP_STA);
  delay(80);
  bool apOk = WiFi.softAP(uploadApSsid, uploadApPass);
  if (apOk) webUploadApIp = WiFi.softAPIP().toString();

  if (staOk) {
    webUploadApMode = false;
    webUploadLastMsg = "STA: " + webUploadSsid + " (" + webUploadStaIp + ")";
    return true;
  }
  webUploadApMode = apOk;
  webUploadLastMsg = apOk ? ("STA selhalo, AP: " + webUploadApIp)
                          : String("AP se nepodarilo spustit.");
  return false;
}

static void startWebUploadMode() {
  webUploadMode = true; // behem prepnuti WiFi nic nestahovat
  // Prednostne sdilena domaci WiFi (STA); pri neuspechu se samo prepne do AP.
  WiFi.setSleep(false);   // bez uspavani radia — web pak odpovida rychleji
  trySwitchWebUploadToSta();
  startWebUploadServer();
  appState = STATE_WEB_UPLOAD;
  zobrazWebUpload();
}

static void stopWebUploadMode() {
  stopWebUploadServer();
  webUploadMode = false;
  WiFi.softAPdisconnect(true);
  if (WiFi.status() == WL_CONNECTED) WiFi.mode(WIFI_STA);
  else WiFi.mode(WIFI_OFF);
  webUploadApMode = false;
  webUploadStaIp = "";
  webUploadApIp = "";
  webUploadSsid = "";
}

// ===== KNIHY — NAČTENÍ S FALLBACKEM NA SD =====
// Obsah knihy se načítá v zobrazKnihu() rovnou do PSRAM cache (viz "DLOUHÉ TEXTY").
// Formát souboru: první řádek = název, zbytek = text.

// Načte seznam knih ze SD karty (název = první řádek souboru).
void nactiSeznamKnih() {
  dynamicKnihovnaCount = nactiSeznamKnihSD(dynamicKnihovnaItems, dynamicKnihovnaFiles, 15);
  if (dynamicKnihovnaCount == 0) {
    // Fallback: statické názvy z PROGMEM
    dynamicKnihovnaItems[0] = "Zaklínač: Brokilonský les";
    dynamicKnihovnaFiles[0] = "/eindata/knihy/kniha_1.txt";
    dynamicKnihovnaItems[1] = "Zaklínač: Cesta do Oxenfurtu";
    dynamicKnihovnaFiles[1] = "/eindata/knihy/kniha_2.txt";
    dynamicKnihovnaItems[2] = "Zaklínač: Kaer Morhen";
    dynamicKnihovnaFiles[2] = "/eindata/knihy/kniha_3.txt";
    dynamicKnihovnaCount = 3;
  }
}

// Interní pomocná funkce — zajistí existenci složky, vrátí false při chybě
static bool _zajistiSlozku(const char* cesta) {
  if (SD.exists(cesta)) return true;
  if (!SD.mkdir(cesta)) {
    Serial.print("SD: chyba mkdir "); Serial.println(cesta);
    return false;
  }
  return true;
}

// Zapíše textový soubor na SD (přepíše existující).
static bool zapisTextNaSD(const String& cesta, const String& data) {
  if (!sdReady) return false;
  if (SD.exists(cesta.c_str())) SD.remove(cesta.c_str());
  File f = SD.open(cesta.c_str(), FILE_WRITE);
  if (!f) return false;
  f.print(data);
  f.close();
  return true;
}

// ===== ARCHIVACE A UKLÁDÁNÍ ZPRÁV NA SD KARTU =====
// Uloží stažená data zpráv do aktuální složky na SD:
// /eindata/zpravy/aktualni/{soubor}
void ulozAktualniZpravuNaSD(const char* soubor, const String& data) {
  if (!sdReady) return;
  if (!_zajistiSlozku("/eindata/zpravy") || !_zajistiSlozku("/eindata/zpravy/aktualni")) return;
  String cesta = String("/eindata/zpravy/aktualni/") + soubor;
  if (SD.exists(cesta.c_str())) SD.remove(cesta.c_str());
  File f = SD.open(cesta.c_str(), FILE_WRITE);
  if (f) { f.print(data); f.close(); }
  else { Serial.println("SD aktualni: nelze zapsat " + cesta); }
}

// Pomocná funkce — spočítá djb2 hash ze Stringu
uint32_t djb2Hash(const String& s) {
  uint32_t h = 5381;
  for (int i = 0; i < (int)s.length(); i++) h = ((h << 5) + h) + s[i];
  return h;
}

// Načte hashe titulků z jednoho archivního souboru na SD.
// Čte po blocích 512 B — starý znak-po-znaku zápis byl na velkých
// archivních souborech (100+ článků) zbytečně pomalý.
int nactiHasheTitulku(const char* cesta, uint32_t* pole, int maxPole) {
  int count = 0;
  if (!SD.exists(cesta)) return 0;
  File f = SD.open(cesta);
  if (!f) return 0;
  String buf = "";
  uint8_t tmp[512];
  while (f.available() && count < maxPole) {
    int n = f.read(tmp, sizeof(tmp));
    if (n <= 0) break;
    buf.concat((const char*)tmp, n);
    while (count < maxPole) {
      int tS = buf.indexOf("|T|");
      if (tS < 0) { if (buf.length() > 8) buf = buf.substring(buf.length() - 3); break; }
      int pS = buf.indexOf("|P|", tS);
      if (pS < 0) { // |P| ještě nedorazilo — zachováme od |T|, až se načte zbytek
        buf = buf.substring(tS);
        if (buf.length() > 500) buf = ""; // jinak je blok poškozený
        break;
      }
      int dS = buf.indexOf("|D|", tS + 3);
      int konec = (dS >= 0 && dS < pS) ? dS : pS;
      String tit = buf.substring(tS + 3, konec);
      tit.trim();
      pole[count++] = djb2Hash(tit);
      int eP = buf.indexOf("|E|", konec);
      if (eP < 0) { buf = buf.substring(tS); break; }
      buf = buf.substring(eP + 3);
    }
  }
  f.close();
  return count;
}

// Načte hashe ze VŠECH archivních denních složek (pro deduplikaci přes celý archiv)
int nactiVsechnyArchivniHashe(uint32_t* pole, int maxPole) {
  int count = 0;
  File archDir = SD.open("/eindata/zpravy/archiv");
  if (!archDir || !archDir.isDirectory()) { if (archDir) archDir.close(); return 0; }
  File entry = archDir.openNextFile();
  while (entry && count < maxPole) {
    if (entry.isDirectory()) {
      const char* fname = entry.name();
      const char* lastSlash = strrchr(fname, '/');
      if (lastSlash) fname = lastSlash + 1;
      const char* soubory[] = { "zpravy_svet.txt", "zpravy_cr.txt", "zpravy_tech.txt", "zpravy_pozitivni.txt" };
      int pocetSouboru = sizeof(soubory) / sizeof(soubory[0]);
      for (int s = 0; s < pocetSouboru && count < maxPole; s++) {
        String cesta = String("/eindata/zpravy/archiv/") + fname + "/" + soubory[s];
        count += nactiHasheTitulku(cesta.c_str(), pole + count, maxPole - count);
      }
    }
    entry.close();
    entry = archDir.openNextFile();
  }
  archDir.close();
  return count;
}

struct ArchivClanek {
  String blok;
  String titulek;
  uint32_t hash;
  long sortKey;
};

// Velké pomocné buffery mimo stack (ESP32 task stack je omezený).
// 4000 = pokryje 7 dní × 4 rubriky × až 100 článků/den (serverový archiv).
static const int MAX_HASH_GLOBAL = 4000;
static const int MAX_MERGE_CLANKU_GLOBAL = 260;
static uint32_t g_existHash[MAX_HASH_GLOBAL];

static long _archivSortKeyZDneCasu(int y, int m, int d, int hh, int mm) {
  return (long)y * 100000000L + (long)m * 1000000L + (long)d * 10000L + (long)hh * 100L + (long)mm;
}

static bool _jeCislice(char c) { return c >= '0' && c <= '9'; }

// Z textu data (|D|) vytáhne den + čas a vrátí složku YYYY-MM-DD.
// Podporuje např.: "2026-04-03 12:34", "3.4.2026 12:34", "3.4. 12:34", "3.4."
static bool _normalizujDatumZClanku(const String& datumRaw, String& outDatumSlozka, long& outSortKey) {
  if (datumRaw.length() == 0) return false;
  String d = datumRaw;
  d.trim();
  d.replace(",", " ");
  d.replace("T", " ");

  int y = -1, m = -1, day = -1, hh = 0, mm = 0;

  // 1) Preferovaný formát YYYY-MM-DD [HH:MM]
  for (int i = 0; i + 9 < (int)d.length(); i++) {
    if (_jeCislice(d[i]) && _jeCislice(d[i + 1]) && _jeCislice(d[i + 2]) && _jeCislice(d[i + 3]) &&
        d[i + 4] == '-' &&
        _jeCislice(d[i + 5]) && _jeCislice(d[i + 6]) &&
        d[i + 7] == '-' &&
        _jeCislice(d[i + 8]) && _jeCislice(d[i + 9])) {
      y = d.substring(i, i + 4).toInt();
      m = d.substring(i + 5, i + 7).toInt();
      day = d.substring(i + 8, i + 10).toInt();
      break;
    }
  }

  // 2) České datum D.M.[YYYY]
  if (y < 0 || m < 0 || day < 0) {
    int p1 = d.indexOf('.');
    if (p1 > 0) {
      int p2 = d.indexOf('.', p1 + 1);
      if (p2 > p1) {
        day = d.substring(0, p1).toInt();
        m = d.substring(p1 + 1, p2).toInt();

        int i = p2 + 1;
        while (i < (int)d.length() && d[i] == ' ') i++;
        if (i + 3 < (int)d.length() && _jeCislice(d[i]) && _jeCislice(d[i + 1]) && _jeCislice(d[i + 2]) && _jeCislice(d[i + 3])) {
          y = d.substring(i, i + 4).toInt();
        } else {
          struct tm nowTm;
          if (!getLocalTime(&nowTm, 120)) return false;
          y = nowTm.tm_year + 1900;
        }
      }
    }
  }

  // Čas HH:MM (pokud je)
  for (int i = 0; i + 4 < (int)d.length(); i++) {
    if (_jeCislice(d[i]) && _jeCislice(d[i + 1]) && d[i + 2] == ':' && _jeCislice(d[i + 3]) && _jeCislice(d[i + 4])) {
      hh = d.substring(i, i + 2).toInt();
      mm = d.substring(i + 3, i + 5).toInt();
      break;
    }
  }

  if (y < 2000 || y > 2100 || m < 1 || m > 12 || day < 1 || day > 31 || hh < 0 || hh > 23 || mm < 0 || mm > 59) {
    return false;
  }

  char dbuf[12];
  sprintf(dbuf, "%04d-%02d-%02d", y, m, day);
  outDatumSlozka = String(dbuf);
  outSortKey = _archivSortKeyZDneCasu(y, m, day, hh, mm);
  return true;
}

static long _sortKeyZBloku(const String& blok) {
  int dS = blok.indexOf("|D|");
  int pS = blok.indexOf("|P|", dS >= 0 ? dS + 3 : 0);
  if (dS < 0 || pS < 0 || pS <= dS + 3) return 0;
  String datumRaw = blok.substring(dS + 3, pS);
  String slozka = "";
  long key = 0;
  if (_normalizujDatumZClanku(datumRaw, slozka, key)) return key;
  return 0;
}

static String _datumSlozkaZBlokuNeboDnes(const String& blok) {
  int dS = blok.indexOf("|D|");
  int pS = blok.indexOf("|P|", dS >= 0 ? dS + 3 : 0);
  if (dS >= 0 && pS > dS + 3) {
    String datumRaw = blok.substring(dS + 3, pS);
    String slozka = "";
    long key = 0;
    if (_normalizujDatumZClanku(datumRaw, slozka, key)) return slozka;
  }
  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 120)) {
    char datumBuf[12];
    sprintf(datumBuf, "%04d-%02d-%02d", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
    return String(datumBuf);
  }
  return "1970-01-01";
}

static bool _najdiClanekVPoli(ArchivClanek* arr, int count, uint32_t h) {
  for (int i = 0; i < count; i++) if (arr[i].hash == h) return true;
  return false;
}

static void _seradClanekPoleDesc(ArchivClanek* arr, int count) {
  for (int i = 0; i < count - 1; i++) {
    int best = i;
    for (int j = i + 1; j < count; j++) {
      if (arr[j].sortKey > arr[best].sortKey) best = j;
    }
    if (best != i) {
      ArchivClanek t = arr[i];
      arr[i] = arr[best];
      arr[best] = t;
    }
  }
}

static int _nactiClanekBlokyZeSouboru(const char* cesta, ArchivClanek* out, int maxOut) {
  if (!SD.exists(cesta)) return 0;
  File f = SD.open(cesta, FILE_READ);
  if (!f) return 0;
  String data = f.readString();
  f.close();

  int count = 0;
  int pos = 0;
  while (pos < (int)data.length() && count < maxOut) {
    int tS = data.indexOf("|T|", pos);
    if (tS < 0) break;
    int eE = data.indexOf("|E|", tS);
    if (eE < 0) break;
    eE += 3;
    String blok = data.substring(tS, eE);

    int dS = blok.indexOf("|D|");
    int pS = blok.indexOf("|P|", dS >= 0 ? dS + 3 : 0);
    int konecTit = (dS >= 0 && pS > dS) ? dS : pS;
    if (pS > 0 && konecTit > 3) {
      String tit = blok.substring(3, konecTit);
      tit.trim();
      out[count].blok = blok;
      out[count].titulek = tit;
      out[count].hash = djb2Hash(tit);
      out[count].sortKey = _sortKeyZBloku(blok);
      count++;
    }
    pos = eE;
  }
  return count;
}

static bool _zapisSerazeneClanky(const String& cesta, ArchivClanek* arr, int count) {
  if (SD.exists(cesta.c_str())) SD.remove(cesta.c_str());
  File f = SD.open(cesta.c_str(), FILE_WRITE);
  if (!f) return false;
  for (int i = 0; i < count; i++) {
    f.print(arr[i].blok);
    f.print("\n");
  }
  f.close();
  return true;
}

static void _pridejClanekDoDneSouboru(const String& den, const char* soubor, const ArchivClanek& cl) {
  String slozka = String("/eindata/zpravy/archiv/") + den;
  if (!_zajistiSlozku(slozka.c_str())) return;
  String cesta = slozka + "/" + soubor;

  // Safe-mode: jen append (deduplikace uz probehla pred volanim).
  File f = SD.open(cesta.c_str(), FILE_APPEND);
  if (!f) f = SD.open(cesta.c_str(), FILE_WRITE);
  if (!f) return;
  f.print(cl.blok);
  f.print("\n");
  f.close();
}

// Sloučí bloky z textu do archivu — deduplikuje podle již načtené hash tabulky
// a nové články přidává do správné denní složky. Vrací počet přidaných článků.
static int sloucArchivniData(const char* soubor, const String& novaData,
                              uint32_t* hashPole, int& hashCount) {
  int pridanoCelkem = 0;
  int pos = 0;
  while (pos < (int)novaData.length()) {
    int tS = novaData.indexOf("|T|", pos);
    if (tS < 0) break;
    int eE = novaData.indexOf("|E|", tS);
    if (eE < 0) break;
    eE += 3;
    String blok = novaData.substring(tS, eE);

    int dS = blok.indexOf("|D|");
    int pS = blok.indexOf("|P|", dS >= 0 ? dS + 3 : 0);
    int konecTit = (dS >= 0 && pS > dS) ? dS : pS;
    if (pS <= 0 || konecTit <= 3) { pos = eE; continue; }

    String tit = blok.substring(3, konecTit);
    tit.trim();
    uint32_t h = djb2Hash(tit);
    bool dup = false;
    for (int i = 0; i < hashCount; i++) { if (hashPole[i] == h) { dup = true; break; } }
    if (dup) { pos = eE; continue; }

    String cilDatum = _datumSlozkaZBlokuNeboDnes(blok);
    ArchivClanek cl;
    cl.blok = blok;
    cl.titulek = tit;
    cl.hash = h;
    cl.sortKey = _sortKeyZBloku(blok);
    _pridejClanekDoDneSouboru(cilDatum, soubor, cl);
    if (hashCount < MAX_HASH_GLOBAL) hashPole[hashCount++] = h;
    pridanoCelkem++;
    pos = eE;
  }
  return pridanoCelkem;
}

// Archivuje zprávy do denní složky — připojí jen zprávy s novým titulkem (deduplikace)
void archivujDeduplikovane(const char* soubor, const String& novaData) {
  if (!sdReady || novaData.length() < 10) return;
  if (!_zajistiSlozku("/eindata/zpravy") || !_zajistiSlozku("/eindata/zpravy/archiv")) return;

  int hashCount = nactiVsechnyArchivniHashe(g_existHash, MAX_HASH_GLOBAL);
  int pridanoCelkem = sloucArchivniData(soubor, novaData, g_existHash, hashCount);

  Serial.println("Archiv " + String(soubor) + ": +" + String(pridanoCelkem) + " novych (podle data clanku)");
}

// Projde archiv a přesune články do správných složek podle |D| + seřadí je od nejnovějších.
void opravArchivDataceASerazeni() {
  // Safe-mode: migrace celeho archivu je docasne vypnuta kvuli stabilite.
  Serial.println("Archiv: auto-oprava vypnuta (safe mode)");
}

// Rekurzivně smaže obsah složky a složku samotnou
void smazSlozku(const char* cesta) {
  File dir = SD.open(cesta);
  if (!dir || !dir.isDirectory()) { if (dir) dir.close(); return; }
  File entry = dir.openNextFile();
  while (entry) {
    const char* fname = entry.name();
    const char* lastSlash = strrchr(fname, '/');
    if (lastSlash) fname = lastSlash + 1;
    String fullPath = String(cesta) + "/" + fname;
    if (entry.isDirectory()) {
      entry.close();
      smazSlozku(fullPath.c_str());
    } else {
      entry.close();
      SD.remove(fullPath.c_str());
    }
    entry = dir.openNextFile();
  }
  dir.close();
  SD.rmdir(cesta);
}

// Smaže archivní složky starší než 7 dní
void promazArchivStarsi7Dni() {
  if (!sdReady) return;
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 150)) return;
  time_t ted = mktime(&timeinfo);
  File archDir = SD.open("/eindata/zpravy/archiv");
  if (!archDir || !archDir.isDirectory()) { if (archDir) archDir.close(); return; }
  String keSmazani[30];
  int smazCount = 0;
  File entry = archDir.openNextFile();
  while (entry && smazCount < 30) {
    if (entry.isDirectory()) {
      const char* fname = entry.name();
      const char* lastSlash = strrchr(fname, '/');
      if (lastSlash) fname = lastSlash + 1;
      if (strlen(fname) == 10 && fname[4] == '-' && fname[7] == '-') {
        struct tm archTm = {};
        archTm.tm_year = atoi(fname) - 1900;
        archTm.tm_mon  = atoi(fname + 5) - 1;
        archTm.tm_mday = atoi(fname + 8);
        archTm.tm_hour = 12;
        time_t archTime = mktime(&archTm);
        double rozdilDni = difftime(ted, archTime) / 86400.0;
        if (rozdilDni > 7.0) {
          keSmazani[smazCount++] = String("/eindata/zpravy/archiv/") + fname;
        }
      }
    }
    entry.close();
    entry = archDir.openNextFile();
  }
  archDir.close();
  for (int i = 0; i < smazCount; i++) {
    Serial.println("Mazu stary archiv: " + keSmazani[i]);
    smazSlozku(keSmazani[i].c_str());
  }
}

// ===== DEEP SLEEP FUNKCE =====
void jdiSpat() {
  // Uložit celý navigační stav do RTC paměti (přežije deep sleep)
  rtc_bylDeepSleep    = true;
  rtc_appState        = (int)appState;
  rtc_menuIndex       = menuIndex;
  rtc_scrollOffset    = scrollOffset;
  rtc_subMenuIndex    = subMenuIndex;
  rtc_subScrollOffset = subScrollOffset;
  rtc_clanekMenuIndex = clanekMenuIndex;
  rtc_textScrollPage  = textScrollPage;
  rtc_pocasiDen       = pocasiDen;
  rtc_horoskopIdx     = horoskopMenuIndex;
  rtc_grafIdx         = grafMenuIndex;

  struct tm timeinfo;
  String datumStr = "Dobrou noc";
  String svatekDnes = "";
  
  if (getLocalTime(&timeinfo, 150)) {
    const char* dnyVTydnu[] = {"Neděle", "Pondělí", "Úterý", "Středa", "Čtvrtek", "Pátek", "Sobota"};
    char dBuf[50]; 
    sprintf(dBuf, "%s %d. %d.", dnyVTydnu[timeinfo.tm_wday], timeinfo.tm_mday, timeinfo.tm_mon + 1);
    datumStr = String(dBuf);
    String sv = getSvatek(timeinfo.tm_mday, timeinfo.tm_mon + 1);
    if (sv.length() > 0) svatekDnes = "Svátek má: " + sv;
  }

  // Náhodná kategorie textu: 0=Vtip, 1=Žalm, 2=Citát, 3=Fakt (SD má přednost před PROGMEM)
  char katBuf[1200];
  const char* katNazev;
  { String _s;
    switch (random(4)) {
      case 0: _s = _nahodnyTextGeneratoru(_generatorPathForCategory("/eindata/generator/vtipy.txt", "vtip").c_str(), vtipy, vtipyPocet); katNazev = "Vtip"; break;
      case 1: _s = _nahodnyTextGeneratoru(_generatorPathForCategory("/eindata/generator/zalmy.txt", "zalm").c_str(), zalmy, zalmyPocet); katNazev = "Žalm"; break;
      case 2: _s = _nahodnyTextGeneratoru(_generatorPathForCategory("/eindata/generator/citaty.txt", "citat").c_str(), citaty, citatyPocet); katNazev = "Citát"; break;
      default: _s = _nahodnyTextGeneratoru(_generatorPathForCategory("/eindata/generator/fakty.txt", "fakt").c_str(), fakty, faktyPocet); katNazev = "Fakt"; break;
    }
    strncpy(katBuf, _s.c_str(), sizeof(katBuf)-1);
  }
  katBuf[sizeof(katBuf)-1] = '\0';

  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(bgColor());
    u8g2Fonts.setFontMode(1); u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
    
    // Datum (velké, vycentrované)
    u8g2Fonts.setFont(getBigFont());
    int tw = u8g2Fonts.getUTF8Width(datumStr.c_str());
    u8g2Fonts.setCursor((display.width() - tw) / 2, 22); 
    u8g2Fonts.print(datumStr.c_str());
    
    // Svátek (malé, vycentrované)
    if (svatekDnes.length() > 0) {
      u8g2Fonts.setFont(getBodyFont());
      tw = u8g2Fonts.getUTF8Width(svatekDnes.c_str());
      u8g2Fonts.setCursor((display.width() - tw) / 2, 38);
      u8g2Fonts.print(svatekDnes.c_str());
    }

    display.drawFastHLine(10, 46, display.width() - 20, fgColor());

    // Kategorie (label) a obsah textu generátoru (5 řádků, getBodyFont)
    u8g2Fonts.setFont(getSmallFont());
    tw = u8g2Fonts.getUTF8Width(katNazev);
    u8g2Fonts.setCursor(display.width() - tw - 4, 58);
    u8g2Fonts.print(katNazev);

    String rTxt = String(katBuf);
    TextLine lines[5];
    int lCount = zalamejText(rTxt.c_str(), rTxt.length(), lines, 5, display.width() - 10);
    u8g2Fonts.setFont(getBodyFont());
    for (int i = 0; i < lCount; i++) {
      char lBuf[120]; int llen = lines[i].len; if (llen > 119) llen = 119;
      strncpy(lBuf, &rTxt.c_str()[lines[i].start], llen); lBuf[llen] = '\0';
      int lw = u8g2Fonts.getUTF8Width(lBuf);
      u8g2Fonts.setCursor((display.width() - lw) / 2, 62 + (i * 14));
      u8g2Fonts.print(lBuf);
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
    u8g2Fonts.setFont(getSmallFont()); napoveda(5, 125, "Drž BTN21 = zpět");
  } while (display.nextPage());
}

// ===== ZOBRAZOVÁNÍ KVÍZU A WYR =====
void nastaviNahodnyKvizIdx() {
  obnovAktivniDatoveSoubory();
  int klicKat = (kvizKatIdx == 6) ? 6 : kvizKatIdx;
  int klicObt = (kvizKatIdx == 6) ? 0 : kvizObtIdx;

  // Zkusíme načíst ze SD. Dva průchody: nejdřív napočítat odpovídající otázky,
  // pak vzít tu, která je na řadě v náhodném pořadí (bez opakování).
  if (sdReady) {
    File f = SD.open(aktivniKvizSoubor.c_str());
    if (f) {
      int pocet = 0;
      while (f.available()) {
        String r = f.readStringUntil('\n'); r.trim();
        if (r.length() == 0) continue;
        int p1 = r.indexOf('|'); int p2 = r.indexOf('|', p1 + 1); int p3 = r.indexOf('|', p2 + 1);
        if (p1 < 0 || p2 < 0 || p3 < 0) continue;
        int kat = r.substring(p2 + 1, p3).toInt(); int obt = r.substring(p3 + 1).toInt();
        if (kvizKatIdx == 6 || (kat == kvizKatIdx && obt == kvizObtiznosti[kvizObtIdx])) pocet++;
      }
      f.close();
      if (pocet > 0) {
        int want = nahodnyIndex(nahodnyKlic(aktivniKvizSoubor.c_str(), 1000 + klicKat * 10 + klicObt), pocet);
        f = SD.open(aktivniKvizSoubor.c_str());
        if (f) {
          int seen = 0; String vybrany = "";
          while (f.available()) {
            String r = f.readStringUntil('\n'); r.trim();
            if (r.length() == 0) continue;
            int p1 = r.indexOf('|'); int p2 = r.indexOf('|', p1 + 1); int p3 = r.indexOf('|', p2 + 1);
            if (p1 < 0 || p2 < 0 || p3 < 0) continue;
            int kat = r.substring(p2 + 1, p3).toInt(); int obt = r.substring(p3 + 1).toInt();
            if (!(kvizKatIdx == 6 || (kat == kvizKatIdx && obt == kvizObtiznosti[kvizObtIdx]))) continue;
            if (seen == want) { vybrany = r; break; }
            seen++;
          }
          f.close();
          if (vybrany.length() > 0) {
            int p1 = vybrany.indexOf('|'); int p2 = vybrany.indexOf('|', p1 + 1); int p3 = vybrany.indexOf('|', p2 + 1);
            int kat = vybrany.substring(p2 + 1, p3).toInt(); int obt = vybrany.substring(p3 + 1).toInt();
            sdKvizOtazka = vybrany.substring(0, p1);
            sdKvizOdpoved = vybrany.substring(p1 + 1, p2);
            sdKvizKatLabel = String(kvizKategorieNazvy[kat < kvizKategoriiCount - 1 ? kat : 0]) + " / " + String(obt);
            kvizZSD = true; return;
          }
        }
      }
    }
  }
  // Fallback: PROGMEM pole
  kvizZSD = false;
  uint32_t klic = nahodnyKlic("kviz-pole", 2000 + klicKat * 10 + klicObt);
  if (kvizKatIdx == 6) {
    aktualniHraIdx = nahodnyIndex(klic, kvizPocet);
    return;
  }
  int targetDiff = kvizObtiznosti[kvizObtIdx];
  int count = 0;
  for (int i = 0; i < kvizPocet; i++)
    if (kvizKategoriePole[i] == kvizKatIdx && kvizObtiznostPole[i] == targetDiff) count++;
  if (count == 0) { aktualniHraIdx = nahodnyIndex(klic, kvizPocet); return; }
  int want = nahodnyIndex(klic, count);
  int seen = 0;
  for (int i = 0; i < kvizPocet; i++) {
    if (kvizKategoriePole[i] == kvizKatIdx && kvizObtiznostPole[i] == targetDiff) {
      if (seen == want) { aktualniHraIdx = i; return; }
      seen++;
    }
  }
  aktualniHraIdx = nahodnyIndex(klic, kvizPocet);
}

void zobrazKviz() {
  String ot = kvizZSD ? sdKvizOtazka : String(kvizOtazky[aktualniHraIdx]);
  String katLabel = kvizZSD ? sdKvizKatLabel : (String(kvizKategorieNazvy[kvizKategoriePole[aktualniHraIdx]]) + " / " + String(kvizObtiznostPole[aktualniHraIdx]));
  display.setPartialWindow(0, 0, display.width(), display.height());
  display.firstPage();
  do {
    display.fillScreen(bgColor());
    u8g2Fonts.setFontMode(1); u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
    u8g2Fonts.setFont(getTitleFont()); u8g2Fonts.setCursor(5, 16); u8g2Fonts.print("KVÍZ");
    u8g2Fonts.setFont(getSmallFont()); u8g2Fonts.setCursor(60, 16); u8g2Fonts.print(katLabel.c_str());
    display.drawFastHLine(0, 20, display.width(), fgColor());
    u8g2Fonts.setFont(getBodyFont());
    TextLine lines[5]; int lCount = zalamejText(ot.c_str(), ot.length(), lines, 5, display.width() - 10);
    int y = 40;
    for(int i=0; i<lCount; i++){
      char lBuf[80]; int len = lines[i].len; if(len>79) len=79;
      strncpy(lBuf, &ot.c_str()[lines[i].start], len); lBuf[len]='\0';
      u8g2Fonts.setCursor(5, y); u8g2Fonts.print(lBuf); y+=getLineHeight();
    }
    u8g2Fonts.setFont(getSmallFont()); napoveda(5, 125, "BTN21=Odpověď | BTN0=Další | Drž=zpět");
  } while (display.nextPage());
}

void zobrazKvizOdpoved() {
  String odp = kvizZSD ? sdKvizOdpoved : String(kvizOdpovedi[aktualniHraIdx]);
  display.setPartialWindow(0, 0, display.width(), display.height());
  display.firstPage();
  do {
    display.fillScreen(bgColor());
    u8g2Fonts.setFontMode(1); u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
    u8g2Fonts.setFont(getTitleFont()); u8g2Fonts.setCursor(5, 16); u8g2Fonts.print("SPRÁVNÁ ODPOVĚĎ:");
    display.drawFastHLine(0, 20, display.width(), fgColor());
    u8g2Fonts.setFont(getBigFont()); 
    int tw = u8g2Fonts.getUTF8Width(odp.c_str());
    if (tw > display.width() - 10) {
      // Příliš dlouhá odpověď — použij menší font
      u8g2Fonts.setFont(getTitleFont());
      u8g2Fonts.setCursor(5, 65); u8g2Fonts.print(odp.c_str());
    } else {
      u8g2Fonts.setCursor((display.width() - tw) / 2, 70); u8g2Fonts.print(odp.c_str());
    }
    u8g2Fonts.setFont(getSmallFont()); napoveda(5, 125, "BTN0 / BTN21 = Další otázka | Drž = zpět");
  } while (display.nextPage());
}

void nastaviNahodnyWyrZSD() {
  obnovAktivniDatoveSoubory();
  if (sdReady) {
    int pocet = spocitejRadkySD(aktivniWyrSoubor.c_str());
    if (pocet > 0) {
      int idx = nahodnyIndex(nahodnyKlic(aktivniWyrSoubor.c_str(), 3000), pocet);
      String vybrany = nactiRadekPodleIndexu(aktivniWyrSoubor.c_str(), idx);
      if (vybrany.length() > 0) {
        int p1 = vybrany.indexOf('|'); int p2 = vybrany.indexOf('|', p1 + 1); int p3 = vybrany.indexOf('|', p2 + 1);
        if (p1 >= 0 && p2 >= 0 && p3 >= 0) {
          sdWyrOtazka = vybrany.substring(0, p1);
          sdWyrMozA = vybrany.substring(p1 + 1, p2);
          sdWyrMozB = vybrany.substring(p2 + 1, p3);
          sdWyrProcenta = vybrany.substring(p3 + 1).toInt();
          wyrZSD = true; return;
        }
      }
    }
  }
  wyrZSD = false;
  aktualniHraIdx = nahodnyIndex(nahodnyKlic("wyr-pole", 3000), wyrPocet);
}

void zobrazWyr() {
  String ot = wyrZSD ? sdWyrOtazka : String(wyrOtazky[aktualniHraIdx]);
  String mozA = wyrZSD ? sdWyrMozA : String(wyrMoznostiA[aktualniHraIdx]);
  String mozB = wyrZSD ? sdWyrMozB : String(wyrMoznostiB[aktualniHraIdx]);
  display.setPartialWindow(0, 0, display.width(), display.height());
  display.firstPage();
  do {
    display.fillScreen(bgColor()); nakresliStatusBar();
    u8g2Fonts.setFontMode(1); u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
    u8g2Fonts.setFont(getTitleFont()); u8g2Fonts.setCursor(5, 16); u8g2Fonts.print("CO BYS RADŠI?");
    display.drawFastHLine(0, 20, display.width(), fgColor());
    u8g2Fonts.setFont(getBodyFont());
    TextLine lines[4]; int lCount = zalamejText(ot.c_str(), ot.length(), lines, 4, display.width() - 10);
    int y = 38;
    for(int i=0; i<lCount; i++){
      char lBuf[80]; int len = lines[i].len; if(len>79) len=79;
      strncpy(lBuf, &ot.c_str()[lines[i].start], len); lBuf[len]='\0';
      int lw = u8g2Fonts.getUTF8Width(lBuf);
      u8g2Fonts.setCursor((display.width() - lw) / 2, y); u8g2Fonts.print(lBuf); y+=getLineHeight();
    }
    display.drawFastHLine(0, 84, display.width(), fgColor());
    u8g2Fonts.setFont(getSmallFont());
    u8g2Fonts.setCursor(5, 96); u8g2Fonts.print("A: "); u8g2Fonts.print(mozA.c_str());
    u8g2Fonts.setCursor(5, 109); u8g2Fonts.print("B: "); u8g2Fonts.print(mozB.c_str());
    napoveda(5, 125, "BTN21=Výsledek | BTN0=Další | Drž=zpět");
  } while (display.nextPage());
}

void zobrazWyrVysledek() {
  int pct = wyrZSD ? sdWyrProcenta : wyrProcenta[aktualniHraIdx];
  String mozA = wyrZSD ? sdWyrMozA : String(wyrMoznostiA[aktualniHraIdx]);
  String mozB = wyrZSD ? sdWyrMozB : String(wyrMoznostiB[aktualniHraIdx]);
  display.setPartialWindow(0, 0, display.width(), display.height());
  display.firstPage();
  do {
    display.fillScreen(bgColor());
    u8g2Fonts.setFontMode(1); u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
    u8g2Fonts.setFont(getTitleFont()); u8g2Fonts.setCursor(5, 18); u8g2Fonts.print("VÝSLEDEK PRŮZKUMU");
    display.drawFastHLine(0, 22, display.width(), fgColor());
    u8g2Fonts.setFont(getBodyFont());
    String lineA = String(pct) + "% volí: " + mozA;
    String lineB = String(100 - pct) + "% volí: " + mozB;
    u8g2Fonts.setCursor(5, 45); u8g2Fonts.print(lineA.c_str());
    u8g2Fonts.setCursor(5, 65); u8g2Fonts.print(lineB.c_str());
    // Vizuální pruh výsledku
    int barY = 78; int barH = 14; int barMaxW = 256;
    display.fillRect(5, barY, (pct * barMaxW) / 100, barH, fgColor());
    display.drawRect(5, barY, barMaxW, barH, fgColor());
    u8g2Fonts.setFont(getSmallFont());
    u8g2Fonts.setCursor(8, barY + 10); u8g2Fonts.setForegroundColor(bgColor()); u8g2Fonts.print(String(pct) + "%");
    u8g2Fonts.setForegroundColor(fgColor());
    u8g2Fonts.setCursor(5, 110); u8g2Fonts.print("Zdroj: globální průzkum veřejného mínění");
    napoveda(5, 125, "BTN0 = Další otázka | Drž BTN21 = zpět");
  } while (display.nextPage());
}

// ===== HOROSKOP =====
void parsujHoroskopy(String raw) {
  for (int i = 0; i < 12; i++) horoskopy[i] = "";
  int pos = 0;
  for (int i = 0; i < 12; i++) {
    int tStart = raw.indexOf("|T|", pos);
    int eStart = raw.indexOf("|E|", tStart + 3);
    if (tStart == -1 || eStart == -1) break;
    horoskopy[i] = raw.substring(tStart + 3, eStart);
    pos = eStart + 3;
  }
}

void zobrazHoroskopDetail(int idx) {
  display.setPartialWindow(0, 0, display.width(), display.height());
  display.firstPage();
  do {
    display.fillScreen(bgColor());
    u8g2Fonts.setFontMode(1); u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
    u8g2Fonts.setFont(getTitleFont());
    String nadpis = "HOROSKOP: " + horoskopItems[idx];
    u8g2Fonts.setCursor(5, 18); u8g2Fonts.print(nadpis.c_str());
    display.drawFastHLine(0, 22, display.width(), fgColor());
    String text = (horoskopy[idx].length() > 0) ? horoskopy[idx] : "Data nejsou k dispozici.";
    u8g2Fonts.setFont(getBodyFont());
    int lpp = getLinesPerPage();
    TextLine allLines[50]; int totalLines = zalamejText(text.c_str(), text.length(), allLines, 50, display.width() - 10);
    int totalPages = (totalLines + lpp - 1) / lpp; if (totalPages < 1) totalPages = 1;
    if (horoskopScrollPage >= totalPages) horoskopScrollPage = 0;
    int startLine = horoskopScrollPage * lpp;
    int y = 38;
    for (int i = 0; i < lpp && (startLine + i) < totalLines; i++) {
      int li = startLine + i;
      char lBuf[80]; int len = allLines[li].len; if (len > 79) len = 79;
      strncpy(lBuf, &text.c_str()[allLines[li].start], len); lBuf[len] = '\0';
      u8g2Fonts.setCursor(5, y); u8g2Fonts.print(lBuf); y += getLineHeight();
    }
    u8g2Fonts.setFont(getSmallFont());
    napoveda(5, 125, "BTN0=strana|BTN21=znak|Drž=zpět");
    String pageInfo = String(horoskopScrollPage + 1) + "/" + String(totalPages);
    int piw = u8g2Fonts.getUTF8Width(pageInfo.c_str());
    u8g2Fonts.setCursor(display.width() - piw - 3, 125); u8g2Fonts.print(pageInfo.c_str());
  } while (display.nextPage());
}

// ===== GRAF KURZŮ =====
void zobrazKurzGraf() {
  String raw = stazenaDataKurzyHistorie;
  const char* menaNames[] = { "EUR", "USD", "BTC", "ZLATO", "STRIBRO" };
  String targetMena = menaNames[grafMenuIndex];
  
  float hodnoty[30]; int pocet = 0;
  int lineStart = 0;
  while (lineStart < (int)raw.length() && pocet == 0) {
    int lineEnd = raw.indexOf('\n', lineStart);
    if (lineEnd == -1) lineEnd = raw.length();
    String line = raw.substring(lineStart, lineEnd);
    int pipePos = line.indexOf('|');
    if (pipePos > 0 && line.substring(0, pipePos) == targetMena) {
      String data = line.substring(pipePos + 1);
      int dPos = 0;
      while (dPos < (int)data.length() && pocet < 30) {
        int comma = data.indexOf(',', dPos);
        if (comma == -1) comma = data.length();
        String val = data.substring(dPos, comma);
        val.trim();
        if (val.length() > 0) hodnoty[pocet++] = val.toFloat();
        dPos = comma + 1;
      }
    }
    lineStart = lineEnd + 1;
  }

  display.setPartialWindow(0, 0, display.width(), display.height());
  display.firstPage();
  do {
    display.fillScreen(bgColor());
    u8g2Fonts.setFontMode(1); u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
    String nadpis = String(grafNazvy[grafMenuIndex]) + " / 30 dni";
    u8g2Fonts.setFont(getTitleFont()); u8g2Fonts.setCursor(5, 16); u8g2Fonts.print(nadpis.c_str());
    display.drawFastHLine(0, 20, display.width(), fgColor());

    if (pocet < 2) {
      u8g2Fonts.setFont(getBodyFont()); u8g2Fonts.setCursor(5, 70); u8g2Fonts.print("Data nejsou k dispozici");
    } else {
      float vMin = hodnoty[0], vMax = hodnoty[0];
      for (int i = 1; i < pocet; i++) { if (hodnoty[i] < vMin) vMin = hodnoty[i]; if (hodnoty[i] > vMax) vMax = hodnoty[i]; }
      float rozsah = vMax - vMin; if (rozsah < 0.001f) rozsah = 1.0f;

      // Menší graf — pravý panel (x=232-290) pro aktuální hodnotu
      int gX = 44, gY = 26, gW = 182, gH = 72;
      char buf[20];

      // Osa Y popisky
      u8g2Fonts.setFont(getSmallFont());
      if (vMax > 10000) sprintf(buf, "%.0f", vMax); else if (vMax > 100) sprintf(buf, "%.1f", vMax); else sprintf(buf, "%.2f", vMax);
      u8g2Fonts.setCursor(2, gY + 8); u8g2Fonts.print(buf);
      if (vMin > 10000) sprintf(buf, "%.0f", vMin); else if (vMin > 100) sprintf(buf, "%.1f", vMin); else sprintf(buf, "%.2f", vMin);
      u8g2Fonts.setCursor(2, gY + gH); u8g2Fonts.print(buf);

      // Osa X popisky
      u8g2Fonts.setCursor(gX, 110); u8g2Fonts.print("-30d");
      u8g2Fonts.setCursor(gX + gW - 18, 110); u8g2Fonts.print("dnes");

      // Osy grafu
      display.drawFastVLine(gX, gY, gH, fgColor());
      display.drawFastHLine(gX, gY + gH, gW, fgColor());

      // Čárový graf
      int prevX = -1, prevY = -1;
      for (int i = 0; i < pocet; i++) {
        int px = gX + (i * gW) / (pocet - 1);
        int py = gY + gH - (int)(((hodnoty[i] - vMin) / rozsah) * gH);
        if (prevX >= 0) {
          display.drawLine(prevX, prevY, px, py, fgColor());
          display.drawLine(prevX, prevY + 1, px, py + 1, fgColor());
        }
        prevX = px; prevY = py;
      }
      display.fillCircle(prevX, prevY, 3, fgColor());

      // Aktuální hodnota v pravém panelu
      if (hodnoty[pocet-1] > 10000) sprintf(buf, "%.0f", hodnoty[pocet-1]);
      else if (hodnoty[pocet-1] > 100) sprintf(buf, "%.1f", hodnoty[pocet-1]);
      else sprintf(buf, "%.2f", hodnoty[pocet-1]);
      int rpX = gX + gW + 8;
      u8g2Fonts.setFont(getSmallFont());
      u8g2Fonts.setCursor(rpX, gY + 10); u8g2Fonts.print("Dnes:");
      u8g2Fonts.setFont(getTitleFont());
      u8g2Fonts.setCursor(rpX, gY + 26); u8g2Fonts.print(buf);
    }
    u8g2Fonts.setFont(getSmallFont()); napoveda(5, 125, "BTN0=dalsi | Drz BTN21=zpet");
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
void ulozPoziciKnihy(int idx, int page) { if (idx >= 0 && idx < 15) buchaPozice[idx] = page; prefs.begin("knihy", false); char key[8]; sprintf(key, "k%d", idx); prefs.putInt(key, page); prefs.end(); }
void nactiPoziceKnih() { prefs.begin("knihy", true); for (int i = 0; i < 15; i++) { char key[8]; sprintf(key, "k%d", i); buchaPozice[i] = prefs.getInt(key, 0); } prefs.end(); }
void ulozGBPozici(int node) { prefs.begin("gb", false); prefs.putInt("node", node); prefs.end(); }
int nactiGBPozici() { prefs.begin("gb", true); int n = prefs.getInt("node", 0); prefs.end(); return n; }

// ===== PARSOVÁNÍ DAT =====
uint32_t hashStr(const char* str) {
  uint32_t h = 2166136261UL;
  for (; *str; ++str) {
    h ^= (uint8_t)*str;
    h *= 16777619UL;
  }
  return h;
}

String uidZpravy(const String& tit, const String& dat) {
  String k = tit + "|" + dat;
  uint32_t h = hashStr(k.c_str());
  char buf[10]; snprintf(buf, sizeof(buf), "%08lx", h);
  return String(buf);
}

bool jeZpravaPrectena(const String& uid) {
  if (uid.length() == 0) return true;
  Preferences p; p.begin("honzueRead", false);
  bool val = p.getBool(uid.c_str(), false);
  p.end();
  return val;
}

void oznacitJakoPrectenou(const String& uid) {
  if (uid.length() == 0) return;
  Preferences p; p.begin("honzueRead", false);
  p.putBool(uid.c_str(), true);
  p.end();
}

// ===== INDEX ČLÁNKŮ (pro řazení a stránkování) =====
// Aby šlo zobrazit všechny články (bez limitu 20) a seřadit je od nejnovějších,
// držíme v paměti jen malý index (pozice + datum), ne celé texty.
static const int MAX_CLANKU_INDEX = 400;
struct ClanekIndex { long sortKey; int start; int len; };
static ClanekIndex g_idxAkt[MAX_CLANKU_INDEX];
static int g_idxAktCount = 0;

// Naplní jednu položku aktualniZpravy[] z bloku |T|…|E|. Vrací true při úspěchu.
static bool _naplnZpravuZBloku(const String& blok) {
  if (pocetZprav >= 20) return false;
  int pS = blok.indexOf("|P|", 3);
  if (pS < 0) return false;
  int xS = blok.indexOf("|X|", pS);
  int eE = (xS > 0) ? blok.indexOf("|E|", xS) : -1;
  if (xS < 0 || eE < 0) return false;
  int dS = blok.indexOf("|D|", 3);
  if (dS > 3 && dS < pS) {
    aktualniZpravy[pocetZprav].titulek = blok.substring(3, dS);
    aktualniZpravy[pocetZprav].datum = blok.substring(dS + 3, pS);
  } else {
    aktualniZpravy[pocetZprav].titulek = blok.substring(3, pS);
    aktualniZpravy[pocetZprav].datum = "";
  }
  aktualniZpravy[pocetZprav].perex = blok.substring(pS + 3, xS);
  // Index zpráv má v |X| jen 12znakové ID článku (text se dotahuje až při
  // otevření). Archiv a plná data mají v |X| celý text článku.
  String xObsah = blok.substring(xS + 3, eE);
  bool jeId = (xObsah.length() > 0 && xObsah.length() <= 20 && xObsah.indexOf(' ') < 0);
  aktualniZpravy[pocetZprav].id = jeId ? xObsah : String("");
  aktualniZpravy[pocetZprav].text = jeId ? String("") : xObsah;
  aktualniZpravy[pocetZprav].uid = uidZpravy(aktualniZpravy[pocetZprav].titulek, aktualniZpravy[pocetZprav].datum);
  pocetZprav++;
  return true;
}

// Projde raw text, zapamatuje si pozici každého článku a seřadí je
// od nejnovějšího po nejstarší (podle data |D|).
static void _postavIndexAktualit(const String& raw) {
  g_idxAktCount = 0;
  int pos = 0;
  while (pos < (int)raw.length() && g_idxAktCount < MAX_CLANKU_INDEX) {
    int tS = raw.indexOf("|T|", pos);
    if (tS < 0) break;
    int eE = raw.indexOf("|E|", tS);
    if (eE < 0) break;
    eE += 3;
    // Pro datum stačí začátek bloku (|T|…|D|…|P|) — nekopírujeme celý text
    int konec = raw.indexOf("|P|", tS);
    if (konec < 0 || konec > eE) konec = eE;
    g_idxAkt[g_idxAktCount].sortKey = _sortKeyZBloku(raw.substring(tS, konec + 3));
    g_idxAkt[g_idxAktCount].start = tS;
    g_idxAkt[g_idxAktCount].len = eE - tS;
    g_idxAktCount++;
    pos = eE;
  }
  // Seřazení od nejnovějšího (výběrové — index má jen stovky položek)
  for (int i = 0; i < g_idxAktCount - 1; i++) {
    int best = i;
    for (int j = i + 1; j < g_idxAktCount; j++) {
      if (g_idxAkt[j].sortKey > g_idxAkt[best].sortKey) best = j;
    }
    if (best != i) {
      ClanekIndex t = g_idxAkt[i]; g_idxAkt[i] = g_idxAkt[best]; g_idxAkt[best] = t;
    }
  }
}

// Načte stránku (max 20 článků) z raw textu v RAM podle offsetu.
static void _nactiStrankuAktualit(int offset) {
  pocetZprav = 0;
  if (!aktualniRawZdroj) return;
  const String& raw = *aktualniRawZdroj;
  for (int i = offset; i < g_idxAktCount && pocetZprav < 20; i++) {
    _naplnZpravuZBloku(raw.substring(g_idxAkt[i].start, g_idxAkt[i].start + g_idxAkt[i].len));
  }
}

// Zobrazí seznam zpráv rubriky — načte index všech článků a první stránku.
void parsujZpravy(const String& raw) {
  pocetZprav = 0;
  archivStranka = 0; archivCelkemClanku = 0; // aktuality mají vlastní stránkování
  aktualniRawZdroj = &raw;
  _postavIndexAktualit(raw);
  aktualniStranka = 0;
  aktualniCelkemClanku = g_idxAktCount;
  _nactiStrankuAktualit(0);
  if (clanekMenuIndex >= pocetZprav) clanekMenuIndex = 0;
  if (pocetZprav == 0) {
    aktualniZpravy[0].titulek = "Chyba dat"; aktualniZpravy[0].datum = ""; aktualniZpravy[0].perex = "Žádná data nejsou v paměti."; aktualniZpravy[0].text = raw;
    aktualniZpravy[0].uid = "chyba";
    pocetZprav = 1;
    aktualniCelkemClanku = 0;
  }
}

// Načte stránku článků (max 20) z archivního souboru; offset = pořadí prvního
// bloku. Vrací celkový počet článků v souboru (pro stránkování). Soubor se
// čte proudově — i velký denní archiv (100+ článků) se vejde do RAM.
// Články se řadí od nejnovějšího (podle data |D|), protože do souboru se
// během dne jen připisují.
struct ArchivIdx { long sortKey; uint32_t offset; uint32_t len; };
static const int MAX_ARCHIV_IDX = 400;
static ArchivIdx g_idxArch[MAX_ARCHIV_IDX];
static int g_idxArchCount = 0;

int parsujZpravyStrankuZeSouboru(const String& cesta, int offset) {
  pocetZprav = 0;
  g_idxArchCount = 0;

  // 1) První průchod: zjistíme pozice a datumy všech bloků
  File f = SD.open(cesta.c_str());
  if (!f) return 0;
  String buf = "";
  uint32_t bufAbs = 0;
  uint8_t tmp[512];
  while (f.available()) {
    int n = f.read(tmp, sizeof(tmp));
    if (n <= 0) break;
    buf.concat((const char*)tmp, n);
    while (true) {
      int tS = buf.indexOf("|T|");
      if (tS < 0) {
        // Ponecháme poslední 3 znaky (může být rozpůlený marker |T|)
        if (buf.length() > 3) { bufAbs += (uint32_t)(buf.length() - 3); buf = buf.substring(buf.length() - 3); }
        break;
      }
      int eS = buf.indexOf("|E|", tS);
      if (eS < 0) {
        if (tS > 0) { bufAbs += (uint32_t)tS; buf = buf.substring(tS); }
        break;
      }
      int len = eS + 3 - tS;
      if (g_idxArchCount < MAX_ARCHIV_IDX) {
        g_idxArch[g_idxArchCount].offset = bufAbs + (uint32_t)tS;
        g_idxArch[g_idxArchCount].len = (uint32_t)len;
        // Pro datum stačí začátek bloku — nekopírujeme celý text článku
        int konec = buf.indexOf("|P|", tS);
        if (konec < 0 || konec > eS) konec = eS;
        g_idxArch[g_idxArchCount].sortKey = _sortKeyZBloku(buf.substring(tS, konec + 3));
        g_idxArchCount++;
      }
      bufAbs += (uint32_t)(eS + 3);
      buf = buf.substring(eS + 3);
    }
  }
  f.close();
  int celkem = g_idxArchCount;

  // 2) Seřazení od nejnovějšího
  for (int i = 0; i < celkem - 1; i++) {
    int best = i;
    for (int j = i + 1; j < celkem; j++) {
      if (g_idxArch[j].sortKey > g_idxArch[best].sortKey) best = j;
    }
    if (best != i) {
      ArchivIdx t = g_idxArch[i]; g_idxArch[i] = g_idxArch[best]; g_idxArch[best] = t;
    }
  }

  // 3) Načtení požadované stránky (20 článků) přes seek — bez celého souboru v RAM
  if (offset < 0) offset = 0;
  File g = SD.open(cesta.c_str());
  if (!g) return celkem;
  uint8_t blk[512];
  int nacteno = 0;
  for (int i = offset; i < celkem && nacteno < 20; i++) {
    if (!g.seek(g_idxArch[i].offset)) break;
    String blok = "";
    uint32_t zbyva = g_idxArch[i].len;
    while (zbyva > 0) {
      size_t r = g.read(blk, zbyva > sizeof(blk) ? sizeof(blk) : zbyva);
      if (r == 0) break;
      blok.concat((const char*)blk, r);
      zbyva -= (uint32_t)r;
    }
    if (_naplnZpravuZBloku(blok)) nacteno++;
  }
  g.close();
  return celkem;
}

int spocitejZpravyVRaw(const String& raw) {
  int count = 0, pos = 0;
  while (true) {
    int t = raw.indexOf("|T|", pos);
    if (t < 0) break;
    int e = raw.indexOf("|E|", t + 3);
    if (e < 0) break;
    count++;
    pos = e + 3;
  }
  return count;
}

int nactiHashTitulkuZRaw(const String& raw, uint32_t* out, int maxOut) {
  int count = 0, pos = 0;
  while (count < maxOut) {
    int t = raw.indexOf("|T|", pos);
    if (t < 0) break;
    int d = raw.indexOf("|D|", t + 3);
    int p = raw.indexOf("|P|", t + 3);
    int e = raw.indexOf("|E|", t + 3);
    if (p < 0 || e < 0) break;
    int endTit = (d >= 0 && d < p) ? d : p;
    if (endTit > t + 3) {
      String tit = raw.substring(t + 3, endTit);
      tit.trim();
      out[count++] = djb2Hash(tit);
    }
    pos = e + 3;
  }
  return count;
}

int spocitejNoveZpravyVuciStarym(const String& oldRaw, const String& newRaw) {
  const int MAX_H = 160;
  uint32_t oldH[MAX_H];
  int oldCount = nactiHashTitulkuZRaw(oldRaw, oldH, MAX_H);

  int count = 0, pos = 0;
  while (true) {
    int t = newRaw.indexOf("|T|", pos);
    if (t < 0) break;
    int d = newRaw.indexOf("|D|", t + 3);
    int p = newRaw.indexOf("|P|", t + 3);
    int e = newRaw.indexOf("|E|", t + 3);
    if (p < 0 || e < 0) break;
    int endTit = (d >= 0 && d < p) ? d : p;
    if (endTit > t + 3) {
      String tit = newRaw.substring(t + 3, endTit);
      tit.trim();
      uint32_t h = djb2Hash(tit);
      bool dup = false;
      for (int i = 0; i < oldCount; i++) { if (oldH[i] == h) { dup = true; break; } }
      if (!dup) count++;
    }
    pos = e + 3;
  }
  return count;
}

String prvniTitulekZRaw(const String& raw) {
  int t = raw.indexOf("|T|");
  if (t < 0) return "";
  int d = raw.indexOf("|D|", t + 3);
  int p = raw.indexOf("|P|", t + 3);
  int konec = (d >= 0 && p > d) ? d : p;
  if (konec <= t + 3) return "";
  String tit = raw.substring(t + 3, konec);
  tit.trim();
  return tit;
}

void zobrazSouhrnAktualizace(int nSvet, int nCR, int nTech, const String& topTitulek) {
  beginUiFrame(false);
  do {
    display.fillScreen(bgColor());
    u8g2Fonts.setFontMode(1);
    u8g2Fonts.setForegroundColor(fgColor());
    u8g2Fonts.setBackgroundColor(bgColor());

    u8g2Fonts.setFont(getTitleFont());
    u8g2Fonts.setCursor(8, 18);
    u8g2Fonts.print("AKTUALIZACE HOTOVA");
    display.drawFastHLine(0, 23, display.width(), fgColor());
    int splitX = display.width() / 2;
    display.drawFastVLine(splitX, 24, display.height() - 24, fgColor());

    u8g2Fonts.setFont(getBodyFont());
    u8g2Fonts.setCursor(10, 44); u8g2Fonts.print("Nove clanky:");
    u8g2Fonts.setCursor(10, 62); u8g2Fonts.print("Svet: " + String(nSvet));
    u8g2Fonts.setCursor(10, 78); u8g2Fonts.print("CR:   " + String(nCR));
    u8g2Fonts.setCursor(10, 94); u8g2Fonts.print("Tech: " + String(nTech));

    String top = topTitulek;
    if (top.length() == 0) top = "(nezjisteno)";
    if (top.length() > 260) top = top.substring(0, 260) + "...";

    int rightX = splitX + 8;
    int rightW = display.width() - rightX - 8;

    u8g2Fonts.setFont(getSmallBoldFont());
    TextLine lines[5];
    int lCount = zalamejText(top.c_str(), top.length(), lines, 5, rightW);
    int lineH = getLineHeight();
    int topAreaY = 28;   // o radek vys, vice prostoru pro titulek
    int topAreaH = 94;
    int blockH = lCount * lineH;
    int y = topAreaY + ((topAreaH - blockH) / 2) + lineH;
    for (int i = 0; i < lCount; i++) {
      char b[150];
      int len = lines[i].len;
      if (len > 149) len = 149;
      strncpy(b, &top.c_str()[lines[i].start], len);
      b[len] = '\0';
      int tw = u8g2Fonts.getUTF8Width(b);
      int cx = rightX + (rightW - tw) / 2;
      if (cx < rightX) cx = rightX;
      u8g2Fonts.setCursor(cx, y);
      u8g2Fonts.print(b);
      y += lineH;
    }
    u8g2Fonts.setFont(getSmallFont());
    napoveda(10, 128, "BTN0/BTN21 = pokracovat");
  } while (display.nextPage());

  // Nezavirat automaticky; ochrana proti "probliknuti":
  // 1) nejdriv pockat na uvolneni tlacitek po predchozi akci
  while (digitalRead(BTN_DOWN) == LOW || digitalRead(BTN_SELECT) == LOW) { delay(30); }
  delay(120);
  // 2) potom cekat na novy vedomy stisk
  while (digitalRead(BTN_DOWN) == HIGH && digitalRead(BTN_SELECT) == HIGH) { delay(30); }
  delay(120);
  // 3) a az nakonec na uvolneni
  while (digitalRead(BTN_DOWN) == LOW || digitalRead(BTN_SELECT) == LOW) { delay(30); }
}

void parsujPocasi(String raw) {
  if (raw.length() < 5) return;
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
  if (raw.length() < 5) return;
  int n1 = raw.indexOf('|'); int n2 = raw.indexOf('|', n1 + 1);
  int n3 = raw.indexOf('|', n2 + 1); int n4 = raw.indexOf('|', n3 + 1);
  int n5 = raw.indexOf('|', n4 + 1); int n6 = raw.indexOf('|', n5 + 1);
  if (n1 > 0 && n4 > 0) {
    kurzEur = raw.substring(0, n1); kurzUsd = raw.substring(n1 + 1, n2); kurzBtc = raw.substring(n2 + 1, n3);
    kurzZlato = raw.substring(n3 + 1, n4);
    // Stříbro končí až u 5. oddělovače — jinak by pohltilo i paliva za ním
    kurzStribro = (n5 > 0) ? raw.substring(n4 + 1, n5) : raw.substring(n4 + 1);
    kurzStribro.trim();
    if (n5 > 0) {
      kurzNafta = (n6 > 0) ? raw.substring(n5 + 1, n6) : raw.substring(n5 + 1);
      kurzNafta.trim();
      if (n6 > 0) { kurzBenzin = raw.substring(n6 + 1); kurzBenzin.trim(); }
    }
  }
}

// ===== WI-FI A STAHOVÁNÍ =====
// ===== ULOŽENÁ WIFI PŘIHLÁŠENÍ (NVS / Preferences) =====
// Klíč v NVS má limit 15 znaků, proto odvozujeme klíč z SSID hashem.
// Hodnota je uložena jako "ssid\nheslo".
String wifiKliceProSsid(const String& ssid) {
  uint32_t h = 2166136261u;
  for (int i = 0; i < (int)ssid.length(); i++) { h ^= (uint8_t)ssid[i]; h *= 16777619u; }
  char buf[16];
  snprintf(buf, sizeof(buf), "p%08x", (unsigned int)h);
  return String(buf);
}

void ulozWifiPrihlaseni(const String& ssid, const String& heslo) {
  Preferences wp;
  if (!wp.begin("wifi", false)) return;
  wp.putString(wifiKliceProSsid(ssid).c_str(), ssid + "\n" + heslo);
  wp.putString("last", ssid);
  wp.end();
}

bool nactiWifiPrihlaseni(const String& ssid, String& hesloOut) {
  Preferences wp;
  if (!wp.begin("wifi", true)) return false;
  String v = wp.getString(wifiKliceProSsid(ssid).c_str(), "");
  wp.end();
  int nl = v.indexOf('\n');
  if (nl < 0) return false;
  hesloOut = v.substring(nl + 1);
  return true;
}

String nactiPosledniWiFiSsid() {
  Preferences wp;
  if (!wp.begin("wifi", true)) return "";
  String s = wp.getString("last", "");
  wp.end();
  return s;
}

bool pripojWiFi() {
  if (WiFi.status() == WL_CONNECTED) return true;
  
  // Zapnutí rádia a vyčištění běžícího spojení (bez vypínání driveru!)
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, true); // odpoj od AP a smaž config, nezhasínej rádio
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  WiFi.persistent(false);      
  WiFi.setSleep(false);        
  delay(100);

  auto pripojNa = [&](const char* ssid, const char* pass) -> bool {
    Serial.println("Pripojuji: " + String(ssid));
    WiFi.disconnect(false, true); // Odpoj od předchozího pokusu a připrav se
    delay(100);
    WiFi.begin(ssid, pass);
    int pokusy = 0;
    while (WiFi.status() != WL_CONNECTED && pokusy < 24) { delay(500); pokusy++; }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("WiFi OK: " + String(ssid) + " | IP: " + WiFi.localIP().toString());
      return true;
    }
    Serial.println("WiFi FAIL (" + String(ssid) + "), status: " + String((int)WiFi.status()));
    return false;
  };

  // Nejdřív zkusíme naposledy uloženou síť z Nastavení → WiFi (heslo z Preferences)
  String ulozenaSsid = nactiPosledniWiFiSsid();
  String ulozeneHeslo = "";
  bool pripojenoZUlozene = false;
  if (ulozenaSsid.length() > 0 && nactiWifiPrihlaseni(ulozenaSsid, ulozeneHeslo)) {
    pripojenoZUlozene = pripojNa(ulozenaSsid.c_str(), ulozeneHeslo.c_str());
  }

  if (!pripojenoZUlozene) {
    // Teprve teď proskenovat okolí — scan zdržuje, takže se dělá jen když
    // přímé připojení k poslední uložené síti nevyšlo.
    bool maSsid1 = false, maSsid2 = false;
    int n = WiFi.scanNetworks(false, true);
    for (int i = 0; i < n; i++) {
      String s = WiFi.SSID(i);
      if (s == ssid1) maSsid1 = true;
      if (s == ssid2) maSsid2 = true;
    }
    WiFi.scanDelete();

    if (maSsid1 && pripojNa(ssid1, pass1)) {
      // connected
    } else if (maSsid2 && pripojNa(ssid2, pass2)) {
      // connected
    } else {
      // Když scan selže/nevrátí nic, zkusíme fallback pokusy i tak.
      if (pripojNa(ssid1, pass1) || pripojNa(ssid2, pass2)) {
        // connected
      }
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    setenv("TZ", TZ_PRAGUE, 1);
    tzset();
    configTzTime(TZ_PRAGUE, "pool.ntp.org", "time.google.com", "time.nist.gov");
    struct tm timeinfo;
    bool ok = false;
    for (int i = 0; i < 3; i++) {
      if (getLocalTime(&timeinfo, 2500)) { ok = true; break; }
      delay(200);
    }
    casSynchronizovan = ok;
    Serial.print("NTP sync: "); Serial.println(ok ? "OK" : "FAIL");
    if (ok) {
      time_t epoch = time(NULL);
      Serial.print("Epoch: "); Serial.println((long)epoch);
      char buf[40];
      strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
      Serial.print("Local CZ time: "); Serial.println(buf);
      Serial.print("TZ: "); Serial.println(TZ_PRAGUE);
    }
    return true;
  }
  return false;
}

String stahniTextZUrl(const String& nazev, const String& url) {
  initSharedClient();
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setReuse(true);
  http.begin(sharedClient, url);
  http.setTimeout(8000);
  http.addHeader("User-Agent", "ESP32-Honzueink");

  int httpCode = http.GET();
  String vysledek = "";

  if (httpCode == 200) {
    vysledek = http.getString();
  } else {
    Serial.println("CHYBA GITHUB: " + String(httpCode) + " u souboru: " + nazev);
  }
  http.end();
  // NEDĚLÁME sharedClient.stop() — držíme spojení pro další soubory!

  // Retry pouze jednou, BEZ nového klienta
  if (vysledek.length() == 0) {
    delay(300);
    HTTPClient http2;
    http2.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http2.setReuse(true);
    http2.begin(sharedClient, url);
    http2.setTimeout(8000);
    http2.addHeader("User-Agent", "ESP32-Honzueink");
    int httpCode2 = http2.GET();
    if (httpCode2 == 200) { vysledek = http2.getString(); }
    else { Serial.println("RETRY CHYBA: " + String(httpCode2) + " u souboru: " + nazev); }
    http2.end();
  }

  // Vracíme "" pokud to selže, abychom nepřepsali dobrá data chybou!
  return vysledek;
}

// Stejné jako stahniTextZUrl, ale s vlastní hlavičkou (např. X-Access-Token).
String stahniTextZUrlHlavicka(const String& nazev, const String& url, const char* hlavicka, const String& hodnota) {
  initSharedClient();
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setReuse(true);
  http.begin(sharedClient, url);
  http.setTimeout(8000);
  http.addHeader("User-Agent", "ESP32-Honzueink");
  http.addHeader(hlavicka, hodnota);

  String vysledek = "";
  int httpCode = http.GET();
  if (httpCode == 200) vysledek = http.getString();
  else Serial.println("CHYBA API: " + String(httpCode) + " u " + nazev);
  http.end();
  return vysledek;
}

// ===== INKREMENTÁLNÍ STAŽENÍ ZPRÁV =====
// Vytáhne jeden zprávu z raw textu podle pozice markeru |T| a |E|
String vytahniClanekZRaw(const String& raw, int tStart) {
  int eStart = raw.indexOf("|E|", tStart);
  if (eStart < 0) return "";
  return raw.substring(tStart, eStart + 3);
}

// Najde článek v raw textu podle djb2 hash titulku
String najdiClanekPodleHashe(const String& raw, uint32_t hledanyHash) {
  int pos = 0;
  while (true) {
    int t = raw.indexOf("|T|", pos);
    if (t < 0) break;
    int d = raw.indexOf("|D|", t + 3);
    int p = raw.indexOf("|P|", t + 3);
    int e = raw.indexOf("|E|", t + 3);
    if (p < 0 || e < 0) break;
    int endTit = (d >= 0 && d < p) ? d : p;
    if (endTit > t + 3) {
      String tit = raw.substring(t + 3, endTit);
      tit.trim();
      if (djb2Hash(tit) == hledanyHash) {
        return raw.substring(t, e + 3);
      }
    }
    pos = e + 3;
  }
  return "";
}

// Hlavní stažení seznamu zpráv
// Stáhne jen index (metadata + ID) — plné texty článků se dotahují až při
// otevření, takže seznam není omezen na 20 článků a šetří se RAM i data.
// Vrací "" při selhání (pak se použije fallback na plný soubor).
String stahniZpravyInkrementalne(const String& debugLabel,
                                  const String& idxUrl,
                                  const String& detailPrefix,
                                  const String& stareRaw,
                                  int& out_pocetNovych)
{
  (void)detailPrefix; // texty se stahují až při otevření článku
  out_pocetNovych = 0;

  // Index: |T|titulek|D|datum|P|perex|X|<ID>|E|
  String idxRaw = stahniTextZUrl(debugLabel + "-idx", idxUrl);
  if (idxRaw.length() < 50) {
    Serial.println(debugLabel + ": idx stažení selhalo → fallback");
    return "";
  }

  out_pocetNovych = spocitejNoveZpravyVuciStarym(stareRaw, idxRaw);
  int celkem = spocitejZpravyVRaw(idxRaw);
  Serial.println(debugLabel + ": seznam OK, článků: " + String(celkem) +
                 ", nových: " + String(out_pocetNovych));
  return idxRaw;
}

// Načte plný text článku: nejdřív z detail cache na SD, pak z denního archivu.
// Síť se při otevírání článku nepoužívá (dřív to vedlo k pádu zařízení).
// Najde plný text článku v lokálním denním archivu (dnes, včera, předevčírem).
// Archivní soubor má články za sebou: |T|titulek|D|datum|P|perex|X|text|E|
// Text může obsahovat odřádkování, proto se bloky skládají po řádcích.
// Výhoda: žádné stahování při otevírání článku → rychlé a bez pádu zařízení.
static String nactiTextZArchivu(const String& titulek) {
  if (!sdReady || titulek.length() == 0) return "";
  struct tm ti;
  if (!getLocalTime(&ti, 100)) return "";
  const char* soubory[] = { "zpravy_svet.txt", "zpravy_cr.txt", "zpravy_tech.txt", "zpravy_pozitivni.txt" };
  for (int zpet = 0; zpet <= 2; zpet++) {
    time_t denT = mktime(&ti) - (time_t)zpet * 86400L;
    struct tm dtm; localtime_r(&denT, &dtm);
    char den[12]; strftime(den, sizeof(den), "%Y-%m-%d", &dtm);
    String slozka = String("/eindata/zpravy/archiv/") + den + "/";
    for (int s = 0; s < 4; s++) {
      File f = SD.open((slozka + soubory[s]).c_str());
      if (!f) continue;
      String blok = "";
      while (f.available()) {
        String radek = f.readStringUntil('\n');
        if (radek.startsWith("|T|")) blok = radek;
        else if (blok.length() > 0) { blok += "\n"; blok += radek; }
        else continue;
        int eE = blok.indexOf("|E|");
        if (eE < 0) continue;                    // článek pokračuje dalšími řádky
        int dS = blok.indexOf("|D|");
        int pS = blok.indexOf("|P|");
        int konecTit = (dS > 0) ? dS : pS;
        if (konecTit > 3) {
          String tit = blok.substring(3, konecTit);
          tit.trim();
          if (tit == titulek) {
            int xS = (pS > 0) ? blok.indexOf("|X|", pS) : blok.indexOf("|X|", konecTit);
            String txt = (xS > 0) ? blok.substring(xS + 3, eE) : String("");
            f.close();
            return txt;
          }
        }
        blok = "";
      }
      f.close();
    }
  }
  return "";
}

bool nactiDetailClanku(int idx) {
  if (idx < 0 || idx >= pocetZprav) return false;
  if (aktualniZpravy[idx].text.length() > 0) return true;   // text už je v paměti
  String id = aktualniZpravy[idx].id;
  if (id.length() == 0) return false;

  String cesta = String("/eindata/zpravy/aktualni/detail/") + id + ".txt";
  String txt = nactiTextZeSD(cesta);
  if (txt.length() > 20) { aktualniZpravy[idx].text = txt; return true; }

  // Plný text vezmeme z lokálního denního archivu (bez internetu) a uložíme
  // do cache, aby se příště četl přímo z detail souboru.
  txt = nactiTextZArchivu(aktualniZpravy[idx].titulek);
  if (txt.length() > 20) {
    _zajistiSlozku("/eindata/zpravy/aktualni");
    _zajistiSlozku("/eindata/zpravy/aktualni/detail");
    zapisTextNaSD(cesta, txt);
    aktualniZpravy[idx].text = txt;
    return true;
  }
  return false;
}

// ===== SYNCHRONIZACE DENNÍHO ARCHIVU ZE SERVERU =====
// Server (GitHub Actions) archivuje všechny články dne do
// eindata/zpravy/archiv/<YYYY-MM-DD>/<rubrika>.txt — až 100+ článků/den.
// ESP si tyto soubory stáhne a slíčí je do lokálního archivu (deduplikace
// podle titulku) — lokální archiv tak není omezen 20 články jako aktuality.
// Stahuje se jen to, co se na serveru změnilo (kontrola přes HEAD + ETag),
// stav je v /eindata/zpravy/archiv_sync.txt (řádky: den/soubor|delka|etag).

// Najde v sync datech řádek pro klíč "den/soubor"; vrací index začátku řádku nebo -1.
static int _syncZacatekRadku(const String& sync, const String& klic) {
  int pos = 0;
  while (pos < (int)sync.length()) {
    int e = sync.indexOf('\n', pos);
    String radek = (e < 0) ? sync.substring(pos) : sync.substring(pos, e);
    if (radek.startsWith(klic + "|")) return pos;
    if (e < 0) break;
    pos = e + 1;
  }
  return -1;
}

// Přepíše nebo doplní stavový řádek pro klíč "den/soubor".
static void _syncUlozRadek(String& sync, const String& klic, long delka, const String& etag) {
  String radek = klic + "|" + String(delka) + "|" + etag;
  int zac = _syncZacatekRadku(sync, klic);
  if (zac >= 0) {
    int kon = sync.indexOf('\n', zac);
    if (kon < 0) kon = sync.length();
    sync = sync.substring(0, zac) + radek + sync.substring(kon);
  } else {
    if (sync.length() > 0 && !sync.endsWith("\n")) sync += "\n";
    sync += radek + "\n";
  }
}

// Zjistí stav souboru na serveru a porovná ho s poslední synchronizací
// (sync = obsah archiv_sync.txt). ESP32 HTTPClient nemá head() — pošleme GET,
// z hlaviček přečteme velikost/ETag a spojení hned zavřeme bez stažení těla.
// Vrací: 0 = stáhnout, 1 = beze změny (přeskočit), 2 = soubor neexistuje.
static int _archivStavSouboru(const String& url, const String& klic,
                               const String& sync, long& outDelka, String& outEtag) {
  outDelka = -1; outEtag = "";
  initSharedClient();
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setReuse(false); // po přerušení se nebude re-useovat spojení s nedočteným tělem
  http.begin(sharedClient, url);
  http.setTimeout(8000);
  http.addHeader("User-Agent", "ESP32-Honzueink");
  const char* klice[] = { "ETag" };
  http.collectHeaders(klice, 1);
  int code = http.GET(); // stáhne jen hlavičky — tělo čteme jen přes getString()
  if (code == 404) { http.end(); return 2; }
  if (code != 200) { http.end(); return 0; } // nejistota (výpadek sítě) → raději stáhnout
  outDelka = http.getSize();
  outEtag = http.header("ETag"); outEtag.trim();
  http.end(); // tělo nestahujeme

  // Porovnáme s naposledy synchronizovaným stavem tohoto souboru
  int zac = _syncZacatekRadku(sync, klic);
  if (zac < 0) return 0; // ještě jsme ho nikdy nesynchronizovali
  int kon = sync.indexOf('\n', zac);
  String radek = (kon < 0) ? sync.substring(zac) : sync.substring(zac, kon);
  // formát: den/soubor|delka|etag
  int p1 = radek.indexOf('|');
  int p2 = (p1 > 0) ? radek.indexOf('|', p1 + 1) : -1;
  if (p1 < 0 || p2 < 0) return 0;
  long staraDelka = radek.substring(p1 + 1, p2).toInt();
  String staryEtag = radek.substring(p2 + 1); staryEtag.trim();
  if (outDelka == staraDelka && outEtag == staryEtag) return 1; // beze změny
  return 0;
}
void stahniArchivZeServeru() {
  if (!sdReady || !LOCAL_ARCHIVE_ON_DEVICE) return;
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 150)) return;

  const char* soubory[] = { "zpravy_svet.txt", "zpravy_cr.txt", "zpravy_tech.txt", "zpravy_pozitivni.txt" };
  const int DNU_ZPET = 2; // dnes, včera, předevčírem — pokryje to i výpadek aktualizace

  // Načteme hashe celého lokálního archivu JEDNOU pro všechny soubory
  if (!_zajistiSlozku("/eindata/zpravy") || !_zajistiSlozku("/eindata/zpravy/archiv")) return;
  String sync = nactiTextZeSD("/eindata/zpravy/archiv_sync.txt"); // stav poslední synchronizace
  int hashCount = nactiVsechnyArchivniHashe(g_existHash, MAX_HASH_GLOBAL);
  int pridanoCelkem = 0;
  int stazenoSouboru = 0, preskocenoSouboru = 0;
  bool syncZmenen = false;

  for (int zpet = 0; zpet <= DNU_ZPET; zpet++) {
    time_t denT = mktime(&timeinfo) - (time_t)zpet * 86400L;
    struct tm denTm;
    localtime_r(&denT, &denTm);
    char den[12];
    strftime(den, sizeof(den), "%Y-%m-%d", &denTm);

    for (int s = 0; s < 4; s++) {
      String url = urlBase + "eindata/zpravy/archiv/" + den + "/" + soubory[s];
      String klic = String(den) + "/" + soubory[s];

      // Lehký HEAD dotaz — když se velikost/ETag nezměnily, soubor nestahujeme
      long delka; String etag;
      int stav = _archivStavSouboru(url, klic, sync, delka, etag);
      if (stav == 2) continue;            // soubor na serveru není (den bez článků)
      if (stav == 1) { preskocenoSouboru++; continue; } // beze změny

      String data = stahniTextZUrl("Archiv " + klic, url);
      if (data.length() < 50) continue; // stažení selhalo — necháme stav nezměněný
      pridanoCelkem += sloucArchivniData(soubory[s], data, g_existHash, hashCount);
      _syncUlozRadek(sync, klic, delka, etag); // zapamatujeme si stav pro příště
      syncZmenen = true;
      stazenoSouboru++;
    }
  }
  if (syncZmenen) zapisTextNaSD("/eindata/zpravy/archiv_sync.txt", sync);

  // Údržba: ze sync souboru mažeme záznamy starší než 8 dní (archiv se maže po 7)
  {
    time_t hranice = mktime(&timeinfo) - 8 * 86400L;
    struct tm hranTm; localtime_r(&hranice, &hranTm);
    char hraniceDen[12]; strftime(hraniceDen, sizeof(hraniceDen), "%Y-%m-%d", &hranTm);
    String novy = "";
    bool zmena = false;
    int pos = 0;
    while (pos < (int)sync.length()) {
      int e = sync.indexOf('\n', pos);
      String radek = (e < 0) ? sync.substring(pos) : sync.substring(pos, e);
      // klíč má tvar YYYY-MM-DD/soubor — porovnáme řetězově datumovou část
      if (radek.length() >= 10 && radek.substring(0, 10) >= String(hraniceDen)) {
        novy += radek + "\n";
      } else if (radek.length() > 0) {
        zmena = true;
      }
      if (e < 0) break;
      pos = e + 1;
    }
    if (zmena) { sync = novy; zapisTextNaSD("/eindata/zpravy/archiv_sync.txt", sync); }
  }

  if (pridanoCelkem > 0 || stazenoSouboru > 0) {
    Serial.println("Archiv ze serveru: +" + String(pridanoCelkem) + " novych, stazeno " +
                   String(stazenoSouboru) + " souboru, preskoceno " + String(preskocenoSouboru) +
                   " (hashu celkem: " + String(hashCount) + ")");
  }
}

void aktualizovatDataNaPozadi(bool vynuceno) {
  // Automatické aktualizace (interval / denní hodina) byly zrušeny —
  // data se stahují jen na výslovné vyžádání z menu.
  if (!vynuceno) return;

  time_t now = time(NULL);
  if (now > 1600000000) rtc_posledniPokusAktualizace = now;

  {
    ledBlikam = true;                 // LED bliká = běží WiFi stahování
    nakresliLoadScreen("Navazuji spojení...", 5);

    if (pripojWiFi()) {
      // NTP re-sync pokud ještě nebyl synchronizován
      if (!casSynchronizovan) {
        setenv("TZ", TZ_PRAGUE, 1);
        tzset();
        configTzTime(TZ_PRAGUE, "pool.ntp.org", "time.google.com", "time.nist.gov");
        struct tm timeinfo;
        if (getLocalTime(&timeinfo, 5000)) casSynchronizovan = true;
      }

      // Uvolníme globální Stringy před stahováním — šetříme RAM pro HTTP+TLS
      // Na ESP32 Arduino: přiřazení prázdného String() uvolní interní buffer
      { String prazdny;
        stazenaDataSvet = prazdny; stazenaDataCR = prazdny; stazenaDataTech = prazdny; stazenaDataPozitivni = prazdny;
        stazenaDataPocasi = prazdny; stazenaDataKurzy = prazdny;
        stazenaDataHoroskop = prazdny; stazenaDataKurzyHistorie = prazdny;
      }

      bool asponNecoSeStahlo = false;
      int noveSvet = 0, noveCR = 0, noveTech = 0, novePozitivni = 0;
      String topTitulek = "";
      sharedClientInitialized = false; // Reset klienta pro čerstvé spojení
      sharedClient.stop(); // Zavřeme případné předchozí spojení

      Serial.print("Volná halda před stahováním: ");
      Serial.println(ESP.getFreeHeap());

      nakresliLoadScreen("Stahuji zprávy ze světa...", 15);
      {
        String oldRaw = nactiSouborZAktualni("zpravy_svet.txt");
        String tmp = stahniZpravyInkrementalne("Svet", urlZpravySvetIdx, urlZpravyDetail, oldRaw, noveSvet);
        if (tmp.length() <= 20) {
          tmp = stahniTextZUrl("Svet", urlZpravySvet);
          if (tmp.length() > 20) noveSvet = spocitejNoveZpravyVuciStarym(oldRaw, tmp);
        }
        if (tmp.length() > 20) {
          if (topTitulek.length() == 0) topTitulek = prvniTitulekZRaw(tmp);
          ulozZpravuDoCache("svet", tmp); ulozAktualniZpravuNaSD("zpravy_svet.txt", tmp);
          asponNecoSeStahlo = true;
        }
      }

      nakresliLoadScreen("Stahuji zprávy z ČR...", 28);
      {
        String oldRaw = nactiSouborZAktualni("zpravy_cr.txt");
        String tmp = stahniZpravyInkrementalne("CR", urlZpravyCRIdx, urlZpravyDetail, oldRaw, noveCR);
        if (tmp.length() <= 20) {
          tmp = stahniTextZUrl("CR", urlZpravyCR);
          if (tmp.length() > 20) noveCR = spocitejNoveZpravyVuciStarym(oldRaw, tmp);
        }
        if (tmp.length() > 20) {
          String t0 = prvniTitulekZRaw(tmp);
          if (t0.length() > 0) topTitulek = t0;
          ulozZpravuDoCache("cr", tmp); ulozAktualniZpravuNaSD("zpravy_cr.txt", tmp);
          asponNecoSeStahlo = true;
        }
      }

      nakresliLoadScreen("Stahuji Tech a AI...", 42);
      {
        String oldRaw = nactiSouborZAktualni("zpravy_tech.txt");
        String tmp = stahniZpravyInkrementalne("Tech", urlZpravyTechIdx, urlZpravyDetail, oldRaw, noveTech);
        if (tmp.length() <= 20) {
          tmp = stahniTextZUrl("Tech", urlTechAI);
          if (tmp.length() > 20) noveTech = spocitejNoveZpravyVuciStarym(oldRaw, tmp);
        }
        if (tmp.length() > 20) {
          ulozZpravuDoCache("tech", tmp); ulozAktualniZpravuNaSD("zpravy_tech.txt", tmp);
          asponNecoSeStahlo = true;
        }
      }

      nakresliLoadScreen("Stahuji Pozitivní...", 50);
      {
        String oldRaw = nactiSouborZAktualni("zpravy_pozitivni.txt");
        String tmp = stahniZpravyInkrementalne("Pozitivni", urlZpravyPozitivniIdx, urlZpravyDetail, oldRaw, novePozitivni);
        if (tmp.length() <= 20) {
          tmp = stahniTextZUrl("Pozitivni", urlZpravyPozitivni);
          if (tmp.length() > 20) novePozitivni = spocitejNoveZpravyVuciStarym(oldRaw, tmp);
        }
        if (tmp.length() > 20) {
          ulozZpravuDoCache("poz", tmp); ulozAktualniZpravuNaSD("zpravy_pozitivni.txt", tmp);
          asponNecoSeStahlo = true;
        }
      }

      // Doplnit lokální archiv o všechny články dne ze serveru (bez limitu 20)
      nakresliLoadScreen("Stahuji archiv...", 52);
      stahniArchivZeServeru();

      nakresliLoadScreen("Stahuji Počasí...", 55);
      {
        String tmp = stahniTextZUrl("Pocasi", urlPocasi);
        if (tmp.length() > 20) { ulozZpravuDoCache("poc", tmp); asponNecoSeStahlo = true; }
      }

      nakresliLoadScreen("Stahuji Kurzy...", 68);
      {
        String tmp = stahniTextZUrl("Kurzy", urlKurzy);
        if (tmp.length() > 10) { ulozZpravuDoCache("kur", tmp); asponNecoSeStahlo = true; }
      }

      nakresliLoadScreen("Stahuji Horoskop...", 80);
      {
        String tmp = stahniTextZUrl("Horoskop", urlHoroskop);
        if (tmp.length() > 20) { ulozZpravuDoCache("horo", tmp); asponNecoSeStahlo = true; }
      }

      nakresliLoadScreen("Stahuji hist. kurzů...", 88);
      {
        String tmp = stahniTextZUrl("KurzyHist", urlKurzyHistorie);
        if (tmp.length() > 10) { ulozZpravuDoCache("khist", tmp); asponNecoSeStahlo = true; }
      }

      nakresliLoadScreen("Stahuji spoje...", 94);
      {
        _zajistiSlozku(JR_DIR);
        time_t jrNow = time(NULL);
        long jrPosledni = nactiTextZeSD(String(JR_DIR) + "/posledni_kontrola.txt").toInt();
        // Stačí kontrolovat jednou za 12 h — jízdní řády se mění jen zřídka.
        bool jrKontrola = (jrNow < 1600000000) || (jrPosledni <= 0) ||
                          (jrNow - jrPosledni > 43200);
        if (jrKontrola) {
          // Verzi poznáme podle data generování z GitHubu — když se nezměnila,
          // nestahujeme znovu index ani jednotlivé soubory.
          String verze = stahniTextZUrl("JR verze", urlBase + "eindata/jr/aktualizace.txt");
          verze.trim();
          String lokalni = nactiTextZeSD(String(JR_DIR) + "/aktualizace.txt");
          lokalni.trim();
          bool stahuj = (verze.length() < 8) || (verze != lokalni);
          if (stahuj) {
            String idx = stahniTextZUrl("JR index", urlBase + "eindata/jr/index.txt");
            if (idx.length() > 5) {
              zapisTextNaSD(String(JR_DIR) + "/index.txt", idx);
              int pos = 0;
              while (pos < (int)idx.length()) {
                int e = idx.indexOf('\n', pos);
                String line = (e < 0) ? idx.substring(pos) : idx.substring(pos, e);
                line.trim();
                int bar = line.indexOf('|');
                if (bar > 0) {
                  String fname = line.substring(0, bar);
                  String obsah = stahniTextZUrl("JR " + fname, urlBase + "eindata/jr/" + fname);
                  if (obsah.length() > 10) zapisTextNaSD(String(JR_DIR) + "/" + fname, obsah);
                }
                if (e < 0) break;
                pos = e + 1;
              }
              String sv = stahniTextZUrl("JR svatky", urlBase + "eindata/jr/svatky.txt");
              if (sv.length() > 5) zapisTextNaSD(String(JR_DIR) + "/svatky.txt", sv);
              if (verze.length() >= 8) zapisTextNaSD(String(JR_DIR) + "/aktualizace.txt", verze);
              asponNecoSeStahlo = true;
            }
          }
          if (verze.length() >= 8 && jrNow > 1600000000) {
            zapisTextNaSD(String(JR_DIR) + "/posledni_kontrola.txt", String((long)jrNow));
          }
        }
      }

      nakresliLoadScreen("Stahuji kalendář...", 97);
      {
        _zajistiSlozku(KAL_DIR);
        String kal = stahniTextZUrl("Kalendar", urlBase + "eindata/kalendar/kalendar.txt");
        if (kal.length() > 10) {
          zapisTextNaSD(String(KAL_DIR) + "/kalendar.txt", kal);
          asponNecoSeStahlo = true;
        }
      }

      // Uklidíme sdílený klient po dokončení všech stahování
      sharedClient.stop();
      sharedClientInitialized = false;
      if (asponNecoSeStahlo) {
        nactiStazenaData(); // Načte z LittleFS zpět do RAM jen to co potřebujeme
        rtc_posledniAktualizace = time(NULL);
        // Den považujeme za splněný i po ruční aktualizaci — jinak by hned
        // následující iterace loop() spustila plánovanou aktualizaci znovu.
        {
          struct tm t;
          if (getLocalTime(&t, 50)) {
            rtc_posledniPlanovanaAktualizace = (t.tm_year + 1900) * 10000 + (t.tm_mon + 1) * 100 + t.tm_mday;
          }
        }
        // opravArchivDataceASerazeni(); // docasne vypnuto kvuli stabilite
        promazArchivStarsi7Dni();
        zahrajUspesnyTon();             // krátké tiché potvrzení, že data dorazila
      }
      if (vynuceno) {
        if (topTitulek.length() == 0) topTitulek = asponNecoSeStahlo ? "Aktualizace dokoncena" : "Stazeni selhalo";
        zobrazSouhrnAktualizace(noveSvet, noveCR, noveTech, topTitulek);
      } else {
        nakresliLoadScreen(asponNecoSeStahlo ? "HOTOVO A ULOŽENO!" : "Stažení selhalo!", 100);
        delay(800);
      }
    } else {
      if (vynuceno) {
        nakresliLoadScreen("Wi-Fi nenalezena!", 0);
        delay(2000);
      }
    }
    ledBlikam = false;               // konec stahování → LED zhasne
    // Vypnout WiFi rádio pro úsporu baterie (pokud neběží web upload režim)
    if (!webUploadMode) {
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
    }
  }
}

// ===== ZOBRAZOVÁNÍ MENU A DAT =====
void zobrazSubMenu(const char* title, String items[], int count, int selIndex, int scrOffset) {
  int visible = getVisibleMenuItems(); int itemH = getMenuItemHeight();

  // Pojistka: vybraná položka musí být vždy vidět, i když volající předá
  // nekonzistentní offset (např. po návratu z podmenu s vysokým indexem).
  if (count > 0) {
    int maxScr = (count > visible) ? (count - visible) : 0;
    if (scrOffset > maxScr) scrOffset = maxScr;
    if (selIndex < scrOffset) scrOffset = selIndex;
    if (selIndex >= scrOffset + visible) scrOffset = selIndex - visible + 1;
    if (scrOffset < 0) scrOffset = 0;
  }

  // Rychlé překreslení: pokud se jen posunul kurzor a seznam se neposunul,
  // obnovíme pouze řádky starého a nového výběru (místo celé obrazovky).
  static int lastSel = -1, lastScr = -1, lastCount = -1;
  static String lastTitle = "";
  bool posun = gPosunKurzoru; gPosunKurzoru = false;
  String tit(title);
  bool stejnaObrazovka = (lastCount == count) && (lastTitle == tit);
  if (posun && stejnaObrazovka && lastScr == scrOffset && lastSel >= 0 && lastSel != selIndex) {
    int rA = lastSel - scrOffset, rB = selIndex - scrOffset;
    if (rA >= 0 && rA < visible && rB >= 0 && rB < visible) {
      int yA = 32 + rA * itemH, yB = 32 + rB * itemH;
      int yTop = min(yA, yB) - 2;
      int yBot = max(yA, yB) + itemH + 2;
      nastavOblastPrekresleni(0, yTop, display.width(), yBot - yTop);
    }
  }
  lastSel = selIndex; lastScr = scrOffset; lastCount = count; lastTitle = tit;

  beginUiFrame(true);
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
    if (pct < 0) {
      u8g2Fonts.setCursor(10, 50); u8g2Fonts.print("N/A");
    } else {
      u8g2Fonts.setCursor(10, 50); u8g2Fonts.print(String(pct) + " %");
    }
    
    u8g2Fonts.setFont(getBodyFont());
    if (v < 0.1) {
      u8g2Fonts.setCursor(10, 75); u8g2Fonts.print("Napětí: N/A");
    } else {
      u8g2Fonts.setCursor(10, 75); u8g2Fonts.print("Napětí: " + String(v, 2) + " V");
    }
    
    u8g2Fonts.setFont(getSmallFont());
    u8g2Fonts.setCursor(10, 95); u8g2Fonts.print("EASYLANDER, Li-pol 2500 mAh");
    u8g2Fonts.setCursor(10, 108); u8g2Fonts.print("3.7v, 9.25wh, Model: 603090");
    napoveda(5, 125, "Drž BTN21 = zpět");
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
    napoveda(5, 127, "Drž BTN21 = zpět");
  } while (display.nextPage());
}

// ===== WiFi PŘIPOJENÍ =====
void spustWifiSken() {
  WiFi.disconnect(true, true);
  delay(100);
  WiFi.mode(WIFI_STA);
  WiFi.scanDelete();
  wifiPocetSiti = 0;
  int n = WiFi.scanNetworks(false, false, false, 200);
  if (n <= 0) {
    wifiPocetSiti = 0;
    return;
  }
  wifiPocetSiti = min(n, 15);
  for (int i = 0; i < wifiPocetSiti; i++) {
    wifiSitSSID[i] = WiFi.SSID(i);
    wifiSitRSSI[i] = WiFi.RSSI(i);
    wifiSitEnc[i]  = WiFi.encryptionType(i);
  }
  WiFi.scanDelete();
  if (wifiSitIndex >= wifiPocetSiti) wifiSitIndex = 0;
}

// Připojení k WiFi se zadaným heslem. Vrací true při úspěchu.
bool pripojKWifi(const String& ssid, const String& heslo) {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, true);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  WiFi.persistent(false);
  WiFi.setSleep(false);
  delay(100);
  if (heslo.length() == 0) WiFi.begin(ssid.c_str());
  else WiFi.begin(ssid.c_str(), heslo.c_str());
  int pokusy = 0;
  while (WiFi.status() != WL_CONNECTED && pokusy < 24) { delay(500); pokusy++; }
  if (WiFi.status() == WL_CONNECTED) {
    setenv("TZ", TZ_PRAGUE, 1);
    tzset();
    Serial.println("WiFi OK: " + ssid + " | IP: " + WiFi.localIP().toString());
    return true;
  }
  Serial.println("WiFi FAIL (" + ssid + "), status: " + String((int)WiFi.status()));
  return false;
}

void zobrazWifiSeznam() {
  beginUiFrame(true);
  do {
    display.fillScreen(bgColor());
    u8g2Fonts.setFontMode(1);
    u8g2Fonts.setFont(getTitleFont()); u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());

    if (wifiPocetSiti == 0) {
      u8g2Fonts.setCursor(5, 18); u8g2Fonts.print("WIFI - SÍTĚ");
      nakresliStatusBar();
      display.drawFastHLine(0, 23, display.width(), fgColor());
      u8g2Fonts.setFont(getBodyFont());
      u8g2Fonts.setCursor(10, 55); u8g2Fonts.print("Probíhá sken sítí...");
      u8g2Fonts.setCursor(10, 75); u8g2Fonts.print("Počkejte prosím.");
    } else {
      String t = "WiFi (" + String(wifiSitIndex + 1) + "/" + String(wifiPocetSiti) + ")";
      u8g2Fonts.setCursor(5, 18); u8g2Fonts.print(t.c_str());
      nakresliStatusBar();
      display.drawFastHLine(0, 23, display.width(), fgColor());

      // Aktuální připojení
      u8g2Fonts.setFont(getSmallFont()); u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
      if (WiFi.status() == WL_CONNECTED) {
        u8g2Fonts.setCursor(8, 36); u8g2Fonts.print("Připojeno: " + WiFi.SSID() + "  " + WiFi.localIP().toString());
      } else {
        u8g2Fonts.setCursor(8, 36); u8g2Fonts.print("Nepřipojeno");
      }

      u8g2Fonts.setFont(getBodyFont());
      int y = 56;
      for (int i = 0; i < 4; i++) {
        int idx = wifiSitIndex + i;
        if (idx >= wifiPocetSiti) break;
        String ssid = wifiSitSSID[idx];
        if (ssid.length() > 18) ssid = ssid.substring(0, 18) + "..";
        String enc = wifiSitEnc[idx] == WIFI_AUTH_OPEN ? " " : "*";
        String s = ssid + "  " + String(wifiSitRSSI[idx]) + "dBm " + enc;
        if (idx == wifiSitIndex) {
          display.fillRect(0, y - 12, display.width(), 16, fgColor());
          u8g2Fonts.setForegroundColor(bgColor()); u8g2Fonts.setBackgroundColor(fgColor());
        } else {
          u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
        }
        u8g2Fonts.setCursor(8, y);
        u8g2Fonts.print(s);
        y += 16;
      }
    }
    display.drawFastHLine(0, 114, display.width(), fgColor());
    u8g2Fonts.setFont(getSmallFont());
    u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
    if (wifiPocetSiti == 0) {
      napoveda(5, 126, "Drž BTN0 = zpět");
    } else {
      napoveda(5, 126, "BTN0=další | BTN21=vybrat | Drž=zpět");
    }
  } while (display.nextPage());
}

void zobrazWifiHeslo() {
  beginUiFrame(true);
  do {
    display.fillScreen(bgColor());
    u8g2Fonts.setFontMode(1);
    u8g2Fonts.setFont(getTitleFont()); u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
    String t = "HESLO: " + wifiHesloSsid;
    if (t.length() > 22) t = t.substring(0, 22) + "..";
    u8g2Fonts.setCursor(5, 18); u8g2Fonts.print(t.c_str());
    nakresliStatusBar();
    display.drawFastHLine(0, 23, display.width(), fgColor());

    // Mřížka znaků (16 sloupců × 3 řádky)
    u8g2Fonts.setFont(getSmallFont());
    for (int i = 0; i < wifiKlavesZnakuPocet; i++) {
      int col = i % wifiKlavesSloupcu; int row = i / wifiKlavesSloupcu;
      int x = 6 + col * 18; int y = 40 + row * 15;
      if (i == wifiKlavesKurzor) { display.fillRect(x - 3, y - 11, 16, 13, fgColor()); u8g2Fonts.setForegroundColor(bgColor()); u8g2Fonts.setBackgroundColor(fgColor()); }
      else { u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor()); }
      char ch[2] = { wifiKlavesniceZnaky[i], '\0' }; u8g2Fonts.setCursor(x, y); u8g2Fonts.print(ch);
    }
    display.drawFastHLine(0, 84, display.width(), fgColor());
    u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());

    // Zadané heslo — pokud je dlouhé, zobrazíme jen konec
    String h = String(wifiHesloBuf);
    if (h.length() > 36) h = "..." + h.substring(h.length() - 33);
    u8g2Fonts.setCursor(5, 98); u8g2Fonts.print(("Heslo: " + h + "_").c_str());

    if (wifiHesloStatus.length() > 0) {
      u8g2Fonts.setCursor(5, 110); u8g2Fonts.print(wifiHesloStatus.c_str());
    } else {
      // Kontextová nápověda podle toho, na kterém znaku stojí kurzor
      char vyb = wifiKlavesniceZnaky[wifiKlavesKurzor];
      u8g2Fonts.setCursor(5, 110);
      if (vyb == '>')      napoveda(5, 110, "Vybraný znak: PŘIPOJIT");
      else if (vyb == '<') napoveda(5, 110, "Vybraný znak: SMAZAT");
      else                 napoveda(5, 110, "BTN21 = zvolit znak");
    }
    napoveda(5, 124, "BTN0 = připojit | Drž BTN21 = zpět");
  } while (display.nextPage());
}

// Nabídne rychlé připojení k síti, pro kterou už máme uložené heslo.
// Krátký BTN21 = připojit, krátký BTN0 = zadat jiné heslo, dlouhý stisk = zpět.
void zobrazWifiPotvrzeni() {
  String ulozene = "";
  bool maUlozene = nactiWifiPrihlaseni(wifiHesloSsid, ulozene);
  String ssid = wifiHesloSsid;
  if (ssid.length() > 24) ssid = ssid.substring(0, 24) + "..";
  String h = ulozene;
  if (h.length() > 26) h = h.substring(0, 26) + "..";

  beginUiFrame(true);
  do {
    display.fillScreen(bgColor());
    u8g2Fonts.setFontMode(1);
    u8g2Fonts.setFont(getTitleFont()); u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
    u8g2Fonts.setCursor(5, 18); u8g2Fonts.print("PŘIPOJIT K WIFI");
    nakresliStatusBar();
    display.drawFastHLine(0, 23, display.width(), fgColor());

    u8g2Fonts.setFont(getBodyFont());
    u8g2Fonts.setCursor(8, 42); u8g2Fonts.print(ssid.c_str());

    u8g2Fonts.setFont(getSmallFont());
    if (maUlozene) {
      u8g2Fonts.setCursor(8, 62); u8g2Fonts.print(("Uložené heslo: " + h).c_str());
    } else {
      u8g2Fonts.setCursor(8, 62); u8g2Fonts.print("Uložené heslo nenalezeno");
    }

    display.drawFastHLine(0, 78, display.width(), fgColor());
    napoveda(8, 96,  "BTN21 = připojit hned");
    napoveda(8, 110, "BTN0  = zadat jiné heslo");
    napoveda(8, 124, "Drž kterékoliv = zpět");
  } while (display.nextPage());
}

// Dopředná deklarace — definice goBack() je až níže ve souboru
void goBack();

// Překreslí klávesnici v malém okně — buď jen mřížku znaků (posun kurzoru),
// nebo i řádek s heslem a stavem (vložení/smazání znaku, připojování).
void wifiKlavesPrekresli(bool vcetneHesla) {
  nastavOblastPrekresleni(0, 26, display.width(), vcetneHesla ? 100 : 50);
  zobrazWifiHeslo();
}

// Potvrzení zadaného hesla a připojení k síti (dřív akce znaku '>').
void wifiKlavesPripoj() {
  wifiHesloStatus = "Připojuji...";
  wifiKlavesPrekresli(true);
  String heslo = String(wifiHesloBuf);
  bool ok = pripojKWifi(wifiHesloSsid, heslo);
  if (ok) {
    ulozWifiPrihlaseni(wifiHesloSsid, heslo);
    wifiHesloStatus = "Připojeno: " + WiFi.localIP().toString();
    wifiKlavesPrekresli(true);
    delay(1500);
    appState = STATE_WIFI_SEZNAM;
    zobrazWifiSeznam();
  } else {
    wifiHesloStatus = "Selhalo - zkuste znovu";
    wifiKlavesPrekresli(true);
  }
}

// Vybere znak na pozici kurzoru: '<' smaže poslední znak, '>' se připojí, jinak vloží znak.
void wifiKlavesVyber() {
  char vybrany = wifiKlavesniceZnaky[wifiKlavesKurzor];
  if (vybrany == '<') {
    if (wifiHesloLen > 0) { wifiHesloLen--; wifiHesloBuf[wifiHesloLen] = '\0'; }
    wifiHesloStatus = "";
    wifiKlavesPrekresli(true);
  } else if (vybrany == '>') {
    wifiKlavesPripoj();
  } else {
    if (wifiHesloLen < (int)sizeof(wifiHesloBuf) - 1) {
      wifiHesloBuf[wifiHesloLen] = vybrany; wifiHesloLen++;
      wifiHesloBuf[wifiHesloLen] = '\0';
    }
    wifiKlavesPrekresli(true);
  }
}

// Neblokující obsluha tlačítek v klávesnici hesla.
// BTN0 = potvrdit a připojit, BTN21 krátce = zvolit znak, BTN21 dlouze = zpět.
// Akce se vyhodnocuje při PUŠTĚNÍ tlačítka.
void obsluhaWifiHesloKlavesnice() {
  static bool b0d = false, b21d = false;
  static unsigned long b21cas = 0;

  unsigned long ted = millis();
  bool b0  = (digitalRead(BTN_DOWN) == LOW);
  bool b21 = (digitalRead(BTN_SELECT) == LOW);
  if (b0 || b21) lastUserActivityMs = ted;

  // --- BTN0: potvrdit a připojit ---
  if (b0 && !b0d) { b0d = true; }
  else if (!b0 && b0d) {
    b0d = false;
    wifiKlavesPripoj();
    return;
  }

  // --- BTN21: krátký = zvolit znak, dlouhý = zpět ---
  if (b21 && !b21d) { b21d = true; b21cas = ted; }
  else if (!b21 && b21d) {
    b21d = false;
    if (ted - b21cas >= WIFI_KB_DLOUHY_MS) { goBack(); return; }
    wifiKlavesVyber();
  }
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
  beginUiFrame(true);
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
      
      u8g2Fonts.setFont(getSmallFont()); napoveda(5, 125, "Drž BTN21 = zpět");
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

    // Levý sloupec — valuty
    u8g2Fonts.setFont(getBodyFont());
    u8g2Fonts.setCursor(10, 42); u8g2Fonts.print("1 EUR = " + kurzEur + " Kč");
    u8g2Fonts.setCursor(10, 62); u8g2Fonts.print("1 USD = " + kurzUsd + " Kč");
    u8g2Fonts.setCursor(10, 82); u8g2Fonts.print("1 BTC = " + kurzBtc + " Kč");

    // Pravý sloupec — kovy
    u8g2Fonts.setFont(getBodyFont());
    u8g2Fonts.setCursor(170, 38); u8g2Fonts.print("Zlato (1g):");
    u8g2Fonts.setFont(getTitleFont()); u8g2Fonts.setCursor(170, 54); u8g2Fonts.print(kurzZlato + " Kč");
    u8g2Fonts.setFont(getBodyFont()); u8g2Fonts.setCursor(170, 74); u8g2Fonts.print("Stříbro (1g):");
    u8g2Fonts.setFont(getTitleFont()); u8g2Fonts.setCursor(170, 90); u8g2Fonts.print(kurzStribro + " Kč");

    // Paliva — pouze aktuální cena
    if (kurzNafta.length() > 0) {
      display.drawFastHLine(0, 100, display.width(), fgColor());
      u8g2Fonts.setFont(getBodyFont());
      u8g2Fonts.setCursor(10, 114); u8g2Fonts.print("Nafta: " + kurzNafta + " Kč");
      if (kurzBenzin.length() > 0) { u8g2Fonts.setCursor(155, 114); u8g2Fonts.print("Benzín: " + kurzBenzin + " Kč"); }
    }

    u8g2Fonts.setFont(getSmallFont()); napoveda(5, 126, "BTN0=Grafy 30d | Drž BTN21=zpět");
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
    napoveda(5, 125, "BTN0=další den | Drž BTN21=zpět");
  } while (display.nextPage());
}

void zobrazSeznamZprav() {
  beginUiFrame(true);
  // Archiv i aktuality se stránkují — ukazujeme globální pozici v celém seznamu
  int celkemZprav, pozice;
  if (zpravyZArchivu && archivCelkemClanku > 0) {
    celkemZprav = archivCelkemClanku;
    pozice = archivStranka * 20 + clanekMenuIndex + 1;
  } else if (zpravyZHledani && hledaniCelkem > 0) {
    celkemZprav = hledaniCelkem;
    pozice = hledaniStranka * 20 + clanekMenuIndex + 1;
  } else if (!zpravyZHledani && aktualniCelkemClanku > 0) {
    celkemZprav = aktualniCelkemClanku;
    pozice = aktualniStranka * 20 + clanekMenuIndex + 1;
  } else {
    celkemZprav = pocetZprav;
    pozice = clanekMenuIndex + 1;
  }
  int pct = celkemZprav > 0 ? (pozice * 100 / celkemZprav) : 0;
  do {
    display.fillScreen(bgColor());
    u8g2Fonts.setFontMode(1);
    u8g2Fonts.setFont(getTitleFont()); u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
    String t = "VÝBĚR (" + String(pozice) + "/" + String(celkemZprav) + ")  " + String(pct) + "%";
    u8g2Fonts.setCursor(5, 18); u8g2Fonts.print(t.c_str());
    nakresliStatusBar();
    display.drawFastHLine(0, 23, display.width(), fgColor());
    // ----- Progress bar (3 px vysoký, přes celou obrazovku) -----
    int barX = 0, barY = 24, barW = display.width(), barH = 3;
    display.drawRect(barX, barY, barW, barH, fgColor());
    int fillW = barW > 4 ? (pct * (barW - 2)) / 100 : 0;
    if (fillW > 0) display.fillRect(barX + 1, barY + 1, fillW, barH - 2, fgColor());

    int y = 38; String tit = aktualniZpravy[clanekMenuIndex].titulek;
    bool nova = !zpravyZHledani && !jeZpravaPrectena(aktualniZpravy[clanekMenuIndex].uid);
    if (nova) tit = "+ " + tit;
    TextLine tLines[3]; int maxW = display.width() - 10;
    int tCount = zalamejText(tit.c_str(), tit.length(), tLines, 3, maxW);
    for (int i = 0; i < tCount; i++) {
      char tBuf[80]; int l = tLines[i].len; if (l > 79) l = 79;
      strncpy(tBuf, &tit[tLines[i].start], l); tBuf[l] = '\0';
      u8g2Fonts.setCursor(5, y); u8g2Fonts.print(tBuf); y += 14;
    }

    if (aktualniZpravy[clanekMenuIndex].datum.length() > 0) {
      u8g2Fonts.setFont(getSmallFont());
      String datumStr = aktualniZpravy[clanekMenuIndex].datum;
      int dw = u8g2Fonts.getUTF8Width(datumStr.c_str());
      u8g2Fonts.setCursor(display.width() - dw - 5, 18);
      u8g2Fonts.print(datumStr.c_str());
      u8g2Fonts.setFont(getTitleFont());
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
    napoveda(5, 125, "BTN0=další | BTN21=číst | Drž=zpět");
  } while (display.nextPage());
}

// ===== HLEDÁNÍ V ARCHIVU (klávesnice + prohledávání titulků) =====

// Odstraní českou diakritiku a převede na malá písmena (hledá se bez diakritiky).
String bezDiakritiky(const String& vstup) {
  String s = vstup; s.toLowerCase();
  const char* z[] = { "á","č","ď","é","ě","í","ň","ó","ř","š","ť","ú","ů","ý","ž" };
  const char* n[] = { "a","c","d","e","e","i","n","o","r","s","t","u","u","y","z" };
  for (int i = 0; i < 15; i++) s.replace(z[i], n[i]);
  return s;
}

// ===== VÝSLEDKY HLEDÁNÍ (bez limitu 20, stránkované) =====
// Do paměti se ukládá jen index nalezených článků (pozice v souboru),
// text se načítá až pro zobrazenou stránku.
static const int MAX_HLEDANI = 300;
struct VysledekHledani {
  uint32_t offset;   // pozice bloku v souboru / v raw textu
  uint32_t len;      // délka bloku
  long     sortKey;  // datum (pro řazení od nejnovějšího)
  uint32_t hash;     // hash titulku (deduplikace)
  uint8_t  den;      // index do g_hledaniDny; 255 = data v RAM
  uint8_t  rubrika;  // 0..3
};
static VysledekHledani g_hledani[MAX_HLEDANI];
static int  g_hledaniCount = 0;
static char g_hledaniDny[12][11];
static int  g_hledaniDnu = 0;

// Titulek z bloku |T|…|E|
static String _titulekZBloku(const String& blok) {
  int tS = blok.indexOf("|T|"); if (tS < 0) return "";
  int pS = blok.indexOf("|P|", tS); if (pS < 0) return "";
  int dS = blok.indexOf("|D|", tS);
  if (dS != -1 && dS > tS && dS < pS) return blok.substring(tS + 3, dS);
  return blok.substring(tS + 3, pS);
}

// Obsahuje titulek VŠECHNA zadaná slova? (hledá se bez diakritiky)
static bool _blokObsahuje(const String& blok, const String& q) {
  String nt = bezDiakritiky(_titulekZBloku(blok));
  if (nt.length() == 0) return false;
  int qp = 0;
  while (true) {
    int sp = q.indexOf(' ', qp);
    String w = (sp < 0) ? q.substring(qp) : q.substring(qp, sp);
    if (w.length() > 0 && nt.indexOf(w) < 0) return false;
    if (sp < 0) break;
    qp = sp + 1;
  }
  return true;
}

// Zapíše nalezený blok do indexu výsledků (duplicitní titulky přeskočí).
static void _pridejVysledek(uint32_t offset, uint32_t len, const String& blok,
                            uint8_t den, uint8_t rubrika) {
  if (g_hledaniCount >= MAX_HLEDANI) return;
  uint32_t h = djb2Hash(bezDiakritiky(_titulekZBloku(blok)));
  for (int i = 0; i < g_hledaniCount; i++) if (g_hledani[i].hash == h) return;
  g_hledani[g_hledaniCount].offset  = offset;
  g_hledani[g_hledaniCount].len     = len;
  g_hledani[g_hledaniCount].sortKey = _sortKeyZBloku(blok);
  g_hledani[g_hledaniCount].hash    = h;
  g_hledani[g_hledaniCount].den     = den;
  g_hledani[g_hledaniCount].rubrika = rubrika;
  g_hledaniCount++;
}

// Prohledá řetězec bloků v RAM (aktuální zprávy).
static void _hledejVRam(const String& raw, uint8_t rubrika, const String& q) {
  int pos = 0;
  while (pos < (int)raw.length() && g_hledaniCount < MAX_HLEDANI) {
    int tS = raw.indexOf("|T|", pos); if (tS < 0) break;
    int eS = raw.indexOf("|E|", tS); if (eS < 0) break;
    eS += 3;
    String blok = raw.substring(tS, eS);
    if (_blokObsahuje(blok, q))
      _pridejVysledek((uint32_t)tS, (uint32_t)(eS - tS), blok, 255, rubrika);
    pos = eS;
  }
}

// Prohledá soubor na SD proudově a zapamatuje si pozice nalezených bloků.
static void _hledejVSouboruIdx(const String& cesta, uint8_t den, uint8_t rubrika, const String& q) {
  File f = SD.open(cesta.c_str());
  if (!f) return;
  String buf = "";
  uint32_t bufAbs = 0;
  uint8_t tmp[512];
  while (f.available() && g_hledaniCount < MAX_HLEDANI) {
    int n = f.read(tmp, sizeof(tmp));
    if (n <= 0) break;
    buf.concat((const char*)tmp, n);
    while (g_hledaniCount < MAX_HLEDANI) {
      int tS = buf.indexOf("|T|");
      if (tS < 0) {
        if (buf.length() > 3) { bufAbs += (uint32_t)(buf.length() - 3); buf = buf.substring(buf.length() - 3); }
        break;
      }
      int eS = buf.indexOf("|E|", tS);
      if (eS < 0) { if (tS > 0) { bufAbs += (uint32_t)tS; buf = buf.substring(tS); } break; }
      String blok = buf.substring(tS, eS + 3);
      if (_blokObsahuje(blok, q))
        _pridejVysledek(bufAbs + (uint32_t)tS, (uint32_t)(eS + 3 - tS), blok, den, rubrika);
      bufAbs += (uint32_t)(eS + 3);
      buf = buf.substring(eS + 3);
    }
  }
  f.close();
}

// Načte stránku výsledků hledání (max 20 článků) podle offsetu.
static void _nactiStrankuHledani(int offset) {
  pocetZprav = 0;
  const char* soubory[] = { "zpravy_svet.txt", "zpravy_cr.txt", "zpravy_tech.txt", "zpravy_pozitivni.txt" };
  for (int i = offset; i < g_hledaniCount && pocetZprav < 20; i++) {
    const VysledekHledani& v = g_hledani[i];
    String blok = "";
    if (v.den == 255) {
      const String* raw = (v.rubrika == 0) ? &stazenaDataSvet
                        : (v.rubrika == 1) ? &stazenaDataCR
                        : (v.rubrika == 2) ? &stazenaDataTech
                                           : &stazenaDataPozitivni;
      if (v.offset + v.len <= (uint32_t)raw->length())
        blok = raw->substring(v.offset, v.offset + v.len);
    } else if (v.den < g_hledaniDnu) {
      String cesta = String("/eindata/zpravy/archiv/") + g_hledaniDny[v.den] + "/" + soubory[v.rubrika];
      File f = SD.open(cesta.c_str());
      if (f) {
        if (f.seek(v.offset)) {
          uint8_t tmp[512];
          uint32_t zbyva = v.len;
          while (zbyva > 0) {
            size_t r = f.read(tmp, zbyva > sizeof(tmp) ? sizeof(tmp) : zbyva);
            if (r == 0) break;
            blok.concat((const char*)tmp, r);
            zbyva -= (uint32_t)r;
          }
        }
        f.close();
      }
    }
    if (blok.length() > 0) _naplnZpravuZBloku(blok);
  }
}

// Spustí hledání: archiv (všechny dny a rubriky) + aktuální zprávy.
void spustHledaniVArchivu() {
  pocetZprav = 0;
  archivStranka = 0; archivCelkemClanku = 0;
  g_hledaniCount = 0; g_hledaniDnu = 0;
  hledaniStranka = 0; hledaniCelkem = 0;
  String q = bezDiakritiky(String(hledaniBuf));
  q.trim();
  if (q.length() == 0) return;

  nakresliLoadScreen("Hledam v archivu...", 30);

  // 1) Aktuální zprávy v RAM
  _hledejVRam(stazenaDataSvet, 0, q);
  _hledejVRam(stazenaDataCR, 1, q);
  _hledejVRam(stazenaDataTech, 2, q);
  _hledejVRam(stazenaDataPozitivni, 3, q);

  // 2) Všechny denní složky archivu na SD
  if (sdReady) {
    File archDir = SD.open("/eindata/zpravy/archiv");
    if (archDir && archDir.isDirectory()) {
      const char* soubory[] = { "zpravy_svet.txt", "zpravy_cr.txt", "zpravy_tech.txt", "zpravy_pozitivni.txt" };
      const int pocetSouboru = 4;
      File den = archDir.openNextFile();
      while (den && g_hledaniCount < MAX_HLEDANI) {
        if (den.isDirectory()) {
          const char* fname = den.name();
          const char* lastSlash = strrchr(fname, '/'); if (lastSlash) fname = lastSlash + 1;
          if (strlen(fname) == 10 && g_hledaniDnu < 12) {
            strncpy(g_hledaniDny[g_hledaniDnu], fname, 10);
            g_hledaniDny[g_hledaniDnu][10] = '\0';
            uint8_t di = (uint8_t)g_hledaniDnu++;
            for (int s = 0; s < pocetSouboru && g_hledaniCount < MAX_HLEDANI; s++) {
              String cesta = String("/eindata/zpravy/archiv/") + fname + "/" + soubory[s];
              if (SD.exists(cesta.c_str())) _hledejVSouboruIdx(cesta, di, (uint8_t)s, q);
            }
          }
        }
        den.close();
        den = archDir.openNextFile();
      }
      if (den) den.close();
      archDir.close();
    }
  }

  // 3) Seřadit výsledky od nejnovějšího
  for (int i = 0; i < g_hledaniCount - 1; i++) {
    int best = i;
    for (int j = i + 1; j < g_hledaniCount; j++) {
      if (g_hledani[j].sortKey > g_hledani[best].sortKey) best = j;
    }
    if (best != i) {
      VysledekHledani t = g_hledani[i]; g_hledani[i] = g_hledani[best]; g_hledani[best] = t;
    }
  }
  hledaniCelkem = g_hledaniCount;

  // 4) Načíst první stránku výsledků
  _nactiStrankuHledani(0);

  if (pocetZprav == 0) {
    aktualniZpravy[0].titulek = "Nic nenalezeno";
    aktualniZpravy[0].datum   = "";
    aktualniZpravy[0].perex   = "Pro \"" + String(hledaniBuf) + "\" nic. Zkuste jiné slovo.";
    aktualniZpravy[0].text    = "";
    aktualniZpravy[0].uid     = "nic";
    pocetZprav = 1;
    hledaniCelkem = 0;
  }
  clanekMenuIndex = 0;
  zpravyZHledani = true;
  appState = STATE_ZPRAVY_SEZNAM;
  zobrazSeznamZprav();
}

// Obrazovka klávesnice pro zadání hledaného výrazu.
void zobrazArchivHledani() {
  beginUiFrame(true);
  do {
    display.fillScreen(bgColor());
    u8g2Fonts.setFontMode(1);
    u8g2Fonts.setFont(getTitleFont()); u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
    u8g2Fonts.setCursor(5, 18); u8g2Fonts.print("HLEDAT V ARCHIVU");
    nakresliStatusBar();
    display.drawFastHLine(0, 23, display.width(), fgColor());

    u8g2Fonts.setFont(getSmallFont());
    for (int i = 0; i < archivHledaniZnakuPocet; i++) {
      int col = i % wifiKlavesSloupcu; int row = i / wifiKlavesSloupcu;
      int x = 6 + col * 18; int y = 40 + row * 15;
      if (i == hledaniKurzor) { display.fillRect(x - 3, y - 11, 16, 13, fgColor()); u8g2Fonts.setForegroundColor(bgColor()); u8g2Fonts.setBackgroundColor(fgColor()); }
      else { u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor()); }
      char ch[2] = { archivHledaniZnaky[i], '\0' }; u8g2Fonts.setCursor(x, y); u8g2Fonts.print(ch);
    }
    display.drawFastHLine(0, 84, display.width(), fgColor());
    u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());

    u8g2Fonts.setCursor(5, 98); u8g2Fonts.print(("Hledat: " + String(hledaniBuf) + "_").c_str());

    char vyb = archivHledaniZnaky[hledaniKurzor];
    u8g2Fonts.setCursor(5, 110);
    if (vyb == '>')      napoveda(5, 110, "Vybraný znak: VYHLEDAT");
    else if (vyb == '<') napoveda(5, 110, "Vybraný znak: SMAZAT");
    else                 napoveda(5, 110, "BTN21 = zvolit znak");
    napoveda(5, 124, "BTN0 = hledat | Drž BTN21 = zpět");
  } while (display.nextPage());
}

// Překreslí klávesnici hledání — jen mřížku, nebo i řádek s výrazem.
void archivHledaniPrekresli(bool vcetneVstupu) {
  nastavOblastPrekresleni(0, 26, display.width(), vcetneVstupu ? 100 : 50);
  zobrazArchivHledani();
}

// Vybere znak na pozici kurzoru: '<' smaže, '>' spustí hledání, jinak vloží znak.
void archivHledaniVyber() {
  char vybrany = archivHledaniZnaky[hledaniKurzor];
  if (vybrany == '<') {
    if (hledaniLen > 0) { hledaniLen--; hledaniBuf[hledaniLen] = '\0'; }
    archivHledaniPrekresli(true);
  } else if (vybrany == '>') {
    spustHledaniVArchivu();
  } else {
    if (hledaniLen < (int)sizeof(hledaniBuf) - 1) { hledaniBuf[hledaniLen] = vybrany; hledaniLen++; hledaniBuf[hledaniLen] = '\0'; }
    archivHledaniPrekresli(true);
  }
}

// Neblokující obsluha tlačítek v klávesnici hledání.
// BTN0 = spustit hledání, BTN21 krátce = zvolit znak, BTN21 dlouze = zpět.
void obsluhaArchivHledaniKlavesnice() {
  static bool b0d = false, b21d = false;
  static unsigned long b21cas = 0;

  unsigned long ted = millis();
  bool b0  = (digitalRead(BTN_DOWN) == LOW);
  bool b21 = (digitalRead(BTN_SELECT) == LOW);
  if (b0 || b21) lastUserActivityMs = ted;

  // --- BTN0: hledat (krátký i dlouhý stisk) ---
  if (b0 && !b0d) { b0d = true; }
  else if (!b0 && b0d) {
    b0d = false;
    spustHledaniVArchivu();
    return;
  }

  // --- BTN21: krátký = zvolit znak, dlouhý = zpět ---
  if (b21 && !b21d) { b21d = true; b21cas = ted; }
  else if (!b21 && b21d) {
    b21d = false;
    if (ted - b21cas >= WIFI_KB_DLOUHY_MS) { goBack(); return; }
    archivHledaniVyber();
  }
}

// ===== DLOUHÉ TEXTY (knihy, články) — cache v PSRAM =====
// Text se načte a zalomí jen JEDNOU; další stránky se už jen vykreslují.
// Otočení stránky tak nečte SD a nezalamuje text znovu.
static char*     dtBuf       = nullptr;  // text v PSRAM
static size_t    dtLen       = 0;
static TextLine* dtLines     = nullptr;  // zalomené řádky v PSRAM
static int       dtLineCount = 0;
static String    dtKey       = "";

static void* dtAlokuj(size_t n) {
  void* p = ps_malloc(n);
  if (!p) p = malloc(n);
  return p;
}

static void dtUvolni() {
  if (dtBuf)   { free(dtBuf);   dtBuf   = nullptr; }
  if (dtLines) { free(dtLines); dtLines = nullptr; }
  dtLen = 0; dtLineCount = 0; dtKey = "";
}

// Cache držíme, dokud je paměti dost. Uvolníme ji jen tehdy, když je volné
// PSRAM málo (např. před spuštěním hudby, jejíž dekodér si bere PSRAM).
static void dtUvolniPriNedostatku() {
  if (!dtBuf) return;
  if (ESP.getFreePsram() < 1024 * 1024) dtUvolni();
}

// Zalomení textu do řádků. uloz=false jen spočítá řádky, uloz=true je zapíše.
static int dtZalom(int maxWidth, bool uloz) {
  int lineCount = 0, pos = 0, charW = getCharWidth();
  while (pos < (int)dtLen) {
    int lineStart = pos, lastSpace = -1, lineW = 0; bool zalomeno = false;
    while (pos < (int)dtLen && dtBuf[pos] != '\n') {
      if (dtBuf[pos] == ' ') lastSpace = pos;
      lineW += charW;
      if (lineW > maxWidth) {
        if (lastSpace > lineStart) { if (uloz) { dtLines[lineCount].start = lineStart; dtLines[lineCount].len = lastSpace - lineStart; } lineCount++; pos = lastSpace + 1; }
        else { if (uloz) { dtLines[lineCount].start = lineStart; dtLines[lineCount].len = pos - lineStart; } lineCount++; }
        zalomeno = true; break;
      } pos++;
    }
    if (!zalomeno) { if (uloz) { dtLines[lineCount].start = lineStart; dtLines[lineCount].len = pos - lineStart; } lineCount++; if (pos < (int)dtLen && dtBuf[pos] == '\n') pos++; }
  }
  return lineCount;
}

// Klíč cache — zahrnuje i styl/velikost písma, aby se text přelomil po změně vzhledu.
static String dtKlic(const String& ident) {
  return String(currentStyle) + "/" + String(currentSize) + "|" + ident;
}

static bool dtZalomDoCache() {
  int maxW = display.width() - 10;
  int cnt = dtZalom(maxW, false);
  if (cnt < 1) cnt = 1;
  dtLines = (TextLine*)dtAlokuj(cnt * sizeof(TextLine));
  if (!dtLines) { dtUvolni(); return false; }
  dtLineCount = dtZalom(maxW, true);
  return true;
}

// Načte text z řetězce (články, fallback) do PSRAM.
static bool dtNactiText(const char* ident, const char* text) {
  size_t n = strlen(text);
  String key = dtKlic(String(ident) + "#" + (unsigned)n);
  if (dtKey == key && dtBuf) return true;
  dtUvolni();
  char* b = (char*)dtAlokuj(n + 1);
  if (!b) return false;
  memcpy(b, text, n + 1);
  dtBuf = b; dtLen = n; dtKey = key;
  return dtZalomDoCache();
}

// Načte text ze souboru na SD (přeskočí první řádek = název knihy) do PSRAM.
static bool dtNactiSoubor(const char* ident, const char* cesta) {
  String key = dtKlic(ident);
  if (dtKey == key && dtBuf) return true;
  dtUvolni();
  File f = SD.open(cesta);
  if (!f) return false;
  f.readStringUntil('\n'); // přeskoč název knihy
  size_t zbyva = f.size() > f.position() ? (size_t)(f.size() - f.position()) : 0;
  const size_t DT_MAX = 4u * 1024u * 1024u; // strop, ať nesežereme celou PSRAM
  if (zbyva > DT_MAX) zbyva = DT_MAX;
  char* b = (char*)dtAlokuj(zbyva + 1);
  if (!b && zbyva > 65536) { zbyva = 65536; b = (char*)dtAlokuj(zbyva + 1); } // záchrana, když není PSRAM
  if (!b) { f.close(); return false; }
  size_t rd = 0;
  while (rd < zbyva) {
    int r = f.read((uint8_t*)b + rd, zbyva - rd);
    if (r <= 0) break;
    rd += r;
  }
  f.close();
  b[rd] = '\0';
  dtBuf = b; dtLen = rd; dtKey = key;
  return dtZalomDoCache();
}

// Vykreslí aktuální stránku z cache.
static void dtVykresli(const char* title) {
  int lineHeight = getLineHeight();
  int linesPerPage = getLinesPerPage();
  int totalPages = (dtLineCount + linesPerPage - 1) / linesPerPage; if (totalPages < 1) totalPages = 1;
  if (textScrollPage < 0) textScrollPage = 0;
  if (textScrollPage >= totalPages) textScrollPage = 0;
  int startLine = textScrollPage * linesPerPage;
  char lineBuf[80];
  beginUiFrame(true);
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
      int idx = startLine + i; if (idx >= dtLineCount) break;
      int l = dtLines[idx].len; if (l > 79) l = 79;
      strncpy(lineBuf, &dtBuf[dtLines[idx].start], l); lineBuf[l] = '\0';
      u8g2Fonts.setCursor(5, y); u8g2Fonts.print(lineBuf); y += lineHeight;
    }
    u8g2Fonts.setFont(getSmallFont());
    String info = String(textScrollPage + 1) + "/" + String(totalPages);
    u8g2Fonts.setCursor(240, 125); u8g2Fonts.print(info.c_str());
  } while (display.nextPage());
}

void zobrazText(const char* title, const char* text) {
  dtNactiText((String("t:") + title).c_str(), text);
  dtVykresli(title);
}

// Zobrazí knihu z knihovny. Obsah se ze SD čte jen při prvním otevření,
// při listování se použije cache v PSRAM.
void zobrazKnihu(int idx) {
  String nazev = (idx >= 0 && idx < dynamicKnihovnaCount) ? dynamicKnihovnaItems[idx] : String("Kniha");
  String cesta = (idx >= 0 && idx < 15) ? dynamicKnihovnaFiles[idx] : "";
  if (cesta.length() == 0) cesta = String("/eindata/knihy/kniha_") + (idx + 1) + ".txt";
  bool ok = false;
  if (sdReady) ok = dtNactiSoubor((String("k:") + cesta).c_str(), cesta.c_str());
  if (!ok && idx >= 0 && idx < 3) dtNactiText((String("kp:") + idx).c_str(), knihy[idx]);
  dtVykresli(nazev.c_str());
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
  // Plynulé roztočení: krok se postupně zmenšuje, takže se flaška rozjede
  // a plynule zpomalí. Žádné umělé delay() — tempo určuje refresh e-paperu.
  float uhel = random(0, 360);
  const float kroky[] = { 100, 82, 64, 48, 34, 22 };
  for (unsigned int i = 0; i < sizeof(kroky) / sizeof(kroky[0]); i++) {
    uhel += kroky[i];
    drawFlaskaFrame(uhel, "Točím...");
  }
  drawFlaskaFrame(uhel + random(0, 360), napovedaZapnuta ? "Stiskni BTN21=znovu | Drž BTN0=zpět" : "");
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

void drawKostkaFrame(int c1, int c2, int strany, const char* msg, bool finale, float mierka = 1.0f) {
  display.setPartialWindow(0, 0, display.width(), display.height());
  display.firstPage();
  do {
    display.fillScreen(bgColor()); 
    u8g2Fonts.setFontMode(1);
    nakresliStatusBar();
    
    if (strany == 66) { 
      int base = 55; int boxSize = (int)(base * mierka); int by = (128 - boxSize) / 2;
      nakresliJednuKostku(45 + (base - boxSize) / 2, by, boxSize, c1, 6, finale); 
      nakresliJednuKostku(115 + (base - boxSize) / 2, by, boxSize, c2, 6, finale);
      u8g2Fonts.setFont(getBodyFont()); u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor()); 
      u8g2Fonts.setCursor(185, 64); u8g2Fonts.print("Součet:"); 
      u8g2Fonts.setFont(getBigFont()); u8g2Fonts.setCursor(200, 95); u8g2Fonts.print(String(c1+c2).c_str());
    } else { 
      int base = 80; int boxSize = (int)(base * mierka); int by = (128 - boxSize) / 2;
      nakresliJednuKostku(30 + (base - boxSize) / 2, by, boxSize, c1, strany, finale);
      u8g2Fonts.setFont(getTitleFont()); u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor()); 
      String hozenoStr = "Hozeno: " + String(c1); u8g2Fonts.setCursor(140, 70); u8g2Fonts.print(hozenoStr.c_str());
      u8g2Fonts.setFont(getSmallFont()); String kostkaStr = "Kostka: D" + String(strany); u8g2Fonts.setCursor(140, 90); u8g2Fonts.print(kostkaStr.c_str());
    }
    if (msg && napovedaZapnuta) { u8g2Fonts.setFont(getSmallFont()); u8g2Fonts.setCursor(5, 124); u8g2Fonts.print(msg); }
  } while (display.nextPage());
}
void zobrazKostkyIdle() { drawKostkaFrame(1, 1, kostkyStran, napovedaZapnuta ? "Drž BTN0=třást | BTN21=hodit" : "", true); }
void hrajKostku(int strany) {
  int maxCislo = (strany == 66) ? 6 : strany;
  // Rychlé míchání: 2 náhodné snímky hned po sobě (bez prodlevy).
  // Kostka je přitom zmenšená.
  const float MIERKA_MICHANI = 0.85f;
  for (int i = 0; i < 2; i++) {
    int r1 = random(1, maxCislo + 1); int r2 = (strany == 66) ? random(1, 7) : 0;
    drawKostkaFrame(r1, r2, strany, "Míchám...", false, MIERKA_MICHANI);
  }
  int c1 = random(1, maxCislo + 1); int c2 = (strany == 66) ? random(1, 7) : 0;
  // Finální snímek v plné velikosti → kostka „povyskočí" zpět.
  drawKostkaFrame(c1, c2, strany, napovedaZapnuta ? "Drž BTN0=třást | BTN21=hodit" : "", true);
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
    u8g2Fonts.setFont(getSmallFont()); napoveda(5, 105, "BTN21 = start/stop"); napoveda(5, 117, "BTN0 = reset"); napoveda(5, 127, "Drž BTN21 = zpět");
  } while (display.nextPage());
}

// ===== "TĚŽBA BITCOINU" — reálné SHA-256d nad 80bajtovou hlavou bloku (jen pro zábavu) =====
// Hledáme nonce tak, aby hash měl co nejvíc vedoucích hex nul. Cíl je schválně nízký,
// aby občas "padl blok" a bylo to zábavné (skutečná obtížnost je astronomická).
static void tezbaInitHeader() {
  for (int i = 0; i < 80; i++) tezbaHeader[i] = (uint8_t)random(256);
  tezbaHeader[0] = 0x20; tezbaHeader[1] = 0x00; tezbaHeader[2] = 0x00; tezbaHeader[3] = 0x00; // verze
  tezbaHeader[72] = 0x1D; tezbaHeader[73] = 0x00; tezbaHeader[74] = 0xFF; tezbaHeader[75] = 0xFF; // obtížnost
}

static int tezbaVedNul(const uint8_t* h) {
  int nuly = 0;
  for (int i = 0; i < 32; i++) {
    if (h[i] == 0) { nuly += 2; continue; }
    if (h[i] < 0x10) nuly += 1;
    break;
  }
  return nuly;
}

static void tezbaHashuj() {
  tezbaHeader[76] = (uint8_t)(tezbaNonce >> 24);
  tezbaHeader[77] = (uint8_t)(tezbaNonce >> 16);
  tezbaHeader[78] = (uint8_t)(tezbaNonce >> 8);
  tezbaHeader[79] = (uint8_t)(tezbaNonce);
  uint8_t h1[32], h2[32];
  mbedtls_sha256(tezbaHeader, 80, h1, 0);
  mbedtls_sha256(h1, 32, h2, 0);
  int nuly = tezbaVedNul(h2);
  if (nuly > tezbaNejNuly) { tezbaNejNuly = nuly; memcpy(tezbaNejHash, h2, 32); }
  if (nuly >= tezbaCilNuly) {
    tezbaBloku++;              // "nalezen blok" → začneme těžit novou hlavu
    tezbaInitHeader();
    tezbaNejNuly = -1;
  }
  tezbaNonce++;
}

static void tezbaHex(const uint8_t* h, int bajtu, char* out) {
  const char* hex = "0123456789abcdef";
  for (int i = 0; i < bajtu; i++) { out[i * 2] = hex[h[i] >> 4]; out[i * 2 + 1] = hex[h[i] & 0xF]; }
  out[bajtu * 2] = '\0';
}

void tezbaReset() {
  tezbaNonce = 0; tezbaHashu = 0; tezbaHashMs = 0; tezbaBloku = 0;
  tezbaNejNuly = -1; tezbaStartMs = millis(); tezbaPosledniDraw = 0;
  memset(tezbaNejHash, 0, 32);
  tezbaInitHeader();
}

void zobrazTezbu() {
  unsigned long hps = tezbaHashMs ? (tezbaHashu * 1000UL) / tezbaHashMs : 0;
  char b[64];
  beginUiFrame(true);
  do {
    display.fillScreen(bgColor());
    u8g2Fonts.setFontMode(1);
    u8g2Fonts.setFont(getTitleFont()); u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
    u8g2Fonts.setCursor(5, 18); u8g2Fonts.print(tezbaReal ? "TĚŽBA BTC (ostrá)" : "TĚŽBA BITCOINU");
    nakresliStatusBar();
    display.drawFastHLine(0, 23, display.width(), fgColor());
    u8g2Fonts.setFont(getSmallFont());
    if (hps >= 1000) sprintf(b, "Rychlost: %lu.%lu kH/s", hps / 1000, (hps / 100) % 10);
    else             sprintf(b, "Rychlost: %lu H/s", hps);
    u8g2Fonts.setCursor(5, 38); u8g2Fonts.print(b);

    char hx[25]; hx[0] = '\0';
    if (tezbaReal) {
      String st = tpStav; if (st.length() > 32) st = st.substring(0, 32);
      u8g2Fonts.setCursor(5, 54); u8g2Fonts.print(("Stav: " + st).c_str());
      sprintf(b, "Nonce: %lu", (unsigned long)tpNonce);
      u8g2Fonts.setCursor(5, 68); u8g2Fonts.print(b);
      sprintf(b, "Kandidatu bloku: %lu", tpBloku);
      u8g2Fonts.setCursor(5, 82); u8g2Fonts.print(b);
      if (tpNejNuly < 0) { u8g2Fonts.setCursor(5, 96); u8g2Fonts.print("Nejlepsi: -"); }
      else { sprintf(b, "Nejlepsi: %d nul", tpNejNuly); u8g2Fonts.setCursor(5, 96); u8g2Fonts.print(b); tezbaHex(tpNejHash, 12, hx); }
      u8g2Fonts.setCursor(5, 110); u8g2Fonts.print("Hash:");
      u8g2Fonts.setCursor(45, 110); u8g2Fonts.print(tpNejNuly >= 0 ? hx : "----------------");
    } else {
      sprintf(b, "Nonce: %lu", tezbaNonce);
      u8g2Fonts.setCursor(5, 54); u8g2Fonts.print(b);
      sprintf(b, "Nalezeno bloku: %lu", tezbaBloku);
      u8g2Fonts.setCursor(5, 68); u8g2Fonts.print(b);
      if (tezbaNejNuly < 0) sprintf(b, "Nejlepsi: -/%d nul", tezbaCilNuly);
      else                  sprintf(b, "Nejlepsi: %d/%d nul", tezbaNejNuly, tezbaCilNuly);
      u8g2Fonts.setCursor(5, 82); u8g2Fonts.print(b);
      if (tezbaNejNuly >= 0) tezbaHex(tezbaNejHash, 12, hx);
      u8g2Fonts.setCursor(5, 96); u8g2Fonts.print("Hash:");
      u8g2Fonts.setCursor(45, 96); u8g2Fonts.print(tezbaNejNuly >= 0 ? hx : "----------------");
    }
    if (tezbaReal) {
      String p = tpPool + ":" + String(tpPort);
      if (p.length() > 40) p = p.substring(0, 40);
      if (tpPrijhlasen) { u8g2Fonts.setCursor(5, 124); u8g2Fonts.print(p.c_str()); }
      else napoveda(5, 124, "Drz = zpet");
    } else {
      napoveda(5, 124, tezbaBezi ? "BTN21=stop | BTN0=reset" : "BTN21=start | BTN0=reset");
    }
  } while (display.nextPage());
}

// ===================================================================
// ČASOVAČ / BUDÍK
// - nastavení v minutách: encoder / D-pad / BTN0
// - BTN21 = start nebo stop, dlouhý BTN21 = zpět
// - po uplynutí zvoní oba buzzery na maximum, dokud nestiskneš tlačítko
// ===================================================================
int  casovacMin = 5;
bool casovacBezi = false, casovacZvoni = false, casovacZvonFaze = false;
unsigned long casovacStartMs = 0, casovacZvonLast = 0;
int  casovacPosledniSek = -1;

// Kolik sekund zbývá (při nastavování vrací nastavenou délku).
int casovacZbyvaSek() {
  if (casovacZvoni) return 0;
  if (!casovacBezi) return casovacMin * 60;
  long zbyva = (long)casovacMin * 60000L - (long)(millis() - casovacStartMs);
  return zbyva > 0 ? (int)(zbyva / 1000) : 0;
}

void zobrazCasovac() {
  int zbyva = casovacZbyvaSek();
  char cas[12]; snprintf(cas, sizeof(cas), "%02d:%02d", zbyva / 60, zbyva % 60);
  beginUiFrame(true);
  do {
    display.fillScreen(bgColor());
    u8g2Fonts.setFontMode(1);
    u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
    u8g2Fonts.setFont(getTitleFont()); u8g2Fonts.setCursor(5, 18); u8g2Fonts.print("ČASOVAČ");
    nakresliStatusBar();
    display.drawFastHLine(0, 23, display.width(), fgColor());

    u8g2Fonts.setFont(getBigFont());
    int tw = u8g2Fonts.getUTF8Width(cas);
    u8g2Fonts.setCursor((display.width() - tw) / 2, 74);
    u8g2Fonts.print(cas);

    u8g2Fonts.setFont(getBodyFont());
    String stav;
    if (casovacZvoni)      stav = "ČAS VYPRŠEL!";
    else if (casovacBezi)  stav = "Běží...";
    else                   stav = "Připraveno (" + String(casovacMin) + " min)";
    tw = u8g2Fonts.getUTF8Width(stav.c_str());
    u8g2Fonts.setCursor((display.width() - tw) / 2, 100);
    u8g2Fonts.print(stav.c_str());

    if (casovacZvoni)      napoveda(5, 124, "Stiskni BTN0 / BTN21 = vypnout zvonění");
    else if (casovacBezi)  napoveda(5, 124, "BTN21 = stop | Drž BTN21 = zpět");
    else                   napoveda(5, 124, "BTN0=+1 min | BTN21=start | Drž BTN21=zpět");
  } while (display.nextPage());
}

// Zastaví zvonění a vrátí buzzery do klidu.
void casovacTicho() {
  casovacZvoni = false;
  buzzerStop();
}

// Start / stop odpočtu (nezastavuje zvonění — to řeší volající).
void casovacStartStop() {
  if (casovacBezi) {
    casovacBezi = false;
  } else {
    casovacBezi = true;
    casovacStartMs = millis();
    casovacPosledniSek = casovacMin * 60;
  }
  zobrazCasovac();
}

// Volá se z loop() — odpočet běží i mimo obrazovku časovače.
void casovacTick() {
  if (casovacZvoni) {
    unsigned long ted = millis();
    if (ted - casovacZvonLast >= 350) {
      casovacZvonLast = ted;
      casovacZvonFaze = !casovacZvonFaze;
      buzzTon(BUZZER1_PIN, casovacZvonFaze ? 1046 : 740);
      buzzTon(BUZZER2_PIN, casovacZvonFaze ? 1568 : 1109);
      ledcWrite(BUZZER1_PIN, 512);   // maximum (50 % při 10 bitech)
      ledcWrite(BUZZER2_PIN, 512);
    }
    return;
  }
  if (!casovacBezi) return;
  int zbyva = casovacZbyvaSek();
  if (zbyva <= 0) {
    casovacBezi = false;
    casovacZvoni = true;
    buzzerPriprav();
    casovacZvonLast = 0;
    casovacZvonFaze = false;
    appState = STATE_CASOVAC;      // přepnout na obrazovku budíku
    zobrazCasovac();
    return;
  }
  if (appState == STATE_CASOVAC && zbyva != casovacPosledniSek) {
    casovacPosledniSek = zbyva;
    nastavOblastPrekresleni(0, 26, display.width(), 84);
    zobrazCasovac();
  }
}

// ===================================================================
// KALKULAČKA — 18 kláves v mřížce 6×3
// Ovládání: encoder / D-pad = výběr, BTN21 = stisk, dlouhý BTN21 = zpět.
// Podporuje + − × ÷ a desetinná čísla (bez závorek).
// ===================================================================
static const char* kalkKlavesy[18] = {
  "7", "8", "9", "/", "C", "<-",
  "4", "5", "6", "*", ".", "=",
  "1", "2", "3", "-", "+", "0"
};
String kalkVstup = "";
String kalkVysledek = "";
int    kalkKurzor = 0;
bool   kalkPoVysledku = false;
bool   kalkChyba = false;

// Provede jednu operaci ze zásobníku (čísla + operátory).
static bool kalkProved(char op, double* cisla, int& cn) {
  if (cn < 2) return false;
  double b = cisla[--cn], a = cisla[--cn];
  double r = 0;
  if      (op == '+') r = a + b;
  else if (op == '-') r = a - b;
  else if (op == '*') r = a * b;
  else if (op == '/') { if (b == 0) return false; r = a / b; }
  else return false;
  cisla[cn++] = r;
  return true;
}

// Vyhodnotí výraz s prioritou operátorů (dva zásobníky).
static bool kalkSpocti(const String& vstup, double& vysl) {
  double cisla[24]; int cn = 0;
  char   oper[24];  int on = 0;
  int i = 0; bool predCislem = false;
  while (i < (int)vstup.length()) {
    char ch = vstup[i];
    if (ch == ' ') { i++; continue; }
    if ((ch >= '0' && ch <= '9') || ch == '.') {
      int start = i; bool tecka = false;
      while (i < (int)vstup.length() && ((vstup[i] >= '0' && vstup[i] <= '9') || vstup[i] == '.')) {
        if (vstup[i] == '.') { if (tecka) return false; tecka = true; }
        i++;
      }
      if (cn >= 24 || on >= 24) return false;
      cisla[cn++] = vstup.substring(start, i).toDouble();
      predCislem = true;
      continue;
    }
    if (ch == '+' || ch == '-' || ch == '*' || ch == '/') {
      if (!predCislem) return false;                 // dva operátory za sebou
      while (on > 0) {
        char pred = oper[on - 1];
        int p1 = (pred == '*' || pred == '/') ? 2 : 1;
        int p2 = (ch   == '*' || ch   == '/') ? 2 : 1;
        if (p1 < p2) break;
        if (!kalkProved(pred, cisla, cn)) return false;
        on--;
      }
      oper[on++] = ch;
      predCislem = false;
      i++;
      continue;
    }
    return false;
  }
  if (!predCislem) return false;
  while (on > 0) { if (!kalkProved(oper[--on], cisla, cn)) return false; }
  if (cn != 1) return false;
  vysl = cisla[0];
  return true;
}

static String kalkFormat(double v) {
  double cela = (double)(long)v;
  if (v > -1e9 && v < 1e9 && fabs(v - cela) < 1e-9) {
    char b[24]; snprintf(b, sizeof(b), "%ld", (long)v); return String(b);
  }
  String s = String(v, 6);
  while (s.endsWith("0")) s.remove(s.length() - 1);
  if (s.endsWith(".")) s.remove(s.length() - 1);
  return s;
}

// Vloží operátor na konec výrazu (řeší začátek výrazu i dvojí operátory).
static void kalkVlozOper(char op) {
  if (kalkChyba) { kalkVstup = ""; kalkVysledek = ""; kalkChyba = false; }
  if (kalkPoVysledku) { kalkVstup = kalkVysledek; kalkVysledek = ""; kalkPoVysledku = false; }
  if (kalkVstup.length() == 0) return;
  char posl = kalkVstup[kalkVstup.length() - 1];
  if (posl == '+' || posl == '-' || posl == '*' || posl == '/')
    kalkVstup.remove(kalkVstup.length() - 1);
  if (kalkVstup.length() > 0 && kalkVstup.length() < 30) kalkVstup += op;
}

void kalkStisk() {
  const char* k = kalkKlavesy[kalkKurzor];
  if (strcmp(k, "C") == 0) {
    kalkVstup = ""; kalkVysledek = ""; kalkChyba = false; kalkPoVysledku = false;
  } else if (strcmp(k, "<-") == 0) {
    if (kalkPoVysledku) { kalkVstup = ""; kalkVysledek = ""; kalkPoVysledku = false; kalkChyba = false; }
    else if (kalkVstup.length() > 0) kalkVstup.remove(kalkVstup.length() - 1);
  } else if (strcmp(k, "=") == 0) {
    if (kalkVstup.length() > 0) {
      double v = 0;
      if (kalkSpocti(kalkVstup, v)) { kalkVysledek = kalkFormat(v); kalkChyba = false; }
      else { kalkVysledek = "Chyba"; kalkChyba = true; }
      kalkPoVysledku = true;
    }
  } else if (strcmp(k, "/") == 0) kalkVlozOper('/');
  else if (strcmp(k, "*") == 0)   kalkVlozOper('*');
  else if (strcmp(k, "-") == 0)   kalkVlozOper('-');
  else if (strcmp(k, "+") == 0)   kalkVlozOper('+');
  else {
    // číslice a desetinná tečka
    if (kalkChyba || kalkPoVysledku) { kalkVstup = ""; kalkVysledek = ""; kalkChyba = false; kalkPoVysledku = false; }
    if (kalkVstup.length() < 30) kalkVstup += k;
  }
  zobrazKalkulacka();
}

void zobrazKalkulacka() {
  beginUiFrame(true);
  do {
    display.fillScreen(bgColor());
    u8g2Fonts.setFontMode(1);
    u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
    u8g2Fonts.setFont(getSmallFont()); u8g2Fonts.setCursor(5, 14); u8g2Fonts.print("KALKULAČKA");
    nakresliStatusBar();
    display.drawFastHLine(0, 20, display.width(), fgColor());

    // Řádek se zadaným výrazem (vpravo)
    u8g2Fonts.setFont(getBodyFont());
    String vstup = (kalkVstup.length() > 0) ? kalkVstup : "0";
    int tw = u8g2Fonts.getUTF8Width(vstup.c_str());
    u8g2Fonts.setCursor(display.width() - tw - 6, 40);
    u8g2Fonts.print(vstup.c_str());

    // Výsledek (menším písmem pod výrazem)
    u8g2Fonts.setFont(getSmallFont());
    String vys = kalkVysledek;
    tw = u8g2Fonts.getUTF8Width(vys.c_str());
    u8g2Fonts.setCursor(display.width() - tw - 6, 56);
    u8g2Fonts.print(vys.c_str());

    // Klávesnice 6 × 3
    const int kw = 48, kh = 17, x0 = 3, y0 = 60;
    for (int i = 0; i < 18; i++) {
      int col = i % 6, row = i / 6;
      int x = x0 + col * (kw + 1);
      int y = y0 + row * (kh + 1);
      if (i == kalkKurzor) {
        display.fillRect(x, y, kw, kh, fgColor());
        u8g2Fonts.setForegroundColor(bgColor()); u8g2Fonts.setBackgroundColor(fgColor());
      } else {
        display.drawRect(x, y, kw, kh, fgColor());
        u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
      }
      int w = u8g2Fonts.getUTF8Width(kalkKlavesy[i]);
      u8g2Fonts.setCursor(x + (kw - w) / 2, y + 12);
      u8g2Fonts.print(kalkKlavesy[i]);
    }
    u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
    napoveda(5, 126, "Kolečko/D-pad = výběr | BTN21 = stisk | Drž BTN21 = zpět");
  } while (display.nextPage());
}

// ===================================================================
// TEST ROTAČNÍHO ENCODERU (kvadraturní dekódování přes přerušení)
// Piny: A=ENC_A_PIN, B=ENC_B_PIN, tlačítko=ENC_SW_PIN (vše INPUT_PULLUP).
// ===================================================================
volatile int     encPozice = 0;        // dekódovaná pozice
volatile uint8_t encStav   = 0;        // poslední stav (A<<1 | B)
int  encZobrPozice = 0;
bool encZobrStisk  = false;
bool encInit = false;
int  encKrokPosledni = 0;              // poslední zpracovaný krok (globální ovládání menu)
unsigned long encTlacitkoCas = 0;      // čas posledního stisku/puštění tlačítka encoderu

// Přechodová tabulka kvadratury: index = (predchozi<<2)|novy, hodnota = ±1 / 0.
static const int8_t encTab[16] = { 0, -1, 1, 0,  1, 0, 0, -1,  -1, 0, 0, 1,  0, 1, -1, 0 };

void IRAM_ATTR encISR() {
  uint8_t s = (digitalRead(ENC_A_PIN) << 1) | digitalRead(ENC_B_PIN);
  encPozice += encTab[(encStav << 2) | s];
  encStav = s;
}

// Globální inicializace encoderu — přerušení běží po celou dobu, aby kolečko
// fungovalo v menu i v klávesnicích (ne jen v testu).
void encGlobalInit() {
  pinMode(ENC_A_PIN, INPUT_PULLUP);
  pinMode(ENC_B_PIN, INPUT_PULLUP);
  pinMode(ENC_SW_PIN, INPUT_PULLUP);
  encStav = (digitalRead(ENC_A_PIN) << 1) | digitalRead(ENC_B_PIN);
  attachInterrupt(digitalPinToInterrupt(ENC_A_PIN), encISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_B_PIN), encISR, CHANGE);
  encInit = true;
}

void encTestStart() {
  encPozice = 0; encZobrPozice = 0; encKrokPosledni = 0;
  encZobrStisk = false;
  encInit = true;
}

void encTestStop() {
  // Přerušení necháváme zapnutá — encoder teď ovládá i menu mimo test.
}

void zobrazEncoderTest() {
  char b[40];
  beginUiFrame(true);
  do {
    display.fillScreen(bgColor());
    u8g2Fonts.setFontMode(1);
    u8g2Fonts.setFont(getTitleFont()); u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
    u8g2Fonts.setCursor(5, 18); u8g2Fonts.print("ENCODER TEST");
    nakresliStatusBar();
    display.drawFastHLine(0, 23, display.width(), fgColor());
    u8g2Fonts.setFont(getBigFont());
    snprintf(b, sizeof(b), "%d", encZobrPozice);
    int tw = u8g2Fonts.getUTF8Width(b);
    u8g2Fonts.setCursor((display.width() - tw) / 2, 72);
    u8g2Fonts.print(b);
    u8g2Fonts.setFont(getTitleFont());
    u8g2Fonts.setCursor(5, 100);
    u8g2Fonts.print(encZobrStisk ? "Tlacitko: STISKNUTO" : "Tlacitko: volno");
    u8g2Fonts.setFont(getSmallFont());
    napoveda(5, 124, "BTN0=reset | drz=zpet");
  } while (display.nextPage());
}

// ===================================================================
// TEST KAPACITNÍCH TOUCH PADŮ (podomácku vyrobené), 4 zóny = D-pad
// Piny: TOUCH_PINY = GPIO10(NAHORU), GPIO11(DOLU), GPIO12(DOLEVA), GPIO14(DOPRAVA).
// Na ESP32-S3 platí: vyšší surová hodnota = větší kapacita = DOTYK.
// ===================================================================
// Medián ze 3 čtení — potlačí jednorázové šumové špičky (levá ploška je náchylná)
static uint32_t touchCti(uint8_t pin) {
  uint32_t a = touchRead(pin), b = touchRead(pin), c = touchRead(pin);
  uint32_t mx = (a > b) ? ((a > c) ? a : c) : ((b > c) ? b : c);
  uint32_t mn = (a < b) ? ((a < c) ? a : c) : ((b < c) ? b : c);
  return a + b + c - mx - mn;
}

void touchKalibruj() {
  for (int p = 0; p < TOUCH_PIN_COUNT; p++) {
    // první čtení inicializuje touch kanál a bývá nepřesné → zahodit
    touchRead(TOUCH_PINY[p]); delay(20);
    touchRead(TOUCH_PINY[p]); delay(20);
    const int N = 15;
    uint32_t vz[N];
    for (int i = 0; i < N; i++) { vz[i] = touchCti(TOUCH_PINY[p]); delay(4); }
    // medián (odolný proti špičkám nahoru i dolů)
    for (int i = 0; i < N; i++)
      for (int j = i + 1; j < N; j++)
        if (vz[j] < vz[i]) { uint32_t t = vz[i]; vz[i] = vz[j]; vz[j] = t; }
    uint32_t med = vz[N / 2];
    // Odhad šumu = největší odchylka od mediánu. Práh = 3× šum, ale aspoň 300
    // (u levé plošky je skok při dotyku malý, jen ~540).
    uint32_t sum = 0;
    for (int i = 0; i < N; i++) {
      uint32_t dd = (vz[i] > med) ? (vz[i] - med) : (med - vz[i]);
      if (dd > sum) sum = dd;
    }
    uint32_t prah = sum * 3;
    if (prah < 300)  prah = 300;
    if (prah > 1600) prah = 1600;
    touchBaseA[p] = med;
    touchNowA[p]  = med;
    touchPrahA[p] = prah;
    touchPeakA[p] = 0;
    touchDotyA[p] = false;
  }
  touchLast = 0; touchLastPoll = 0;
}

// Vykreslí šipku v kříži. dir: 0=NAHORU, 1=DOLU, 2=DOLEVA, 3=DOPRAVA.
// plna=true → vyplněná černě (dotyk), jinak jen obrys.
void sipka(int cx, int cy, int dir, bool plna) {
  uint16_t fg = fgColor(), bg = bgColor();
  uint16_t fill = plna ? fg : bg;
  if (dir == 0) {          // nahoru
    display.fillTriangle(cx, cy - 14, cx - 11, cy + 1, cx + 11, cy + 1, fill);
    display.drawTriangle(cx, cy - 14, cx - 11, cy + 1, cx + 11, cy + 1, fg);
    display.fillRect(cx - 4, cy + 1, 8, 12, fill);
    display.drawRect(cx - 4, cy + 1, 8, 12, fg);
  } else if (dir == 1) {   // dolu
    display.fillTriangle(cx, cy + 14, cx - 11, cy - 1, cx + 11, cy - 1, fill);
    display.drawTriangle(cx, cy + 14, cx - 11, cy - 1, cx + 11, cy - 1, fg);
    display.fillRect(cx - 4, cy - 13, 8, 12, fill);
    display.drawRect(cx - 4, cy - 13, 8, 12, fg);
  } else if (dir == 2) {   // doleva
    display.fillTriangle(cx - 14, cy, cx + 1, cy - 11, cx + 1, cy + 11, fill);
    display.drawTriangle(cx - 14, cy, cx + 1, cy - 11, cx + 1, cy + 11, fg);
    display.fillRect(cx + 1, cy - 4, 12, 8, fill);
    display.drawRect(cx + 1, cy - 4, 12, 8, fg);
  } else {                 // doprava
    display.fillTriangle(cx + 14, cy, cx - 1, cy - 11, cx - 1, cy + 11, fill);
    display.drawTriangle(cx + 14, cy, cx - 1, cy - 11, cx - 1, cy + 11, fg);
    display.fillRect(cx - 13, cy - 4, 12, 8, fill);
    display.drawRect(cx - 13, cy - 4, 12, 8, fg);
  }
}

void zobrazTouchTest() {
  char b[40];
  beginUiFrame(true);
  do {
    display.fillScreen(bgColor());
    u8g2Fonts.setFontMode(1);
    u8g2Fonts.setFont(getTitleFont()); u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
    u8g2Fonts.setCursor(5, 18); u8g2Fonts.print("TOUCH TEST");
    nakresliStatusBar();
    display.drawFastHLine(0, 23, display.width(), fgColor());

    // kříž se 4 šipkami — dotyk plošky šipku vyplní černě
    sipka(85, 48, 0, touchDotyA[0]);    // NAHORU
    sipka(85, 96, 1, touchDotyA[1]);    // DOLU
    sipka(60, 72, 2, touchDotyA[2]);    // DOLEVA
    sipka(110, 72, 3, touchDotyA[3]);   // DOPRAVA

    // vpravo: skok oproti klidu (delta) a práh — pro kontrolu, když se něco nedaří
    u8g2Fonts.setFont(getSmallFont());
    for (int p = 0; p < TOUCH_PIN_COUNT; p++) {
      long d = (long)touchNowA[p] - (long)touchBaseA[p];
      snprintf(b, sizeof(b), "%-7s %+5ld p%4lu", TOUCH_NAZVY[p], d, (unsigned long)touchPrahA[p]);
      u8g2Fonts.setCursor(140, 40 + p * 20); u8g2Fonts.print(b);
    }
    napoveda(5, 124, "BTN0=rekalib | drz=zpet");
  } while (display.nextPage());
}

// ===================================================================
// REÁLNÁ TĚŽBA — Stratum sólo klient (mainnet) — "loterijní tiket"
// Konfigurace v /eindata/tezba.txt:
//   pool=solo.ckpool.org
//   port=3333
//   worker=bc1...tvojeadresa[.nazev]
// POZOR: šance najít skutečný blok je astronomicky malá. Jde o loterii.
// ===================================================================
static int hexHod(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}
static bool hexDoBin(const String& h, uint8_t* out, size_t maxBytes) {
  size_t n = h.length() / 2; if (n > maxBytes) n = maxBytes;
  for (size_t i = 0; i < n; i++) { int a = hexHod(h[i * 2]), b = hexHod(h[i * 2 + 1]); if (a < 0 || b < 0) return false; out[i] = (uint8_t)((a << 4) | b); }
  return true;
}

// Načte konfiguraci poolu z SD.
bool tezbaNactiConfig() {
  tpPool = ""; tpWorker = ""; tpPort = 3333;
  if (!sdReady) return false;
  File f = SD.open("/eindata/tezba.txt");
  if (!f) return false;
  while (f.available()) {
    String l = f.readStringUntil('\n'); l.trim();
    if (l.length() == 0 || l.startsWith("#")) continue;
    int eq = l.indexOf('='); if (eq < 0) continue;
    String k = l.substring(0, eq); k.trim(); k.toLowerCase();
    String v = l.substring(eq + 1); v.trim();
    if (k == "pool") tpPool = v;
    else if (k == "port") tpPort = (uint16_t)v.toInt();
    else if (k == "worker") tpWorker = v;
  }
  f.close();
  return tpPool.length() > 0 && tpWorker.length() > 0;
}

// Vytáhne další JSON řetězec z textu od pozice pos.
static String tpToken(const String& s, int& pos) {
  int n = s.length();
  while (pos < n && (s[pos] == ' ' || s[pos] == ',' || s[pos] == '[' || s[pos] == ']' || s[pos] == '{' || s[pos] == '}')) pos++;
  if (pos >= n) return "";
  String out = "";
  if (s[pos] == '"') {
    pos++;
    while (pos < n && s[pos] != '"') { out += s[pos]; pos++; }
    pos++;
  } else {
    while (pos < n && s[pos] != ',' && s[pos] != ']' && s[pos] != '}') { out += s[pos]; pos++; }
    out.trim();
  }
  return out;
}

// Z nbits (hex) spočítá 32bajtový big-endian target.
static void targetZNBits(uint32_t nbits, uint8_t* t) {
  memset(t, 0, 32);
  uint32_t mant = nbits & 0x00FFFFFF;
  int exp = (nbits >> 24) & 0xFF;
  if (exp <= 3) { mant >>= (8 * (3 - exp)); t[28] = (uint8_t)(mant >> 24); t[29] = (uint8_t)(mant >> 16); t[30] = (uint8_t)(mant >> 8); t[31] = (uint8_t)mant; return; }
  int idx = 31 - (exp - 3);
  if (idx >= 0)   t[idx]     = (uint8_t)(mant);
  if (idx - 1 >= 0) t[idx - 1] = (uint8_t)(mant >> 8);
  if (idx - 2 >= 0) t[idx - 2] = (uint8_t)(mant >> 16);
}

// Porovná SHA256d výstup (interní pořadí) s reálným targetem sítě.
static bool hashPodTarget(const uint8_t* h) {
  for (int i = 0; i < 32; i++) {
    uint8_t hb = h[31 - i];
    if (hb < tpTarget[i]) return true;
    if (hb > tpTarget[i]) return false;
  }
  return true;
}

// Sestaví coinbase + merkle root + hlavu bloku (0..75) a target pro aktuální job.
static void tezbaZalozJob() {
  String en2 = ""; for (int i = 0; i < tpEn2Size; i++) en2 += "00";
  String cbHex = tpCoinb1 + tpExtranonce1 + en2 + tpCoinb2;
  static uint8_t cb[768];
  int cbLen = cbHex.length() / 2; if (cbLen > (int)sizeof(cb)) cbLen = sizeof(cb);
  if (!hexDoBin(cbHex, cb, sizeof(cb))) return;
  uint8_t cbHash[32];
  mbedtls_sha256(cb, cbLen, cbHash, 0);
  mbedtls_sha256(cbHash, 32, cbHash, 0);

  uint8_t merkle[32]; memcpy(merkle, cbHash, 32);
  for (int i = 0; i < tpMerkleCount; i++) {
    uint8_t br[32]; if (!hexDoBin(tpMerkle[i], br, 32)) continue;
    uint8_t cat[64]; memcpy(cat, merkle, 32); memcpy(cat + 32, br, 32);
    mbedtls_sha256(cat, 64, merkle, 0);
    mbedtls_sha256(merkle, 32, merkle, 0);
  }

  uint8_t t4[32];
  hexDoBin(tpVersion, t4, 4);
  tezbaHeader[0] = t4[3]; tezbaHeader[1] = t4[2]; tezbaHeader[2] = t4[1]; tezbaHeader[3] = t4[0];
  hexDoBin(tpPrevhash, t4, 32);
  for (int i = 0; i < 32; i++) tezbaHeader[4 + i] = t4[31 - i];   // prevhash: obrácené pořadí bajtů
  memcpy(tezbaHeader + 36, merkle, 32);
  hexDoBin(tpNtime, t4, 4);
  tezbaHeader[68] = t4[3]; tezbaHeader[69] = t4[2]; tezbaHeader[70] = t4[1]; tezbaHeader[71] = t4[0];
  hexDoBin(tpNbits, t4, 4);
  tezbaHeader[72] = t4[3]; tezbaHeader[73] = t4[2]; tezbaHeader[74] = t4[1]; tezbaHeader[75] = t4[0];

  targetZNBits((uint32_t)strtoul(tpNbits.c_str(), NULL, 16), tpTarget);
  tpNonce = 0; tpNejNuly = -1;
  for (int i = 0; i < 32; i++) tpNejHash[i] = 0;
}

// Pošle share/block na pool.
static void tezbaPosliSubmit(uint32_t nonce) {
  char nh[9]; sprintf(nh, "%08x", nonce);
  String en2 = ""; for (int i = 0; i < tpEn2Size; i++) en2 += "00";
  String j = "{\"id\":" + String(tpMsgId++) + ",\"method\":\"mining.submit\",\"params\":[\"" + tpWorker + "\",\"" + tpJobId + "\",\"" + en2 + "\",\"" + tpNtime + "\",\"" + String(nh) + "\"]}\n";
  tpClient.print(j);
  tpBloku++;
  tpStav = "ODESLAN KANDIDAT BLOKU!";
  Serial.println("TĚŽBA: submit nonce " + String(nh));
}

// Zpracuje jednu řádku odpovědi/notifikace z poolu.
static void tezbaStratumRadek(const String& line) {
  if (line.indexOf("\"method\":\"mining.notify\"") >= 0) {
    int p = line.indexOf("\"params\":["); if (p < 0) return;
    int pos = p + 10;
    tpJobId   = tpToken(line, pos);
    tpPrevhash = tpToken(line, pos);
    tpCoinb1  = tpToken(line, pos);
    tpCoinb2  = tpToken(line, pos);
    tpMerkleCount = 0;
    int b0 = line.indexOf('[', pos);
    int b1 = (b0 >= 0) ? line.indexOf(']', b0 + 1) : -1;
    if (b0 >= 0 && b1 > b0) {
      String inner = line.substring(b0 + 1, b1); int q = 0;
      while (tpMerkleCount < 16) { String s = tpToken(inner, q); if (s.length() == 0) break; tpMerkle[tpMerkleCount++] = s; }
      pos = b1 + 1;
    }
    tpVersion = tpToken(line, pos);
    tpNbits   = tpToken(line, pos);
    tpNtime   = tpToken(line, pos);
    tezbaZalozJob();
    tpStav = "Tezim (pool)";
    return;
  }
  if (line.indexOf("\"id\":1") >= 0 && line.indexOf("\"result\":[[") >= 0) {
    int q = line.indexOf("]],\"");
    if (q > 0) {
      int p2 = q + 4;
      tpExtranonce1 = tpToken(line, p2);
      String sz = tpToken(line, p2);
      tpEn2Size = sz.toInt(); if (tpEn2Size < 1 || tpEn2Size > 8) tpEn2Size = 4;
      tpClient.print("{\"id\":2,\"method\":\"mining.authorize\",\"params\":[\"" + tpWorker + "\",\"x\"]}\n");
    }
    return;
  }
  if (line.indexOf("\"id\":2") >= 0) {
    if (line.indexOf("\"result\":true") >= 0) { tpPrijhlasen = true; tpStav = "Prihlasen, cekam na job"; }
    else tpStav = "Autorizace selhala";
    return;
  }
  if (line.indexOf("\"error\":") >= 0 && line.indexOf("\"error\":null") < 0) {
    Serial.println("TĚŽBA chyba: " + line);
  }
}

void tezbaStratumCti() {
  while (tpClient.available()) { tpRx += (char)tpClient.read(); if (tpRx.length() > 8192) tpRx = tpRx.substring(tpRx.length() - 4096); }
  int nl;
  while ((nl = tpRx.indexOf('\n')) >= 0) {
    String line = tpRx.substring(0, nl); tpRx = tpRx.substring(nl + 1); line.trim();
    if (line.length() > 0) tezbaStratumRadek(line);
  }
}

bool tezbaStratumPripoj() {
  if (tpPool.length() == 0) { tpStav = "Chybi config"; return false; }
  if (WiFi.status() != WL_CONNECTED) {
    tpStav = "Pripojuji WiFi..."; zobrazTezbu();
    if (!pripojWiFi()) { tpStav = "WiFi se nepripojilo"; return false; }
  }
  tpClient.stop();
  tpStav = "Pripojuji pool..."; zobrazTezbu();
  if (!tpClient.connect(tpPool.c_str(), tpPort)) { tpStav = "Pool nedostupny"; return false; }
  tpRx = ""; tpPrijhlasen = false; tpExtranonce1 = ""; tpJobId = "";
  tpClient.print("{\"id\":1,\"method\":\"mining.subscribe\",\"params\":[\"HonzueINK/1.0\"]}\n");
  tpMsgId = 2; tpPosledniRx = millis(); tpStav = "Prihlasuji...";
  return true;
}

// Odhešuje jeden nonce pro aktuální job.
static void tezbaStratumHashuj() {
  tezbaHeader[76] = (uint8_t)(tpNonce);
  tezbaHeader[77] = (uint8_t)(tpNonce >> 8);
  tezbaHeader[78] = (uint8_t)(tpNonce >> 16);
  tezbaHeader[79] = (uint8_t)(tpNonce >> 24);
  uint8_t h1[32], h2[32];
  mbedtls_sha256(tezbaHeader, 80, h1, 0);
  mbedtls_sha256(h1, 32, h2, 0);
  int nuly = tezbaVedNul(h2);
  if (nuly > tpNejNuly) { tpNejNuly = nuly; memcpy(tpNejHash, h2, 32); }
  if (hashPodTarget(h2)) tezbaPosliSubmit(tpNonce);
  tpNonce++;
}

// Periodická obsluha reálné těžby (voláno z loop).
void tezbaStratumTick() {
  if (!tezbaBezi) return;
  if (!tpClient.connected()) {
    if (millis() - tpPosledniRetry > 5000) { tpPosledniRetry = millis(); tezbaStratumPripoj(); zobrazTezbu(); }
    return;
  }
  tezbaStratumCti();
  if (tpJobId.length() == 0) return;

  unsigned long t0 = millis();
  while (millis() - t0 < 500 && tezbaBezi) {
    for (int i = 0; i < 300 && tezbaBezi; i++) { tezbaStratumHashuj(); tezbaHashu++; }
    tezbaStratumCti();
    if (!tpClient.connected()) break;
  }
  tezbaHashMs += millis() - t0;
  zobrazTezbu();
}


// Načte gamebook uzel ze SD souboru (jeden uzel = jeden řádek ve formátu text|volbaA|volbaB|popisA|popisB).
void nactiGBNodeZSD(int node) {
  obnovAktivniDatoveSoubory();
  if (!sdReady) { gbZSD = false; return; }
  int lineIdx = node + (aktivniGamebookMaTitulek ? 1 : 0);
  String r = nactiRadekZSD(aktivniGamebookSoubor.c_str(), lineIdx);
  if (r.length() == 0) { gbZSD = false; return; }
  // Najdeme první 4 pozice znaku '|' (text neobsahuje '|')
  int p[4] = {-1, -1, -1, -1}; int cnt = 0;
  for (int i = 0; i < (int)r.length() && cnt < 4; i++) {
    if (r[i] == '|') p[cnt++] = i;
  }
  if (cnt < 4) { gbZSD = false; return; }
  gbTextSD   = _textNaUtf8(r.substring(0, p[0]));
  gbVolbaASD = r.substring(p[0] + 1, p[1]).toInt();
  gbVolbaBSD = r.substring(p[1] + 1, p[2]).toInt();
  gbPopisASD = _textNaUtf8(r.substring(p[2] + 1, p[3]));
  gbPopisBSD = _textNaUtf8(r.substring(p[3] + 1));
  gbZSD = true;
}

void zobrazGBNode() {
  nactiGBNodeZSD(gbNode);
  char buf[600]; char popisA[60], popisB[60]; int volbaA, volbaB;
  if (gbZSD) {
    strncpy(buf, gbTextSD.c_str(), sizeof(buf) - 1); buf[sizeof(buf) - 1] = '\0';
    strncpy(popisA, gbPopisASD.c_str(), sizeof(popisA) - 1); popisA[sizeof(popisA) - 1] = '\0';
    strncpy(popisB, gbPopisBSD.c_str(), sizeof(popisB) - 1); popisB[sizeof(popisB) - 1] = '\0';
    volbaA = gbVolbaASD; volbaB = gbVolbaBSD;
  } else {
    GBNode node; memcpy_P(&node, &gamebook[gbNode], sizeof(GBNode));
    strncpy_P(buf, (const char*)node.text, sizeof(buf) - 1); buf[sizeof(buf) - 1] = '\0';
    strncpy_P(popisA, (const char*)node.popisA, sizeof(popisA) - 1); popisA[sizeof(popisA) - 1] = '\0';
    strncpy_P(popisB, (const char*)node.popisB, sizeof(popisB) - 1); popisB[sizeof(popisB) - 1] = '\0';
    memcpy_P(&volbaA, &gamebook[gbNode].volbaA, sizeof(int)); memcpy_P(&volbaB, &gamebook[gbNode].volbaB, sizeof(int));
  }
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

OdhalObrazek nactiAktualniOdhalovacku() {
  OdhalObrazek o;
  memcpy_P(&o, &odhalovackaData[odhalovackaIndex], sizeof(OdhalObrazek));
  return o;
}

void drawOdhalBitmap(int x, int y, const unsigned char *bitmap, uint16_t color, int scale, bool masked) {
  // bitmap je 8×8 (8 bajtů), každý bit = jeden blok hádanky
  for (int j = 0; j < 8; j++) {
    unsigned char row = pgm_read_byte(bitmap + j);
    for (int i = 0; i < 8; i++) {
      int blockIdx = j * 8 + i;
      if (masked && !odhalenoPole[blockIdx]) continue;
      if (row & (0x80 >> i)) {
        display.fillRect(x + i * scale, y + j * scale, scale, scale, color);
      }
    }
  }
}

void zobrazOdhalovacku() {
  OdhalObrazek o = nactiAktualniOdhalovacku();
  int scale = 8;
  int imgX = (display.width() - 8 * scale) / 2;
  int imgY = 30;
  display.setPartialWindow(0, 0, display.width(), display.height());
  display.firstPage();
  do {
    display.fillScreen(bgColor()); u8g2Fonts.setFontMode(1); nakresliStatusBar();
    u8g2Fonts.setFont(getTitleFont()); u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
    u8g2Fonts.setCursor(5, 16);
    String tit = "ODHALOVAČKA " + String(odhalovackaIndex + 1) + "/" + String(ODHALOVACKA_POCET);
    u8g2Fonts.print(tit.c_str());
    u8g2Fonts.setCursor(235, 16);
    u8g2Fonts.print(("Sk:" + String(odhalovackaSkore)).c_str());
    display.drawFastHLine(0, 20, display.width(), fgColor());

    drawOdhalBitmap(imgX, imgY, o.bitmap, fgColor(), scale, true);

    u8g2Fonts.setFont(getSmallFont());
    napoveda(5, 124, "BTN21 = Vím to! | BTN0 = Zpět");
  } while (display.nextPage());
}

void zobrazOdhalovackuDetail() {
  OdhalObrazek o = nactiAktualniOdhalovacku();
  char odpBuf[40];
  strncpy_P(odpBuf, o.odpoved, 39); odpBuf[39] = '\0';
  int scale = 8;
  int imgX = 15, imgY = 30;
  display.setPartialWindow(0, 0, display.width(), display.height());
  display.firstPage();
  do {
    display.fillScreen(bgColor()); u8g2Fonts.setFontMode(1); nakresliStatusBar();
    drawOdhalBitmap(imgX, imgY, o.bitmap, fgColor(), scale, false);

    u8g2Fonts.setFont(getBigFont());
    u8g2Fonts.setForegroundColor(fgColor());
    u8g2Fonts.setCursor(100, 70);
    u8g2Fonts.print(odpBuf);

    u8g2Fonts.setFont(getSmallFont());
    napoveda(5, 124, "BTN21=další | Drž BTN0=zpět");
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

String aktualniSlovnikCesta() {
  return String("/eindata/slovnik/") + slovnikLangSoubory[slovnikLangIdx];
}

bool parseSlovnikPair(const String& lineRaw, String& left, String& right) {
  String line = lineRaw;
  line.trim();
  int sep = line.indexOf('|');
  if (sep <= 0 || sep >= line.length() - 1) return false;
  left = line.substring(0, sep);
  right = line.substring(sep + 1);
  left.trim(); right.trim();
  return left.length() > 0 && right.length() > 0;
}

void zobrazHledani() {
  String vysledky[10];
  int shodCount = 0;
  String hledaneStr = String(hledanyText); hledaneStr.toLowerCase();
  File f = SD.open(aktualniSlovnikCesta().c_str());
  if (f) {
    while (f.available() && shodCount < 10) {
      String left, right;
      if (!parseSlovnikPair(f.readStringUntil('\n'), left, right)) continue;
      String src = (hledanySmer == 0) ? left : right;
      String dst = (hledanySmer == 0) ? right : left;
      String cistyZdroj = odstranDiakritiku(src); cistyZdroj.toLowerCase();
      if (hledanyLen > 0 && cistyZdroj.startsWith(hledaneStr)) {
        vysledky[shodCount++] = src + " = " + dst;
      }
    }
    f.close();
  } else if (slovnikLangIdx == 0) {
    // Fallback: pokud neni SD soubor pro Aj, hledame ve vestavenem poli v PROGMEM
    int totalWords = sizeof(slovnikData) / sizeof(slovnikData[0]);
    for (int i = 0; i < totalWords && shodCount < 10; i++) {
        SlovnikSlovo s; 
        memcpy_P(&s, &slovnikData[i], sizeof(SlovnikSlovo));
        char bufCz[40], bufEn[40];
        strncpy_P(bufCz, s.cz, 39); bufCz[39] = '\0';
        strncpy_P(bufEn, s.en, 39); bufEn[39] = '\0';
        String left = String(bufCz); String right = String(bufEn);
        String src = (hledanySmer == 0) ? left : right;
        String dst = (hledanySmer == 0) ? right : left;
        String cistyZdroj = odstranDiakritiku(src); cistyZdroj.toLowerCase();
        if (hledanyLen > 0 && cistyZdroj.startsWith(hledaneStr)) {
          vysledky[shodCount++] = src + " = " + dst;
        }
    }
  }
  beginUiFrame(true);
  do {
    display.fillScreen(bgColor()); u8g2Fonts.setFontMode(1); nakresliStatusBar();
    u8g2Fonts.setFont(getBodyFont()); u8g2Fonts.setCursor(5, 16); u8g2Fonts.print(hledanySmer == 0 ? "HLEDAT (CZ): " : "HLEDAT (JZ): ");
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
        u8g2Fonts.setCursor(5, y); u8g2Fonts.print(vysledky[i].c_str()); y += 10;
      }
    }
    u8g2Fonts.setFont(getSmallFont()); napoveda(5, 125, "BTN21 = zvolit znak | BTN0 = hledat | Drž BTN21 = zpět");
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
      char cz[80], en[80]; strncpy_P(cz, (const char*)f.cz, sizeof(cz) - 1); cz[sizeof(cz) - 1] = '\0'; strncpy_P(en, (const char*)f.en, sizeof(en) - 1); en[sizeof(en) - 1] = '\0';
      u8g2Fonts.setFont(getBodyFont()); u8g2Fonts.setCursor(5, y); u8g2Fonts.print(cz); y += 11;
      u8g2Fonts.setFont(getSmallFont()); u8g2Fonts.setCursor(15, y); u8g2Fonts.print(en); y += 14;
    }
  } while (display.nextPage());
}

void zobrazUceni() {
  String pair = nactiNahodnyRadek(aktualniSlovnikCesta().c_str());
  String left = "", right = "";
  if (!parseSlovnikPair(pair, left, right)) { left = "slovnik"; right = "prazdny"; }
  const char* otazka = (uceniSmer == 0) ? left.c_str() : right.c_str();
  const char* odpoved = (uceniSmer == 0) ? right.c_str() : left.c_str();
  const char* smerText = (uceniSmer == 0) ? "CZ -> JAZYK" : "JAZYK -> CZ";
  
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
    if (uceniOdhaleno) { napoveda(5, 124, "BTN21 = další slovo | BTN0 = změna směru | Drž = zpět"); } 
    else { napoveda(5, 124, "BTN21 = odhalit | BTN0 = změna směru | Drž = zpět"); }
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
    napoveda(10, 80, "Klikni = přepnout"); napoveda(10, 105, "Drž = zpět");
  } while (display.nextPage());
}

void zobrazRefreshToast() {
  const char* refreshNazvy[] = {"Střední (partial)", "Rychlý (fast)"};
  beginUiFrame(true);
  do {
    display.fillScreen(bgColor());
    u8g2Fonts.setFontMode(1);
    u8g2Fonts.setForegroundColor(fgColor());
    u8g2Fonts.setBackgroundColor(bgColor());
    printCentered("REFRESH", 45, getTitleFont());
    printCentered(String(refreshNazvy[refreshMode]), 75, getBodyFont());
  } while (display.nextPage());
  delay(1500);
}

// ==== SD PROHLÍŽEČ SOUBORŮ ====
String formatFileSize(uint32_t bytes) {
  if (bytes < 1024) return String(bytes) + " B";
  else if (bytes < 1024UL * 1024UL) return String(bytes / 1024) + " KB";
  else return String(bytes / (1024UL * 1024UL)) + " MB";
}

void nactiSDSlozku() {
  sdEntryCount = 0;
  sdBrowserIndex = 0;
  sdBrowserScrollOffset = 0;
  if (!sdReady) return;
  File dir = SD.open(sdCurrentPath.c_str());
  if (!dir || !dir.isDirectory()) { if (dir) dir.close(); return; }
  if (sdCurrentPath != "/") {
    strncpy(sdEntries[sdEntryCount].name, "..", sizeof(sdEntries[0].name) - 1);
    sdEntries[sdEntryCount].name[sizeof(sdEntries[0].name) - 1] = '\0';
    sdEntries[sdEntryCount].isDir = true;
    sdEntries[sdEntryCount].size = 0;
    sdEntryCount++;
  }
  File entry = dir.openNextFile();
  while (entry && sdEntryCount < 50) {
    const char* fname = entry.name();
    const char* lastSlash = strrchr(fname, '/');
    if (lastSlash) fname = lastSlash + 1;
    strncpy(sdEntries[sdEntryCount].name, fname, sizeof(sdEntries[0].name) - 1);
    sdEntries[sdEntryCount].name[sizeof(sdEntries[0].name) - 1] = '\0';
    sdEntries[sdEntryCount].isDir = entry.isDirectory();
    sdEntries[sdEntryCount].size = entry.size();
    sdEntryCount++;
    entry.close();
    entry = dir.openNextFile();
  }
  if (entry) entry.close();
  dir.close();
}

void zobrazSDProhlizec() {
  int itemH = getMenuItemHeight();
  int visible = getVisibleMenuItems();
  display.setPartialWindow(0, 0, display.width(), display.height());
  display.firstPage();
  do {
    display.fillScreen(bgColor());
    u8g2Fonts.setFontMode(1);
    u8g2Fonts.setFont(getTitleFont()); u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
    String pathLabel = "SD: " + sdCurrentPath;
    if (pathLabel.length() > 20) pathLabel = "SD: ..." + sdCurrentPath.substring(sdCurrentPath.length() - 15);
    u8g2Fonts.setCursor(5, 18); u8g2Fonts.print(pathLabel.c_str());
    nakresliStatusBar();
    display.drawFastHLine(0, 23, display.width(), fgColor());
    if (!sdReady) {
      u8g2Fonts.setFont(getBodyFont()); u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
      u8g2Fonts.setCursor(5, 60); u8g2Fonts.print("SD karta není dostupná!");
      u8g2Fonts.setFont(getSmallFont()); napoveda(5, 125, "Drž BTN21 = zpět");
    } else if (sdEntryCount == 0) {
      u8g2Fonts.setFont(getBodyFont()); u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
      u8g2Fonts.setCursor(5, 60); u8g2Fonts.print("(prázdná složka)");
      u8g2Fonts.setFont(getSmallFont()); napoveda(5, 125, "Drž BTN21 = zpět");
    } else {
      u8g2Fonts.setFont(getBodyFont());
      for (int i = 0; i < visible; i++) {
        int idx = i + sdBrowserScrollOffset;
        if (idx >= sdEntryCount) break;
        int y = 28 + itemH + (i * itemH);
        if (idx == sdBrowserIndex) {
          display.fillRect(0, y - itemH + 4, display.width(), itemH, fgColor());
          u8g2Fonts.setForegroundColor(bgColor()); u8g2Fonts.setBackgroundColor(fgColor());
        } else {
          u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
        }
        String label;
        if (sdEntries[idx].isDir) {
          label = String("[D] ") + sdEntries[idx].name;
        } else {
          label = String("[F] ") + sdEntries[idx].name + " (" + formatFileSize(sdEntries[idx].size) + ")";
        }
        u8g2Fonts.setCursor(5, y); u8g2Fonts.print(label.c_str());
      }
      u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
      u8g2Fonts.setFont(getSmallFont());
      napoveda(5, 125, "BTN0=pohyb | BTN21=vstup | Drž=zpět");
    }
  } while (display.nextPage());
}

// ==== BMP PROHLÍŽEČ — ZOBRAZENÍ 1-BITOVÉHO BMP NA E-INK ====
// Maximální velikost BMP dat v bajtech (296×128 1-bit = 5120 B + zarovnání)
#define BMP_MAX_BUFFER_SIZE 6144
bool zobrazBmpZSD(const char* cesta) {
  File f = SD.open(cesta);
  if (!f) return false;

  uint8_t header[54];
  if (f.read(header, 54) < 54) { f.close(); return false; }
  if (header[0] != 'B' || header[1] != 'M') { f.close(); return false; }

  uint32_t dataOffset = (uint32_t)header[10] | ((uint32_t)header[11] << 8) |
                        ((uint32_t)header[12] << 16) | ((uint32_t)header[13] << 24);
  int32_t bmpW = (int32_t)(header[18] | ((uint32_t)header[19] << 8) |
                            ((uint32_t)header[20] << 16) | ((uint32_t)header[21] << 24));
  int32_t bmpH = (int32_t)(header[22] | ((uint32_t)header[23] << 8) |
                            ((uint32_t)header[24] << 16) | ((uint32_t)header[25] << 24));
  uint16_t bpp = header[28] | ((uint16_t)header[29] << 8);

  if (bpp != 1 || bmpW <= 0) { f.close(); return false; }

  bool flipped = (bmpH > 0);
  int32_t absH = flipped ? bmpH : -bmpH;

  int rowBytes = ((bmpW + 31) / 32) * 4;
  long totalBytes = (long)rowBytes * absH;
  if (totalBytes > BMP_MAX_BUFFER_SIZE) { f.close(); return false; } // BMP příliš velký

  static uint8_t bmpBuf[BMP_MAX_BUFFER_SIZE];
  f.seek(dataOffset);
  f.read(bmpBuf, (size_t)totalBytes);
  f.close();

  int dispW = min(bmpW, (int32_t)display.width());
  int dispH = min(absH, (int32_t)display.height());

  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(bgColor());
    for (int y = 0; y < dispH; y++) {
      int bmpRow = flipped ? (absH - 1 - y) : y;
      uint8_t* row = bmpBuf + (long)bmpRow * rowBytes;
      for (int x = 0; x < dispW; x++) {
        bool pixel = (row[x / 8] >> (7 - (x & 7))) & 1;
        if (pixel) display.drawPixel(x, y, fgColor());
      }
    }
    u8g2Fonts.setFontMode(1); u8g2Fonts.setFont(getSmallFont());
    u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
    napoveda(5, 125, "Drz BTN21 = zpet");
  } while (display.nextPage());

  return true;
}

// ==== TLAČÍTKA A NÁVRATY ====
bool longPress() { unsigned long start = millis(); while (digitalRead(BTN_SELECT) == LOW) { if (millis() - start > 500) { while (digitalRead(BTN_SELECT) == LOW) delay(10); return true; } delay(10); } return false; }
bool longPressBTN0() { unsigned long start = millis(); while (digitalRead(BTN_DOWN) == LOW) { if (millis() - start > 500) { while (digitalRead(BTN_DOWN) == LOW) delay(10); return true; } delay(10); } return false; }
void menuDown(int& idx, int& scr, int count) { int visible = getVisibleMenuItems(); idx = (idx + 1) % count; if (idx >= scr + visible) scr++; if (idx == 0) scr = 0; gPosunKurzoru = true; }
// Posun nahoru: kurzor o jeden zpět (s obalením na konec).
// Okno se posouvá jen tehdy, když kurzor vyjede z viditelné oblasti —
// při cestě nahoru uvnitř okna tak lišta normálně odjíždí ode dna.
void menuUp(int& idx, int& scr, int count) {
  if (count <= 1) return;
  int visible = getVisibleMenuItems();
  idx = (idx - 1 + count) % count;
  if (idx < scr) scr = idx;                                // nahoru nad okno → posun okna
  else if (idx >= scr + visible) scr = idx - visible + 1;  // obalení na konec seznamu
  if (scr < 0) scr = 0;
  gPosunKurzoru = true;
}
int scrollForIdx(int idx) { int v = getVisibleMenuItems(); return (idx >= v) ? idx - v + 1 : 0; }

void goBack() {
  switch (appState) {
    case STATE_KNIHOVNA_DETAIL: ulozPoziciKnihy(subMenuIndex, textScrollPage); appState = STATE_KNIHOVNA; zobrazSubMenu("KNIHOVNA", dynamicKnihovnaItems, dynamicKnihovnaCount, subMenuIndex, subScrollOffset); break;
    case STATE_KNIHOVNA: appState = STATE_MAIN_MENU; zobrazSubMenu("HLAVNÍ MENU", mainMenuItems, mainMenuCount, menuIndex, scrollOffset); break;
    
    case STATE_ZPRAVY_TEXT: appState = STATE_ZPRAVY_SEZNAM; zobrazSeznamZprav(); break;
    case STATE_ZPRAVY_SEZNAM:
      clanekMenuIndex = 0;
      if (zpravyZHledani) {
        zpravyZHledani = false;
        appState = STATE_ARCHIV_DATUMY;
        archivDatumIndex = 0; archivDatumScrollOffset = 0;
        zobrazSubMenu("ARCHIV ZPRÁV", archivDatumy, archivDatumCount, 0, 0);
      } else if (zpravyZArchivu) {
        appState = STATE_ARCHIV_ZPRAVY_MENU;
        zobrazSubMenu(("ARCHIV: " + archivAktualniDatum).c_str(), zpravyMenuItems, 4, archivZpravySubIndex, archivZpravyScrollOffset);
      } else {
        appState = STATE_ZPRAVY_MENU; zobrazSubMenu("ZPRÁVY", zpravyMenuItems, zpravyMenuCount, subMenuIndex, subScrollOffset);
      }
      break;
    case STATE_ARCHIV_HLEDANI:
      appState = STATE_ARCHIV_DATUMY;
      archivDatumIndex = 0; archivDatumScrollOffset = 0;
      zobrazSubMenu("ARCHIV ZPRÁV", archivDatumy, archivDatumCount, 0, 0);
      break;
    case STATE_ZPRAVY_MENU: appState = STATE_AKTUALITY; subMenuIndex = 0; subScrollOffset = 0; zobrazSubMenu("AKTUALITY", aktualityItems, aktualityCount, subMenuIndex, subScrollOffset); break;
    case STATE_POCASI_UI: pocasiDen = 0; appState = STATE_AKTUALITY; zobrazSubMenu("AKTUALITY", aktualityItems, aktualityCount, subMenuIndex, subScrollOffset); break;
    case STATE_HODINY: case STATE_KURZY: appState = STATE_AKTUALITY; zobrazSubMenu("AKTUALITY", aktualityItems, aktualityCount, subMenuIndex, subScrollOffset); break;
    case STATE_JR_DETAIL: appState = STATE_JR_SEZNAM; if (jrCount > 0) zobrazSubMenu("SPOJE", jrItems, jrCount, subMenuIndex, subScrollOffset); break;
    case STATE_JR_SEZNAM: appState = STATE_AKTUALITY; subMenuIndex = 5; subScrollOffset = scrollForIdx(5); zobrazSubMenu("AKTUALITY", aktualityItems, aktualityCount, subMenuIndex, subScrollOffset); break;
    case STATE_KALENDAR_DEN:
      if (kalDetailZMesice) {
        struct tm t;
        if (kalDenTM(kalDetailOffset, t)) {
          kalMesic = t.tm_mon + 1; kalRok = t.tm_year + 1900; kalMesicDen = t.tm_mday;
        }
        appState = STATE_KALENDAR_MESIC; zobrazKalMesic();
      } else {
        appState = STATE_KALENDAR_TYDEN; zobrazKalTyden();
      }
      break;
    case STATE_KALENDAR_TYDEN: appState = STATE_KALENDAR_MENU; subMenuIndex = 0; subScrollOffset = 0; zobrazSubMenu("KALENDÁŘ", kalendarMenuItems, kalendarMenuCount, 0, 0); break;
    case STATE_KALENDAR_MESIC: appState = STATE_KALENDAR_MENU; subMenuIndex = 1; subScrollOffset = 0; zobrazSubMenu("KALENDÁŘ", kalendarMenuItems, kalendarMenuCount, 1, 0); break;
    case STATE_KALENDAR_MENU: appState = STATE_AKTUALITY; subMenuIndex = 6; subScrollOffset = scrollForIdx(6); zobrazSubMenu("AKTUALITY", aktualityItems, aktualityCount, subMenuIndex, subScrollOffset); break;
    case STATE_AKTUALITY: appState = STATE_MAIN_MENU; zobrazSubMenu("HLAVNÍ MENU", mainMenuItems, mainMenuCount, menuIndex, scrollOffset); break;

    case STATE_FRAZE_DETAIL: appState = STATE_FRAZE_MENU; zobrazSubMenu("FRÁZE", frazeMenuItems, frazeMenuCount, subMenuIndex, subScrollOffset); break;
    case STATE_FRAZE_MENU: appState = STATE_SLOVNIK_JAZYK; subMenuIndex = 1; subScrollOffset = 0; zobrazSubMenu("SLOVNÍK", slovnikItems, slovnikCount, subMenuIndex, subScrollOffset); break;
    case STATE_UCENI: appState = STATE_SLOVNIK_JAZYK; subMenuIndex = 2; subScrollOffset = 0; zobrazSubMenu("SLOVNÍK", slovnikItems, slovnikCount, subMenuIndex, subScrollOffset); break;
    case STATE_SLOVNIK_DETAIL: appState = STATE_SLOVNIK_JAZYK; subMenuIndex = 0; subScrollOffset = 0; zobrazSubMenu("SLOVNÍK", slovnikItems, slovnikCount, subMenuIndex, subScrollOffset); break;
    case STATE_SLOVNIK_JAZYK: appState = STATE_SLOVNIK; subMenuIndex = slovnikLangIdx; subScrollOffset = 0; zobrazSubMenu("JAZYK SLOVNÍKU", slovnikLangNazvy, slovnikLangCount, subMenuIndex, subScrollOffset); break;
    case STATE_SLOVNIK: appState = STATE_MAIN_MENU; zobrazSubMenu("HLAVNÍ MENU", mainMenuItems, mainMenuCount, menuIndex, scrollOffset); break;
    
    case STATE_QR_ZOBRAZ: appState = STATE_QR_MENU; zobrazSubMenu("QR KÓDY", qrItems, qrCount, subMenuIndex, subScrollOffset); break;
    case STATE_QR_MENU: appState = STATE_TOOLBOX; subMenuIndex = 4; subScrollOffset = scrollForIdx(subMenuIndex); zobrazSubMenu("TOOLBOX", toolboxItems, toolboxCount, subMenuIndex, subScrollOffset); break;
    case STATE_LED_ON: ledState = false; digitalWrite(LED_PIN, LOW); appState = STATE_TOOLBOX; zobrazSubMenu("TOOLBOX", toolboxItems, toolboxCount, subMenuIndex, subScrollOffset); break;
    case STATE_STOPKY: stopkyRunning = false; stopkyElapsed = 0; appState = STATE_TOOLBOX; zobrazSubMenu("TOOLBOX", toolboxItems, toolboxCount, subMenuIndex, subScrollOffset); break;
    case STATE_TEZBA: tezbaBezi = false; if (tezbaReal) { tpClient.stop(); tezbaReal = false; } appState = STATE_TOOLBOX; subMenuIndex = 5; subScrollOffset = scrollForIdx(subMenuIndex); zobrazSubMenu("TOOLBOX", toolboxItems, toolboxCount, subMenuIndex, subScrollOffset); break;
    case STATE_TOUCH_TEST: appState = STATE_TOOLBOX; subMenuIndex = 6; subScrollOffset = scrollForIdx(subMenuIndex); zobrazSubMenu("TOOLBOX", toolboxItems, toolboxCount, subMenuIndex, subScrollOffset); break;
    case STATE_ENCODER_TEST: encTestStop(); appState = STATE_TOOLBOX; subMenuIndex = 7; subScrollOffset = scrollForIdx(subMenuIndex); zobrazSubMenu("TOOLBOX", toolboxItems, toolboxCount, subMenuIndex, subScrollOffset); break;
    case STATE_MIC_TEST: micStop(); appState = STATE_TOOLBOX; subMenuIndex = 8; subScrollOffset = scrollForIdx(subMenuIndex); zobrazSubMenu("TOOLBOX", toolboxItems, toolboxCount, subMenuIndex, subScrollOffset); break;
    case STATE_BUZZER_TEST: buzzerStop(); appState = STATE_TOOLBOX; subMenuIndex = 9; subScrollOffset = scrollForIdx(subMenuIndex); zobrazSubMenu("TOOLBOX", toolboxItems, toolboxCount, subMenuIndex, subScrollOffset); break;
    case STATE_OPICAK: opicakStop(); appState = STATE_TOOLBOX; subMenuIndex = 1; subScrollOffset = scrollForIdx(subMenuIndex); zobrazSubMenu("TOOLBOX", toolboxItems, toolboxCount, subMenuIndex, subScrollOffset); break;
    // Časovač: odpočet běží dál i po odchodu, zvonění se ale vypne.
    case STATE_CASOVAC:
      if (casovacZvoni) { casovacTicho(); }
      appState = STATE_TOOLBOX; subMenuIndex = 11; subScrollOffset = scrollForIdx(subMenuIndex);
      zobrazSubMenu("TOOLBOX", toolboxItems, toolboxCount, subMenuIndex, subScrollOffset); break;
    case STATE_KALKULACKA: appState = STATE_TOOLBOX; subMenuIndex = 12; subScrollOffset = scrollForIdx(subMenuIndex); zobrazSubMenu("TOOLBOX", toolboxItems, toolboxCount, subMenuIndex, subScrollOffset); break;
    case STATE_DIKTAFON_MENU: appState = STATE_TOOLBOX; subMenuIndex = 0; subScrollOffset = scrollForIdx(0); zobrazSubMenu("TOOLBOX", toolboxItems, toolboxCount, subMenuIndex, subScrollOffset); break;
    case STATE_DIKTAFON_SEZNAM: appState = STATE_DIKTAFON_MENU; subMenuIndex = 0; subScrollOffset = 0; zobrazSubMenu("DIKTAFON", diktafonMenuItems, diktafonMenuCount, 0, 0); break;
    case STATE_TOOLBOX: appState = STATE_MAIN_MENU; zobrazSubMenu("HLAVNÍ MENU", mainMenuItems, mainMenuCount, menuIndex, scrollOffset); break;
    case STATE_RECEPT_DETAIL: appState = STATE_RECEPTY_SEZNAM; if (receptyCount > 0) zobrazSubMenu("RECEPTY", receptyItems, receptyCount, subMenuIndex, subScrollOffset); break;
    case STATE_RECEPTY_SEZNAM: appState = STATE_TOOLBOX; subMenuIndex = 10; subScrollOffset = scrollForIdx(10); zobrazSubMenu("TOOLBOX", toolboxItems, toolboxCount, subMenuIndex, subScrollOffset); break;
    
    case STATE_KVIZ: case STATE_KVIZ_ODPOVED: appState = STATE_HRY; subMenuIndex = 4; subScrollOffset = scrollForIdx(4); zobrazSubMenu("HRY", hryItems, hryCount, subMenuIndex, subScrollOffset); break;
    case STATE_WYR: case STATE_WYR_VYSLEDEK: appState = STATE_HRY; subMenuIndex = 5; subScrollOffset = scrollForIdx(5); zobrazSubMenu("HRY", hryItems, hryCount, subMenuIndex, subScrollOffset); break;
    case STATE_FLASKA: case STATE_KOSTKY: case STATE_ODHALOVACKA: case STATE_ODHALOVACKA_DETAIL: appState = STATE_HRY; zobrazSubMenu("HRY", hryItems, hryCount, subMenuIndex, subScrollOffset); break;
    case STATE_KOSTKY_VYBER: appState = STATE_HRY; subMenuIndex = 1; subScrollOffset = 0; zobrazSubMenu("HRY", hryItems, hryCount, subMenuIndex, subScrollOffset); break;
    case STATE_GAMEBOOK: ulozGBPozici(gbNode); appState = STATE_GAMEBOOK_MENU; zobrazSubMenu("GAMEBOOK", dynamicGamebookItems, dynamicGamebookCount, subMenuIndex, subScrollOffset); break;
    case STATE_GAMEBOOK_MENU: appState = STATE_HRY; subMenuIndex = 2; subScrollOffset = 0; zobrazSubMenu("HRY", hryItems, hryCount, subMenuIndex, subScrollOffset); break;
    case STATE_KVIZ_OBTIZNOST: appState = STATE_KVIZ_KATEGORIE; zobrazSubMenu("KATEGORIE KVÍZU", kvizKategorieItems, kvizKategoriiCount, kvizKatIdx, kvizKatScrollOffset); break;
    case STATE_KVIZ_KATEGORIE: appState = STATE_HRY; subMenuIndex = 4; subScrollOffset = scrollForIdx(4); zobrazSubMenu("HRY", hryItems, hryCount, subMenuIndex, subScrollOffset); break;
    case STATE_HRY: appState = STATE_MAIN_MENU; zobrazSubMenu("HLAVNÍ MENU", mainMenuItems, mainMenuCount, menuIndex, scrollOffset); break;
    
    case STATE_HOROSKOP_DETAIL: horoskopScrollPage = 0; appState = STATE_HOROSKOP_MENU; zobrazSubMenu("HOROSKOP", horoskopItems, horoskopCount, horoskopMenuIndex, 0); break;
    case STATE_HOROSKOP_MENU: appState = STATE_AKTUALITY; subMenuIndex = 4; subScrollOffset = scrollForIdx(4); zobrazSubMenu("AKTUALITY", aktualityItems, aktualityCount, subMenuIndex, subScrollOffset); break;
    case STATE_KURZY_GRAF: appState = STATE_KURZY; zobrazKurzyUI(); break;
    
    case STATE_SD_BROWSER:
      if (sdCurrentPath == "/") {
        appState = STATE_MAIN_MENU;
        zobrazSubMenu("HLAVNÍ MENU", mainMenuItems, mainMenuCount, menuIndex, scrollOffset);
      } else {
        int lastSlash = sdCurrentPath.lastIndexOf('/');
        sdCurrentPath = (lastSlash > 0) ? sdCurrentPath.substring(0, lastSlash) : "/";
        nactiSDSlozku();
        zobrazSDProhlizec();
      }
      break;

    case STATE_SD_TEXT_VIEW: case STATE_SD_BMP_VIEW:
      appState = STATE_SD_BROWSER; zobrazSDProhlizec(); break;

    case STATE_ARCHIV_ZPRAVY_MENU:
      appState = STATE_ARCHIV_DATUMY;
      zobrazSubMenu("ARCHIV ZPRÁV", archivDatumy, archivDatumCount, archivDatumIndex, archivDatumScrollOffset);
      break;
    case STATE_ARCHIV_DATUMY:
      subMenuIndex = 3; subScrollOffset = scrollForIdx(3);
      appState = STATE_ZPRAVY_MENU;
      zobrazSubMenu("ZPRÁVY", zpravyMenuItems, zpravyMenuCount, subMenuIndex, subScrollOffset);
      break;
    
    case STATE_GENERATOR_RESULT: appState = STATE_GENERATOR; zobrazSubMenu("GENERÁTOR", generatorItems, generatorCount, subMenuIndex, subScrollOffset); break;
    case STATE_GENERATOR: appState = STATE_MAIN_MENU; zobrazSubMenu("HLAVNÍ MENU", mainMenuItems, mainMenuCount, menuIndex, scrollOffset); break;
    
    case STATE_NASTAVENI_O_ZARIZENI: appState = STATE_NASTAVENI; subMenuIndex = 5; subScrollOffset = scrollForIdx(5); zobrazSubMenu("NASTAVENÍ", nastaveniItems, nastaveniCount, subMenuIndex, subScrollOffset); break;
    case STATE_NASTAVENI_BATERIE: appState = STATE_NASTAVENI; subMenuIndex = 4; subScrollOffset = scrollForIdx(4); zobrazSubMenu("NASTAVENÍ", nastaveniItems, nastaveniCount, subMenuIndex, subScrollOffset); break;
    case STATE_NASTAVENI_AUTOUSPANI: appState = STATE_NASTAVENI; subMenuIndex = 3; subScrollOffset = scrollForIdx(3); zobrazSubMenu("NASTAVENÍ", nastaveniItems, nastaveniCount, subMenuIndex, subScrollOffset); break;
    case STATE_NASTAVENI_AKTUALIZACE: appState = STATE_NASTAVENI; subMenuIndex = 0; subScrollOffset = 0; zobrazSubMenu("NASTAVENÍ", nastaveniItems, nastaveniCount, 0, 0); break;
    case STATE_WEB_UPLOAD: stopWebUploadMode(); appState = STATE_NASTAVENI_AKTUALIZACE; subMenuIndex = 1; subScrollOffset = 0; zobrazSubMenu("AKTUALIZACE", aktualizaceItems, aktualizaceCount, subMenuIndex, subScrollOffset); break;
    case STATE_NASTAVENI_VZHLED: appState = STATE_NASTAVENI; subMenuIndex = 2; subScrollOffset = scrollForIdx(2); zobrazSubMenu("NASTAVENÍ", nastaveniItems, nastaveniCount, subMenuIndex, subScrollOffset); break;
    case STATE_WIFI_HESLO: case STATE_WIFI_POTVRDIT: appState = STATE_WIFI_SEZNAM; zobrazWifiSeznam(); break;
    case STATE_WIFI_SEZNAM: appState = STATE_NASTAVENI_WIFI; subMenuIndex = 0; subScrollOffset = 0; zobrazSubMenu("WIFI", wifiMenuItems, wifiMenuCount, 0, 0); break;
    case STATE_NASTAVENI_WIFI: appState = STATE_NASTAVENI; subMenuIndex = 1; subScrollOffset = scrollForIdx(1); zobrazSubMenu("NASTAVENÍ", nastaveniItems, nastaveniCount, subMenuIndex, subScrollOffset); break;
    case STATE_HUDBA: appState = STATE_MAIN_MENU; zobrazSubMenu("HLAVNÍ MENU", mainMenuItems, mainMenuCount, menuIndex, scrollOffset); break;
    case STATE_USB_SD: usbSdVypni(); appState = STATE_NASTAVENI; subMenuIndex = 6; subScrollOffset = scrollForIdx(6); zobrazSubMenu("NASTAVENÍ", nastaveniItems, nastaveniCount, subMenuIndex, subScrollOffset); break;
    case STATE_NASTAVENI: appState = STATE_MAIN_MENU; zobrazSubMenu("HLAVNÍ MENU", mainMenuItems, mainMenuCount, menuIndex, scrollOffset); break;

    default: appState = STATE_MAIN_MENU; zobrazSubMenu("HLAVNÍ MENU", mainMenuItems, mainMenuCount, menuIndex, scrollOffset); break;
  }
}

// ===== HUDBA (jednoduchý WAV přehrávač přes I2S / PCM5102A) =====
// Zapojení PCM5102A: BCK=GPIO42, LRCK(WS)=GPIO41, DIN=GPIO17, VIN=3V3, GND=GND.
// Na modulu propoj SCK→GND (aby si DAC vzal hodiny z BCK) a XSMT→3V3 (zrušení mutování).
// Záměrně nepoužíváme GPIO10/11/12/14 — ty zůstávají volné pro dotyková tlačítka (touch je jen na GPIO1–14).
// Pozn.: GPIO41/42 jsou JTAG piny, takže s tímto zapojením nepůjde JTAG ladění (USB sériový výstup funguje dál).
// Soubory .wav (PCM 8/16/24/32bit, mono/stereo) se hledají ve složce /hudba na SD kartě.
// Vyšší bitové hloubky se za běhu převádějí na 16bit I2S (DAC i tak hraje čistě).
#define I2S_BCLK_PIN 42
#define I2S_LRCK_PIN 41
#define I2S_DIN_PIN  17
#define HUDBA_DIR    "/hudba"
// Spínání napájení audio modulu přes P-kanálový MOSFET (např. AO3401).
// Zapojení: S=3V3, D=DAC VIN, G=GPIO48, + rezistor 10k mezi G a S.
// Logika: GPIO LOW = DAC zapnutý, GPIO HIGH = DAC vypnutý (LED nesvítí).
// (Bez postaveného spínače je funkce nečinná a nic neškodí.)
// POZN.: GPIO48 (DAC_PWR_PIN) už není použit na napájení audio modulu — u nás
// není zapojený a pin teď slouží jako BUZZER2. Funkce je proto nečinná.
static void dacNapajeni(bool) { /* no-op (GPIO48 = BUZZER2) */ }

String hudbaItems[60];
String hudbaCesty[60];
int hudbaCount = 0;
static Audio* gAudio = nullptr; // aktivni přehrávač knihovny (MP3 atd.), nullptr = nic nehraje
static int hudbaHrajeIdx = -1;  // index právě hrající skladby v seznamu (-1 = nic)
#define HUDBA_VOL_MAX 15 // 0..15 = 0..150 % (nad 100 % jde o zesílení, může zkreslovat)
int hudbaHlasitost = 8; // krok 10 %, ulozeno v Preferences

static void nactiHlasitostHudby() {
  prefs.begin("hudba", true);
  hudbaHlasitost = prefs.getInt("vol", 8);
  prefs.end();
  if (hudbaHlasitost < 0) hudbaHlasitost = 0;
  if (hudbaHlasitost > HUDBA_VOL_MAX) hudbaHlasitost = HUDBA_VOL_MAX;
}
static void ulozHlasitostHudby() {
  prefs.begin("hudba", false);
  prefs.putInt("vol", hudbaHlasitost);
  prefs.end();
}

static i2s_chan_handle_t i2sTx = NULL;
#define HUDBA_FRAMES 512
static uint8_t hudbaRaw[HUDBA_FRAMES * 2 * 4]; // vstup: max 32bit stereo
static int16_t hudbaBuf[HUDBA_FRAMES * 2];     // výstup: 16bit stereo (interleaved)

void i2sZavri() {
  if (i2sTx) {
    i2s_channel_disable(i2sTx);
    i2s_del_channel(i2sTx);
    i2sTx = NULL;
  }
}

bool i2sOtevri(uint32_t sampleRate) {
  i2sZavri();
  i2s_chan_config_t chanCfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  chanCfg.auto_clear = true;
  if (i2s_new_channel(&chanCfg, &i2sTx, NULL) != ESP_OK) { i2sTx = NULL; return false; }
  i2s_std_config_t stdCfg = {};
  stdCfg.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sampleRate);
  stdCfg.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
  stdCfg.gpio_cfg.mclk = I2S_GPIO_UNUSED;
  stdCfg.gpio_cfg.bclk = (gpio_num_t)I2S_BCLK_PIN;
  stdCfg.gpio_cfg.ws   = (gpio_num_t)I2S_LRCK_PIN;
  stdCfg.gpio_cfg.dout = (gpio_num_t)I2S_DIN_PIN;
  stdCfg.gpio_cfg.din  = I2S_GPIO_UNUSED;
  if (i2s_channel_init_std_mode(i2sTx, &stdCfg) != ESP_OK) { i2sZavri(); return false; }
  if (i2s_channel_enable(i2sTx) != ESP_OK) { i2sZavri(); return false; }
  return true;
}

// ===================================================================
// TEST MIKROFONU (INMP441) — I2S RX na stejné sběrnici jako DAC.
// Zapojení: SCK/BCK=GPIO42, WS/LRCK=GPIO41, SD(data z micu)=MIC_SD_PIN, L/R=GND.
// Pozn.: mikrofon a přehrávání nejdou současně (jeden I2S port).
// ===================================================================
static i2s_chan_handle_t i2sRx = NULL;
#define MIC_VZORKOVANI 16000
#define MIC_FRAMES     128
static int32_t micBuf[MIC_FRAMES * 2];   // 32bit stereo

int  micUroven    = 0;    // 0..100 (poslední vykreslené okno)
int  micUrovenMax = 0;    // maximum od posledního vykreslení
int  micPeak      = 0;    // peak-hold
bool micBezi      = false;
unsigned long micLast = 0;

void micStop() {
  if (i2sRx) { i2s_channel_disable(i2sRx); i2s_del_channel(i2sRx); i2sRx = NULL; }
  micBezi = false;
}

bool micStart(uint16_t dmaDesc, uint16_t dmaFrame) {
  micStop();
  if (gAudio) { gAudio->stopSong(); delete gAudio; gAudio = nullptr; }
  i2sZavri();                        // uvolni TX kanál na stejném portu
  i2s_chan_config_t chanCfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  chanCfg.auto_clear = true;
  chanCfg.dma_desc_num  = dmaDesc;
  chanCfg.dma_frame_num = dmaFrame;
  if (i2s_new_channel(&chanCfg, NULL, &i2sRx) != ESP_OK) { i2sRx = NULL; return false; }
  i2s_std_config_t stdCfg = {};
  stdCfg.clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(MIC_VZORKOVANI);
  stdCfg.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO);
  stdCfg.gpio_cfg.mclk = I2S_GPIO_UNUSED;
  stdCfg.gpio_cfg.bclk = (gpio_num_t)I2S_BCLK_PIN;
  stdCfg.gpio_cfg.ws   = (gpio_num_t)I2S_LRCK_PIN;
  stdCfg.gpio_cfg.dout = I2S_GPIO_UNUSED;
  stdCfg.gpio_cfg.din  = (gpio_num_t)MIC_SD_PIN;
  if (i2s_channel_init_std_mode(i2sRx, &stdCfg) != ESP_OK) { micStop(); return false; }
  if (i2s_channel_enable(i2sRx) != ESP_OK) { micStop(); return false; }
  micUroven = 0; micUrovenMax = 0; micPeak = 0; micLast = 0; micBezi = true;
  return true;
}

// Přečte blok vzorků a spočítá hladinu; volá se často v loop().
void micTick() {
  if (!i2sRx) return;
  size_t precteno = 0;
  if (i2s_channel_read(i2sRx, micBuf, sizeof(micBuf), &precteno, 20) != ESP_OK) return;
  int n = (int)(precteno / 8);          // 2 kanály × 4 bajty
  if (n <= 0) return;
  int64_t soucet = 0; int maxAbs = 0;
  for (int i = 0; i < n; i++) {
    int32_t v = micBuf[i * 2] >> 14;    // levý kanál (L/R=GND), 24 bit v horních bitech
    if (v > 32767) v = 32767; else if (v < -32768) v = -32768;
    soucet += (int64_t)v * (int64_t)v;
    int a = (v < 0) ? (int)(-v) : (int)v;
    if (a > maxAbs) maxAbs = a;
  }
  int rms = (int)sqrt((double)soucet / (double)n);
  int uroven = (rms * 100) / 6000;     if (uroven > 100) uroven = 100;
  int peak   = (maxAbs * 100) / 16000; if (peak > 100) peak = 100;
  if (uroven > micUrovenMax) micUrovenMax = uroven;
  if (peak > micPeak) micPeak = peak;
}

void zobrazMikrofonTest() {
  char b[48];
  micUroven = micUrovenMax; micUrovenMax = 0;
  beginUiFrame(true);
  do {
    display.fillScreen(bgColor());
    u8g2Fonts.setFontMode(1);
    u8g2Fonts.setFont(getTitleFont()); u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
    u8g2Fonts.setCursor(5, 18);
    u8g2Fonts.print(micBezi ? "MIKROFON" : "MIKROFON (I2S chyba)");
    nakresliStatusBar();
    display.drawFastHLine(0, 23, display.width(), fgColor());
    int bx = 6, by = 32, bw = display.width() - 12, bh = 26;
    display.drawRect(bx, by, bw, bh, fgColor());
    int vw = (bw - 4) * micUroven / 100;
    if (vw > 0) display.fillRect(bx + 2, by + 2, vw, bh - 4, fgColor());
    int pw = (bw - 4) * micPeak / 100;
    if (pw > 1) display.fillRect(bx + 2 + pw - 2, by + 2, 2, bh - 4, fgColor());
    u8g2Fonts.setFont(getTitleFont());
    snprintf(b, sizeof(b), "Hladina: %d %%   Max: %d %%", micUroven, micPeak);
    u8g2Fonts.setCursor(6, 82); u8g2Fonts.print(b);
    u8g2Fonts.setFont(getSmallFont());
    u8g2Fonts.setCursor(6, 104); u8g2Fonts.print("Mluv / tleskni - hladina se hybe");
    napoveda(6, 124, "BTN0=reset maxima | drz=zpet");
  } while (display.nextPage());
}

// ===================================================================
// DIKTAFON — záznam z INMP441 na SD jako WAV (16 kHz, mono, 16 bit).
// Soubory: /diktafon/RRRR-MM-DD_HH-MM-SS.wav (název = datum a čas).
// Pozn.: MP3 enkodér v projektu není, WAV je bezztrátové PCM bez knihovny.
// ===================================================================
#define DIKTAFON_DIR "/diktafon"
static int16_t dikBuf[2048];   // 4 KB — zápis na SD po ~128 ms záznamu
static int     dikBufN = 0;

static void zapisLE32(File& f, uint32_t v) {
  uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
  f.write(b, 4);
}

// Hlavička WAV: PCM, mono, 16 bit, 16 kHz. dataBytes se doplní po nahrání.
static void diktafonHlavicka(File& f, uint32_t dataBytes) {
  const uint32_t rate = MIC_VZORKOVANI;
  const uint32_t byteRate = rate * 2;
  f.write((const uint8_t*)"RIFF", 4); zapisLE32(f, 36 + dataBytes);
  f.write((const uint8_t*)"WAVEfmt ", 8); zapisLE32(f, 16);
  uint8_t fmt[16] = {
    1, 0,                                          // PCM
    1, 0,                                          // mono
    (uint8_t)rate, (uint8_t)(rate >> 8), (uint8_t)(rate >> 16), (uint8_t)(rate >> 24),
    (uint8_t)byteRate, (uint8_t)(byteRate >> 8), (uint8_t)(byteRate >> 16), (uint8_t)(byteRate >> 24),
    2, 0,                                          // block align
    16, 0                                          // bits per sample
  };
  f.write(fmt, 16);
  f.write((const uint8_t*)"data", 4); zapisLE32(f, dataBytes);
}

// Název = datum a čas. Když čas není nastavený (bez WiFi), použije se pořadové číslo.
static String diktafonNovyNazev() {
  struct tm t;
  char b[52];
  if (getLocalTime(&t, 100) && (t.tm_year + 1900) >= 2020) {
    snprintf(b, sizeof(b), DIKTAFON_DIR "/%04d-%02d-%02d_%02d-%02d-%02d.wav",
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
    if (!SD.exists(b)) return String(b);
  }
  for (int i = 1; i < 1000; i++) {
    snprintf(b, sizeof(b), DIKTAFON_DIR "/nahravka_%03d.wav", i);
    if (!SD.exists(b)) return String(b);
  }
  return String(DIKTAFON_DIR "/nahravka.wav");
}

static String diktafonJmeno(const String& cesta) {
  String b = cesta; int s = b.lastIndexOf('/');
  return (s >= 0) ? b.substring(s + 1) : b;
}

void zobrazDiktafonZprava(const char* text) {
  beginUiFrame(true);
  do {
    display.fillScreen(bgColor());
    u8g2Fonts.setFontMode(1);
    u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
    u8g2Fonts.setFont(getTitleFont()); u8g2Fonts.setCursor(6, 18); u8g2Fonts.print("DIKTAFON");
    nakresliStatusBar();
    display.drawFastHLine(0, 23, display.width(), fgColor());
    u8g2Fonts.setFont(getBodyFont()); u8g2Fonts.setCursor(8, 64); u8g2Fonts.print(text);
  } while (display.nextPage());
  delay(1500);
}

void zobrazDiktafonNahrava(const String& nazev) {
  String b = diktafonJmeno(nazev);
  beginUiFrame(true);
  do {
    display.fillScreen(bgColor());
    u8g2Fonts.setFontMode(1);
    u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
    u8g2Fonts.setFont(getTitleFont()); u8g2Fonts.setCursor(6, 18); u8g2Fonts.print("DIKTAFON");
    nakresliStatusBar();
    display.drawFastHLine(0, 23, display.width(), fgColor());
    u8g2Fonts.setFont(getTitleFont()); u8g2Fonts.setCursor(8, 58); u8g2Fonts.print("NAHRAVAM...");
    u8g2Fonts.setFont(getSmallFont()); u8g2Fonts.setCursor(8, 82); u8g2Fonts.print(b.c_str());
    napoveda(8, 108, "Stop: podrz BTN21 / kolecko");
  } while (display.nextPage());
}

void zobrazDiktafonUlozeno(const String& nazev, uint32_t dataBytes) {
  String b = diktafonJmeno(nazev);
  uint32_t sec = dataBytes / 32000;
  char cas[28];
  snprintf(cas, sizeof(cas), "Delka: %lu:%02lu", (unsigned long)(sec / 60), (unsigned long)(sec % 60));
  beginUiFrame(true);
  do {
    display.fillScreen(bgColor());
    u8g2Fonts.setFontMode(1);
    u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
    u8g2Fonts.setFont(getTitleFont()); u8g2Fonts.setCursor(6, 18); u8g2Fonts.print("DIKTAFON");
    nakresliStatusBar();
    display.drawFastHLine(0, 23, display.width(), fgColor());
    u8g2Fonts.setFont(getTitleFont()); u8g2Fonts.setCursor(8, 56); u8g2Fonts.print("ULOZENO");
    u8g2Fonts.setFont(getSmallFont()); u8g2Fonts.setCursor(8, 82); u8g2Fonts.print(b.c_str());
    u8g2Fonts.setCursor(8, 106); u8g2Fonts.print(cas);
  } while (display.nextPage());
  delay(1500);
}

// --- Zpracování zvuku pro diktafon ---------------------------------
// 1) dvoustupňová horní propust (~75 Hz) — potlačí dunění, nárazy větru a DC
// 2) šumová brána — v tichu stáhne šum na 5 %, otevírá se jen nad klidovou hladinou
// 3) AGC — zesiluje jen řeč nad prahem; max. zesílení je omezené šumem okolí,
//    takže ticho se nezesiluje (opak „nahlasování" prázdného pokoje)
// 4) měkký limiter — fouknutí ohne místo tvrdého digitálního ořezu
static float dikHpX1 = 0, dikHpY1 = 0, dikHpX2 = 0, dikHpY2 = 0;
static float dikNf = -1;          // odhad klidové hladiny (blokové RMS), -1 = neznámý
static float dikGain = 4.0f, dikGate = 0.05f;   // brána začíná zavřená → ticho zůstane tichem

static void diktafonDspReset() {
  dikHpX1 = dikHpY1 = dikHpX2 = dikHpY2 = 0;
  dikNf = -1; dikGain = 4.0f; dikGate = 0.05f;
}

// Zpracuje blok mono vzorků (n ≤ 256) a zapíše int16 výstup.
static void diktafonDspBlok(const int32_t* vstup, int n, int16_t* vystup) {
  static float tmp[256];
  if (n > 256) n = 256;

  // Horní propust + energie bloku
  float sumSq = 0;
  for (int i = 0; i < n; i++) {
    float x = (float)(vstup[i * 2] >> 14);          // levý kanál, 24bit → 16bit
    float y = x - dikHpX1 + 0.97f * dikHpY1; dikHpX1 = x; dikHpY1 = y;
    float z = y - dikHpX2 + 0.97f * dikHpY2; dikHpX2 = y; dikHpY2 = z;
    tmp[i] = z;
    sumSq += z * z;
  }
  float rms = sqrtf(sumSq / (float)n);

  // Klidová hladina: dolů rychle (pauzy), nahoru velmi pomalu (ať řeč neuteče)
  if (dikNf < 0)        dikNf = rms * 0.5f;             // první blok
  else if (rms < dikNf) dikNf += (rms - dikNf) * 0.02f; // ~0,4 s
  else                  dikNf += (rms - dikNf) * 0.0005f; // ~16 s
  float thr = dikNf * 1.8f + 20.0f;

  // Brána: pod prahem 5 % (ticho zůstane tichem), nad prahem plně otevřeno
  float cil = (rms > thr) ? 1.0f : 0.05f;

  // AGC jen nad prahem; maximum zesílení určuje klidová hladina
  float maxG = 5000.0f / (dikNf * 3.0f + 50.0f);
  if (maxG > 32.0f) maxG = 32.0f;
  if (maxG < 1.0f)  maxG = 1.0f;
  float zel = dikGain;
  if (rms > thr) {
    zel = 5000.0f / (rms + 1.0f);
    if (zel > maxG) zel = maxG;
    if (zel < 0.2f) zel = 0.2f;
  }

  const float lim = 26000.0f;
  for (int i = 0; i < n; i++) {
    // plynulé přechody, ať to nekliká
    dikGate += ((cil > dikGate) ? 0.004f : 0.0002f) * (cil - dikGate);
    dikGain += ((zel < dikGain) ? 0.005f : 0.0001f) * (zel - dikGain);   // ztišit rychle, zesílit pomalu

    float v = tmp[i] * dikGain * dikGate;
    if (v > lim) v = lim + (v - lim) * 0.05f;
    else if (v < -lim) v = -lim + (v + lim) * 0.05f;
    if (v > 32767.0f) v = 32767.0f;
    else if (v < -32768.0f) v = -32768.0f;
    vystup[i] = (int16_t)v;
  }
}

// Blokující záznam — dokud běží, hlavní smyčka nic jiného nedělá (žádné
// překreslování displeje, aby SD a displej nesoupeřily o SPI).
void diktafonNahraj() {
  if (!sdReady) { zobrazDiktafonZprava("SD karta neni dostupna"); return; }
  if (!SD.exists(DIKTAFON_DIR)) SD.mkdir(DIKTAFON_DIR);

  String cesta = diktafonNovyNazev();
  File f = SD.open(cesta.c_str(), FILE_WRITE);
  if (!f) { zobrazDiktafonZprava("Soubor nelze vytvorit"); return; }
  diktafonHlavicka(f, 0);

  // Počkat, až uživatel pustí tlačítko, kterým do menu vstoupil
  unsigned long t0 = millis();
  while ((digitalRead(BTN_SELECT) == LOW || digitalRead(ENC_SW_PIN) == LOW) && millis() - t0 < 3000) delay(10);

  zobrazDiktafonNahrava(cesta);

  // Mikrofon se spouští až po vykreslení — jinak by se během refreshe
  // displeje (~0,75 s) přeplnil DMA buffer a začátek nahrávky by chyběl.
  // Větší DMA buffer (8×512 vzorků ≈ 256 ms) — přežije i pomalejší zápis na SD
  if (!micStart(8, 512)) {
    f.close(); SD.remove(cesta.c_str());
    zobrazDiktafonZprava("Mikrofon se nepodarilo spustit");
    return;
  }

  dikBufN = 0;
  diktafonDspReset();
  uint32_t dataBytes = 0;
  unsigned long stiskOd = 0;
  bool stop = false;
  bool ledByl = ledState;
  digitalWrite(LED_PIN, HIGH);      // LED svítí = nahrává se
  while (!stop) {
    size_t precteno = 0;
    if (i2s_channel_read(i2sRx, micBuf, sizeof(micBuf), &precteno, 100) == ESP_OK) {
      int n = (int)(precteno / 8);           // 2 kanály × 4 bajty
      if (n > 256) n = 256;
      if (dikBufN + n > (int)(sizeof(dikBuf) / sizeof(dikBuf[0]))) {
        f.write((const uint8_t*)dikBuf, dikBufN * 2);
        dataBytes += dikBufN * 2;
        dikBufN = 0;
      }
      diktafonDspBlok(micBuf, n, dikBuf + dikBufN);
      dikBufN += n;
      if (dikBufN >= (int)(sizeof(dikBuf) / sizeof(dikBuf[0]))) {
        f.write((const uint8_t*)dikBuf, dikBufN * 2);
        dataBytes += dikBufN * 2;
        dikBufN = 0;
      }
    }
    // Stop = podržení BTN21 nebo tlačítka encoderu aspoň 250 ms
    bool stisk = (digitalRead(BTN_SELECT) == LOW) || (digitalRead(ENC_SW_PIN) == LOW);
    if (stisk) {
      if (stiskOd == 0) stiskOd = millis();
      else if (millis() - stiskOd >= 250) stop = true;
    } else stiskOd = 0;
    lastUserActivityMs = millis();   // při dlouhém záznamu neusínat
  }

  if (dikBufN > 0) { f.write((const uint8_t*)dikBuf, dikBufN * 2); dataBytes += dikBufN * 2; dikBufN = 0; }
  digitalWrite(LED_PIN, ledByl ? HIGH : LOW);
  f.flush();
  f.seek(4);  zapisLE32(f, 36 + dataBytes);
  f.seek(40); zapisLE32(f, dataBytes);
  f.close();
  micStop();

  if (dataBytes < 3200) {                    // < 0,1 s = omylem spuštěno
    SD.remove(cesta.c_str());
    zobrazDiktafonZprava("Nahravka je prazdna");
    return;
  }
  zobrazDiktafonUlozeno(cesta, dataBytes);
}

// Načte /diktafon/*.wav, od nejnovějších (názvy seřadí datum+čas).
void nactiDiktafonSeznam() {
  diktafonCount = 0;
  File dir = SD.open(DIKTAFON_DIR);
  if (!dir) return;
  while (diktafonCount < 40) {
    File e = dir.openNextFile();
    if (!e) break;
    if (!e.isDirectory()) {
      String n = diktafonJmeno(String(e.name()));
      String low = n; low.toLowerCase();
      if (low.endsWith(".wav")) {
        int i = diktafonCount;
        while (i > 0 && diktafonItems[i - 1] < n) {
          diktafonItems[i] = diktafonItems[i - 1];
          diktafonCesty[i] = diktafonCesty[i - 1];
          i--;
        }
        diktafonItems[i] = n;
        diktafonCesty[i] = String(DIKTAFON_DIR) + "/" + n;
        diktafonCount++;
      }
    }
    e.close();
  }
  dir.close();
}

void diktafonPrehraj(int idx) {
  if (idx < 0 || idx >= diktafonCount) return;
  prehrajWav(diktafonCesty[idx].c_str());
}

// ===================================================================
// TEST BUZZERŮ — 2× pasivní buzzer přes LEDC/PWM (umí i dvojhlas).
// B1 = BUZZER1_PIN, B2 = BUZZER2_PIN, druhé vývody → GND.
// ===================================================================
struct BuzzKrok { uint16_t f1, f2, ms; };

static const BuzzKrok buzzP1[] = { {262,0,200},{294,0,200},{330,0,200},{349,0,200},{392,0,200},{440,0,200},{494,0,200},{523,0,350} };
static const BuzzKrok buzzP2[] = { {262,330,450},{392,494,450},{523,392,650} };
static const BuzzKrok buzzP3[] = { {523,262,250},{494,330,250},{440,349,250},{392,392,600} };
// Jeden buzzer nízko, druhý vysoko (a pak prohozeně) — ať je slyšet, že hrají nezávisle.
static const BuzzKrok buzzP4[] = { {200,1000,1300},{1000,200,1300} };
static const BuzzKrok* const buzzPresety[] = { buzzP1, buzzP2, buzzP3, buzzP4 };
static const uint8_t buzzDelky[] = { 8, 3, 4, 2 };
static const char*   buzzNazvy[] = { "Kratka melodie", "Akordy (2 hlasy)", "Dvojhlas", "Nizky vs vysoky" };
#define BUZZ_PRESETU 4

int  buzzPreset = 0, buzzKrok = 0;
bool buzzHraje = false;
unsigned long buzzKrokStart = 0;

void buzzTon(uint8_t pin, uint16_t f) {
  if (f == 0) { ledcWrite(pin, 0); return; }
  ledcChangeFrequency(pin, f, 10);
  ledcWrite(pin, 512);                        // 50 % při 10 bitech
}

void zobrazBuzzerTest() {
  char b[48];
  int k = (buzzKrok < buzzDelky[buzzPreset]) ? buzzKrok : (buzzDelky[buzzPreset] - 1);
  const BuzzKrok* p = buzzPresety[buzzPreset];
  beginUiFrame(true);
  do {
    display.fillScreen(bgColor());
    u8g2Fonts.setFontMode(1);
    u8g2Fonts.setFont(getTitleFont()); u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
    u8g2Fonts.setCursor(5, 18); u8g2Fonts.print("BUZZER TEST");
    nakresliStatusBar();
    display.drawFastHLine(0, 23, display.width(), fgColor());
    snprintf(b, sizeof(b), "[%d/%d] %s", buzzPreset + 1, BUZZ_PRESETU, buzzNazvy[buzzPreset]);
    u8g2Fonts.setCursor(6, 48); u8g2Fonts.print(b);
    snprintf(b, sizeof(b), "B1: %u Hz   B2: %u Hz", (unsigned)p[k].f1, (unsigned)p[k].f2);
    u8g2Fonts.setCursor(6, 74); u8g2Fonts.print(b);
    u8g2Fonts.setCursor(6, 98); u8g2Fonts.print(buzzHraje ? "Stav: hraje..." : "Stav: hotovo");
    u8g2Fonts.setFont(getSmallFont());
    napoveda(6, 124, "BTN0=dalsi melodie | drz=zpet");
  } while (display.nextPage());
}

void buzzZahraj(int idx) {
  if (idx < 0 || idx >= BUZZ_PRESETU) return;
  buzzPreset = idx; buzzKrok = 0; buzzHraje = true; buzzKrokStart = millis();
  buzzTon(BUZZER1_PIN, buzzPresety[idx][0].f1);
  buzzTon(BUZZER2_PIN, buzzPresety[idx][0].f2);
}

void buzzTick() {
  if (!buzzHraje) return;
  const BuzzKrok* p = buzzPresety[buzzPreset];
  if (millis() - buzzKrokStart < p[buzzKrok].ms) return;
  buzzKrok++;
  if (buzzKrok >= buzzDelky[buzzPreset]) {
    buzzHraje = false;
    buzzTon(BUZZER1_PIN, 0); buzzTon(BUZZER2_PIN, 0);
    zobrazBuzzerTest();
    return;
  }
  buzzKrokStart = millis();
  buzzTon(BUZZER1_PIN, p[buzzKrok].f1);
  buzzTon(BUZZER2_PIN, p[buzzKrok].f2);
}

void buzzerPriprav() {
  // Různé LEDC kanály = různé časovače → každý buzzer může hrát nezávisle jiný tón.
  // POZOR: core při attachi znovu použije časovač, pokud má stejnou frekvenci/rozlišení.
  // Proto každý buzzer attachneme s JINOU výchozí frekvencí, aby dostal vlastní časovač.
  ledcAttachChannel(BUZZER1_PIN, 440, 10, 0);
  ledcAttachChannel(BUZZER2_PIN, 220, 10, 2);
  ledcWrite(BUZZER1_PIN, 0); ledcWrite(BUZZER2_PIN, 0);
  buzzHraje = false;
}

void buzzerStart() {
  buzzerPriprav();
  buzzKrok = 0;
  buzzZahraj(0);
}

void buzzerStop() {
  buzzHraje = false;
  ledcWrite(BUZZER1_PIN, 0); ledcWrite(BUZZER2_PIN, 0);
  ledcDetach(BUZZER1_PIN);   ledcDetach(BUZZER2_PIN);
}

// Krátké tiché "povedlo se" po úspěšné aktualizaci dat.
// Hraje jen na první buzzer, aby to nebylo moc hlasité.
void zahrajUspesnyTon() {
  buzzerPriprav();
  static const uint16_t noty[] = { 523, 659, 784 };   // C5, E5, G5
  for (int i = 0; i < 3; i++) {
    ledcChangeFrequency(BUZZER1_PIN, noty[i], 10);
    ledcWrite(BUZZER1_PIN, 90);       // ~9 % hlasitosti
    delay(110);
    ledcWrite(BUZZER1_PIN, 0);
    delay(45);
  }
  buzzerStop();                       // odpojí LEDC, ať nezůstane nic pískat
}

// ===================================================================
// OPIČÁK — poslouchá okolí a opakuje krátkou melodii.
// Během pískání/hučení jen tiše sbírá noty. Když nastane ticho (~0,7 s),
// zapamatovanou melodii zahraje na buzzery. Během hry mikrofon ignoruje
// (vzorky jen vyčítá), takže nemůže vzniknout vazba s bzučákem.
// ===================================================================
static int16_t opiVz[512];
static int     opiN = 0;
static int     opiSkip = 0;
static float   opiNf = -1;
static unsigned long opiAnalCas = 0;   // čas poslední analýzy (pro přesné délky not)

static uint8_t opiMel[16];        // zapamatované noty (MIDI čísla)
static uint16_t opiMelDur[16];    // délka každé noty v ms (rytmus)
static int     opiMelN = 0;
static int     opiNoteCur = 0;    // právě znějící nota (MIDI), 0 = žádná
static int     opiNoteMs = 0;     // jak dlouho aktuální nota zní
static int     opiRestMs = 0;     // délka mezery (pauzy) mezi notami
static int     opiKand = 0, opiKandCnt = 0;
static unsigned long opiSilenceMs = 0;
static bool    opiPrehravam = false;
static int     opiHrajIdx = -1;
static bool    opiTonZni = false;
static unsigned long opiTonOd = 0;

void opicakStop() {
  buzzTon(BUZZER1_PIN, 0); buzzTon(BUZZER2_PIN, 0);
  buzzerStop();
  micStop();
  digitalWrite(LED_PIN, ledState ? HIGH : LOW);   // vrátit stav LED z nástroje Dioda
}

void opicakStart() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  opiN = 0; opiSkip = 0; opiNf = -1; opiAnalCas = millis();
  opiMelN = 0; opiNoteCur = 0; opiNoteMs = 0; opiRestMs = 0; opiKand = 0; opiKandCnt = 0;
  opiSilenceMs = 0; opiPrehravam = false;
  opiHrajIdx = -1; opiTonZni = false;
  buzzerPriprav();
  micStart();
}

// Nejbližší nota v rozsahu A2–E6 (110–1320 Hz) jako MIDI číslo
static int opiMidi(float f) {
  if (f < 40.0f) return 0;
  int n = (int)lroundf(69.0f + 12.0f * log2f(f / 440.0f));
  if (n < 45) n = 45;
  if (n > 88) n = 88;
  return n;
}

static uint16_t opiFreq(int n) {
  float f = 440.0f * powf(2.0f, (float)(n - 69) / 12.0f);
  if (f < 110.0f) f = 110.0f;
  if (f > 1320.0f) f = 1320.0f;
  return (uint16_t)(f + 0.5f);
}

static void opiZahrajNotu(int n) {
  uint16_t f = opiFreq(n);
  buzzTon(BUZZER1_PIN, f);
  float f2 = (float)f * 1.5f;          // kvinta na druhý buzzer
  if (f2 > 2000.0f) f2 = 2000.0f;
  buzzTon(BUZZER2_PIN, (uint16_t)(f2 + 0.5f));
}

static void opiPridejNotu(int n, int durMs) {
  if (n < 0) return;                  // n = 0 je pomlka (ticho)
  if (durMs < 100)  durMs = 100;      // moc krátké noty prodloužit
  if (durMs > 1500) durMs = 1500;     // a dlouhé zkrátit
  if (opiMelN > 0 && opiMel[opiMelN - 1] == (uint8_t)n) {
    // stejná nota znovu (držený tón) — prodluž tu předchozí
    int d = opiMelDur[opiMelN - 1] + durMs;
    if (d > 1500) d = 1500;
    opiMelDur[opiMelN - 1] = (uint16_t)d;
    return;
  }
  if (opiMelN >= 16) {                 // pamatuj max. 16 not (posun)
    for (int i = 1; i < 16; i++) { opiMel[i - 1] = opiMel[i]; opiMelDur[i - 1] = opiMelDur[i]; }
    opiMelN = 15;
  }
  opiMel[opiMelN] = (uint8_t)n;
  opiMelDur[opiMelN] = (uint16_t)durMs;
  opiMelN++;
}

static bool opiDetekuj(float* fOut, float* rmsOut) {
  static float x[512];
  static float d[221];
  static float cmnd[221];
  const int N = 512, maxLag = 220;          // maxLag 220 ≈ 73 Hz (nízké hučení)

  // stejnosměrná složka pryč + energie
  float mean = 0;
  for (int i = 0; i < N; i++) mean += opiVz[i];
  mean /= N;
  float e0 = 0;
  for (int i = 0; i < N; i++) { x[i] = (float)opiVz[i] - mean; e0 += x[i] * x[i]; }
  float rms = sqrtf(e0 / N);
  *rmsOut = rms;

  // adaptivní práh proti šumu okolí (nahoru velmi pomalu, dolů rychle)
  if (opiNf < 0)        opiNf = rms * 0.5f;
  else if (rms < opiNf) opiNf += (rms - opiNf) * 0.05f;
  else                  opiNf += (rms - opiNf) * 0.0003f;
  if (rms < opiNf * 1.2f + 40.0f) return false;

  // YIN — rozdílová funkce (odolnější proti oktávovým chybám než autokorelace)
  d[0] = 0;
  for (int lag = 1; lag <= maxLag; lag++) {
    float s = 0;
    for (int i = 0; i < N - lag; i++) {
      float t = x[i] - x[i + lag];
      s += t * t;
    }
    d[lag] = s;
  }
  float running = 0;
  cmnd[0] = 1.0f;
  for (int lag = 1; lag <= maxLag; lag++) {
    running += d[lag];
    cmnd[lag] = (running > 0.0001f) ? (d[lag] * (float)lag / running) : 1.0f;
  }

  const int lagMin = 10;                    // ~1600 Hz
  const float yinPrah = 0.25f;
  int bestLag = 0;
  for (int lag = lagMin; lag <= maxLag; lag++) {
    if (cmnd[lag] < yinPrah) {
      while (lag + 1 <= maxLag && cmnd[lag + 1] < cmnd[lag]) lag++;  // locals minimum
      bestLag = lag;
      break;
    }
  }
  if (bestLag == 0) return false;           // žádná periodicita

  // jemné doladění paraboly mezi vzorky (přesnější výška)
  float f = 16000.0f / (float)bestLag;
  if (bestLag > 1 && bestLag < maxLag) {
    float y0 = cmnd[bestLag - 1], y1 = cmnd[bestLag], y2 = cmnd[bestLag + 1];
    float den = y0 - 2.0f * y1 + y2;
    if (fabsf(den) > 0.000001f) {
      float shift = 0.5f * (y0 - y2) / den;
      if (shift > -0.5f && shift < 0.5f) f = 16000.0f / ((float)bestLag + shift);
    }
  }
  *fOut = f;
  return true;
}

void opicakTick() {
  if (!i2sRx) return;
  lastUserActivityMs = millis();   // poslouchá/hraje → neusínat

  // ===== PŘEHRÁVÁNÍ ZAPAMATOVANÉ MELODIE (včetně rytmu) =====
  if (opiPrehravam) {
    size_t precteno = 0;
    i2s_channel_read(i2sRx, micBuf, sizeof(micBuf), &precteno, 5);  // zahodit vstup
    if (opiHrajIdx < 0) {                        // začátek přehrávání
      opiHrajIdx = 0;
      opiTonOd = millis();
      opiTonZni = (opiMel[0] != 0);              // 0 = pomlka
      if (opiTonZni) opiZahrajNotu(opiMel[0]);
      digitalWrite(LED_PIN, HIGH);               // LED = přehrávání
    }
    unsigned long el = millis() - opiTonOd;
    uint16_t dur = opiMelDur[opiHrajIdx];
    if (opiTonZni && el >= (unsigned long)dur) { // nota dozněla
      buzzTon(BUZZER1_PIN, 0); buzzTon(BUZZER2_PIN, 0);
      opiTonZni = false;
    }
    if (!opiTonZni && el >= (unsigned long)dur + 60UL) {  // 60 ms mezera
      opiHrajIdx++;
      if (opiHrajIdx >= opiMelN) {               // konec melodie
        buzzTon(BUZZER1_PIN, 0); buzzTon(BUZZER2_PIN, 0);
        digitalWrite(LED_PIN, LOW);
        opiPrehravam = false;
        opiMelN = 0;                             // po přehrání začni znovu
        opiSilenceMs = 0;
        opiSkip = 10;                            // ~80 ms, ať mikrofon nenasaje dozvuk
        opiHrajIdx = -1;
        opiN = 0;
        opiAnalCas = millis();
      } else {
        opiTonOd = millis();
        opiTonZni = (opiMel[opiHrajIdx] != 0);
        if (opiTonZni) opiZahrajNotu(opiMel[opiHrajIdx]);
      }
    }
    return;
  }

  // ===== POSLECH =====
  size_t precteno = 0;
  if (i2s_channel_read(i2sRx, micBuf, sizeof(micBuf), &precteno, 20) != ESP_OK) return;
  if (opiSkip > 0) { opiSkip--; return; }

  int n = (int)(precteno / 8);
  for (int i = 0; i < n && opiN < 512; i++) {
    int32_t v = micBuf[i * 2] >> 14;
    if (v > 32767) v = 32767; else if (v < -32768) v = -32768;
    opiVz[opiN++] = (int16_t)v;
  }
  if (opiN < 512) return;

  // reálný čas od minulé analýzy (pro správné délky not)
  unsigned long now = millis();
  int dt = (int)(now - opiAnalCas);
  if (dt < 8) dt = 8; else if (dt > 100) dt = 100;
  opiAnalCas = now;

  float f = 0, rms = 0;
  bool notaOk = opiDetekuj(&f, &rms);
  int midi = notaOk ? opiMidi(f) : 0;
  digitalWrite(LED_PIN, (midi > 0) ? HIGH : LOW);   // LED = slyším tón

  if (midi > 0) {
    // nota musí vydržet 2 analýzy, aby se nechytal šum na přechodech
    if (midi == opiKand) { if (opiKandCnt < 20) opiKandCnt++; }
    else { opiKand = midi; opiKandCnt = 1; }
    if (midi == opiNoteCur) {
      opiNoteMs += dt;                    // měříme délku noty (rytmus)
    } else if (opiKandCnt >= 2) {
      if (opiNoteCur > 0) opiPridejNotu(opiNoteCur, opiNoteMs);
      // pauza mezi notami = pomlka (aby sedl i rytmus)
      if (opiNoteCur == 0 && opiRestMs >= 96 && opiMelN > 0) opiPridejNotu(0, opiRestMs);
      opiRestMs = 0;
      opiNoteCur = midi;
      opiNoteMs = dt * 2;                 // nota už zněla 2 analýzy
    }
    opiSilenceMs = 0;
  } else {
    if (opiNoteCur > 0) { opiPridejNotu(opiNoteCur, opiNoteMs); opiNoteCur = 0; opiNoteMs = 0; }
    opiKand = 0; opiKandCnt = 0;
    // „ticho" pro spuštění přehrávání: bere i běžný šum okolí (vyšší práh
    // než detekce not — jinak by tichý pokoj nikdy nepřestal být „zvuk")
    bool ticho = (rms < opiNf * 2.2f + 200.0f);
    if (ticho) {
      opiSilenceMs += dt;
      if (opiNoteCur == 0 && opiMelN > 0 && opiSilenceMs < 700) opiRestMs += dt;
    } else {
      opiSilenceMs = 0;      // nějaký zvuk, ale bez tónu → čekej
    }
    if (opiSilenceMs >= 700 && opiMelN > 0) {
      opiPrehravam = true;
      opiHrajIdx = -1;
      opiTonZni = false;
      opiSilenceMs = 0;
      opiRestMs = 0;
    }
  }

  // 50% překryv oken → další analýza za ~16 ms (rychlejší reakce, kratší noty)
  memmove(opiVz, opiVz + 256, 256 * sizeof(int16_t));
  opiN = 256;
}

void zobrazOpicak() {
  beginUiFrame(true);
  do {
    display.fillScreen(bgColor());
    u8g2Fonts.setFontMode(1);
    u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
    u8g2Fonts.setFont(getTitleFont()); u8g2Fonts.setCursor(6, 18); u8g2Fonts.print("OPIČÁK");
    nakresliStatusBar();
    display.drawFastHLine(0, 23, display.width(), fgColor());
    u8g2Fonts.setFont(getBodyFont()); u8g2Fonts.setCursor(8, 56); u8g2Fonts.print("Piskej nebo humej melodii");
    u8g2Fonts.setCursor(8, 80); u8g2Fonts.print("a pak chvili mlc...");
    u8g2Fonts.setFont(getSmallFont());
    u8g2Fonts.setCursor(8, 108); u8g2Fonts.print("Pak ti ji zahraju na buzzery");
    napoveda(8, 124, "drz BTN0 = zpet");
  } while (display.nextPage());
}

// ===================================================================
// JÍZDNÍ ŘÁDY — offline data z GTFS PID na SD (/eindata/jr)
// Volitelně realtime z Golemio API, pokud je na SD klíč a běží WiFi.
// ===================================================================
void nactiJrSeznam() {
  jrCount = 0;
  File f = SD.open(String(JR_DIR) + "/index.txt");
  if (!f) return;
  while (f.available() && jrCount < 12) {
    String line = f.readStringUntil('\n'); line.trim();
    if (line.length() < 3) continue;
    int bar = line.indexOf('|');
    if (bar <= 0) continue;
    jrFiles[jrCount]  = line.substring(0, bar);
    jrLabels[jrCount] = line.substring(bar + 1);
    jrItems[jrCount]  = jrLabels[jrCount];
    jrCount++;
  }
  f.close();
}

static bool jrJeSvatek(const struct tm& t) {
  char ds[12];
  snprintf(ds, sizeof(ds), "%04d-%02d-%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
  File f = SD.open(String(JR_DIR) + "/svatky.txt");
  if (!f) return false;
  bool nalezeno = false;
  while (f.available()) {
    String line = f.readStringUntil('\n'); line.trim();
    if (line == ds) { nalezeno = true; break; }
  }
  f.close();
  return nalezeno;
}

static const char* jrDenniTyp(const struct tm& t) {
  if (t.tm_wday == 0 || jrJeSvatek(t)) return "SU";
  if (t.tm_wday == 6) return "SA";
  return "WD";
}

// Realtime odjezdy z Golemio (jen pokud je klíč a WiFi)
static bool jrRealtime(const String& ids, const String& linka, String& vysledek) {
  if (ids.length() < 3 || WiFi.status() != WL_CONNECTED) return false;
  String klic = nactiTextZeSD((String(JR_DIR) + "/golemio_key.txt"));
  klic.trim();
  if (klic.length() < 10) return false;

  String url = "https://api.golemio.cz/v2/pid/departureboards?ids=" + ids + "&limit=20&minutesAfter=90";
  String json = stahniTextZUrlHlavicka("Golemio", url, "X-Access-Token", klic);
  if (json.length() < 20) return false;

  JsonDocument doc;
  if (deserializeJson(doc, json) != DeserializationError::Ok) return false;
  JsonArray deps = doc["departures"].as<JsonArray>();
  String out = "";
  int n = 0;
  for (JsonObject d : deps) {
    String ln = d["route"]["short_name"] | "";
    if (ln != linka) continue;
    String head = d["trip"]["headsign"] | "";
    String mins = d["departure_timestamp"]["minutes"] | "";
    if (head.length() == 0 || mins.length() == 0) continue;
    out += head + "|" + mins + "\n";
    if (++n >= 4) break;
  }
  if (n == 0) return false;
  vysledek = out;
  return true;
}

void zobrazJrZprava(const char* text) {
  beginUiFrame(true);
  do {
    display.fillScreen(bgColor());
    u8g2Fonts.setFontMode(1);
    u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
    u8g2Fonts.setFont(getTitleFont()); u8g2Fonts.setCursor(6, 18); u8g2Fonts.print("SPOJE");
    nakresliStatusBar();
    display.drawFastHLine(0, 23, display.width(), fgColor());
    u8g2Fonts.setFont(getBodyFont()); u8g2Fonts.setCursor(8, 62); u8g2Fonts.print(text);
    u8g2Fonts.setFont(getSmallFont()); napoveda(8, 118, "drz BTN0 = zpet");
  } while (display.nextPage());
}

void zobrazJrDetail() {
  String obsah = nactiTextZeSD((String(JR_DIR) + "/" + jrAktSoubor));
  if (obsah.length() < 10) { zobrazJrZprava("Soubor se nepodarilo nacist"); return; }

  // hlavička: linka + GTFS ids zastávek
  String linka = "", ids = "";
  {
    int pos = 0;
    while (pos < (int)obsah.length()) {
      int e = obsah.indexOf('\n', pos);
      String line = (e < 0) ? obsah.substring(pos) : obsah.substring(pos, e);
      line.trim();
      if (line.startsWith("# ids=")) ids = line.substring(6);
      else if (line.startsWith("# ")) { int bar = line.indexOf('|'); if (bar > 2) linka = line.substring(2, bar); }
      else if (!line.startsWith("#")) break;
      if (e < 0) break;
      pos = e + 1;
    }
  }

  struct tm t;
  bool casOk = getLocalTime(&t, 50) && (t.tm_year + 1900) >= 2020;
  const char* typ = casOk ? jrDenniTyp(t) : "WD";
  int nowMin = casOk ? (t.tm_hour * 60 + t.tm_min) : -1;

  String realtime = "";
  bool rtOk = false;
  if (jrRealtimeChci) {                 // realtime jen na vyžádání (BTN0)
    jrRealtimeChci = false;
    String klic = nactiTextZeSD((String(JR_DIR) + "/golemio_key.txt"));
    klic.trim();
    if (klic.length() >= 10) {
      if (WiFi.status() != WL_CONNECTED) {
        nakresliLoadScreen("Pripojuji WiFi...", 10);
        if (pripojWiFi()) jrWifiZapnuto = true;
      }
      if (WiFi.status() == WL_CONNECTED) rtOk = jrRealtime(ids, linka, realtime);
      if (jrWifiZapnuto) {
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        jrWifiZapnuto = false;
        sharedClient.stop();
        sharedClientInitialized = false;
      }
    }
  }

  beginUiFrame(true);
  do {
    display.fillScreen(bgColor());
    u8g2Fonts.setFontMode(1);
    u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
    u8g2Fonts.setFont(getTitleFont()); u8g2Fonts.setCursor(6, 18);
    u8g2Fonts.print(jrAktLabel.c_str());
    nakresliStatusBar();
    display.drawFastHLine(0, 23, display.width(), fgColor());
    u8g2Fonts.setFont(getSmallFont());

    if (rtOk) {
      // seznam nejbližších spojů (realtime)
      int pos = 0, n = 0, y = 46;
      while (pos < (int)realtime.length() && n < 4) {
        int e = realtime.indexOf('\n', pos);
        String line = (e < 0) ? realtime.substring(pos) : realtime.substring(pos, e);
        int bar = line.indexOf('|');
        if (bar > 0) {
          String head = line.substring(0, bar);
          String mins = line.substring(bar + 1);
          String txt = head + "   " + ((mins == "<1") ? String("prave ted") : ("za " + mins + " min"));
          u8g2Fonts.setCursor(8, y); u8g2Fonts.print(txt.c_str());
          y += 21; n++;
        }
        if (e < 0) break;
        pos = e + 1;
      }
      napoveda(8, 124, "REALTIME | BTN0=obnovit | drz=zpet");
    } else {
      // offline: dva směry, vždy 3 nejbližší časy
      String smerHead[2], smerCasy[2];
      int smerN = 0;
      int pos = 0;
      while (pos < (int)obsah.length()) {
        int e = obsah.indexOf('\n', pos);
        String line = (e < 0) ? obsah.substring(pos) : obsah.substring(pos, e);
        line.trim();
        if (line.length() > 0 && !line.startsWith("#")) {
          int p1 = line.indexOf('|');
          int p2 = line.indexOf('|', p1 + 1);
          int p3 = line.indexOf('|', p2 + 1);
          if (p1 >= 0 && p2 > p1 && p3 > p2) {
            if (line.substring(p2 + 1, p3) == typ && smerN < 2) {
              String head = line.substring(p1 + 1, p2);
              String times = line.substring(p3 + 1);
              String vyber = "";
              int found = 0, q = 0;
              while (q < (int)times.length() && found < 3) {
                int sp = times.indexOf(' ', q);
                String hm = (sp < 0) ? times.substring(q) : times.substring(q, sp);
                hm.trim();
                if (hm.length() >= 5) {
                  int hh = hm.substring(0, 2).toInt();
                  int mm = hm.substring(3, 5).toInt();
                  int tm2 = hh * 60 + mm;
                  if (nowMin < 0 || tm2 >= nowMin - 1) {
                    if (vyber.length()) vyber += " ";
                    vyber += hm;
                    found++;
                  }
                }
                if (sp < 0) break;
                q = sp + 1;
              }
              smerHead[smerN] = head;
              smerCasy[smerN] = vyber.length() ? vyber : "-";
              smerN++;
            }
          }
        }
        if (e < 0) break;
        pos = e + 1;
      }

      u8g2Fonts.setFont(getBodyFont());
      for (int i = 0; i < smerN; i++) {
        u8g2Fonts.setCursor(8, 50 + i * 30);
        u8g2Fonts.print(smerHead[i].c_str());
        u8g2Fonts.setCursor(8, 50 + i * 30 + 15);
        u8g2Fonts.print(smerCasy[i].c_str());
      }
      if (smerN == 0) { u8g2Fonts.setCursor(8, 60); u8g2Fonts.print("Zadna data pro dnesni den"); }

      u8g2Fonts.setFont(getSmallFont());
      if (!casOk) { u8g2Fonts.setCursor(8, 108); u8g2Fonts.print("Cas neni nastaven"); }
      napoveda(8, 124, "BTN0=realtime | drz=zpet");
    }
  } while (display.nextPage());
}

// ===================================================================
// KALENDÁŘ — data z Google kalendáře (iCal → /eindata/kalendar/kalendar.txt)
// Formát řádku: RRRR-MM-DD|HH:MM|HH:MM|název|místo|popis
// ===================================================================
static const char* kalZkratkyDnu[] = { "NE", "PO", "ÚT", "ST", "ČT", "PÁ", "SO" };
static const char* kalNazvyMesicu[] = { "LEDEN", "ÚNOR", "BŘEZEN", "DUBEN", "KVĚTEN", "ČERVEN",
                                        "ČERVENEC", "SRPEN", "ZÁŘÍ", "ŘÍJEN", "LISTOPAD", "PROSINEC" };

static bool kalDenTM(int offset, struct tm& out) {
  struct tm t;
  if (!getLocalTime(&t, 10)) return false;
  t.tm_hour = 12; t.tm_min = 0; t.tm_sec = 0;
  t.tm_mday += offset;
  t.tm_isdst = -1;
  time_t tt = mktime(&t);
  if (tt == (time_t)-1) return false;
  localtime_r(&tt, &out);
  return true;
}

static String kalDatumKlic(int offset) {
  struct tm t;
  if (!kalDenTM(offset, t)) return "";
  char b[12];
  snprintf(b, sizeof(b), "%04d-%02d-%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
  return String(b);
}

// Posun výběru v měsíční mřížce o dny/týdny; mimo měsíc se automaticky překlopí.
static void kalMesicPohyb(int dny, int tydny) {
  struct tm t = {};
  t.tm_year = kalRok - 1900; t.tm_mon = kalMesic - 1; t.tm_mday = kalMesicDen;
  t.tm_hour = 12; t.tm_isdst = -1;
  t.tm_mday += dny + tydny * 7;
  time_t tt = mktime(&t);
  if (tt == (time_t)-1) return;
  struct tm nt;
  localtime_r(&tt, &nt);
  kalRok = nt.tm_year + 1900;
  kalMesic = nt.tm_mon + 1;
  kalMesicDen = nt.tm_mday;
  zobrazKalMesic();
}

// Kolik dní je datum od dneška (záporně = minulost).
static int kalOffsetOdDnes(int rok, int mes, int den) {
  struct tm a, b = {};
  if (!kalDenTM(0, a)) return 0;
  b.tm_year = rok - 1900; b.tm_mon = mes - 1; b.tm_mday = den;
  b.tm_hour = 12; b.tm_isdst = -1;
  time_t ta = mktime(&a), tb = mktime(&b);
  long d = (long)(tb - ta);
  return (int)((d >= 0 ? d + 43200 : d - 43200) / 86400L);
}

// Vykreslí text do sloupce s automatickým zalamováním řádků.
// Řeže po celých UTF-8 znacích, zalamuje přednostně na mezerách.
static void kalVykresliText(const String& s, int x, int& y, int maxW, int maxY, int lineH, int maxRadku) {
  int pos = 0, n = s.length(), radku = 0;
  while (pos < n && y <= maxY && radku < maxRadku) {
    int i = pos, fit = pos;
    while (i < n) {
      int cl = 1;
      uint8_t c = (uint8_t)s[i];
      if ((c & 0xE0) == 0xC0) cl = 2;
      else if ((c & 0xF0) == 0xE0) cl = 3;
      else if ((c & 0xF8) == 0xF0) cl = 4;
      if (i + cl > n) cl = n - i;
      String test = s.substring(pos, i + cl);
      if (u8g2Fonts.getUTF8Width(test.c_str()) > maxW) break;
      fit = i + cl;
      i += cl;
    }
    if (fit == pos) break;
    int cut = fit;
    for (int j = fit - 1; j > pos; j--) {
      if (s[j] == ' ') { cut = j; break; }
    }
    u8g2Fonts.setCursor(x, y);
    u8g2Fonts.print(s.substring(pos, cut).c_str());
    y += lineH;
    radku++;
    pos = cut;
    while (pos < n && s[pos] == ' ') pos++;
  }
}

static bool kalRozloz(const String& line, String& datum, String& od, String& doCasu,
                      String& nazev, String& misto, String& popis) {
  int p1 = line.indexOf('|');
  if (p1 != 10) return false;
  int p2 = line.indexOf('|', p1 + 1);
  int p3 = line.indexOf('|', p2 + 1);
  int p4 = line.indexOf('|', p3 + 1);
  int p5 = line.indexOf('|', p4 + 1);
  if (p2 < 0 || p3 < 0 || p4 < 0 || p5 < 0) return false;
  datum  = line.substring(0, p1);
  od     = line.substring(p1 + 1, p2);
  doCasu = line.substring(p2 + 1, p3);
  nazev  = line.substring(p3 + 1, p4);
  misto  = line.substring(p4 + 1, p5);
  popis  = line.substring(p5 + 1);
  return true;
}

static int kalTitulyDne(const String& datum, String out[], int maxN, bool sCasem) {
  if (datum.length() != 10) { for (int i = 0; i < maxN; i++) out[i] = ""; return 0; }
  int n = 0, pos = 0;
  while (pos < (int)kalObsah.length() && n < maxN) {
    int e = kalObsah.indexOf('\n', pos);
    String line = (e < 0) ? kalObsah.substring(pos) : kalObsah.substring(pos, e);
    if (line.startsWith(datum) && line.length() > 10 && line[10] == '|') {
      String d, od, dc, nazev, misto, popis;
      if (kalRozloz(line, d, od, dc, nazev, misto, popis)) {
        out[n] = (sCasem && od.length() > 0) ? (od + " " + nazev) : nazev;
        n++;
      }
    }
    if (e < 0) break;
    pos = e + 1;
  }
  for (int i = n; i < maxN; i++) out[i] = "";
  return n;
}

static bool kalMaAkci(const String& datum) {
  // rychlé hledání v celém souboru (bez alokace řádků pro každý den)
  return kalObsah.indexOf(datum + "|") >= 0;
}

void zobrazKalZprava(const char* text) {
  beginUiFrame(true);
  do {
    display.fillScreen(bgColor());
    u8g2Fonts.setFontMode(1);
    u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
    u8g2Fonts.setFont(getTitleFont()); u8g2Fonts.setCursor(5, 17); u8g2Fonts.print("KALENDÁŘ");
    nakresliStatusBar();
    display.drawFastHLine(0, 21, display.width(), fgColor());
    u8g2Fonts.setFont(getBodyFont()); u8g2Fonts.setCursor(8, 64); u8g2Fonts.print(text);
    u8g2Fonts.setFont(getSmallFont()); napoveda(8, 120, "drz BTN0 = zpet");
  } while (display.nextPage());
}

void zobrazKalTyden() {
  struct tm tmp;
  if (!kalDenTM(0, tmp)) { zobrazKalZprava("Cas neni nastaven"); return; }
  beginUiFrame(true);
  do {
    display.fillScreen(bgColor());
    u8g2Fonts.setFontMode(1);
    u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
    u8g2Fonts.setFont(getTitleFont()); u8g2Fonts.setCursor(5, 17); u8g2Fonts.print("KALENDÁŘ");
    nakresliStatusBar();
    display.drawFastHLine(0, 21, display.width(), fgColor());

    int w = display.width() / 7;
    for (int i = 0; i < 7; i++) {
      struct tm t;
      bool ok = kalDenTM(i, t);
      int x = i * w;
      bool vybran = (i == kalDenIdx);
      if (vybran) display.fillRect(x + 1, 24, w - 2, 96, fgColor());
      else display.drawRect(x + 1, 24, w - 2, 96, fgColor());

      u8g2Fonts.setForegroundColor(vybran ? bgColor() : fgColor());
      u8g2Fonts.setBackgroundColor(vybran ? fgColor() : bgColor());

      if (ok) {
        char hdr[16];
        snprintf(hdr, sizeof(hdr), "%s %d", kalZkratkyDnu[t.tm_wday], t.tm_mday);
        u8g2Fonts.setFont(getSmallBoldFont());
        u8g2Fonts.setCursor(x + 3, 36); u8g2Fonts.print(hdr);
      }

      String tit[5];
      int nTit = kalTitulyDne(kalDatumKlic(i), tit, 5, false);
      u8g2Fonts.setFont(getSmallFont());
      int y = 49;
      if (nTit > 0) {
        int radku = (121 - y) / 11 + 1;     // kolik řádků se do sloupce vejde
        int naUdalost = radku / nTit;
        if (naUdalost < 1) naUdalost = 1;
        if (naUdalost > 6) naUdalost = 6;
        for (int k = 0; k < nTit; k++) {
          if (tit[k].length() == 0) continue;
          kalVykresliText(tit[k], x + 3, y, w - 7, 121, 11, naUdalost);
        }
      }
    }

    u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
    u8g2Fonts.setFont(getSmallFont()); napoveda(5, 126, "BTN0=den | BTN21=detail | drz=zpet");
  } while (display.nextPage());
}

void zobrazKalMesic() {
  if (kalMesic < 1 || kalMesic > 12) {
    struct tm t;
    if (!kalDenTM(0, t)) { zobrazKalZprava("Cas neni nastaven"); return; }
    kalMesic = t.tm_mon + 1; kalRok = t.tm_year + 1900;
  }

  static const int dnyMesicu[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  int dny = dnyMesicu[kalMesic - 1];
  if (kalMesic == 2 && ((kalRok % 4 == 0 && kalRok % 100 != 0) || kalRok % 400 == 0)) dny = 29;
  if (kalMesicDen < 1) kalMesicDen = 1;
  if (kalMesicDen > dny) kalMesicDen = dny;

  struct tm fd = {};
  fd.tm_year = kalRok - 1900; fd.tm_mon = kalMesic - 1; fd.tm_mday = 1;
  fd.tm_hour = 12; fd.tm_isdst = -1;
  mktime(&fd);
  int startCol = (fd.tm_wday + 6) % 7;   // pondělí = 0

  beginUiFrame(true);
  do {
    display.fillScreen(bgColor());
    u8g2Fonts.setFontMode(1);
    u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
    u8g2Fonts.setFont(getTitleFont());
    String tit = String("KALENDÁŘ  ") + kalNazvyMesicu[kalMesic - 1] + " " + String(kalRok);
    u8g2Fonts.setCursor(5, 17); u8g2Fonts.print(tit.c_str());
    nakresliStatusBar();
    display.drawFastHLine(0, 21, display.width(), fgColor());

    int w = display.width() / 7;
    int cellH = (128 - 24) / 6;
    for (int i = 0; i < 42; i++) {
      int col = i % 7, row = i / 7;
      int den = i - startCol + 1;
      if (den < 1 || den > dny) continue;
      int x = col * w, y = 24 + row * cellH;

      char b[8]; snprintf(b, sizeof(b), "%d", den);
      u8g2Fonts.setFont((den == kalMesicDen) ? getSmallBoldFont() : getSmallFont());
      u8g2Fonts.setCursor(x + 3, y + 12); u8g2Fonts.print(b);

      char ds[12];
      snprintf(ds, sizeof(ds), "%04d-%02d-%02d", kalRok, kalMesic, den);
      bool akce = kalMaAkci(String(ds));
      int sx = x + 23, sy = y + 3, ss = 11;
      if (akce) display.fillRect(sx, sy, ss, ss, fgColor());
      else display.drawRect(sx, sy, ss, ss, fgColor());

      // obtažení vybraného dne
      if (den == kalMesicDen) display.drawRect(x + 2, y + 1, w - 4, cellH - 2, fgColor());
    }

    u8g2Fonts.setFont(getSmallFont()); napoveda(5, 126, "D-pad=vyber | BTN21=detail | drz=zpet");
  } while (display.nextPage());
}

void zobrazKalDetail() {
  struct tm t;
  if (!kalDenTM(kalDetailOffset, t)) { zobrazKalZprava("Cas neni nastaven"); return; }
  String datum = kalDatumKlic(kalDetailOffset);

  String txt = "";
  int pos = 0;
  while (pos < (int)kalObsah.length()) {
    int e = kalObsah.indexOf('\n', pos);
    String line = (e < 0) ? kalObsah.substring(pos) : kalObsah.substring(pos, e);
    if (line.startsWith(datum) && line.length() > 10 && line[10] == '|') {
      String d, od, dc, nazev, misto, popis;
      if (kalRozloz(line, d, od, dc, nazev, misto, popis)) {
        if (od.length() > 0) {
          txt += od;
          if (dc.length() > 0) txt += "-" + dc;
          txt += "  ";
        }
        txt += nazev + "\n";
        if (misto.length() > 0) txt += "  " + misto + "\n";
        if (popis.length() > 0) txt += "  " + popis + "\n";
        txt += "\n";
      }
    }
    if (e < 0) break;
    pos = e + 1;
  }
  if (txt.length() == 0) txt = "Zadne akce";
  if (txt.length() > 3000) txt = txt.substring(0, 3000);

  TextLine lines[100];
  int lineCount = zalamejText(txt.c_str(), txt.length(), lines, 100, display.width() - 10);
  int linesPerPage = getLinesPerPage();
  if (linesPerPage < 1) linesPerPage = 1;
  int totalPages = (lineCount + linesPerPage - 1) / linesPerPage; if (totalPages < 1) totalPages = 1;
  if (kalStranka >= totalPages) kalStranka = totalPages - 1;
  if (kalStranka < 0) kalStranka = 0;
  int startLine = kalStranka * linesPerPage;

  beginUiFrame(true);
  do {
    display.fillScreen(bgColor());
    u8g2Fonts.setFontMode(1);
    u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
    u8g2Fonts.setFont(getTitleFont());
    char hdr[40];
    snprintf(hdr, sizeof(hdr), "%s %d. %d.", kalZkratkyDnu[t.tm_wday], t.tm_mday, t.tm_mon + 1);
    u8g2Fonts.setCursor(5, 17); u8g2Fonts.print(hdr);
    char pg[16]; snprintf(pg, sizeof(pg), "%d/%d", kalStranka + 1, totalPages);
    u8g2Fonts.setFont(getSmallFont()); u8g2Fonts.setCursor(228, 17); u8g2Fonts.print(pg);
    nakresliStatusBar();
    display.drawFastHLine(0, 21, display.width(), fgColor());

    u8g2Fonts.setFont(getBodyFont());
    int y = 36;
    for (int i = 0; i < linesPerPage; i++) {
      int idx = startLine + i;
      if (idx >= lineCount) break;
      int st = lines[idx].start, l = lines[idx].len;
      if (l > 99) l = 99;
      while (l > 0 && st + l < (int)txt.length() && ((uint8_t)txt[st + l] & 0xC0) == 0x80) l--;
      char lb[100];
      strncpy(lb, &txt[st], l); lb[l] = '\0';
      u8g2Fonts.setCursor(5, y); u8g2Fonts.print(lb);
      y += getLineHeight();
    }

    u8g2Fonts.setFont(getSmallFont()); napoveda(5, 126, "BTN0=dalsi | BTN21=dalsi den | drz=zpet");
  } while (display.nextPage());
}

// ===================================================================
// RECEPTY — jeden textový soubor na recept v /eindata/recepty
// Formát: 1. řádek = název, volitelně řádek "Porce: 4",
//         suroviny, prázdný řádek (nebo ---), postup.
// ===================================================================
static void nactiReceptySeznam() {
  receptyCount = 0;
  if (!sdReady) return;
  File dir = SD.open(REC_DIR);
  if (!dir || !dir.isDirectory()) { if (dir) dir.close(); return; }
  File entry = dir.openNextFile();
  while (entry && receptyCount < 40) {
    if (!entry.isDirectory()) {
      const char* full = entry.name();
      const char* base = strrchr(full, '/');
      base = base ? (base + 1) : full;
      String nm = String(base);
      String low = nm; low.toLowerCase();
      if (low.endsWith(".txt") && !low.startsWith("readme")) {
        String cesta = String(REC_DIR) + "/" + nm;
        String t = "";
        File f = SD.open(cesta.c_str());
        if (f) {
          // Název = první neprázdný řádek (toleruje BOM i úvodní prázdné řádky)
          for (int r = 0; r < 5 && f.available(); r++) {
            t = f.readStringUntil('\n');
            t = _textNaUtf8(t);
            t.trim();
            if (t.length() > 0) break;
          }
          f.close();
        }
        if (t.length() == 0) {
          t = nm.substring(0, nm.length() - 4);
          if (t.length() > 0 && t[0] >= 'a' && t[0] <= 'z') t.setCharAt(0, t[0] - 32);
        }
        receptyFiles[receptyCount] = nm;
        receptyItems[receptyCount] = t;
        receptyCount++;
      }
    }
    entry.close();
    entry = dir.openNextFile();
  }
  if (entry) entry.close();
  dir.close();

  // seřadit podle názvu
  for (int i = 0; i < receptyCount; i++) {
    for (int j = i + 1; j < receptyCount; j++) {
      if (receptyItems[j] < receptyItems[i]) {
        String t = receptyItems[i]; receptyItems[i] = receptyItems[j]; receptyItems[j] = t;
        t = receptyFiles[i]; receptyFiles[i] = receptyFiles[j]; receptyFiles[j] = t;
      }
    }
  }
}

static void nactiRecept(int idx) {
  receptNazev = (idx >= 0 && idx < receptyCount) ? receptyItems[idx] : "";
  receptSuroviny = ""; receptPostup = ""; receptyZaklad = 4;
  if (idx < 0 || idx >= receptyCount) return;
  String obsah = nactiTextZeSD(String(REC_DIR) + "/" + receptyFiles[idx]);
  if (obsah.length() < 3) return;
  obsah = _textNaUtf8(obsah);

  bool titulekVzat = false, vSurovinach = true, cekaPrvniSurovinu = true;
  int pos = 0;
  while (pos < (int)obsah.length()) {
    int e = obsah.indexOf('\n', pos);
    String line = (e < 0) ? obsah.substring(pos) : obsah.substring(pos, e);
    line.trim();
    pos = (e < 0) ? obsah.length() : (e + 1);

    if (!titulekVzat) {
      if (line.length() == 0) continue;
      receptNazev = line;
      titulekVzat = true;
      continue;
    }
    String low = line; low.toLowerCase();
    if (low.startsWith("porce")) {
      int dvoj = line.indexOf(':');
      int n = (dvoj >= 0) ? line.substring(dvoj + 1).toInt() : line.substring(5).toInt();
      if (n > 0 && n <= 99) receptyZaklad = n;
      continue;
    }
    if (vSurovinach) {
      if (line.length() == 0 || line == "---" || line == "===" || low.startsWith("postup")) {
        // Prázdný řádek hned za hlavičkou (název / Porce) ještě neznamená konec surovin.
        if (line.length() == 0 && cekaPrvniSurovinu) continue;
        vSurovinach = false;
        continue;
      }
      cekaPrvniSurovinu = false;
      if (receptSuroviny.length()) receptSuroviny += "\n";
      receptSuroviny += line;
    } else {
      // Řádek "POSTUP" je jen oddělovač — do textu se nepřidává dvakrát
      if (receptPostup.length() == 0 && low.startsWith("postup")) continue;
      receptPostup += line;
      receptPostup += "\n";
    }
  }
  receptyPorce = receptyZaklad;
  if (receptyPorce < 1) receptyPorce = 1;
  receptStranka = 0;
}

// Přeškáluje číslo na začátku řádku (množství suroviny), umí i rozsah "2-3".
static String receptSkaluj(const String& radek, float faktor) {
  auto preved = [](const String& c) -> float {
    int lom = c.indexOf('/');
    if (lom > 0) {
      float a = c.substring(0, lom).toFloat();
      float b = c.substring(lom + 1).toFloat();
      return (b == 0) ? -1.0f : (a / b);
    }
    String t = c; t.replace(",", ".");
    return t.toFloat();
  };
  auto naText = [faktor](float v) -> String {
    v *= faktor;
    if (fabsf(v - roundf(v)) < 0.05f) return String((long)lroundf(v));
    String o = String(v, 2);
    while (o.endsWith("0")) o = o.substring(0, o.length() - 1);   // 0,50 -> 0,5
    if (o.endsWith(".")) o = o.substring(0, o.length() - 1);
    o.replace(".", ",");
    return o;
  };
  auto konecCisla = [&radek](int i) -> int {
    int n = radek.length();
    while (i < n) {
      char c = radek[i];
      if ((c >= '0' && c <= '9') || c == ',' || c == '.' || c == '/') i++;
      else break;
    }
    return i;
  };

  int n = radek.length();
  int i = 0;
  while (i < n && radek[i] == ' ') i++;
  int start = i;
  int konec = konecCisla(i);
  if (konec == start) return radek;         // řádek nezačíná číslem

  String cislo = radek.substring(start, konec);
  float v = preved(cislo);
  if (v < 0) return radek;
  String vysledek = naText(v);

  // rozsah, např. "2-3"
  if (konec + 1 < n && radek[konec] == '-' &&
      radek[konec + 1] >= '0' && radek[konec + 1] <= '9') {
    int k2 = konecCisla(konec + 1);
    String cislo2 = radek.substring(konec + 1, k2);
    float v2 = preved(cislo2);
    if (v2 >= 0) {
      return radek.substring(0, start) + vysledek + "-" + naText(v2) + radek.substring(k2);
    }
  }
  return radek.substring(0, start) + vysledek + radek.substring(konec);
}

void zobrazReceptZprava(const char* text) {
  beginUiFrame(true);
  do {
    display.fillScreen(bgColor());
    u8g2Fonts.setFontMode(1);
    u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
    u8g2Fonts.setFont(getTitleFont()); u8g2Fonts.setCursor(5, 17); u8g2Fonts.print("RECEPTY");
    nakresliStatusBar();
    display.drawFastHLine(0, 21, display.width(), fgColor());
    u8g2Fonts.setFont(getBodyFont()); u8g2Fonts.setCursor(8, 64); u8g2Fonts.print(text);
    u8g2Fonts.setFont(getSmallFont()); napoveda(8, 120, "drz BTN0 = zpet");
  } while (display.nextPage());
}

void zobrazReceptDetail() {
  String txt = "";
  if (receptyZaklad > 0) {
    txt += "Porce: " + String(receptyPorce);
    if (receptyPorce != receptyZaklad) txt += "  (zaklad " + String(receptyZaklad) + ")";
    txt += "\n\n";
  }
  float faktor = (receptyZaklad > 0) ? ((float)receptyPorce / (float)receptyZaklad) : 1.0f;
  if (receptSuroviny.length() > 0) {
    txt += "SUROVINY\n";
    int pos = 0;
    while (pos < (int)receptSuroviny.length()) {
      int e = receptSuroviny.indexOf('\n', pos);
      String line = (e < 0) ? receptSuroviny.substring(pos) : receptSuroviny.substring(pos, e);
      txt += receptSkaluj(line, faktor);
      txt += "\n";
      if (e < 0) break;
      pos = e + 1;
    }
  }
  if (receptPostup.length() > 0) {
    txt += "\nPOSTUP\n";
    txt += receptPostup;
  }
  if (txt.length() == 0) txt = "Recept je prazdny";
  if (txt.length() > 3500) txt = txt.substring(0, 3500);

  TextLine lines[100];
  int lineCount = zalamejText(txt.c_str(), txt.length(), lines, 100, display.width() - 10);
  int linesPerPage = getLinesPerPage();
  if (linesPerPage < 1) linesPerPage = 1;
  int totalPages = (lineCount + linesPerPage - 1) / linesPerPage; if (totalPages < 1) totalPages = 1;
  if (receptStranka >= totalPages) receptStranka = 0;   // BTN0 prochází stránky cyklicky
  if (receptStranka < 0) receptStranka = 0;
  int startLine = receptStranka * linesPerPage;

  beginUiFrame(true);
  do {
    display.fillScreen(bgColor());
    u8g2Fonts.setFontMode(1);
    u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
    u8g2Fonts.setFont(getTitleFont());
    u8g2Fonts.setCursor(5, 17); u8g2Fonts.print(receptNazev.c_str());
    char pg[16]; snprintf(pg, sizeof(pg), "%d/%d", receptStranka + 1, totalPages);
    u8g2Fonts.setFont(getSmallFont()); u8g2Fonts.setCursor(228, 17); u8g2Fonts.print(pg);
    nakresliStatusBar();
    display.drawFastHLine(0, 21, display.width(), fgColor());

    u8g2Fonts.setFont(getBodyFont());
    int y = 36;
    for (int i = 0; i < linesPerPage; i++) {
      int idx = startLine + i;
      if (idx >= lineCount) break;
      int st = lines[idx].start, l = lines[idx].len;
      if (l > 99) l = 99;
      while (l > 0 && st + l < (int)txt.length() && ((uint8_t)txt[st + l] & 0xC0) == 0x80) l--;
      char lb[100];
      strncpy(lb, &txt[st], l); lb[l] = '\0';
      u8g2Fonts.setCursor(5, y); u8g2Fonts.print(lb);
      y += getLineHeight();
    }

    u8g2Fonts.setFont(getSmallFont()); napoveda(5, 126, "BTN0=dalsi | BTN21=+ | drz21=-");
  } while (display.nextPage());
}

void nactiHudbaSeznam() {
  hudbaCount = 0;
  if (!sdReady) return;
  File dir = SD.open(HUDBA_DIR);
  if (!dir || !dir.isDirectory()) return;
  File f = dir.openNextFile();
  while (f && hudbaCount < 60) {
    if (!f.isDirectory()) {
      String nm = String(f.name());
      String low = nm; low.toLowerCase();
      if (_isAudioName(low)) {
        String base = nm; int slash = base.lastIndexOf('/'); if (slash >= 0) base = base.substring(slash + 1);
        hudbaItems[hudbaCount] = base;
        hudbaCesty[hudbaCount] = String(HUDBA_DIR) + "/" + base;
        hudbaCount++;
      }
    }
    f.close();
    f = dir.openNextFile();
  }
  dir.close();
}

static String hudbaDisp[62];
int hudbaMenuCount() { return hudbaCount + (gAudio ? 1 : 0); }
void zobrazHudbaSeznam() {
  int n = hudbaMenuCount();
  if (subMenuIndex >= n) subMenuIndex = (n > 0) ? (n - 1) : 0;
  if (subScrollOffset > subMenuIndex) subScrollOffset = subMenuIndex;
  if (n > 0) {
    int k = 0;
    if (gAudio) {
      hudbaDisp[k++] = "< Zastavit prehravani";
      hudbaDisp[k++] = "Hlasitost: " + String(hudbaHlasitost * 10) + " %";
    }
    for (int i = 0; i < hudbaCount; i++) {
      hudbaDisp[k++] = (i == hudbaHrajeIdx) ? (String("> ") + hudbaItems[i]) : hudbaItems[i];
    }
    zobrazSubMenu("HUDBA", hudbaDisp, n, subMenuIndex, subScrollOffset);
    return;
  }
  beginUiFrame(true);
  do {
    display.fillScreen(bgColor());
    u8g2Fonts.setFontMode(1);
    u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
    u8g2Fonts.setFont(getTitleFont()); u8g2Fonts.setCursor(10, 18); u8g2Fonts.print("HUDBA");
    nakresliStatusBar();
    display.drawFastHLine(0, 23, display.width(), fgColor());
    u8g2Fonts.setFont(getBodyFont()); u8g2Fonts.setCursor(10, 60); u8g2Fonts.print("Zadne audio soubory");
    u8g2Fonts.setFont(getSmallFont()); u8g2Fonts.setCursor(10, 85); u8g2Fonts.print("Vloz WAV/MP3 do /hudba na SD");
  } while (display.nextPage());
}

void zobrazHudbaZprava(const char* text) {
  beginUiFrame(true);
  do {
    display.fillScreen(bgColor());
    u8g2Fonts.setFontMode(1);
    u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
    u8g2Fonts.setFont(getTitleFont()); u8g2Fonts.setCursor(10, 40); u8g2Fonts.print("HUDBA");
    u8g2Fonts.setFont(getBodyFont()); u8g2Fonts.setCursor(10, 75); u8g2Fonts.print(text);
  } while (display.nextPage());
  delay(1500);
}

void zobrazHudbaPrehrava(const String& nazev) {
  beginUiFrame(true);
  do {
    display.fillScreen(bgColor());
    u8g2Fonts.setFontMode(1);
    u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
    nakresliStatusBar();
    u8g2Fonts.setFont(getTitleFont()); u8g2Fonts.setCursor(10, 26); u8g2Fonts.print("PRAVE HRAJE");
    display.drawFastHLine(0, 31, display.width(), fgColor());
    u8g2Fonts.setFont(getBodyFont()); u8g2Fonts.setCursor(10, 62); u8g2Fonts.print(nazev.c_str());
    u8g2Fonts.setFont(getSmallFont()); napoveda(10, 96, "BTN0/21 = hlasitost, drz = stop");
    u8g2Fonts.setCursor(10, 114); u8g2Fonts.print(("Hlasitost: " + String(hudbaHlasitost * 10) + " %").c_str());
  } while (display.nextPage());
}

static uint32_t wavLE32(File& f) { uint8_t b[4]; if (f.read(b, 4) != 4) return 0; return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24); }
static uint16_t wavLE16(File& f) { uint8_t b[2]; if (f.read(b, 2) != 2) return 0; return (uint16_t)b[0] | ((uint16_t)b[1] << 8); }

void prehrajWav(const char* cesta) {
  lastUserActivityMs = millis();
  File f = SD.open(cesta);
  if (!f) { zobrazHudbaZprava("Soubor nelze otevrit"); return; }

  if (wavLE32(f) != 0x46464952) { f.close(); zobrazHudbaZprava("Neni WAV (RIFF)"); return; }   // "RIFF"
  wavLE32(f); // velikost souboru
  if (wavLE32(f) != 0x45564157) { f.close(); zobrazHudbaZprava("Neni WAV (WAVE)"); return; }   // "WAVE"

  uint16_t audioFormat = 0, channels = 0, blockAlign = 0; uint32_t sampleRate = 0, dataStart = 0, dataSize = 0;
  bool ok = false;
  while (f.available() >= 8) {
    uint32_t id = wavLE32(f);
    uint32_t sz = wavLE32(f);
    if (id == 0x20746D66) {          // "fmt "
      audioFormat = wavLE16(f); channels = wavLE16(f); sampleRate = wavLE32(f);
      wavLE32(f); blockAlign = wavLE16(f); wavLE16(f); // poslední pole = bity/vzorek (nepoužijeme, ale musíme přeskočit)
      if (audioFormat == 0xFFFE && sz >= 40) {
        // WAVE_FORMAT_EXTENSIBLE — skutečný formát je v SubFormat GUID
        wavLE16(f);                        // cbSize
        wavLE16(f);                        // wValidBitsPerSample
        wavLE32(f);                        // dwChannelMask
        uint16_t subFormat = wavLE16(f);   // první 2 bajty GUID = formát
        if (subFormat == 1) audioFormat = 1; // PCM
        f.seek(f.position() + (sz - 26));
      } else if (sz > 16) {
        f.seek(f.position() + (sz - 16));
      }
    } else if (id == 0x61746164) {   // "data"
      dataStart = f.position(); dataSize = sz; ok = true; break;
    } else {
      f.seek(f.position() + sz + (sz & 1));
    }
  }
  if (!ok || dataSize == 0) { f.close(); zobrazHudbaZprava("Chybi audio data"); return; }
  int bytesPerSample = (channels > 0) ? (blockAlign / channels) : 0; // 1/2/3/4 = 8/16/24/32 bit
  if (bytesPerSample < 1 || bytesPerSample > 4) { f.close(); zobrazHudbaZprava("Nepodporovana hloubka"); return; }
  bool isFloat = false;
  if (audioFormat == 3) {            // IEEE float (jen 32bit)
    if (bytesPerSample != 4) { f.close(); zobrazHudbaZprava("Float WAV musi byt 32bit"); return; }
    isFloat = true;
  } else if (audioFormat != 1) {
    String m = "WAV fmt " + String(audioFormat) + " nepodporovan";
    f.close(); zobrazHudbaZprava(m.c_str()); return;
  }
  if (channels != 1 && channels != 2) { f.close(); zobrazHudbaZprava("Jen mono/stereo WAV"); return; }
  if (sampleRate < 8000 || sampleRate > 192000) { f.close(); zobrazHudbaZprava("Divny sample rate"); return; }

  dacNapajeni(true);           // zapnout napájení audio modulu
  delay(100);                  // než se DAC nastartuje
  if (!i2sOtevri(sampleRate)) { f.close(); dacNapajeni(false); zobrazHudbaZprava("I2S se nepodarilo spustit"); return; }

  nactiHlasitostHudby();
  String base = String(cesta); int slash = base.lastIndexOf('/'); if (slash >= 0) base = base.substring(slash + 1);
  zobrazHudbaPrehrava(base);
  f.seek(dataStart);

  bool prevDown = false, prevSel = false, stopNow = false;
  unsigned long downDownMs = 0, selDownMs = 0;
  size_t remaining = dataSize;
  const size_t inFrameBytes = (size_t)channels * bytesPerSample;
  size_t maxFrames = sizeof(hudbaRaw) / inFrameBytes;
  if (maxFrames > (size_t)HUDBA_FRAMES) maxFrames = HUDBA_FRAMES;
  while (remaining > 0) {
    // Ovladani behem prehravani (displej zamerne neprekreslujeme, aby zvuk neskubal):
    // kratky stisk BTN0 = hlasitost -, kratky BTN21 = hlasitost +, drzeni (~0.8 s) = stop.
    bool down = (digitalRead(BTN_DOWN) == LOW);
    bool sel  = (digitalRead(BTN_SELECT) == LOW);
    unsigned long nowMs = millis();
    if (down && !prevDown) downDownMs = nowMs;
    if (sel  && !prevSel)  selDownMs  = nowMs;
    if (down && (nowMs - downDownMs >= 800)) stopNow = true;
    if (sel  && (nowMs - selDownMs  >= 800)) stopNow = true;
    if (!down && prevDown && (nowMs - downDownMs < 800)) { if (hudbaHlasitost > 0) hudbaHlasitost--; }
    if (!sel  && prevSel  && (nowMs - selDownMs  < 800)) { if (hudbaHlasitost < HUDBA_VOL_MAX) hudbaHlasitost++; }
    prevDown = down; prevSel = sel;
    lastUserActivityMs = nowMs;      // při dlouhém přehrávání neusínat
    if (stopNow) break;

    size_t want = maxFrames * inFrameBytes;
    if (want > remaining) want = (remaining / inFrameBytes) * inFrameBytes;
    if (want == 0) break;
    size_t got = f.read(hudbaRaw, want);
    if (got == 0) break;
    remaining -= got;
    size_t inFrames = got / inFrameBytes;
    int16_t* out = hudbaBuf;
    for (size_t i = 0; i < inFrames; i++) {
      for (uint16_t ch = 0; ch < channels; ch++) {
        const uint8_t* p = hudbaRaw + (i * channels + ch) * bytesPerSample;
        int16_t s;
        if (bytesPerSample == 1) s = (int16_t)(((int)p[0] - 128) << 8); // 8bit unsigned
        else if (bytesPerSample == 2) s = (int16_t)(p[0] | (p[1] << 8));
        else if (bytesPerSample == 3) s = (int16_t)(p[1] | (p[2] << 8)); // 24bit → horní 2 bajty
        else if (isFloat) { float fv; memcpy(&fv, p, 4); if (fv > 1.0f) fv = 1.0f; else if (fv < -1.0f) fv = -1.0f; s = (int16_t)(fv * 32767.0f); }
        else s = (int16_t)(p[2] | (p[3] << 8));                          // 32bit int → horní 2 bajty
        if (hudbaHlasitost != 10) {                                       // softwarova hlasitost (0..150 %)
          int32_t v = ((int32_t)s * hudbaHlasitost) / 10;
          if (v > 32767) v = 32767; else if (v < -32768) v = -32768;      // orez (nad 100 % zkresluje)
          s = (int16_t)v;
        }
        *out++ = s;
        if (channels == 1) *out++ = s; // mono → stereo
      }
    }
    size_t bytesToWrite = (size_t)(out - hudbaBuf) * sizeof(int16_t);
    size_t written = 0;
    i2s_channel_write(i2sTx, hudbaBuf, bytesToWrite, &written, portMAX_DELAY);
  }

  f.close();
  i2sZavri();
  dacNapajeni(false);          // vypnout napájení audio modulu (LED zhasne)
  ulozHlasitostHudby();
  // Počkáme na uvolnění tlačítek, aby se hned nespustila další akce.
  while (digitalRead(BTN_SELECT) == LOW || digitalRead(BTN_DOWN) == LOW) { delay(10); lastUserActivityMs = millis(); }
  lastUserActivityMs = millis();
}

// ===== MP3 a další komprimované formáty (ESP32-audioI2S) =====
// Knihovna má vlastní audio task a neblokující loop(), takže hudba může hrát na
// pozadí (třeba při čtení knihy). Hlavní smyčka ji jen „krmí" přes audioTick().
// Vytváříme ji dynamicky a po skončení uvolníme, aby se I2S neuvolnilo WAV přehrávači.

// Zesílení nad 100 % (knihovna sama dává max 0 dB). Hook dostane vzorky PŘED
// hlasitostí; aplikujeme jen když je nastaveno víc než 100 %.
void audio_process_raw_samples(int32_t* outBuff, int16_t validSamples) {
  if (hudbaHlasitost <= 10) return;
  const int32_t num = hudbaHlasitost; // 11..15 = 110..150 %
  for (int i = 0; i < validSamples; i++) {
    int64_t v = (int64_t)outBuff[i] * num / 10;
    if (v > 2147483647LL) v = 2147483647LL; else if (v < -2147483648LL) v = -2147483648LL;
    outBuff[i] = (int32_t)v;
  }
}

static uint8_t hlasitostDoAudioLib() { // 0..10 -> 0..21 (nad 100 % už jen přes hook)
  int h = hudbaHlasitost; if (h > 10) h = 10;
  int v = (h * 21 + 5) / 10;
  if (v < 0) v = 0;
  if (v > 21) v = 21;
  return (uint8_t)v;
}

static void audioZastav() {
  if (gAudio) { gAudio->stopSong(); delete gAudio; gAudio = nullptr; }
  hudbaHrajeIdx = -1;
  dacNapajeni(false);
}

// Spustí přehrávání NA POZADÍ (neblokující). Vrací true při úspěchu.
static bool prehrajAudioLibStart(const char* cesta) {
  audioZastav();
  dtUvolniPriNedostatku();  // hudba si bere PSRAM – uvolni cache knih, pokud je ji málo
  dacNapajeni(true);
  delay(100);
  gAudio = new Audio();
  if (!gAudio) { dtUvolni(); gAudio = new Audio(); }  // druhá šance: uvolni cache textů
  if (!gAudio) { dacNapajeni(false); zobrazHudbaZprava("Malo pameti pro dekoder"); return false; }
  gAudio->setPinout(I2S_BCLK_PIN, I2S_LRCK_PIN, I2S_DIN_PIN);
  gAudio->setVolume(hlasitostDoAudioLib());
  if (!gAudio->connecttoFS(SD, cesta)) {
    audioZastav();
    zobrazHudbaZprava("Soubor nelze prehrat");
    return false;
  }
  return true;
}

// Voláno z hlavní smyčky: krmí dekodér a hlídá konec skladby.
void audioTick() {
  if (!gAudio) return;
  gAudio->loop();
  if (!gAudio->isRunning()) { // dohráno
    audioZastav();
    ulozHlasitostHudby();
    if (appState == STATE_HUDBA) zobrazHudbaSeznam();
  }
  lastUserActivityMs = millis(); // během přehrávání neusínat
}

// Rozhodne podle přípony: WAV = vlastní (blokující) přehrávač se zesílením,
// ostatní = dekodér knihovny (hraje na pozadí).
bool prehrajHudbu(const char* cesta) {
  String low = String(cesta); low.toLowerCase();
  if (low.endsWith(".wav")) {
    audioZastav();      // uvolní I2S pro vlastní WAV přehrávač
    prehrajWav(cesta);
    return false;       // WAV se přehrává na popředí
  }
  return prehrajAudioLibStart(cesta); // běží na pozadí
}

// ===== USB SD KARTA (Mass Storage — SD jako USB disk) =====
// Po pripojeni k PC se SD karta zobrazi jako USB flash → da se na ni kopirovat.
// V Arduinu musi byt: Tools → USB Mode = "USB-OTG (TinyUSB)"
//               a zaroven: Tools → USB CDC On Boot = "Disabled"
// (USB-OTG a seriova USB konzole sdili stejne piny, nemohou bezet soucasne.)
static bool usbSdAktivni = false;
static String usbSdStav = "";

#if USB_SD_MOZNE
static USBMSC usbMsc;

static int32_t usbSdOnRead(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize) {
  if (offset != 0) return -1;
  uint32_t sec = SD.sectorSize();
  if (!sec) return -1;
  for (uint32_t x = 0; x < bufsize / sec; x++) {
    if (!SD.readRAW((uint8_t*)buffer + (x * sec), lba + x)) return -1;
  }
  return (int32_t)bufsize;
}
static int32_t usbSdOnWrite(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize) {
  if (offset != 0) return -1;
  uint32_t sec = SD.sectorSize();
  if (!sec) return -1;
  for (uint32_t x = 0; x < bufsize / sec; x++) {
    if (!SD.writeRAW(buffer + (x * sec), lba + x)) return -1;
  }
  return (int32_t)bufsize;
}
static bool usbSdOnStartStop(uint8_t power_condition, bool start, bool load_eject) { return true; }
#endif

void zobrazUsbSd() {
  beginUiFrame(true);
  do {
    display.fillScreen(bgColor());
    u8g2Fonts.setFontMode(1);
    u8g2Fonts.setForegroundColor(fgColor()); u8g2Fonts.setBackgroundColor(bgColor());
    u8g2Fonts.setFont(getTitleFont()); u8g2Fonts.setCursor(10, 20); u8g2Fonts.print("USB SD KARTA");
    nakresliStatusBar();
    display.drawFastHLine(0, 25, display.width(), fgColor());
    u8g2Fonts.setFont(getBodyFont());
#if USB_SD_MOZNE
    if (usbSdStav.length()) {
      u8g2Fonts.setCursor(10, 60); u8g2Fonts.print(usbSdStav.c_str());
    } else {
      u8g2Fonts.setCursor(10, 52); u8g2Fonts.print("Karta je pripojena k PC");
      u8g2Fonts.setFont(getSmallFont());
      u8g2Fonts.setCursor(10, 78); u8g2Fonts.print("Na PC se objevi jako disk.");
      u8g2Fonts.setCursor(10, 96); u8g2Fonts.print("Nejdriv ji na PC vysun (Eject),");
      napoveda(10, 114, "pak drz BTN0 = ukoncit.");
    }
#else
    u8g2Fonts.setCursor(10, 48); u8g2Fonts.print("Nedostupne.");
    u8g2Fonts.setFont(getSmallFont());
#if USB_SD_HW_OK
    u8g2Fonts.setCursor(10, 70); u8g2Fonts.print("USB-OTG blokuje seriova");
    u8g2Fonts.setCursor(10, 86); u8g2Fonts.print("konzole. V Arduinu nastav:");
    u8g2Fonts.setCursor(10, 104); u8g2Fonts.print("Tools -> USB CDC On Boot");
    u8g2Fonts.setCursor(10, 120); u8g2Fonts.print("-> Disabled");
#else
    u8g2Fonts.setCursor(10, 78); u8g2Fonts.print("Nastav v Arduinu:");
    u8g2Fonts.setCursor(10, 96); u8g2Fonts.print("Tools -> USB Mode");
    u8g2Fonts.setCursor(10, 114); u8g2Fonts.print("-> USB-OTG (TinyUSB)");
#endif
#endif
  } while (display.nextPage());
}

void usbSdZapni() {
  usbSdStav = "";
#if USB_SD_MOZNE
  if (!sdReady) { usbSdStav = "SD karta neni pripravena"; }
  else {
    uint32_t sektoru = SD.numSectors();
    uint16_t velikost = SD.sectorSize();
    if (!sektoru || !velikost) { usbSdStav = "SD karta nema platnou kapacitu"; }
    else {
      audioZastav();                 // nic nesmi drzet SD
      usbMsc.vendorID("HonzueINK");
      usbMsc.productID("SD Card");
      usbMsc.productRevision("1.0");
      usbMsc.onRead(usbSdOnRead);
      usbMsc.onWrite(usbSdOnWrite);
      usbMsc.onStartStop(usbSdOnStartStop);
      usbMsc.mediaPresent(true);     // medium MUSI byt pritomne uz pri enumeraci
      usbMsc.isWritable(true);
      usbMsc.begin(sektoru, velikost);
      USB.begin();                   // spusti USB-OTG az ted (bez CDC na bootu)
      usbSdAktivni = true;
    }
  }
#endif
  zobrazUsbSd();
}

void usbSdVypni() {
#if USB_SD_MOZNE
  if (!usbSdAktivni) return;
  usbMsc.end();
  usbSdAktivni = false;
  // USB-OTG nelze za behu "odpojit" - restart uvolni disk na PC
  // a zaroven znovu namountuje SD kartu pro ESP.
  delay(150);
  ESP.restart();
#endif
}

// ===== SETUP =====
void setup() {
  // Serial vypnut kvůli uvolnění GPIO43/44 (mikrofon SD, buzzer1).
  // Sériovou konzoli stejně nevidíš (USB CDC On Boot = Disabled).
  // Serial.print bez begin() je bezpečný no-op.
  // Serial.begin(115200);
  setenv("TZ", TZ_PRAGUE, 1);
  tzset();
  // --- VLOŽ TESTOVACÍ KÓD SEM ---
  Serial.println("--- KONTROLA PAMETI ---");
  Serial.print("Celkova PSRAM: ");
  Serial.println(ESP.getPsramSize());
  Serial.print("Volna PSRAM: ");
  Serial.println(ESP.getFreePsram());
  // ------------------------------
  randomSeed(analogRead(0) + millis());
  pinMode(Vext, OUTPUT); digitalWrite(Vext, HIGH); delay(100);
  dacNapajeni(false); // audio modul je defaultně bez napájení (LED nesvítí)
  pinMode(LED_PIN, OUTPUT); digitalWrite(LED_PIN, LOW);
  // Časovač pro dýchání LED (30 ms) — používá se při stahování a web uploadu
  {
    esp_timer_create_args_t ledArgs = {};
    ledArgs.callback = &ledCasovacCb;
    ledArgs.dispatch_method = ESP_TIMER_TASK;
    ledArgs.name = "led";
    esp_timer_handle_t ledTimer = nullptr;
    if (esp_timer_create(&ledArgs, &ledTimer) == ESP_OK) esp_timer_start_periodic(ledTimer, 30000);
  }
  // ADC_CTRL (GPIO46): ovládá bateriový dělič — LOW = vypnuto (šetří energii)
  pinMode(ADC_CTRL, OUTPUT); digitalWrite(ADC_CTRL, LOW);
  analogSetPinAttenuation(BAT_PIN, ADC_11db);

  SPI.begin(2, SD_MISO_PIN, 1, 3); 
  display.init(); 
  display.setRotation(1);
  
  u8g2Fonts.begin(display); 
  u8g2Fonts.setFontMode(1);

  // Probuzení z deep sleep: e-ink si drží poslední obraz, proto splash
  // ("Probouzím se...") nekreslíme — ušetříme jedno plné překreslení panelu.
  bool probuzeniZDeepSleep = (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0 && rtc_bylDeepSleep);
  if (probuzeniZDeepSleep) gPrvniRamPoProbuzeni = true;

  if (!probuzeniZDeepSleep) {
    display.setFullWindow();
    display.firstPage();
    do { 
      display.fillScreen(bgColor()); 
      u8g2Fonts.setFont(u8g2_font_helvB14_tf); 
      u8g2Fonts.setForegroundColor(fgColor());
      u8g2Fonts.setBackgroundColor(bgColor());
      const char* loadTxt = (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0 && rtc_bylDeepSleep) ? "Probouzím se..." : "Startuji systém...";
      int textWidth = u8g2Fonts.getUTF8Width(loadTxt);
      u8g2Fonts.setCursor((display.width() - textWidth) / 2, display.height() / 2 + 5);
      u8g2Fonts.print(loadTxt);
    } while (display.nextPage());
  }
  pinMode(BTN_DOWN, INPUT_PULLUP); pinMode(BTN_SELECT, INPUT_PULLUP);
  encGlobalInit();      // rotační encoder — přerušení běží po celou dobu
  touchKalibruj();      // výchozí kalibrace dotykového D-padu

  // SD karta: inicializace na pozadí (bez blokující obrazovky při startu)
  // pripravSD() volá SD.begin() a ověří/vytvoří adresářovou strukturu.
  // Při probuzení z deep sleep složky neověřujeme (už existují) — rychlejší start.
  sdReady = pripravSD(probuzeniZDeepSleep);
  obnovAktivniDatoveSoubory();
  
  nactiPoziceKnih(); gbNode = nactiGBPozici();
  
  prefs.begin("nastaveni", true);
  autoSleepIdx = prefs.getInt("sleepIdx", 6);
  slovnikLangIdx = prefs.getInt("dictLang", 0);
  napovedaZapnuta = prefs.getBool("help", true);
  prefs.end();
  vzhledItems[5] = napovedaZapnuta ? "Nápověda: ZAP" : "Nápověda: VYP";
  if (autoSleepIdx < 0 || autoSleepIdx >= autoSleepCount) autoSleepIdx = 6;
  if (slovnikLangIdx < 0 || slovnikLangIdx >= slovnikLangCount) slovnikLangIdx = 0;
  lastUserActivityMs = millis();

  // LittleFS init: nejdriv bez formatu, pak jednorazovy pokus s formatem.
  littlefsReady = LittleFS.begin(false);
  if (!littlefsReady) {
    Serial.println("LittleFS: montaz selhala, zkousim opravu formatem...");
    littlefsReady = LittleFS.begin(true);
  }
  if (!littlefsReady) {
    Serial.println("LittleFS: nelze pripojit ani po formatu (cache pojede jen ze SD).");
  } else {
    Serial.println("LittleFS: OK");
  }

  // 1. NAČTEME DATA Z ULOŽENÉ PAMĚTI (SD / LittleFS)
  nactiStazenaData();

  // 2. Při restartu (ne deep sleep) — NEPŘIPOJOVAT se k WiFi automaticky.
  // Aktualizaci si uživatel vynutí sám přes Nastavení → Aktualizace → Vynutit nyní.
  // (Automatická periodická aktualizace v loop() zůstává funkční podle nastaveného intervalu.)

  // ===== OBNOVA STAVU PO PROBUZENÍ Z DEEP SLEEP =====
  if (probuzeniZDeepSleep) {
    rtc_bylDeepSleep  = false; // Resetovat příznak — příště startujeme čistě
    menuIndex         = rtc_menuIndex;
    scrollOffset      = rtc_scrollOffset;
    subMenuIndex      = rtc_subMenuIndex;
    subScrollOffset   = rtc_subScrollOffset;
    clanekMenuIndex   = rtc_clanekMenuIndex;
    textScrollPage    = rtc_textScrollPage;
    pocasiDen         = rtc_pocasiDen;
    horoskopMenuIndex = rtc_horoskopIdx;
    grafMenuIndex     = rtc_grafIdx;
    appState          = (AppState)rtc_appState;

    Serial.print("Deep sleep wakeup — obnova stavu: "); Serial.println(rtc_appState);

    // Obnovit obrazovku dle uloženého stavu
    switch (appState) {
      case STATE_AKTUALITY:
        zobrazSubMenu("AKTUALITY", aktualityItems, aktualityCount, subMenuIndex, subScrollOffset); break;
      case STATE_KALENDAR_MENU:
        zobrazSubMenu("KALENDÁŘ", kalendarMenuItems, kalendarMenuCount, subMenuIndex, subScrollOffset); break;
      case STATE_KALENDAR_TYDEN:
        kalObsah = nactiTextZeSD(String(KAL_DIR) + "/kalendar.txt"); zobrazKalTyden(); break;
      case STATE_KALENDAR_MESIC:
        kalObsah = nactiTextZeSD(String(KAL_DIR) + "/kalendar.txt"); zobrazKalMesic(); break;
      case STATE_KALENDAR_DEN:
        kalObsah = nactiTextZeSD(String(KAL_DIR) + "/kalendar.txt"); zobrazKalDetail(); break;
      case STATE_RECEPTY_SEZNAM:
        nactiReceptySeznam();
        if (receptyCount > 0) zobrazSubMenu("RECEPTY", receptyItems, receptyCount, subMenuIndex, subScrollOffset);
        break;
      case STATE_RECEPT_DETAIL:
        nactiReceptySeznam();
        nactiRecept(subMenuIndex);
        zobrazReceptDetail();
        break;
      case STATE_ZPRAVY_MENU:
        zobrazSubMenu("ZPRÁVY", zpravyMenuItems, zpravyMenuCount, subMenuIndex, subScrollOffset); break;
      case STATE_ZPRAVY_SEZNAM:
        // subMenuIndex nese kategorii: 0=svět, 1=ČR, 2=tech
        zpravyZHledani = false;
        if      (subMenuIndex == 0) parsujZpravy(stazenaDataSvet);
        else if (subMenuIndex == 1) parsujZpravy(stazenaDataCR);
        else if (subMenuIndex == 2) parsujZpravy(stazenaDataTech);
        else                        parsujZpravy(stazenaDataPozitivni);
        if (clanekMenuIndex >= pocetZprav) clanekMenuIndex = 0;
        zobrazSeznamZprav(); break;
      case STATE_ZPRAVY_TEXT:
        if      (subMenuIndex == 0) parsujZpravy(stazenaDataSvet);
        else if (subMenuIndex == 1) parsujZpravy(stazenaDataCR);
        else if (subMenuIndex == 2) parsujZpravy(stazenaDataTech);
        else                        parsujZpravy(stazenaDataPozitivni);
        if (clanekMenuIndex >= pocetZprav) clanekMenuIndex = 0;
        nactiDetailClanku(clanekMenuIndex);
        if (aktualniZpravy[clanekMenuIndex].text.length() == 0)
          aktualniZpravy[clanekMenuIndex].text = aktualniZpravy[clanekMenuIndex].perex;
        zobrazText(aktualniZpravy[clanekMenuIndex].titulek.c_str(),
                   aktualniZpravy[clanekMenuIndex].text.c_str()); break;
      case STATE_HODINY:
        lastSecHodiny = -1; zobrazSvetovyCas(); break;
      case STATE_KURZY:
        parsujKurzy(stazenaDataKurzy); zobrazKurzyUI(); break;
      case STATE_KURZY_GRAF:
        parsujKurzy(stazenaDataKurzy); zobrazKurzGraf(); break;
      case STATE_POCASI_UI:
        zobrazPocasiDen(pocasiDen); break;
      case STATE_HOROSKOP_MENU:
        parsujHoroskopy(stazenaDataHoroskop);
        zobrazSubMenu("HOROSKOP", horoskopItems, horoskopCount, horoskopMenuIndex, 0); break;
      case STATE_HOROSKOP_DETAIL:
        parsujHoroskopy(stazenaDataHoroskop);
        zobrazHoroskopDetail(horoskopMenuIndex); break;
      case STATE_KNIHOVNA:
        nactiSeznamKnih();
        zobrazSubMenu("KNIHOVNA", dynamicKnihovnaItems, dynamicKnihovnaCount, subMenuIndex, subScrollOffset); break;
      case STATE_KNIHOVNA_DETAIL:
        nactiSeznamKnih();
        zobrazKnihu(subMenuIndex);
        break;
      case STATE_GAMEBOOK:
        gbNode = nactiGBPozici(); gbTextPage = 0; zobrazGBNode(); break;
      case STATE_HRY:
        zobrazSubMenu("HRY", hryItems, hryCount, subMenuIndex, subScrollOffset); break;
      case STATE_GENERATOR:
        zobrazSubMenu("GENERÁTOR", generatorItems, generatorCount, subMenuIndex, subScrollOffset); break;
      case STATE_NASTAVENI:
        zobrazSubMenu("NASTAVENÍ", nastaveniItems, nastaveniCount, subMenuIndex, subScrollOffset); break;
      case STATE_NASTAVENI_WIFI:
        zobrazSubMenu("WIFI", wifiMenuItems, wifiMenuCount, subMenuIndex, subScrollOffset); break;
      case STATE_WIFI_SEZNAM:
      case STATE_WIFI_HESLO:
      case STATE_WIFI_POTVRDIT:
        // Seznam sítí ani rozepsané heslo nepřežijí deep sleep → zpět do WiFi menu
        appState = STATE_NASTAVENI_WIFI; subMenuIndex = 0; subScrollOffset = 0;
        zobrazSubMenu("WIFI", wifiMenuItems, wifiMenuCount, 0, 0); break;
      case STATE_TOOLBOX:
        zobrazSubMenu("TOOLBOX", toolboxItems, toolboxCount, subMenuIndex, subScrollOffset); break;
      case STATE_SLOVNIK: case STATE_SLOVNIK_JAZYK:
        zobrazSubMenu("SLOVNÍK", slovnikItems, slovnikCount, subMenuIndex, subScrollOffset); break;
      default:
        appState = STATE_MAIN_MENU;
        zobrazSubMenu("HLAVNÍ MENU", mainMenuItems, mainMenuCount, menuIndex, scrollOffset); break;
    }
  } else {
    // Normální start — hlavní menu
    zobrazSubMenu("HLAVNÍ MENU", mainMenuItems, mainMenuCount, menuIndex, scrollOffset);
  }
}

// ===================================================================
// GLOBÁLNÍ OVLÁDÁNÍ: rotační encoder + dotykový D-pad
// - encoder: doprava = v menu dolů, doleva = nahoru, stisk = potvrdit/vstoupit
// - klávesnice: doprava/doleva = posun kurzoru, stisk = potvrdit znak
// - touch: nahoru/dolů = posun, doprava = vstoupit, doleva = zpět
// Stávající BTN0/BTN21 zůstávají beze změny (jen k nim přibyly simulované stisky).
// ===================================================================
int simBtn0Pocet  = 0;   // kolik "stisků BTN0" (dolů) ještě provést
int simBtn21Pocet = 0;   // kolik "stisků BTN21" (vstoupit) ještě provést

static unsigned long touchGPollCas = 0;
static unsigned long touchGAkceCas = 0;
static bool touchGStisk[TOUCH_PIN_COUNT] = { false };

bool jeTestStav() {
  return appState == STATE_TOUCH_TEST || appState == STATE_ENCODER_TEST ||
         appState == STATE_MIC_TEST   || appState == STATE_BUZZER_TEST ||
         appState == STATE_OPICAK     || appState == STATE_TEZBA;
}

bool jeKlavesnice() {
  return appState == STATE_SLOVNIK_DETAIL ||
         appState == STATE_WIFI_HESLO ||
         appState == STATE_ARCHIV_HLEDANI;
}

void klavesnicePosun(int d) {
  if (d == 0) return;
  if (appState == STATE_SLOVNIK_DETAIL) {
    int dd = d % abcCount; if (dd < 0) dd += abcCount;
    abcKurzor = (abcKurzor + dd) % abcCount;
    nastavOblastPrekresleni(0, 20, display.width(), 34);
    zobrazHledani();
  } else if (appState == STATE_WIFI_HESLO) {
    int dd = d % wifiKlavesZnakuPocet; if (dd < 0) dd += wifiKlavesZnakuPocet;
    wifiKlavesKurzor = (wifiKlavesKurzor + dd) % wifiKlavesZnakuPocet;
    wifiHesloStatus = "";
    wifiKlavesPrekresli(false);
  } else if (appState == STATE_ARCHIV_HLEDANI) {
    int dd = d % archivHledaniZnakuPocet; if (dd < 0) dd += archivHledaniZnakuPocet;
    hledaniKurzor = (hledaniKurzor + dd) % archivHledaniZnakuPocet;
    archivHledaniPrekresli(false);
  }
}

// Počet sloupců znakové mřížky aktuální klávesnice.
int klavesniceSloupcu() {
  if (appState == STATE_SLOVNIK_DETAIL) return 13;   // slovník: 26 znaků / 13 sloupců
  return wifiKlavesSloupcu;                          // WiFi i archiv: 16 sloupců
}

// Pohyb kurzorem o dx sloupců a dy řádků (s obalením). Používá D-pad.
void klavesnicePosunSmer(int dx, int dy) {
  int cols  = klavesniceSloupcu();
  int count = (appState == STATE_SLOVNIK_DETAIL) ? abcCount
            : (appState == STATE_WIFI_HESLO)     ? wifiKlavesZnakuPocet
                                                 : archivHledaniZnakuPocet;
  int krok = dx + dy * cols;
  int dd = krok % count; if (dd < 0) dd += count;
  if (appState == STATE_SLOVNIK_DETAIL) {
    abcKurzor = (abcKurzor + dd) % abcCount;
    nastavOblastPrekresleni(0, 20, display.width(), 34);
    zobrazHledani();
  } else if (appState == STATE_WIFI_HESLO) {
    wifiKlavesKurzor = (wifiKlavesKurzor + dd) % wifiKlavesZnakuPocet;
    wifiHesloStatus = "";
    wifiKlavesPrekresli(false);
  } else if (appState == STATE_ARCHIV_HLEDANI) {
    hledaniKurzor = (hledaniKurzor + dd) % archivHledaniZnakuPocet;
    archivHledaniPrekresli(false);
  }
}

void klavesnicePotvrd() {
  if (appState == STATE_SLOVNIK_DETAIL) {
    if (hledanyLen < 11) { hledanyText[hledanyLen] = abeceda[abcKurzor] + 32; hledanyLen++; hledanyText[hledanyLen] = '\0'; }
    zobrazHledani();
  } else if (appState == STATE_WIFI_HESLO) {
    wifiKlavesVyber();
  } else if (appState == STATE_ARCHIV_HLEDANI) {
    archivHledaniVyber();
  }
}

// Posun nahoru v menu (protiklad BTN0 = dolů).
void akceNahoru() {
  switch (appState) {
    case STATE_MAIN_MENU: menuUp(menuIndex, scrollOffset, mainMenuCount); zobrazSubMenu("HLAVNÍ MENU", mainMenuItems, mainMenuCount, menuIndex, scrollOffset); break;
    case STATE_HUDBA: if (hudbaMenuCount() > 0) { menuUp(subMenuIndex, subScrollOffset, hudbaMenuCount()); zobrazHudbaSeznam(); } break;
    case STATE_KNIHOVNA: menuUp(subMenuIndex, subScrollOffset, dynamicKnihovnaCount); zobrazSubMenu("KNIHOVNA", dynamicKnihovnaItems, dynamicKnihovnaCount, subMenuIndex, subScrollOffset); break;
    case STATE_AKTUALITY: menuUp(subMenuIndex, subScrollOffset, aktualityCount); zobrazSubMenu("AKTUALITY", aktualityItems, aktualityCount, subMenuIndex, subScrollOffset); break;
    case STATE_KALENDAR_MENU: menuUp(subMenuIndex, subScrollOffset, kalendarMenuCount); zobrazSubMenu("KALENDÁŘ", kalendarMenuItems, kalendarMenuCount, subMenuIndex, subScrollOffset); break;
    case STATE_KALENDAR_TYDEN: kalDenIdx = (kalDenIdx + 6) % 7; zobrazKalTyden(); break;
    case STATE_KALENDAR_MESIC: kalMesicPohyb(0, -1); break;
    case STATE_KALENDAR_DEN: if (kalStranka > 0) kalStranka--; zobrazKalDetail(); break;
    case STATE_JR_SEZNAM: if (jrCount > 0) { menuUp(subMenuIndex, subScrollOffset, jrCount); zobrazSubMenu("SPOJE", jrItems, jrCount, subMenuIndex, subScrollOffset); } break;
    case STATE_ZPRAVY_MENU: menuUp(subMenuIndex, subScrollOffset, zpravyMenuCount); zobrazSubMenu("ZPRÁVY", zpravyMenuItems, zpravyMenuCount, subMenuIndex, subScrollOffset); break;
    case STATE_ZPRAVY_SEZNAM: if (pocetZprav > 0) {
        if (zpravyZArchivu && clanekMenuIndex == 0 && archivStranka > 0) {
          // Začátek stránky archivu — načti předchozích 20 článků
          archivStranka--;
          parsujZpravyStrankuZeSouboru(archivOtevrenySoubor, archivStranka * 20);
          clanekMenuIndex = pocetZprav - 1;
        } else if (!zpravyZArchivu && !zpravyZHledani && clanekMenuIndex == 0 && aktualniStranka > 0) {
          // Začátek stránky aktualit — načti předchozích 20 článků
          aktualniStranka--;
          _nactiStrankuAktualit(aktualniStranka * 20);
          clanekMenuIndex = pocetZprav - 1;
        } else if (zpravyZHledani && clanekMenuIndex == 0 && hledaniStranka > 0) {
          // Začátek stránky výsledků hledání — načti předchozích 20
          hledaniStranka--;
          _nactiStrankuHledani(hledaniStranka * 20);
          clanekMenuIndex = pocetZprav - 1;
        } else {
          clanekMenuIndex = (clanekMenuIndex - 1 + pocetZprav) % pocetZprav;
        }
        nastavOblastPrekresleni(0, 0, display.width(), 110); zobrazSeznamZprav();
      } break;
    // Listování v textu zpět (o stránku) — ovládá se enkoderem doleva
    case STATE_ZPRAVY_TEXT:
      if (textScrollPage > 0) { textScrollPage--; zobrazText(aktualniZpravy[clanekMenuIndex].titulek.c_str(), aktualniZpravy[clanekMenuIndex].text.c_str()); }
      break;
    case STATE_KNIHOVNA_DETAIL:
      if (textScrollPage > 0) { textScrollPage--; ulozPoziciKnihy(subMenuIndex, textScrollPage); zobrazKnihu(subMenuIndex); }
      break;
    case STATE_SD_TEXT_VIEW:
      if (textScrollPage > 0) { textScrollPage--; zobrazText(sdTextViewTitle.c_str(), sdTextViewBuffer.c_str()); }
      break;
    case STATE_ARCHIV_DATUMY: menuUp(archivDatumIndex, archivDatumScrollOffset, archivDatumCount); zobrazSubMenu("ARCHIV ZPRÁV", archivDatumy, archivDatumCount, archivDatumIndex, archivDatumScrollOffset); break;
    case STATE_ARCHIV_ZPRAVY_MENU: menuUp(archivZpravySubIndex, archivZpravyScrollOffset, 4); zobrazSubMenu(("ARCHIV: " + archivAktualniDatum).c_str(), zpravyMenuItems, 4, archivZpravySubIndex, archivZpravyScrollOffset); break;
    case STATE_TOOLBOX: menuUp(subMenuIndex, subScrollOffset, toolboxCount); zobrazSubMenu("TOOLBOX", toolboxItems, toolboxCount, subMenuIndex, subScrollOffset); break;
    case STATE_CASOVAC:
      if (!casovacBezi && !casovacZvoni) { casovacMin--; if (casovacMin < 1) casovacMin = 180; zobrazCasovac(); }
      break;
    case STATE_KALKULACKA: kalkKurzor = (kalkKurzor + 17) % 18; zobrazKalkulacka(); break;
    case STATE_RECEPTY_SEZNAM: if (receptyCount > 0) { menuUp(subMenuIndex, subScrollOffset, receptyCount); zobrazSubMenu("RECEPTY", receptyItems, receptyCount, subMenuIndex, subScrollOffset); } break;
    case STATE_RECEPT_DETAIL: if (receptStranka > 0) receptStranka--; zobrazReceptDetail(); break;
    case STATE_DIKTAFON_MENU: menuUp(subMenuIndex, subScrollOffset, diktafonMenuCount); zobrazSubMenu("DIKTAFON", diktafonMenuItems, diktafonMenuCount, subMenuIndex, subScrollOffset); break;
    case STATE_DIKTAFON_SEZNAM: if (diktafonCount > 0) { menuUp(subMenuIndex, subScrollOffset, diktafonCount); zobrazSubMenu("DIKTAFON", diktafonItems, diktafonCount, subMenuIndex, subScrollOffset); } break;
    case STATE_QR_MENU: menuUp(subMenuIndex, subScrollOffset, qrCount); zobrazSubMenu("QR KÓDY", qrItems, qrCount, subMenuIndex, subScrollOffset); break;
    case STATE_SLOVNIK: menuUp(subMenuIndex, subScrollOffset, slovnikLangCount); zobrazSubMenu("JAZYK SLOVNÍKU", slovnikLangNazvy, slovnikLangCount, subMenuIndex, subScrollOffset); break;
    case STATE_SLOVNIK_JAZYK: menuUp(subMenuIndex, subScrollOffset, slovnikCount); zobrazSubMenu("SLOVNÍK", slovnikItems, slovnikCount, subMenuIndex, subScrollOffset); break;
    case STATE_FRAZE_MENU: menuUp(subMenuIndex, subScrollOffset, frazeMenuCount); zobrazSubMenu("FRÁZE", frazeMenuItems, frazeMenuCount, subMenuIndex, subScrollOffset); break;
    case STATE_GENERATOR: menuUp(subMenuIndex, subScrollOffset, generatorCount); zobrazSubMenu("GENERÁTOR", generatorItems, generatorCount, subMenuIndex, subScrollOffset); break;
    case STATE_HRY: menuUp(subMenuIndex, subScrollOffset, hryCount); zobrazSubMenu("HRY", hryItems, hryCount, subMenuIndex, subScrollOffset); break;
    case STATE_GAMEBOOK_MENU: menuUp(subMenuIndex, subScrollOffset, dynamicGamebookCount); zobrazSubMenu("GAMEBOOK", dynamicGamebookItems, dynamicGamebookCount, subMenuIndex, subScrollOffset); break;
    case STATE_KOSTKY_VYBER: menuUp(subMenuIndex, subScrollOffset, kostkyCount); zobrazSubMenu("KOSTKY", kostkyItems, kostkyCount, subMenuIndex, subScrollOffset); break;
    case STATE_KVIZ_KATEGORIE: menuUp(kvizKatIdx, kvizKatScrollOffset, kvizKategoriiCount); zobrazSubMenu("KATEGORIE KVÍZU", kvizKategorieItems, kvizKategoriiCount, kvizKatIdx, kvizKatScrollOffset); break;
    case STATE_KVIZ_OBTIZNOST: kvizObtIdx = (kvizObtIdx + 2) % 3; zobrazSubMenu("OBTÍŽNOST", kvizObtiznostItems, 3, kvizObtIdx, 0); break;
    case STATE_HOROSKOP_MENU: horoskopMenuIndex = (horoskopMenuIndex - 1 + horoskopCount) % horoskopCount; zobrazSubMenu("HOROSKOP", horoskopItems, horoskopCount, horoskopMenuIndex, 0); break;
    case STATE_KURZY_GRAF: grafMenuIndex = (grafMenuIndex - 1 + grafCount) % grafCount; zobrazKurzGraf(); break;
    case STATE_NASTAVENI: menuUp(subMenuIndex, subScrollOffset, nastaveniCount); zobrazSubMenu("NASTAVENÍ", nastaveniItems, nastaveniCount, subMenuIndex, subScrollOffset); break;
    case STATE_NASTAVENI_VZHLED: menuUp(subMenuIndex, subScrollOffset, vzhledCount); zobrazSubMenu("VZHLED", vzhledItems, vzhledCount, subMenuIndex, subScrollOffset); break;
    case STATE_NASTAVENI_AKTUALIZACE: menuUp(subMenuIndex, subScrollOffset, aktualizaceCount); zobrazSubMenu("AKTUALIZACE", aktualizaceItems, aktualizaceCount, subMenuIndex, subScrollOffset); break;
    case STATE_NASTAVENI_AUTOUSPANI: menuUp(subMenuIndex, subScrollOffset, autoSleepCount); zobrazSubMenu("AUTO USPANI", autoSleepItems, autoSleepCount, subMenuIndex, subScrollOffset); break;
    case STATE_NASTAVENI_WIFI: menuUp(subMenuIndex, subScrollOffset, wifiMenuCount); zobrazSubMenu("WIFI", wifiMenuItems, wifiMenuCount, subMenuIndex, subScrollOffset); break;
    case STATE_WIFI_SEZNAM: if (wifiPocetSiti > 0) { wifiSitIndex = (wifiSitIndex - 1 + wifiPocetSiti) % wifiPocetSiti; nastavOblastPrekresleni(0, 42, display.width(), 70); zobrazWifiSeznam(); } break;
    case STATE_SD_BROWSER:
      if (sdEntryCount > 0) {
        int visible = getVisibleMenuItems();
        sdBrowserIndex = (sdBrowserIndex - 1 + sdEntryCount) % sdEntryCount;
        if (sdBrowserIndex < sdBrowserScrollOffset) sdBrowserScrollOffset = sdBrowserIndex;
        if (sdBrowserIndex >= sdBrowserScrollOffset + visible) sdBrowserScrollOffset = sdBrowserIndex - visible + 1;
        zobrazSDProhlizec();
      }
      break;
    default: break;
  }
}

void obsluzEncoder() {
  int krok = encPozice / ENC_KROKY_NA_CVAK;
  // Při stisku kolečka se mechanika nepatrně pootočí → ignorovat kroky během
  // stisku a 120 ms po něm (jinak menu občas uskočí).
  if (digitalRead(ENC_SW_PIN) == LOW || (millis() - encTlacitkoCas) < 120) {
    encKrokPosledni = krok;
    return;
  }
  int delta = krok - encKrokPosledni;
  if (delta == 0) return;
  encKrokPosledni = krok;
  lastUserActivityMs = millis();
  if (jeTestStav()) return;
  if (jeKlavesnice()) { klavesnicePosun(delta); return; }

  int n = (delta > 0) ? delta : -delta;
  for (int i = 0; i < n; i++) {
    if (delta > 0) simBtn0Pocet++;   // doprava = dolů (reuse stávající logiky BTN0)
    else           akceNahoru();     // doleva = nahoru
  }
}

void obsluzEncoderTlacitko() {
  static bool minule = false;
  static unsigned long cas = 0;
  bool sw = (digitalRead(ENC_SW_PIN) == LOW);
  if (sw != minule) encTlacitkoCas = millis();   // náběžná i sestupná hrana
  if (sw && !minule && millis() - cas > 60) {
    cas = millis();
    lastUserActivityMs = millis();
    if (!jeTestStav()) {
      if (jeKlavesnice()) klavesnicePotvrd();
      else if (appState == STATE_RECEPT_DETAIL) simBtn0Pocet++;   // stisk = potvrdit / další stránka receptu
      else simBtn21Pocet++;          // stisk = potvrdit / vstoupit (logika BTN21)
    }
  }
  minule = sw;
}

void akceDotyk(int p) {   // 0=NAHORU, 1=DOLU, 2=DOLEVA, 3=DOPRAVA
  if (jeKlavesnice()) {
    // V klávesnici se D-padem jen pohybujeme po mřížce všemi směry.
    // (Potvrzení znaku zůstává na stisku encoderu / BTN21.)
    if (p == 0) klavesnicePosunSmer(0, -1);
    else if (p == 1) klavesnicePosunSmer(0, +1);
    else if (p == 2) klavesnicePosunSmer(-1, 0);
    else if (p == 3) klavesnicePosunSmer(+1, 0);
    return;
  }
  if (appState == STATE_KALENDAR_MESIC) {
    // V měsíční mřížce se D-padem pohybujeme po dnech všemi směry.
    if (p == 0) kalMesicPohyb(0, -1);
    else if (p == 1) kalMesicPohyb(0, +1);
    else if (p == 2) kalMesicPohyb(-1, 0);
    else if (p == 3) kalMesicPohyb(+1, 0);
    return;
  }
  if (appState == STATE_KALKULACKA) {
    // V kalkulačce se D-padem pohybujeme po klávesách všemi směry.
    if (p == 0)      kalkKurzor = (kalkKurzor + 12) % 18;
    else if (p == 1) kalkKurzor = (kalkKurzor + 6) % 18;
    else if (p == 2) kalkKurzor = (kalkKurzor + 17) % 18;
    else             kalkKurzor = (kalkKurzor + 1) % 18;
    zobrazKalkulacka();
    return;
  }
  if (p == 0) akceNahoru();
  else if (p == 1) simBtn0Pocet++;
  else if (p == 2) {
    // Doleva v textu = o stránku zpět; mimo text = návrat o úroveň výš
    if (appState == STATE_ZPRAVY_TEXT && textScrollPage > 0) {
      textScrollPage--;
      zobrazText(aktualniZpravy[clanekMenuIndex].titulek.c_str(), aktualniZpravy[clanekMenuIndex].text.c_str());
    } else if (appState == STATE_KNIHOVNA_DETAIL && textScrollPage > 0) {
      textScrollPage--;
      ulozPoziciKnihy(subMenuIndex, textScrollPage);
      zobrazKnihu(subMenuIndex);
    } else if (appState == STATE_SD_TEXT_VIEW && textScrollPage > 0) {
      textScrollPage--;
      zobrazText(sdTextViewTitle.c_str(), sdTextViewBuffer.c_str());
    } else {
      goBack();
    }
  }
  else if (p == 3) simBtn21Pocet++;
}

void obsluzTouchDpad() {
  if (jeTestStav()) return;
  if (millis() - touchGPollCas < 60) return;
  touchGPollCas = millis();

  uint32_t raw[TOUCH_PIN_COUNT];
  bool stisk[TOUCH_PIN_COUNT];
  bool nejaky = false;
  int  nej = -1; long nejPrebytek = 0;
  for (int p = 0; p < TOUCH_PIN_COUNT; p++) {
    raw[p] = touchCti(TOUCH_PINY[p]);
    long d = (long)raw[p] - (long)touchBaseA[p];
    stisk[p] = (d > (long)touchPrahA[p]);
    if (stisk[p]) {
      nejaky = true;
      // Jak silně zóna přesahuje svůj práh — podle toho vybereme vítěze.
      long prebytek = d - (long)touchPrahA[p];
      if (prebytek > nejPrebytek) { nejPrebytek = prebytek; nej = p; }
    }
  }
  if (!nejaky) {
    // pomalé dorovnání klidu (kompenzace driftu); práh zůstává z kalibrace
    for (int p = 0; p < TOUCH_PIN_COUNT; p++) {
      int32_t d = (int32_t)raw[p] - (int32_t)touchBaseA[p];
      touchBaseA[p] = (uint32_t)((int32_t)touchBaseA[p] + d / 64);
    }
  }
  // Jeden dotyk může překročit práh na víc ploškách současně (přeslechy).
  // Proto se provede jen NEJSILNĚJŠÍ zóna — dřív vyhrávala první v pořadí
  // (NAHORU), takže každý dotyk pak v článku otáčel stránku zpět.
  if (nej >= 0 && !touchGStisk[nej] && (millis() - touchGAkceCas) > 250) {
    touchGStisk[nej] = true;
    touchGAkceCas = millis();
    akceDotyk(nej);
  }
  for (int p = 0; p < TOUCH_PIN_COUNT; p++) {
    if (!stisk[p]) touchGStisk[p] = false;
  }
}

// ===== LOOP =====
void loop() {
  // Aktualizace dat se spouští jen ručně (Nastavení → Aktualizace → Vynutit nyní).
  if (!usbSdAktivni) audioTick();
  if (webUploadMode && webUploadServerReady) {
    webServer.handleClient();
    if (webUploadNeedsRefresh) {
      webUploadNeedsRefresh = false;
      zobrazWebUpload();
    }
  }

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

  // ===== ENCODER + DOTYKOVÝ D-PAD (globální ovládání) =====
  if (!usbSdAktivni && !webUploadMode) {
    obsluzEncoder();
    obsluzEncoderTlacitko();
    obsluzTouchDpad();
  }

  // ===== KLÁVESNICE HESLA / HLEDÁNÍ — neblokující obsluha tlačítek =====
  // Pro tyto stavy obsluhujeme tlačítka sami (aby krátký stisk reagoval hned)
  if (appState == STATE_WIFI_HESLO) {
    obsluhaWifiHesloKlavesnice();
  }
  if (appState == STATE_ARCHIV_HLEDANI) {
    obsluhaArchivHledaniKlavesnice();
  }

  // ===== BTN_DOWN (GPIO 0) =====
  if (appState != STATE_WIFI_HESLO && appState != STATE_ARCHIV_HLEDANI && (digitalRead(BTN_DOWN) == LOW || simBtn0Pocet > 0)) {
    delay(10); 
    bool fyz0 = (digitalRead(BTN_DOWN) == LOW);
    if (fyz0 || simBtn0Pocet > 0) {
      if (!fyz0) simBtn0Pocet--;
      lastUserActivityMs = millis();

      if (appState == STATE_MAIN_MENU && longPressBTN0()) {
        jdiSpat();
        return;
      }

      if (appState == STATE_KOSTKY) {
        while (digitalRead(BTN_DOWN) == LOW) {
          int rndMax = (kostkyStran == 66) ? 6 : kostkyStran;
          drawKostkaFrame(random(1, rndMax + 1), random(1, 7), kostkyStran, "Třesu...", false, 0.85f);
        }
        hrajKostku(kostkyStran); return;
      }
      if (appState == STATE_GAMEBOOK) {
        if (longPressBTN0()) {
          int volbaB;
          if (gbZSD) volbaB = gbVolbaBSD;
          else { memcpy_P(&volbaB, &gamebook[gbNode].volbaB, sizeof(int)); }
          if (volbaB >= 0) { gbNode = volbaB; gbTextPage = 0; ulozGBPozici(gbNode); zobrazGBNode(); }
        } else { gbTextPage++; zobrazGBNode(); }
        return;
      }
      if (appState == STATE_POCASI_UI) {
        if (longPressBTN0()) { goBack(); return; }
        pocasiDen = (pocasiDen + 1) % 7; zobrazPocasiDen(pocasiDen);
        while (digitalRead(BTN_DOWN) == LOW) delay(10); return;
      }

      if (appState == STATE_SLOVNIK_DETAIL) {
        // BTN0 = hledat (počkáme na puštění, aby se e-ink překreslil jen jednou)
        while (digitalRead(BTN_DOWN) == LOW) delay(10);
        zobrazHledani();
        return;
      }
      if (longPressBTN0()) { goBack(); return; }

      switch (appState) {
        case STATE_MAIN_MENU: menuDown(menuIndex, scrollOffset, mainMenuCount); zobrazSubMenu("HLAVNÍ MENU", mainMenuItems, mainMenuCount, menuIndex, scrollOffset); break;
        case STATE_HUDBA: if (hudbaMenuCount() > 0) { menuDown(subMenuIndex, subScrollOffset, hudbaMenuCount()); zobrazHudbaSeznam(); } break;
        case STATE_KNIHOVNA: menuDown(subMenuIndex, subScrollOffset, dynamicKnihovnaCount); zobrazSubMenu("KNIHOVNA", dynamicKnihovnaItems, dynamicKnihovnaCount, subMenuIndex, subScrollOffset); break;
        case STATE_KNIHOVNA_DETAIL: textScrollPage++; ulozPoziciKnihy(subMenuIndex, textScrollPage); zobrazKnihu(subMenuIndex); break;
        case STATE_AKTUALITY: menuDown(subMenuIndex, subScrollOffset, aktualityCount); zobrazSubMenu("AKTUALITY", aktualityItems, aktualityCount, subMenuIndex, subScrollOffset); break;
        case STATE_KALENDAR_MENU: menuDown(subMenuIndex, subScrollOffset, kalendarMenuCount); zobrazSubMenu("KALENDÁŘ", kalendarMenuItems, kalendarMenuCount, subMenuIndex, subScrollOffset); break;
        case STATE_KALENDAR_TYDEN: kalDenIdx = (kalDenIdx + 1) % 7; zobrazKalTyden(); break;
        case STATE_KALENDAR_MESIC: kalMesicPohyb(0, +1); break;
        case STATE_KALENDAR_DEN: kalStranka++; zobrazKalDetail(); break;
        case STATE_JR_SEZNAM: if (jrCount > 0) { menuDown(subMenuIndex, subScrollOffset, jrCount); zobrazSubMenu("SPOJE", jrItems, jrCount, subMenuIndex, subScrollOffset); } break;
        case STATE_JR_DETAIL: jrRealtimeChci = true; zobrazJrDetail(); break;
        case STATE_ZPRAVY_MENU: menuDown(subMenuIndex, subScrollOffset, zpravyMenuCount); zobrazSubMenu("ZPRÁVY", zpravyMenuItems, zpravyMenuCount, subMenuIndex, subScrollOffset); break;
        case STATE_ZPRAVY_SEZNAM: if (pocetZprav > 0) {
            if (zpravyZArchivu && clanekMenuIndex >= pocetZprav - 1 && (archivStranka + 1) * 20 < archivCelkemClanku) {
              // Konec stránky archivu — načti dalších 20 článků
              archivStranka++;
              parsujZpravyStrankuZeSouboru(archivOtevrenySoubor, archivStranka * 20);
              clanekMenuIndex = 0;
            } else if (!zpravyZArchivu && !zpravyZHledani && clanekMenuIndex >= pocetZprav - 1
                       && (aktualniStranka + 1) * 20 < aktualniCelkemClanku) {
              // Konec stránky aktualit — načti dalších 20 článků
              aktualniStranka++;
              _nactiStrankuAktualit(aktualniStranka * 20);
              clanekMenuIndex = 0;
            } else if (zpravyZHledani && clanekMenuIndex >= pocetZprav - 1
                       && (hledaniStranka + 1) * 20 < hledaniCelkem) {
              // Konec stránky výsledků hledání — načti dalších 20
              hledaniStranka++;
              _nactiStrankuHledani(hledaniStranka * 20);
              clanekMenuIndex = 0;
            } else {
              clanekMenuIndex = (clanekMenuIndex + 1) % pocetZprav;
            }
            nastavOblastPrekresleni(0, 0, display.width(), 110); zobrazSeznamZprav();
          } break;
        case STATE_ZPRAVY_TEXT: textScrollPage++; zobrazText(aktualniZpravy[clanekMenuIndex].titulek.c_str(), aktualniZpravy[clanekMenuIndex].text.c_str()); break;
        case STATE_SD_TEXT_VIEW: textScrollPage++; zobrazText(sdTextViewTitle.c_str(), sdTextViewBuffer.c_str()); break;
        case STATE_ARCHIV_DATUMY: menuDown(archivDatumIndex, archivDatumScrollOffset, archivDatumCount); zobrazSubMenu("ARCHIV ZPRÁV", archivDatumy, archivDatumCount, archivDatumIndex, archivDatumScrollOffset); break;
        case STATE_ARCHIV_ZPRAVY_MENU: menuDown(archivZpravySubIndex, archivZpravyScrollOffset, 4); zobrazSubMenu(("ARCHIV: " + archivAktualniDatum).c_str(), zpravyMenuItems, 4, archivZpravySubIndex, archivZpravyScrollOffset); break;
        case STATE_TOOLBOX: menuDown(subMenuIndex, subScrollOffset, toolboxCount); zobrazSubMenu("TOOLBOX", toolboxItems, toolboxCount, subMenuIndex, subScrollOffset); break;
        case STATE_RECEPTY_SEZNAM: if (receptyCount > 0) { menuDown(subMenuIndex, subScrollOffset, receptyCount); zobrazSubMenu("RECEPTY", receptyItems, receptyCount, subMenuIndex, subScrollOffset); } break;
        case STATE_RECEPT_DETAIL: receptStranka++; zobrazReceptDetail(); break;
        case STATE_DIKTAFON_MENU: menuDown(subMenuIndex, subScrollOffset, diktafonMenuCount); zobrazSubMenu("DIKTAFON", diktafonMenuItems, diktafonMenuCount, subMenuIndex, subScrollOffset); break;
        case STATE_DIKTAFON_SEZNAM: if (diktafonCount > 0) { menuDown(subMenuIndex, subScrollOffset, diktafonCount); zobrazSubMenu("DIKTAFON", diktafonItems, diktafonCount, subMenuIndex, subScrollOffset); } break;
        case STATE_TEZBA: tezbaReset(); zobrazTezbu(); break;
        case STATE_TOUCH_TEST: touchKalibruj(); zobrazTouchTest(); break;
        case STATE_ENCODER_TEST: encPozice = 0; encZobrPozice = 0; zobrazEncoderTest(); break;
        case STATE_MIC_TEST: micPeak = 0; zobrazMikrofonTest(); break;
        case STATE_BUZZER_TEST: buzzZahraj((buzzPreset + 1) % BUZZ_PRESETU); zobrazBuzzerTest(); break;
        case STATE_CASOVAC:
          if (casovacZvoni) { casovacTicho(); zobrazCasovac(); }
          else if (!casovacBezi) { casovacMin++; if (casovacMin > 180) casovacMin = 1; zobrazCasovac(); }
          break;
        case STATE_KALKULACKA: kalkKurzor = (kalkKurzor + 1) % 18; zobrazKalkulacka(); break;
        case STATE_QR_MENU: menuDown(subMenuIndex, subScrollOffset, qrCount); zobrazSubMenu("QR KÓDY", qrItems, qrCount, subMenuIndex, subScrollOffset); break;
        
        case STATE_SLOVNIK: menuDown(subMenuIndex, subScrollOffset, slovnikLangCount); zobrazSubMenu("JAZYK SLOVNÍKU", slovnikLangNazvy, slovnikLangCount, subMenuIndex, subScrollOffset); break;
        case STATE_SLOVNIK_JAZYK: menuDown(subMenuIndex, subScrollOffset, slovnikCount); zobrazSubMenu("SLOVNÍK", slovnikItems, slovnikCount, subMenuIndex, subScrollOffset); break;
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
        case STATE_GAMEBOOK_MENU: menuDown(subMenuIndex, subScrollOffset, dynamicGamebookCount); zobrazSubMenu("GAMEBOOK", dynamicGamebookItems, dynamicGamebookCount, subMenuIndex, subScrollOffset); break;
        case STATE_KOSTKY_VYBER: menuDown(subMenuIndex, subScrollOffset, kostkyCount); zobrazSubMenu("KOSTKY", kostkyItems, kostkyCount, subMenuIndex, subScrollOffset); break;
        
        case STATE_KVIZ: nastaviNahodnyKvizIdx(); zobrazKviz(); break;
        case STATE_KVIZ_ODPOVED: appState = STATE_KVIZ; nastaviNahodnyKvizIdx(); zobrazKviz(); break;
        case STATE_WYR: nastaviNahodnyWyrZSD(); zobrazWyr(); break;
        case STATE_WYR_VYSLEDEK: appState = STATE_WYR; nastaviNahodnyWyrZSD(); zobrazWyr(); break;
        
        case STATE_KVIZ_KATEGORIE: menuDown(kvizKatIdx, kvizKatScrollOffset, kvizKategoriiCount); zobrazSubMenu("KATEGORIE KVÍZU", kvizKategorieItems, kvizKategoriiCount, kvizKatIdx, kvizKatScrollOffset); break;
        case STATE_KVIZ_OBTIZNOST: kvizObtIdx = (kvizObtIdx + 1) % 3; zobrazSubMenu("OBTÍŽNOST", kvizObtiznostItems, 3, kvizObtIdx, 0); break;
        
        case STATE_HOROSKOP_MENU: horoskopMenuIndex = (horoskopMenuIndex + 1) % horoskopCount; zobrazSubMenu("HOROSKOP", horoskopItems, horoskopCount, horoskopMenuIndex, 0); break;
        case STATE_HOROSKOP_DETAIL: horoskopScrollPage++; zobrazHoroskopDetail(horoskopMenuIndex); break;
        case STATE_KURZY_GRAF: grafMenuIndex = (grafMenuIndex + 1) % grafCount; zobrazKurzGraf(); break;
        
        case STATE_NASTAVENI: menuDown(subMenuIndex, subScrollOffset, nastaveniCount); zobrazSubMenu("NASTAVENÍ", nastaveniItems, nastaveniCount, subMenuIndex, subScrollOffset); break;
        case STATE_NASTAVENI_VZHLED: menuDown(subMenuIndex, subScrollOffset, vzhledCount); zobrazSubMenu("VZHLED", vzhledItems, vzhledCount, subMenuIndex, subScrollOffset); break;
        case STATE_NASTAVENI_AKTUALIZACE: menuDown(subMenuIndex, subScrollOffset, aktualizaceCount); zobrazSubMenu("AKTUALIZACE", aktualizaceItems, aktualizaceCount, subMenuIndex, subScrollOffset); break;
        case STATE_NASTAVENI_AUTOUSPANI: menuDown(subMenuIndex, subScrollOffset, autoSleepCount); zobrazSubMenu("AUTO USPANI", autoSleepItems, autoSleepCount, subMenuIndex, subScrollOffset); break;
        case STATE_NASTAVENI_WIFI: menuDown(subMenuIndex, subScrollOffset, wifiMenuCount); zobrazSubMenu("WIFI", wifiMenuItems, wifiMenuCount, subMenuIndex, subScrollOffset); break;
        case STATE_WIFI_SEZNAM:
          if (wifiPocetSiti > 0) { wifiSitIndex = (wifiSitIndex + 1) % wifiPocetSiti; nastavOblastPrekresleni(0, 42, display.width(), 70); zobrazWifiSeznam(); }
          break;
        case STATE_WIFI_HESLO: break; // obsluhu řeší obsluhaWifiHesloKlavesnice() v loop()
        case STATE_WIFI_POTVRDIT:
          {
            // Krátký BTN0 = zadat jiné heslo (uložené předplníme, ať se dá upravit)
            String ulozene = "";
            nactiWifiPrihlaseni(wifiHesloSsid, ulozene);
            strncpy(wifiHesloBuf, ulozene.c_str(), sizeof(wifiHesloBuf) - 1);
            wifiHesloBuf[sizeof(wifiHesloBuf) - 1] = '\0';
            wifiHesloLen = strlen(wifiHesloBuf);
            wifiKlavesKurzor = 0;
            wifiHesloStatus = "";
            appState = STATE_WIFI_HESLO;
            zobrazWifiHeslo();
          }
          break;
        case STATE_WEB_UPLOAD: zobrazWebUpload(); break;
        
        case STATE_KURZY: appState = STATE_KURZY_GRAF; grafMenuIndex = 0; zobrazKurzGraf(); break;
        
        case STATE_SD_BROWSER:
          if (sdEntryCount > 0) {
            int visible = getVisibleMenuItems();
            sdBrowserIndex = (sdBrowserIndex + 1) % sdEntryCount;
            if (sdBrowserIndex >= sdBrowserScrollOffset + visible) sdBrowserScrollOffset++;
            if (sdBrowserIndex == 0) sdBrowserScrollOffset = 0;
            zobrazSDProhlizec();
          }
          break;
        
        case STATE_ODHALOVACKA: case STATE_ODHALOVACKA_DETAIL: goBack(); break;
        
        default: break;
      }
      if (fyz0) while (digitalRead(BTN_DOWN) == LOW) delay(10);
    }
  }

  // ===== BTN_SELECT (GPIO 21) =====
  if (appState != STATE_WIFI_HESLO && appState != STATE_ARCHIV_HLEDANI && (digitalRead(BTN_SELECT) == LOW || simBtn21Pocet > 0)) {
    delay(10); 
    bool fyz21 = (digitalRead(BTN_SELECT) == LOW);
    if (fyz21 || simBtn21Pocet > 0) {
      if (!fyz21) simBtn21Pocet--;
      lastUserActivityMs = millis();
      if (longPress()) {
        // V receptu držení BTN21 ubírá porce (krátký stisk přidává)
        if (appState == STATE_RECEPT_DETAIL) {
          if (receptyPorce > 1) receptyPorce--;
          zobrazReceptDetail();
          return;
        }
        goBack(); return;
      }

      switch (appState) {
        case STATE_MAIN_MENU:
          subMenuIndex = 0; subScrollOffset = 0; textScrollPage = 0;
          if (menuIndex == 0) { nactiSeznamKnih(); appState = STATE_KNIHOVNA; zobrazSubMenu("KNIHOVNA", dynamicKnihovnaItems, dynamicKnihovnaCount, 0, 0); }
          else if (menuIndex == 1) { appState = STATE_AKTUALITY; zobrazSubMenu("AKTUALITY", aktualityItems, aktualityCount, 0, 0); }
          else if (menuIndex == 2) { nactiHudbaSeznam(); appState = STATE_HUDBA; subMenuIndex = 0; subScrollOffset = 0; zobrazHudbaSeznam(); }
          else if (menuIndex == 3) { appState = STATE_TOOLBOX; zobrazSubMenu("TOOLBOX", toolboxItems, toolboxCount, 0, 0); }
          else if (menuIndex == 4) { appState = STATE_SLOVNIK; zobrazSubMenu("JAZYK SLOVNÍKU", slovnikLangNazvy, slovnikLangCount, 0, 0); }
          else if (menuIndex == 5) { appState = STATE_GENERATOR; zobrazSubMenu("GENERÁTOR", generatorItems, generatorCount, 0, 0); }
          else if (menuIndex == 6) { appState = STATE_HRY; zobrazSubMenu("HRY", hryItems, hryCount, 0, 0); }
          else if (menuIndex == 7) { appState = STATE_SD_BROWSER; sdCurrentPath = "/"; nactiSDSlozku(); zobrazSDProhlizec(); }
          else if (menuIndex == 8) { appState = STATE_NASTAVENI; zobrazSubMenu("NASTAVENÍ", nastaveniItems, nastaveniCount, 0, 0); }
          break;

        case STATE_HUDBA:
          {
            int off = gAudio ? 2 : 0;
            if (gAudio && subMenuIndex == 0) {          // zastavit přehrávání
              audioZastav(); ulozHlasitostHudby();
              subMenuIndex = 0; subScrollOffset = 0;
            } else if (gAudio && subMenuIndex == 1) {   // změnit hlasitost (cyklicky)
              hudbaHlasitost = (hudbaHlasitost + 1) % (HUDBA_VOL_MAX + 1);
              gAudio->setVolume(hlasitostDoAudioLib());
            } else {
              int idx = subMenuIndex - off;
              if (idx >= 0 && idx < hudbaCount) {
                bool bg = prehrajHudbu(hudbaCesty[idx].c_str());
                hudbaHrajeIdx = bg ? idx : -1;
                subMenuIndex = idx + (bg ? 2 : 0);
                subScrollOffset = 0;
              }
            }
            zobrazHudbaSeznam();
          }
          break;

        case STATE_KNIHOVNA: textScrollPage = buchaPozice[subMenuIndex]; appState = STATE_KNIHOVNA_DETAIL; zobrazKnihu(subMenuIndex); break;

        case STATE_AKTUALITY:
          if (subMenuIndex == 0) { appState = STATE_ZPRAVY_MENU; subMenuIndex = 0; subScrollOffset = 0; zobrazSubMenu("ZPRÁVY", zpravyMenuItems, zpravyMenuCount, 0, 0); }
          else if (subMenuIndex == 1) { appState = STATE_HODINY; lastSecHodiny = -1; zobrazSvetovyCas(); }
          else if (subMenuIndex == 2) { appState = STATE_KURZY; parsujKurzy(stazenaDataKurzy); zobrazKurzyUI(); }
          else if (subMenuIndex == 3) { appState = STATE_POCASI_UI; pocasiDen = 0; zobrazPocasiDen(pocasiDen); }
          else if (subMenuIndex == 4) { appState = STATE_HOROSKOP_MENU; horoskopMenuIndex = 0; parsujHoroskopy(stazenaDataHoroskop); zobrazSubMenu("HOROSKOP", horoskopItems, horoskopCount, 0, 0); }
          else if (subMenuIndex == 5) {
            nactiJrSeznam();
            subMenuIndex = 0; subScrollOffset = 0;
            if (jrCount > 0) { appState = STATE_JR_SEZNAM; zobrazSubMenu("SPOJE", jrItems, jrCount, 0, 0); }
            else { appState = STATE_AKTUALITY; zobrazJrZprava("Zadna data - pripoj WiFi"); }
          }
          else if (subMenuIndex == 6) {
            kalObsah = nactiTextZeSD(String(KAL_DIR) + "/kalendar.txt");
            if (kalObsah.length() > 10) {
              appState = STATE_KALENDAR_MENU; subMenuIndex = 0; subScrollOffset = 0;
              zobrazSubMenu("KALENDÁŘ", kalendarMenuItems, kalendarMenuCount, 0, 0);
            } else {
              zobrazKalZprava("Zadna data - pripoj WiFi");
            }
          }
          break;

        case STATE_KALENDAR_MENU:
          if (subMenuIndex == 0) { kalDenIdx = 0; appState = STATE_KALENDAR_TYDEN; zobrazKalTyden(); }
          else {
            struct tm kt;
            if (kalDenTM(0, kt)) {
              kalMesic = kt.tm_mon + 1; kalRok = kt.tm_year + 1900;
              kalMesicDen = kt.tm_mday;
            }
            appState = STATE_KALENDAR_MESIC; zobrazKalMesic();
          }
          break;

        case STATE_KALENDAR_TYDEN:
          kalDetailZMesice = false;
          kalDetailOffset = kalDenIdx; kalStranka = 0;
          appState = STATE_KALENDAR_DEN; zobrazKalDetail();
          break;

        case STATE_KALENDAR_MESIC:
          kalDetailZMesice = true;
          kalDetailOffset = kalOffsetOdDnes(kalRok, kalMesic, kalMesicDen);
          kalStranka = 0;
          appState = STATE_KALENDAR_DEN; zobrazKalDetail();
          break;

        case STATE_KALENDAR_DEN:
          kalDetailOffset = (kalDetailOffset + 1) % 7; kalStranka = 0;
          zobrazKalDetail();
          break;

        case STATE_JR_SEZNAM:
          if (jrCount > 0) {
            jrAktSoubor = jrFiles[subMenuIndex];
            jrAktLabel  = jrLabels[subMenuIndex];
            jrRealtimeChci = false;
            appState = STATE_JR_DETAIL;
            zobrazJrDetail();
          }
          break;

        case STATE_JR_DETAIL:
          zobrazJrDetail();      // obnovit (při WiFi i realtime)
          break;

        case STATE_ZPRAVY_MENU:
          clanekMenuIndex = 0;
          if (subMenuIndex == 4) {
            // Archiv zpráv — první položka je hledání, pak jednotlivé dny
            {
              int dnu = nactiArchivDatumy(archivDatumy + 1, 29);
              if (dnu == 0) { archivDatumy[1] = "(Archiv je prázdný)"; dnu = 1; }
              archivDatumy[0] = "Hledat v archivu";
              archivDatumCount = dnu + 1;
            }
            archivDatumIndex = 0; archivDatumScrollOffset = 0;
            zpravyZHledani = false;
            appState = STATE_ARCHIV_DATUMY;
            zobrazSubMenu("ARCHIV ZPRÁV", archivDatumy, archivDatumCount, 0, 0);
          } else {
            zpravyZArchivu = false;
            appState = STATE_ZPRAVY_SEZNAM;
            if (subMenuIndex == 0) parsujZpravy(stazenaDataSvet);
            else if (subMenuIndex == 1) parsujZpravy(stazenaDataCR);
            else if (subMenuIndex == 2) parsujZpravy(stazenaDataTech);
            else if (subMenuIndex == 3) parsujZpravy(stazenaDataPozitivni);
            zobrazSeznamZprav();
          }
          break;

        case STATE_ARCHIV_DATUMY:
          if (archivDatumIndex == 0) {
            // Hledat v archivu
            hledaniBuf[0] = '\0'; hledaniLen = 0; hledaniKurzor = 0;
            appState = STATE_ARCHIV_HLEDANI;
            zobrazArchivHledani();
          } else if (archivDatumCount > 1 && archivDatumy[archivDatumIndex].length() >= 8) {
            archivAktualniDatum = archivDatumy[archivDatumIndex];
            archivZpravySubIndex = 0; archivZpravyScrollOffset = 0;
            appState = STATE_ARCHIV_ZPRAVY_MENU;
            zobrazSubMenu(("ARCHIV: " + archivAktualniDatum).c_str(), zpravyMenuItems, 4, 0, 0);
          }
          break;

        case STATE_ARCHIV_ZPRAVY_MENU:
          {
            const char* soubory[] = { "zpravy_svet.txt", "zpravy_cr.txt", "zpravy_tech.txt", "zpravy_pozitivni.txt" };
            archivOtevrenySoubor = String("/eindata/zpravy/archiv/") + archivAktualniDatum + "/" + soubory[archivZpravySubIndex];
            archivStranka = 0;
            archivCelkemClanku = parsujZpravyStrankuZeSouboru(archivOtevrenySoubor, 0);
            if (pocetZprav == 0) {
              aktualniZpravy[0].titulek = "Žádná data";
              aktualniZpravy[0].datum   = "";
              aktualniZpravy[0].perex   = "Archiv pro " + archivAktualniDatum + " není dostupný.";
              aktualniZpravy[0].text    = "";
              pocetZprav = 1;
              archivCelkemClanku = 0;
            }
            zpravyZArchivu = true;
            clanekMenuIndex = 0;
            appState = STATE_ZPRAVY_SEZNAM;
            zobrazSeznamZprav();
          }
          break;

        case STATE_ZPRAVY_SEZNAM:
          if (!zpravyZHledani) oznacitJakoPrectenou(aktualniZpravy[clanekMenuIndex].uid);
          // Plný text se dotahuje až při otevření článku (seznam je jen index)
          nactiDetailClanku(clanekMenuIndex);
          if (aktualniZpravy[clanekMenuIndex].text.length() == 0)
            aktualniZpravy[clanekMenuIndex].text = aktualniZpravy[clanekMenuIndex].perex;
          appState = STATE_ZPRAVY_TEXT; textScrollPage = 0; zobrazText(aktualniZpravy[clanekMenuIndex].titulek.c_str(), aktualniZpravy[clanekMenuIndex].text.c_str()); break;

        case STATE_TOOLBOX:
          if (subMenuIndex == 0) { appState = STATE_DIKTAFON_MENU; subMenuIndex = 0; subScrollOffset = 0; zobrazSubMenu("DIKTAFON", diktafonMenuItems, diktafonMenuCount, 0, 0); }
          else if (subMenuIndex == 1) { appState = STATE_OPICAK; opicakStart(); zobrazOpicak(); }
          else if (subMenuIndex == 2) { appState = STATE_LED_ON; zobrazLedStav(); }
          else if (subMenuIndex == 3) { appState = STATE_STOPKY; stopkyRunning = false; stopkyElapsed = 0; stopkyLastDraw = 0; drawStopky(0, false); }
          else if (subMenuIndex == 4) { appState = STATE_QR_MENU; subMenuIndex = 0; subScrollOffset = 0; zobrazSubMenu("QR KÓDY", qrItems, qrCount, 0, 0); }
          else if (subMenuIndex == 5) {
            appState = STATE_TEZBA; tezbaReset(); tezbaBezi = true;
            tpBloku = 0; tpNonce = 0; tpNejNuly = -1; tpStav = "Nepripojeno";
            tezbaReal = tezbaNactiConfig();
            if (tezbaReal) { tezbaStratumPripoj(); }
            zobrazTezbu();
          }
          else if (subMenuIndex == 6) { appState = STATE_TOUCH_TEST; touchKalibruj(); zobrazTouchTest(); }
          else if (subMenuIndex == 7) { appState = STATE_ENCODER_TEST; encTestStart(); zobrazEncoderTest(); }
          else if (subMenuIndex == 8) { appState = STATE_MIC_TEST; micStart(); zobrazMikrofonTest(); }
          else if (subMenuIndex == 9) { appState = STATE_BUZZER_TEST; buzzerStart(); zobrazBuzzerTest(); }
          else if (subMenuIndex == 10) {
            nactiReceptySeznam();
            subMenuIndex = 0; subScrollOffset = 0;
            if (receptyCount > 0) { appState = STATE_RECEPTY_SEZNAM; zobrazSubMenu("RECEPTY", receptyItems, receptyCount, 0, 0); }
            else { zobrazReceptZprava("Zadne recepty na SD"); }
          }
          else if (subMenuIndex == 11) { appState = STATE_CASOVAC; zobrazCasovac(); }
          else if (subMenuIndex == 12) { appState = STATE_KALKULACKA; kalkKurzor = 0; zobrazKalkulacka(); }
          break;

        case STATE_RECEPTY_SEZNAM:
          if (receptyCount > 0) {
            nactiRecept(subMenuIndex);
            appState = STATE_RECEPT_DETAIL;
            zobrazReceptDetail();
          }
          break;

        case STATE_RECEPT_DETAIL:
          receptyPorce++;
          if (receptyPorce > 12) receptyPorce = 1;
          receptStranka = 0;
          zobrazReceptDetail();
          break;

        case STATE_DIKTAFON_MENU:
          if (subMenuIndex == 0) {
            appState = STATE_DIKTAFON_NAHRAVANI;
            diktafonNahraj();
            appState = STATE_DIKTAFON_MENU; subMenuIndex = 0; subScrollOffset = 0;
            zobrazSubMenu("DIKTAFON", diktafonMenuItems, diktafonMenuCount, 0, 0);
          } else {
            nactiDiktafonSeznam();
            appState = STATE_DIKTAFON_SEZNAM; subMenuIndex = 0; subScrollOffset = 0;
            if (diktafonCount == 0) zobrazDiktafonZprava("Zadne nahravky");
            else zobrazSubMenu("DIKTAFON", diktafonItems, diktafonCount, 0, 0);
          }
          break;

        case STATE_DIKTAFON_SEZNAM:
          diktafonPrehraj(subMenuIndex);
          if (diktafonCount > 0) zobrazSubMenu("DIKTAFON", diktafonItems, diktafonCount, subMenuIndex, subScrollOffset);
          break;

        case STATE_QR_MENU: appState = STATE_QR_ZOBRAZ; zobrazQR(qrItems[subMenuIndex].c_str(), qrData[subMenuIndex].c_str()); break;

        case STATE_LED_ON: ledState = !ledState; digitalWrite(LED_PIN, ledState ? HIGH : LOW); zobrazLedStav(); break;
        case STATE_STOPKY:
          if (stopkyRunning) { stopkyElapsed += millis() - stopkyStart; stopkyRunning = false; drawStopky(stopkyElapsed, false); }
          else { stopkyStart = millis(); stopkyRunning = true; }
          break;
        case STATE_CASOVAC:
          if (casovacZvoni) { casovacTicho(); zobrazCasovac(); }
          else casovacStartStop();
          break;
        case STATE_KALKULACKA: kalkStisk(); break;
        case STATE_TEZBA: tezbaBezi = !tezbaBezi; zobrazTezbu(); break;

        case STATE_SLOVNIK:
          textScrollPage = 0;
          slovnikLangIdx = subMenuIndex;
          prefs.begin("nastaveni", false); prefs.putInt("dictLang", slovnikLangIdx); prefs.end();
          appState = STATE_SLOVNIK_JAZYK; subMenuIndex = 0; subScrollOffset = 0; zobrazSubMenu("SLOVNÍK", slovnikItems, slovnikCount, 0, 0);
          break;
        case STATE_SLOVNIK_DETAIL:
          if (hledanyLen < 11) { hledanyText[hledanyLen] = abeceda[abcKurzor] + 32; hledanyLen++; hledanyText[hledanyLen] = '\0'; }
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
          if (uceniOdhaleno) { uceniOdhaleno = false; } else { uceniOdhaleno = true; }
          zobrazUceni();
          break;
        case STATE_SLOVNIK_JAZYK:
          if (subMenuIndex == 0) {
            appState = STATE_SLOVNIK_DETAIL; hledanySmer = 0; hledanyLen = 0; hledanyText[0] = '\0'; abcKurzor = 0; zobrazHledani();
          } else if (subMenuIndex == 1) {
            appState = STATE_FRAZE_MENU; subMenuIndex = 0; subScrollOffset = 0; zobrazSubMenu("FRÁZE", frazeMenuItems, frazeMenuCount, 0, 0);
          } else if (subMenuIndex == 2) {
            appState = STATE_UCENI; uceniOdhaleno = false; zobrazUceni();
          }
          break;

       case STATE_GENERATOR:
         appState = STATE_GENERATOR_RESULT;
         if (subMenuIndex == 0) zobrazGeneratorResult("VTIP", _nahodnyTextGeneratoru(_generatorPathForCategory("/eindata/generator/vtipy.txt", "vtip").c_str(), vtipy, vtipyPocet).c_str());
         else if (subMenuIndex == 1) zobrazGeneratorResult("ŽALM", _nahodnyTextGeneratoru(_generatorPathForCategory("/eindata/generator/zalmy.txt", "zalm").c_str(), zalmy, zalmyPocet).c_str());
         else if (subMenuIndex == 2) zobrazGeneratorResult("CITÁT", _nahodnyTextGeneratoru(_generatorPathForCategory("/eindata/generator/citaty.txt", "citat").c_str(), citaty, citatyPocet).c_str());
         else if (subMenuIndex == 3) zobrazGeneratorResult("FAKT", _nahodnyTextGeneratoru(_generatorPathForCategory("/eindata/generator/fakty.txt", "fakt").c_str(), fakty, faktyPocet).c_str());
         break;
        case STATE_GENERATOR_RESULT:
          if (subMenuIndex == 0) zobrazGeneratorResult("VTIP", _nahodnyTextGeneratoru(_generatorPathForCategory("/eindata/generator/vtipy.txt", "vtip").c_str(), vtipy, vtipyPocet).c_str());
          else if (subMenuIndex == 1) zobrazGeneratorResult("ŽALM", _nahodnyTextGeneratoru(_generatorPathForCategory("/eindata/generator/zalmy.txt", "zalm").c_str(), zalmy, zalmyPocet).c_str());
          else if (subMenuIndex == 2) zobrazGeneratorResult("CITÁT", _nahodnyTextGeneratoru(_generatorPathForCategory("/eindata/generator/citaty.txt", "citat").c_str(), citaty, citatyPocet).c_str());
          else if (subMenuIndex == 3) zobrazGeneratorResult("FAKT", _nahodnyTextGeneratoru(_generatorPathForCategory("/eindata/generator/fakty.txt", "fakt").c_str(), fakty, faktyPocet).c_str());
          break;

        case STATE_HRY:
          if (subMenuIndex == 0) { appState = STATE_FLASKA; hrajFlasku(); }
          else if (subMenuIndex == 1) { appState = STATE_KOSTKY_VYBER; subMenuIndex = 0; subScrollOffset = 0; zobrazSubMenu("KOSTKY", kostkyItems, kostkyCount, 0, 0); }
          else if (subMenuIndex == 2) { nactiSeznamGamebooku(); appState = STATE_GAMEBOOK_MENU; subMenuIndex = 0; subScrollOffset = 0; zobrazSubMenu("GAMEBOOK", dynamicGamebookItems, dynamicGamebookCount, 0, 0); }
          else if (subMenuIndex == 3) { appState = STATE_ODHALOVACKA; odhalovackaKonec = false; odhalovackaIndex = 0; odhalovackaSkore = 0; for(int i=0; i<64; i++) odhalenoPole[i] = false; zobrazOdhalovacku(); }
          else if (subMenuIndex == 4) { appState = STATE_KVIZ_KATEGORIE; kvizKatIdx = 0; kvizObtIdx = 0; kvizKatScrollOffset = 0; zobrazSubMenu("KATEGORIE KVÍZU", kvizKategorieItems, kvizKategoriiCount, 0, 0); }
          else if (subMenuIndex == 5) { appState = STATE_WYR; nastaviNahodnyWyrZSD(); zobrazWyr(); }
          break;
        case STATE_KVIZ_KATEGORIE:
          if (kvizKatIdx == 6) { nastaviNahodnyKvizIdx(); appState = STATE_KVIZ; zobrazKviz(); }
          else { appState = STATE_KVIZ_OBTIZNOST; kvizObtIdx = 0; zobrazSubMenu("OBTÍŽNOST", kvizObtiznostItems, 3, 0, 0); }
          break;
        case STATE_KVIZ_OBTIZNOST:
          nastaviNahodnyKvizIdx(); appState = STATE_KVIZ; zobrazKviz();
          break;
        case STATE_FLASKA: hrajFlasku(); break;
        case STATE_KOSTKY_VYBER: kostkyStran = kostkyStrany[subMenuIndex]; appState = STATE_KOSTKY; zobrazKostkyIdle(); break;
        case STATE_KOSTKY: hrajKostku(kostkyStran); break;
        case STATE_GAMEBOOK_MENU:
          aktivniGamebookSoubor = dynamicGamebookFiles[subMenuIndex];
          aktivniGamebookMaTitulek = _gamebookHasTitleLine(aktivniGamebookSoubor.c_str());
          gbNode = nactiGBPozici();
          gbTextPage = 0;
          appState = STATE_GAMEBOOK;
          zobrazGBNode();
          break;
        case STATE_GAMEBOOK:
          {
            int volbaA;
            if (gbZSD) volbaA = gbVolbaASD;
            else { memcpy_P(&volbaA, &gamebook[gbNode].volbaA, sizeof(int)); }
            if (volbaA == -1) { gbNode = 0; gbTextPage = 0; ulozGBPozici(0); zobrazGBNode(); }
            else { gbNode = volbaA; gbTextPage = 0; ulozGBPozici(gbNode); zobrazGBNode(); }
            break;
          }
        case STATE_ODHALOVACKA: odhalovackaKonec = true; odhalovackaSkore++; appState = STATE_ODHALOVACKA_DETAIL; zobrazOdhalovackuDetail(); break;
        case STATE_ODHALOVACKA_DETAIL:
          odhalovackaIndex++;
          if (odhalovackaIndex >= ODHALOVACKA_POCET) {
            odhalovackaIndex = 0;
            appState = STATE_HRY;
            zobrazSubMenu("HRY", hryItems, hryCount, subMenuIndex, subScrollOffset);
          } else {
            appState = STATE_ODHALOVACKA;
            odhalovackaKonec = false;
            for (int i = 0; i < 64; i++) odhalenoPole[i] = false;
            zobrazOdhalovacku();
          }
          break;
        
        case STATE_KVIZ: appState = STATE_KVIZ_ODPOVED; zobrazKvizOdpoved(); break;
        case STATE_KVIZ_ODPOVED: appState = STATE_KVIZ; nastaviNahodnyKvizIdx(); zobrazKviz(); break;
        case STATE_WYR: appState = STATE_WYR_VYSLEDEK; zobrazWyrVysledek(); break;
        case STATE_WYR_VYSLEDEK: appState = STATE_WYR; nastaviNahodnyWyrZSD(); zobrazWyr(); break;
        
        case STATE_HOROSKOP_MENU: appState = STATE_HOROSKOP_DETAIL; horoskopScrollPage = 0; zobrazHoroskopDetail(horoskopMenuIndex); break;
        case STATE_HOROSKOP_DETAIL: horoskopScrollPage = 0; horoskopMenuIndex = (horoskopMenuIndex + 1) % horoskopCount; zobrazHoroskopDetail(horoskopMenuIndex); break;
        case STATE_KURZY_GRAF: grafMenuIndex = (grafMenuIndex + 1) % grafCount; zobrazKurzGraf(); break;

        case STATE_SD_BROWSER:
          if (sdReady && sdEntryCount > 0) {
            SdEntry& sel = sdEntries[sdBrowserIndex];
            if (sel.isDir) {
              if (strcmp(sel.name, "..") == 0) {
                int lastSlash = sdCurrentPath.lastIndexOf('/');
                sdCurrentPath = (lastSlash > 0) ? sdCurrentPath.substring(0, lastSlash) : "/";
              } else {
                sdCurrentPath = (sdCurrentPath == "/") ? (String("/") + sel.name) : (sdCurrentPath + "/" + sel.name);
              }
              nactiSDSlozku();
              zobrazSDProhlizec();
            } else {
              String cestaSouboru = (sdCurrentPath == "/") ? (String("/") + sel.name) : (sdCurrentPath + "/" + sel.name);
              // Zjistíme příponu souboru
              String extStr = cestaSouboru.substring(cestaSouboru.lastIndexOf('.'));
              extStr.toLowerCase();
              if (extStr == ".bmp") {
                // Zobrazit BMP obrázek
                appState = STATE_SD_BMP_VIEW;
                bool ok = zobrazBmpZSD(cestaSouboru.c_str());
                if (!ok) {
                  // BMP nejde zobrazit — zobrazíme chybovou zprávu a zůstaneme v browseru
                  appState = STATE_SD_BROWSER;
                  zobrazSDProhlizec();
                }
              } else if (extStr == ".txt") {
                // Otevření TXT: zprávy (|T| formát) nebo plain text
                File fSd = SD.open(cestaSouboru.c_str());
                if (fSd) {
                  String obsah = fSd.readString();
                  fSd.close();
                  if (obsah.indexOf("|T|") >= 0) {
                    // Data musí zůstat v paměti i při stránkování seznamu
                    sdZpravyRaw = obsah;
                    zpravyZArchivu = false; parsujZpravy(sdZpravyRaw); clanekMenuIndex = 0;
                    appState = STATE_ZPRAVY_SEZNAM; zobrazSeznamZprav();
                  } else {
                    sdTextViewTitle = sel.name;
                    sdTextViewBuffer = obsah;
                    textScrollPage = 0;
                    appState = STATE_SD_TEXT_VIEW;
                    zobrazText(sdTextViewTitle.c_str(), sdTextViewBuffer.c_str());
                  }
                }
              }
            }
          }
          break;

        case STATE_NASTAVENI:
          if (subMenuIndex == 0) { appState = STATE_NASTAVENI_AKTUALIZACE; subMenuIndex = 0; subScrollOffset = 0; zobrazSubMenu("AKTUALIZACE", aktualizaceItems, aktualizaceCount, 0, 0); }
          else if (subMenuIndex == 1) { appState = STATE_NASTAVENI_WIFI; subMenuIndex = 0; subScrollOffset = 0; zobrazSubMenu("WIFI", wifiMenuItems, wifiMenuCount, 0, 0); }
          else if (subMenuIndex == 2) { appState = STATE_NASTAVENI_VZHLED; subMenuIndex = 0; subScrollOffset = 0; zobrazSubMenu("VZHLED", vzhledItems, vzhledCount, 0, 0); }
          else if (subMenuIndex == 3) { appState = STATE_NASTAVENI_AUTOUSPANI; subMenuIndex = autoSleepIdx; subScrollOffset = 0; zobrazSubMenu("AUTO USPANI", autoSleepItems, autoSleepCount, subMenuIndex, subScrollOffset); }
          else if (subMenuIndex == 4) { appState = STATE_NASTAVENI_BATERIE; zobrazBateriiMenu(); }
          else if (subMenuIndex == 5) { appState = STATE_NASTAVENI_O_ZARIZENI; zobrazOZaRizeni(); }
          else if (subMenuIndex == 6) { appState = STATE_USB_SD; usbSdZapni(); }
          break;

        case STATE_NASTAVENI_WIFI:
          if (subMenuIndex == 0) {
            // Hledat sítě → sken a seznam
            appState = STATE_WIFI_SEZNAM; wifiSitIndex = 0; wifiPocetSiti = 0;
            zobrazWifiSeznam(); delay(50);
            spustWifiSken();
            zobrazWifiSeznam();
          } else if (subMenuIndex == 1) {
            // Vypnout WiFi
            WiFi.disconnect(true);
            WiFi.mode(WIFI_OFF);
            nakresliLoadScreen("WiFi vypnuto", 100); delay(700);
            subMenuIndex = 0; subScrollOffset = 0;
            zobrazSubMenu("WIFI", wifiMenuItems, wifiMenuCount, 0, 0);
          }
          break;

        case STATE_NASTAVENI_VZHLED:
          if (subMenuIndex == 0) { currentSize = (currentSize + 1) % 3; delay(100); zobrazSubMenu("VZHLED", vzhledItems, vzhledCount, subMenuIndex, subScrollOffset); }
          else if (subMenuIndex == 1) { currentBold = (currentBold + 1) % 2; delay(100); zobrazSubMenu("VZHLED", vzhledItems, vzhledCount, subMenuIndex, subScrollOffset); }
          else if (subMenuIndex == 2) { currentStyle = (currentStyle + 1) % 4; delay(100); zobrazSubMenu("VZHLED", vzhledItems, vzhledCount, subMenuIndex, subScrollOffset); }
          else if (subMenuIndex == 3) { refreshMode = (refreshMode + 1) % 2; zobrazRefreshToast(); zobrazSubMenu("VZHLED", vzhledItems, vzhledCount, subMenuIndex, subScrollOffset); }
          else if (subMenuIndex == 4) { reverseMode = !reverseMode; delay(100); zobrazSubMenu("VZHLED", vzhledItems, vzhledCount, subMenuIndex, subScrollOffset); }
          else if (subMenuIndex == 5) {
            napovedaZapnuta = !napovedaZapnuta;
            vzhledItems[5] = napovedaZapnuta ? "Nápověda: ZAP" : "Nápověda: VYP";
            prefs.begin("nastaveni", false); prefs.putBool("help", napovedaZapnuta); prefs.end();
            delay(100);
            zobrazSubMenu("VZHLED", vzhledItems, vzhledCount, subMenuIndex, subScrollOffset);
          }
          break;

        case STATE_WIFI_SEZNAM:
          if (wifiPocetSiti > 0) {
            String ssid = wifiSitSSID[wifiSitIndex];
            if (wifiSitEnc[wifiSitIndex] == WIFI_AUTH_OPEN) {
              // Otevřená síť — připojíme se přímo bez hesla
              nakresliLoadScreen("Připojuji: " + ssid, 60);
              bool ok = pripojKWifi(ssid, "");
              if (ok) ulozWifiPrihlaseni(ssid, "");
              nakresliLoadScreen(ok ? String("Připojeno! " + WiFi.localIP().toString()) : String("Připojení selhalo"), 100);
              delay(1200);
              zobrazWifiSeznam();
            } else {
              // Zabezpečená síť
              wifiHesloSsid = ssid;
              String ulozene = "";
              if (nactiWifiPrihlaseni(ssid, ulozene)) {
                // Uložené heslo máme → nabídneme rychlé připojení bez psaní
                appState = STATE_WIFI_POTVRDIT;
                zobrazWifiPotvrzeni();
              } else {
                // Bez uloženého hesla → rovnou klávesnice
                wifiHesloStatus = "";
                wifiHesloBuf[0] = '\0'; wifiHesloLen = 0;
                wifiKlavesKurzor = 0;
                appState = STATE_WIFI_HESLO;
                zobrazWifiHeslo();
              }
            }
          }
          break;

        case STATE_WIFI_HESLO:
          // Obsluhu tlačítek řeší obsluhaWifiHesloKlavesnice() v loop() (neblokující).
          break;

        case STATE_WIFI_POTVRDIT:
          {
            // Krátký BTN21 = připojit s uloženým heslem
            String ulozene = "";
            nactiWifiPrihlaseni(wifiHesloSsid, ulozene);
            nakresliLoadScreen("Připojuji: " + wifiHesloSsid, 60);
            bool ok = pripojKWifi(wifiHesloSsid, ulozene);
            nakresliLoadScreen(ok ? String("Připojeno! " + WiFi.localIP().toString()) : String("Připojení selhalo"), 100);
            delay(1200);
            appState = STATE_WIFI_SEZNAM;
            zobrazWifiSeznam();
          }
          break;
          
        case STATE_NASTAVENI_AKTUALIZACE:
          if (subMenuIndex == 0) { aktualizovatDataNaPozadi(true); goBack(); }
          else if (subMenuIndex == 1) { startWebUploadMode(); }
          break;

        case STATE_NASTAVENI_AUTOUSPANI:
          autoSleepIdx = subMenuIndex;
          prefs.begin("nastaveni", false); prefs.putInt("sleepIdx", autoSleepIdx); prefs.end();
          goBack();
          break;

        case STATE_WEB_UPLOAD:
          if (webUploadApMode) trySwitchWebUploadToSta(); else startWebUploadApOnly();
          // Po změně WiFi režimu obnov poslouchající socket — jinak může zůstat
          // navázaný na staré rozhraní a na nové IP neodpovídat.
          if (webUploadServerReady) { webServer.stop(); webServer.begin(); }
          zobrazWebUpload();
          break;

        default: break;
      }
      if (fyz21) while (digitalRead(BTN_SELECT) == LOW) delay(10);
    }
  }

  // ===== ČASOVAČ — odpočet běží i mimo obrazovku časovače =====
  casovacTick();

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

  // ===== TĚŽBA BITCOINU =====
  if (appState == STATE_TEZBA && tezbaBezi) {
    if (tezbaReal) {
      tezbaStratumTick();     // reálný Stratum sólo klient (obsluhuje i socket)
    } else {
      unsigned long t0 = millis();
      while (millis() - t0 < 600) {
        for (int i = 0; i < 500; i++) tezbaHashuj();
        tezbaHashu += 500;
        delay(1); // ať běží i ostatní úlohy (WiFi, audio...)
      }
      tezbaHashMs += millis() - t0;
      zobrazTezbu();
    }
  }

  // ===== TEST TOUCH PINŮ (D-pad, 4 zóny) =====
  if (appState == STATE_TOUCH_TEST) {
    // rychlé vzorkování, ať se krátký dotyk neztratí
    if (millis() - touchLastPoll >= 80) {
      touchLastPoll = millis();
      for (int p = 0; p < TOUCH_PIN_COUNT; p++) {
        uint32_t v = touchCti(TOUCH_PINY[p]);
        touchNowA[p] = v;
        long d = (long)v - (long)touchBaseA[p];
        if (d > touchPeakA[p]) touchPeakA[p] = d;
      }
    }
    // překreslení ~1x za 700 ms — [X] se vyhodnotí z největšího skoku za tu dobu
    if (millis() - touchLast >= 700) {
      for (int p = 0; p < TOUCH_PIN_COUNT; p++)
        touchDotyA[p] = (touchPeakA[p] > (long)touchPrahA[p]);
      zobrazTouchTest();                    // blokuje ~750 ms
      for (int p = 0; p < TOUCH_PIN_COUNT; p++) touchPeakA[p] = 0;
      touchLast = millis();                 // okno vzorkování začíná až PO vykreslení
      touchLastPoll = millis();
    }
  }

  // ===== TEST ENCODERU =====
  if (appState == STATE_ENCODER_TEST) {
    int  krok = encPozice / ENC_KROKY_NA_CVAK;   // 1 mechanické cvaknutí = 1 krok
    bool sw   = (digitalRead(ENC_SW_PIN) == LOW);
    if (krok != encZobrPozice || sw != encZobrStisk) {
      encZobrPozice = krok; encZobrStisk = sw;
      zobrazEncoderTest();
    }
  }

  // ===== TEST MIKROFONU =====
  if (appState == STATE_MIC_TEST) {
    micTick();                                   // čte bloky a počítá hladinu
    if (millis() - micLast >= 1000) { micLast = millis(); zobrazMikrofonTest(); }
  }

  // ===== TEST BUZZERŮ =====
  if (appState == STATE_BUZZER_TEST) buzzTick();  // neblokující přehrávání melodie

  // ===== OPIČÁK — poslouchá a odpovídá tónem =====
  if (appState == STATE_OPICAK) opicakTick();

  // ===== AUTO USPANI PRI NEAKTIVITE =====
  if (!webUploadMode && !usbSdAktivni && appState != STATE_WEB_UPLOAD && !casovacBezi && !casovacZvoni && autoSleepIdx >= 0 && autoSleepIdx < autoSleepCount && autoSleepMs[autoSleepIdx] > 0) {
    if (millis() - lastUserActivityMs >= autoSleepMs[autoSleepIdx]) {
      jdiSpat();
      return;
    }
  }
}