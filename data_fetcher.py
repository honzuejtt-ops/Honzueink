import requests
import xml.etree.ElementTree as ET
from datetime import datetime

# --- FUNKCE PRO STAHOVÁNÍ ZPRÁV (RSS) ---
def stahni_zpravy(url, limit=6):
    try:
        odpoved = requests.get(url, timeout=10)
        root = ET.fromstring(odpoved.content)
        items = root.findall('.//item')
        
        vysledny_text = ""
        # Vezmeme prvních X nejnovějších článků (tím máme jistotu, že jsou čerstvé)
        for item in items[:limit]:
            titulek = item.find('title').text
            vysledny_text += f"- {titulek}\n\n"
            
        return vysledny_text
    except Exception as e:
        return f"Nepodarilo se stahnout zprávy.\nChyba: {e}"

# --- FUNKCE PRO STAHOVÁNÍ POČASÍ ---
def stahni_pocasi():
    # Získání dat pro Prahu z bezplatného Open-Meteo
    url = "https://api.open-meteo.com/v1/forecast?latitude=50.088&longitude=14.4208&daily=weathercode,temperature_2m_max,temperature_2m_min,precipitation_sum,windspeed_10m_max&timezone=Europe%2FBerlin"
    
    try:
        odpoved = requests.get(url, timeout=10)
        data = odpoved.json()
        daily = data['daily']
        
        vysledny_text = ""
        # Projdeme všech 7 dní předpovědi
        for i in range(7):
            datum_str = daily['time'][i]
            # Převod data z 2024-03-24 na 24.03.
            dt = datetime.strptime(datum_str, "%Y-%m-%d")
            datum_format = dt.strftime("%d.%m.")
            
            t_max = round(daily['temperature_2m_max'][i])
            t_min = round(daily['temperature_2m_min'][i])
            srazky = daily['precipitation_sum'][i]
            vitr_ms = round(daily['windspeed_10m_max'][i] / 3.6) # Převod z km/h na m/s
            kod_pocasi = daily['weathercode'][i]
            
            # Jednoduché přiřazení ikony podle WMO kódu počasí
            if kod_pocasi in [0, 1]: ikona = "SLUNCE"
            elif kod_pocasi in [2, 3]: ikona = "MRAKY"
            elif kod_pocasi in [61, 63, 65, 80, 81, 82]: ikona = "DEST"
            elif kod_pocasi in [95, 96, 99]: ikona = "BOURKA"
            elif kod_pocasi in [71, 73, 75, 85, 86]: ikona = "SNIH"
            else: ikona = "OBLACNO"

            # Formát, který bude snadno čitelný pro ESP32
            vysledny_text += f"{datum_format}|{t_max}|{t_min}|{srazky}|{ikona}|{vitr_ms}\n"
            
        return vysledny_text
    except Exception as e:
        return f"Nepodarilo se stahnout pocasi.\nChyba: {e}"

if __name__ == "__main__":
    print("Spoustim stahovani realnych dat...")
    
    # 1. Stažení dat z různých zdrojů
    text_svet = stahni_zpravy("https://ct24.ceskatelevize.cz/rss/svet")
    text_cr = stahni_zpravy("https://ct24.ceskatelevize.cz/rss/domaci")
    text_tech = stahni_zpravy("https://www.lupa.cz/rss/clanky/")
    text_pocasi = stahni_pocasi()

    # 2. Uložení do samostatných souborů
    with open("zpravy_svet.txt", "w", encoding="utf-8") as f:
        f.write(text_svet)
        
    with open("zpravy_cr.txt", "w", encoding="utf-8") as f:
        f.write(text_cr)
        
    with open("zpravy_tech.txt", "w", encoding="utf-8") as f:
        f.write(text_tech)
        
    with open("pocasi.txt", "w", encoding="utf-8") as f:
        f.write(text_pocasi)
        
    print("Hotovo! Aktualni data byla ulozena.")
