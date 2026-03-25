import requests
import xml.etree.ElementTree as ET
from datetime import datetime
from bs4 import BeautifulSoup
import math

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
def stahni_zpravy(rss_url, limit=10):
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
            
            if len(perex) > 100:
                perex_kratky = perex[:100] + "..."
            else:
                perex_kratky = perex
            
            text_clanku = stahni_text_clanku(odkaz, perex) if odkaz else perex
            
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

# --- FUNKCE PRO STAHOVÁNÍ KURZŮ ---
def stahni_kurzy():
    vysledek = "=== KURZY ===\n\n"
    try:
        # ČNB Kurzy pro EUR a USD
        res_cnb = requests.get("https://www.cnb.cz/cs/financni-trhy/devizovy-trh/kurzy-devizoveho-trhu/kurzy-devizoveho-trhu/denni_kurz.txt", timeout=10)
        lines = res_cnb.text.split('\n')
        eur = usd = "N/A"
        for line in lines:
            if "|EUR|" in line: eur = line.split('|')[-1].strip()
            if "|USD|" in line: usd = line.split('|')[-1].strip()
        
        vysledek += f"1 EUR = {eur} CZK\n"
        vysledek += f"1 USD = {usd} CZK\n\n"

        # Yahoo Finance pro krypto a kovy
        hlavicky = {"User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) Chrome/120.0.0.0 Safari/537.36"}
        
        def get_yahoo(ticker):
            url = f"https://query1.finance.yahoo.com/v8/finance/chart/{ticker}"
            r = requests.get(url, headers=hlavicky, timeout=10)
            data = r.json()
            cena = data['chart']['result'][0]['meta']['regularMarketPrice']
            return f"{cena:,.2f}".replace(',', ' ')

        vysledek += f"Bitcoin = {get_yahoo('BTC-USD')} USD\n"
        vysledek += f"Zlato (oz) = {get_yahoo('GC=F')} USD\n"
        vysledek += f"Stribro (oz) = {get_yahoo('SI=F')} USD\n"

    except Exception as e:
        vysledek += f"Nepodarilo se stahnout kurzy.\nChyba: {e}"
    
    return vysledek

# --- FUNKCE PRO SLUNCE A MĚSÍC (ASTRO) ---
def stahni_astro():
    vysledek = "=== SLUNCE A MESIC ===\n\n"
    try:
        # Sunrise/Sunset API (lokace Praha)
        url = "https://api.sunrisesunset.io/json?lat=50.088&lng=14.4208&timezone=Europe/Prague"
        res = requests.get(url, timeout=10).json()
        r = res['results']
        
        # Převod AM/PM na 24h formát
        def to_24h(t_str):
            try:
                return datetime.strptime(t_str, "%I:%M:%S %p").strftime("%H:%M")
            except:
                return t_str

        vychod = to_24h(r['sunrise'])
        zapad = to_24h(r['sunset'])
        zlata_vecer = to_24h(r['golden_hour'])
        
        vysledek += f"Vychod slunce: {vychod}\n"
        vysledek += f"Zapad slunce: {zapad}\n"
        vysledek += f"Zlata hodinka (vecer): od {zlata_vecer}\n\n"
        
        # Fáze měsíce výpočtem z aktuálního data
        dt = datetime.now()
        diff = dt - datetime(2001, 1, 1)
        days = diff.days + (diff.seconds / 86400.0)
        lunations = 0.20439731 + (days * 0.03386319269)
        faze = lunations % 1.0
        
        if faze < 0.03 or faze > 0.97: nazev_faze = "Nov"
        elif faze < 0.22: nazev_faze = "Dorustajici srpek"
        elif faze < 0.28: nazev_faze = "Prvni ctvrt"
        elif faze < 0.47: nazev_faze = "Dorustajici mesic"
        elif faze < 0.53: nazev_faze = "Uplnek"
        elif faze < 0.72: nazev_faze = "Couvajici mesic"
        elif faze < 0.78: nazev_faze = "Posledni ctvrt"
        else: nazev_faze = "Ubyvajici srpek"

        # Výpočet osvětlení měsíce v procentech
        osvetleni = round((0.5 * (1 - math.cos(2 * math.pi * faze))) * 100)
        
        vysledek += f"Faze mesice: {nazev_faze}\n"
        vysledek += f"Osvetleni: {osvetleni} %\n"

    except Exception as e:
        vysledek += f"Nepodarilo se stahnout astro data.\nChyba: {e}"
        
    return vysledek

if __name__ == "__main__":
    print("Spoustim stahovani clanku, pocasi, kurzu a astro dat...")
    
    text_svet = stahni_zpravy("https://ct24.ceskatelevize.cz/rss/svet", limit=10)
    text_cr = stahni_zpravy("https://ct24.ceskatelevize.cz/rss/domaci", limit=10)
    text_tech = stahni_zpravy("https://www.lupa.cz/rss/clanky/", limit=10)
    text_pocasi = stahni_pocasi()
    text_kurzy = stahni_kurzy()
    text_astro = stahni_astro()

    with open("zpravy_svet.txt", "w", encoding="utf-8") as f: f.write(text_svet)
    with open("zpravy_cr.txt", "w", encoding="utf-8") as f: f.write(text_cr)
    with open("zpravy_tech.txt", "w", encoding="utf-8") as f: f.write(text_tech)
    with open("pocasi.txt", "w", encoding="utf-8") as f: f.write(text_pocasi)
    with open("kurzy.txt", "w", encoding="utf-8") as f: f.write(text_kurzy)
    with open("astro.txt", "w", encoding="utf-8") as f: f.write(text_astro)
        
    print("Hotovo! Vsechna data vcetne kurzu.txt a astro.txt jsou pripravena pro ESP32.")
