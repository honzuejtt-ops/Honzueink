// TextFileCache.h — PSRAM CACHE PRO E-BOOK READER
// Drží 3 poslední knihy v PSRAM pro ultra-rychlý přístup

#ifndef TEXT_FILE_CACHE_H
#define TEXT_FILE_CACHE_H

#include <map>
#include <memory>

struct CachedBook {
  String filePath;
  std::vector<String> pages;  // Jednotlivé stránky v PSRAM
  unsigned long accessTime;    // Kdy byla naposledy použita
  int totalPages;
  
  CachedBook() : totalPages(0), accessTime(0) {}
};

class TextFileCache {
private:
  static const int MAX_CACHED_BOOKS = 3;
  static std::map<String, CachedBook*> cache;
  static unsigned long accessCounter;
  
public:
  // Načti knihu do cache (nebo vrať, je-li už tam)
  static bool loadBook(const char* filePath, File& bookFile, int estimatedPages) {
    String path(filePath);
    
    // Již v cache?
    if (cache.find(path) != cache.end()) {
      cache[path]->accessTime = accessCounter++;
      return true;
    }
    
    // Pokud máme moc knih, vyhoď nejstarší
    while (cache.size() >= MAX_CACHED_BOOKS) {
      evictLRU();
    }
    
    // Načti knihu po stránkách do PSRAM
    CachedBook* newBook = new CachedBook();
    newBook->filePath = path;
    newBook->totalPages = 0;
    newBook->accessTime = accessCounter++;
    
    // Čti soubor po 4 KB chunkcích a skládej stránky
    const int PAGE_CHUNK = 4096;
    char buf[PAGE_CHUNK];
    String currentPage = "";
    
    bookFile.seek(0);
    while (bookFile.available()) {
      int n = bookFile.read((uint8_t*)buf, sizeof(buf));
      if (n <= 0) break;
      
      for (int i = 0; i < n; i++) {
        currentPage += buf[i];
        
        // Nová stránka každých ~1500 znaků (e-ink limit)
        if (currentPage.length() >= 1500 || (currentPage.length() > 100 && buf[i] == '\n')) {
          newBook->pages.push_back(currentPage);
          currentPage = "";
          newBook->totalPages++;
        }
      }
    }
    
    // Poslední stránka
    if (currentPage.length() > 0) {
      newBook->pages.push_back(currentPage);
      newBook->totalPages++;
    }
    
    cache[path] = newBook;
    
    Serial.print("TextFileCache: načtena kniha ");
    Serial.print(path.c_str());
    Serial.print(" (");
    Serial.print(newBook->totalPages);
    Serial.println(" stránek v PSRAM)");
    
    return true;
  }
  
  // Přečti stránku z cache
  static String getPage(const char* filePath, int pageNum) {
    String path(filePath);
    
    if (cache.find(path) == cache.end()) return "";
    if (pageNum < 0 || pageNum >= cache[path]->totalPages) return "";
    
    cache[path]->accessTime = accessCounter++;
    return cache[path]->pages[pageNum];
  }
  
  // Kolik stránek má kniha v cache
  static int getPageCount(const char* filePath) {
    String path(filePath);
    if (cache.find(path) == cache.end()) return 0;
    return cache[path]->totalPages;
  }
  
  // Vyčisti cache
  static void clear() {
    for (auto& pair : cache) {
      delete pair.second;
    }
    cache.clear();
  }
  
  // Diagnostika
  static void printStats() {
    Serial.print("TextFileCache: ");
    Serial.print(cache.size());
    Serial.println(" knih v PSRAM");
    for (auto& pair : cache) {
      Serial.print("  - ");
      Serial.print(pair.first.c_str());
      Serial.print(": ");
      Serial.print(pair.second->totalPages);
      Serial.println(" stránek");
    }
  }
  
private:
  // Vyhodí nejméně nedávno použitou knihu (LRU)
  static void evictLRU() {
    String lruPath;
    unsigned long oldestTime = ULONG_MAX;
    
    for (auto& pair : cache) {
      if (pair.second->accessTime < oldestTime) {
        oldestTime = pair.second->accessTime;
        lruPath = pair.first;
      }
    }
    
    if (cache.find(lruPath) != cache.end()) {
      Serial.print("TextFileCache: evikuji ");
      Serial.println(lruPath.c_str());
      delete cache[lruPath];
      cache.erase(lruPath);
    }
  }
};

// Statické inicializace
std::map<String, CachedBook*> TextFileCache::cache;
unsigned long TextFileCache::accessCounter = 0;

#endif
