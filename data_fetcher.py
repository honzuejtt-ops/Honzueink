import requests
import xml.etree.ElementTree as ET
from datetime import datetime
from bs4 import BeautifulSoup

# --- FUNKCE PRO VYTĚŽENÍ TEXTU PŘÍMO Z ČLÁNKU ---
def stahni_text_clanku(url):
    hlavicky = {"User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) Chrome/120.0.0.0 Safari/537.36"}
    try:
        odpoved = requests.get(url, headers=hlavicky, timeout=10)
        soup = BeautifulSoup(odpoved.content, 'html.parser')
        
        # Najdeme všechny odstavce (tag <p>)
        odstavce = soup.find_all('p')
        obsah = ""
        
        for p in odstavce:
            text = p.get_text().strip()
            # Vyfiltrujeme krátké nesmysly (jako "Sdílet", "Tisk" atd.)
            if len(text) > 40:
                obsah += text + "\n"
        
        # Ořízneme to na 1500 znaků, aby nám to nezbořilo paměť v ESP32
        if len(obsah) > 1500:
            obsah = obsah[:1500] + "...\n(Pokracovani na webu)"
            
        return obsah
    except Exception:
        return "Nepodarilo se nacist text clanku."

# --- FUNKCE PRO STAHOVÁNÍ ZPRÁV (RSS) ---
def stahni_zpravy(rss_url, limit=3): # Taháme jen 3 články, protože teď budou dlouhé
    hlavicky = {"User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) Chrome/120.0.0.0 Safari/537.36"}
    try:
        odpoved = requests.get(rss_url, headers=hlavicky, timeout=10)
        odpoved.raise_for_status()
        
        root = ET.fromstring(odpoved.content)
        items = root.findall('.//item')
        
        vysledny_text = ""
        if not items:
            return "Zadne clanky nenalezeny."

        for item in items[:limit]:
            titulek = item.find('title').text.strip()
            odkaz = item.find('link').text.strip()
            
            # Formátování titulku, ať je to hezky vidět
            vysledny_text += f"=== {titulek} ===\n"
            
            # Tady zavoláme naši novou funkci, která vleze do odkazu pro text
            text_clanku = stahni_text_clanku(odkaz)
            vysledny_text += f"{text_clanku}\n\n"
            
        return vysledny_text
    except Exception as e:
        return f"Chyba pri stahovani z {rss_url}:\n{e}"

# --- FUNKCE PRO STAHOVÁNÍ POČASÍ ---
def stahni_pocasi():
    url = "https://api.open-meteo.com/v1/forecast?latitude=50.088&longitude=14.4208&daily=weathercode,temperature_2m_max,temperature_2m_min,precipitation_sum,windspeed_10m_max&timezone=Europe%2FBerlin"
    try:
        odpoved = requests.get(url, timeout=10)
        data = odpoved.json()
        daily = data['daily']
        
        vysledny_text = ""
        for i in range(7):
            datum_str = daily['time'][i]
            dt = datetime.strptime(datum_str, "%Y-%m-%d")
            datum_format = dt.strftime("%d.%m.")
            
            t_max = round(daily['temperature_2m_max'][i])
            t_min = round(daily['temperature_2m_min'][i])
            srazky = daily['precipitation_sum'][i]
            vitr_ms = round(daily['windspeed_10m_max'][i] / 3.6)
            kod_pocasi = daily['weathercode'][i]
            
            if kod_pocasi in [0, 1]: ikona = "SLUNCE"
            elif kod_pocasi in [2, 3]: ikona = "MRAKY"
            elif kod_pocasi in [61, 63, 65, 80, 81, 82]: ikona = "DEST"
            elif kod_pocasi in [95, 96, 99]: ikona = "BOURKA"
            elif kod_pocasi in [71, 73, 75, 85, 86]: ikona = "SNIH"
            else: ikona = "OBLACNO"

            vysledny_text += f"{datum_format}|{t_max}|{t_min}|{srazky}|{ikona}|{vitr_ms}\n"
            
        return vysledny_text
    except Exception as e:
        return f"Nepodarilo se stahnout pocasi.\nChyba: {e}"

if __name__ == "__main__":
    print("Spoustim stahovani realnych dat (vcetne textu)...")
    
    text_svet = stahni_zpravy("https://www.irozhlas.cz/rss/irozhlas/svet")
    text_cr = stahni_zpravy("https://www.irozhlas.cz/rss/irozhlas/zpravy-domov")
    text_tech = stahni_zpravy("https://www.lupa.cz/rss/clanky/")
    text_pocasi = stahni_pocasi()

    with open("zpravy_svet.txt", "w", encoding="utf-8") as f: f.write(text_svet)
    with open("zpravy_cr.txt", "w", encoding="utf-8") as f: f.write(text_cr)
    with open("zpravy_tech.txt", "w", encoding="utf-8") as f: f.write(text_tech)
    with open("pocasi.txt", "w", encoding="utf-8") as f: f.write(text_pocasi)
        
    print("Hotovo! Aktualni data byla ulozena.")
