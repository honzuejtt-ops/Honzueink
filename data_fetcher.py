import requests
import xml.etree.ElementTree as ET
from datetime import datetime
from bs4 import BeautifulSoup

# --- FUNKCE PRO VYTĚŽENÍ TEXTU PŘÍMO Z ČLÁNKU ---
def stahni_text_clanku(url, perex):
    hlavicky = {"User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) Chrome/120.0.0.0 Safari/537.36"}
    try:
        odpoved = requests.get(url, headers=hlavicky, timeout=10)
        soup = BeautifulSoup(odpoved.content, 'html.parser')
        
        odstavce = soup.find_all('p')
        obsah = ""
        
        for p in odstavce:
            text = p.get_text().strip()
            if len(text) > 40:
                obsah += text + "\n"
        
        if "Enable JavaScript" in obsah or len(obsah) < 100:
            return perex + "\n\n(Pozn: Celý text web zablokoval)"
        
        if len(obsah) > 1500:
            obsah = obsah[:1500] + "...\n(Pokracovani na webu)"
            
        return obsah
    except Exception:
        return perex + "\n\n(Pozn: Chyba při stahování celého textu)"

# --- FUNKCE PRO STAHOVÁNÍ ZPRÁV (RSS) ---
def stahni_zpravy(rss_url, limit=10): # ZVÝŠENO NA 10 ČLÁNKŮ
    hlavicky = {"User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) Chrome/120.0.0.0 Safari/537.36"}
    try:
        odpoved = requests.get(rss_url, headers=hlavicky, timeout=10)
        odpoved.raise_for_status()
        
        root = ET.fromstring(odpoved.content)
        items = root.findall('.//item')
        
        vysledny_text = ""
        if not items:
            return "|T|Žádné zprávy|P|Nebyly nalezeny žádné články.|X|Zkuste to později.|E|"

        for item in items[:limit]:
            titulek_el = item.find('title')
            odkaz_el = item.find('link')
            popis_el = item.find('description')
            
            titulek = titulek_el.text.strip() if titulek_el is not None and titulek_el.text else "Bez titulku"
            odkaz = odkaz_el.text.strip() if odkaz_el is not None and odkaz_el.text else ""
            
            perex_raw = popis_el.text.strip() if popis_el is not None and popis_el.text else ""
            perex = BeautifulSoup(perex_raw, "html.parser").get_text() if perex_raw else ""
            
            # Aby perex nebyl moc dlouhý do menu, zkrátíme ho případně na 100 znaků
            if len(perex) > 100:
                perex_kratky = perex[:100] + "..."
            else:
                perex_kratky = perex
            
            text_clanku = stahni_text_clanku(odkaz, perex) if odkaz else perex
            
            # SPECIÁLNÍ FORMÁTOVÁNÍ PRO ČTEČKU (T = Titulek, P = Perex, X = Text, E = Konec článku)
            vysledny_text += f"|T|{titulek}|P|{perex_kratky}|X|{text_clanku}|E|"
            
        return vysledny_text
    except Exception as e:
        return f"|T|Chyba stahování|P|Něco se pokazilo.|X|{e}|E|"

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
    print("Spoustim stahovani (5 clanku s tajnymi znackami pro menu)...")
    
    text_svet = stahni_zpravy("https://ct24.ceskatelevize.cz/rss/svet", limit=10)
    text_cr = stahni_zpravy("https://ct24.ceskatelevize.cz/rss/domaci", limit=10)
    text_tech = stahni_zpravy("https://www.lupa.cz/rss/clanky/", limit=10)
    text_pocasi = stahni_pocasi()

    with open("zpravy_svet.txt", "w", encoding="utf-8") as f: f.write(text_svet)
    with open("zpravy_cr.txt", "w", encoding="utf-8") as f: f.write(text_cr)
    with open("zpravy_tech.txt", "w", encoding="utf-8") as f: f.write(text_tech)
    with open("pocasi.txt", "w", encoding="utf-8") as f: f.write(text_pocasi)
        
    print("Hotovo! Data jsou pripravena pro ESP32.")
