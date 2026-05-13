// StreamedTextReader.h — OPTIMALIZOVANÉ ČTENÍ TEXTU S PSRAM PODPOROU
// Čte textové soubory streamovaně bez načítání do paměti

#ifndef STREAMED_TEXT_READER_H
#define STREAMED_TEXT_READER_H

#include <FS.h>
#include <SD.h>
#include <memory>

// Konstanta: jak moc znaků se vejde na jednu e-ink stránku (296×128 px)
const int CHARS_PER_PAGE = 1200;  // Přibližně
const int BUFFER_SIZE = 8192;      // Čtení po 8 KB chunkcích

struct PageIndex {
  uint32_t byteOffset;  // Kde se stránka začíná v souboru
  int lineCount;        // Kolik řádků na stránce
};

class StreamedTextReader {
public:
  String filePath;
  File fileHandle;
  uint32_t fileSize;
  std::vector<PageIndex> pageIndex;  // Index stránek
  int totalPages;
  
  StreamedTextReader() : fileSize(0), totalPages(0) {}
  
  ~StreamedTextReader() {
    if (fileHandle) fileHandle.close();
  }
  
  // Otevři soubor a vytvoř index stránek (PSRAM-friendly)
  bool open(const char* path) {
    fileHandle = SD.open(path);
    if (!fileHandle) return false;
    
    filePath = path;
    fileSize = fileHandle.size();
    
    // Vytvoříme index stránek — čteme po 8 KB
    buildPageIndex();
    totalPages = pageIndex.size();
    
    Serial.print("StreamedTextReader: otevřen soubor ");
    Serial.print(path);
    Serial.print(" (");
    Serial.print(fileSize);
    Serial.print(" B) na ");
    Serial.print(totalPages);
    Serial.println(" stránkách");
    
    return true;
  }
  
  // Přečti N-tou stránku (vrací String — POZOR: všechny stránky se vejdou do PSRAM)
  String readPage(int pageNum) {
    if (pageNum < 0 || pageNum >= totalPages) return "";
    if (!fileHandle) return "";
    
    PageIndex& page = pageIndex[pageNum];
    fileHandle.seek(page.byteOffset);
    
    String result = "";
    char buf[BUFFER_SIZE];
    int lineCounter = 0;
    int charsRead = 0;
    int maxChars = CHARS_PER_PAGE * 2;  // Max. obsah pro stránku
    
    // Čteme, dokud nebude dost řádků nebo znaků
    while (lineCounter < page.lineCount && charsRead < maxChars) {
      int n = fileHandle.read((uint8_t*)buf, sizeof(buf));
      if (n <= 0) break;
      
      for (int i = 0; i < n; i++) {
        result += buf[i];
        if (buf[i] == '\n') lineCounter++;
        charsRead++;
        if (lineCounter >= page.lineCount) break;
      }
    }
    
    return result;
  }
  
  int getTotalPages() { return totalPages; }
  
private:
  void buildPageIndex() {
    pageIndex.clear();
    if (!fileHandle) return;
    
    fileHandle.seek(0);
    PageIndex currentPage = {0, 0};
    char buf[BUFFER_SIZE];
    uint32_t totalBytes = 0;
    
    while (totalBytes < fileSize) {
      int n = fileHandle.read((uint8_t*)buf, sizeof(buf));
      if (n <= 0) break;
      
      for (int i = 0; i < n; i++) {
        if (buf[i] == '\n') {
          currentPage.lineCount++;
          
          // Když máme dost řádků, uložíme stránku
          if (currentPage.lineCount >= 50) {  // ~50 řádků na stránku
            pageIndex.push_back(currentPage);
            currentPage = {totalBytes + i + 1, 0};
          }
        }
      }
      totalBytes += n;
    }
    
    // Poslední stránka
    if (currentPage.lineCount > 0) {
      pageIndex.push_back(currentPage);
    }
  }
};

#endif
