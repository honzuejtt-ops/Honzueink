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

# --- FUNKCE PRO ZPRÁVY (RSS) ---
def stahni_zpravy(rss_url, limit=10):
    hlavicky = {"User-Agent": "Mozilla/5.0"}
    try:
        odpoved = requests.get(rss_url, headers=hlavicky, timeout=10)
        root = ET.fromstring(odpoved.content)
        items = root.findall('.//item')
        vysledny_text = ""
        if not items: return "|T|Žádné zprávy|P|Nebyly nalezeny žádné články.|X|Zkuste to později.|E|"
        for item in items[:limit]:
            titulek = item.find('title').text.strip() if item.find('title') is not None else "Bez titulku"
            odkaz = item.find('link').text.strip() if item.find('link') is not None else ""
            perex_raw = item.find('description').text.strip() if item.find('description') is not None else ""
            perex = BeautifulSoup(perex_raw, "html.parser").get_text()
            perex_kratky = perex[:100] + "..." if len(perex) > 100 else perex
            text_clanku = stahni_text_clanku(odkaz, perex) if odkaz else perex
            vysledny_text += f"|T|{titulek}|P|{perex_kratky}|X|{text_clanku}|E|"
        return vysledny_text
    except Exception as e: return f"|T|Chyba stahování|P|Něco se pokazilo.|X|{e}|E|"

# --- FUNKCE PRO POČASÍ ---
def stahni_pocasi():
    url = "https://api.open-meteo.com/v1/forecast?latitude=50.088&longitude=14.4208&daily=weathercode,temperature_2m_max,temperature_2m_min,precipitation_sum,windspeed_10m_max&timezone=Europe/Berlin"
    try:
        data = requests.get(url, timeout=10).json()['daily']
        vysledny_text = ""
        for i in range(7):
            dt = datetime.strptime(data['time'][i], "%Y-%m-%d").strftime("%d.%m.")
            ikona = "OBLACNO"
            kod = data['weathercode'][i]
            if kod in [0, 1]: ikona = "SLUNCE"
            elif kod in [2, 3]: ikona = "MRAKY"
            elif kod in [61, 63, 65, 80, 81, 82]: ikona = "DEST"
            elif kod in [95, 96, 99]: ikona = "BOURKA"
            elif kod in [71, 73, 75, 85, 86]: ikona = "SNIH"
            vysledny_text += f"{dt}|{round(data['temperature_2m_max'][i])}|{round(data['temperature_2m_min'][i])}|{data['precipitation_sum'][i]}|{ikona}|{round(data['windspeed_10m_max'][i]/3.6)}\n"
        return vysledny_text
    except Exception as e: return f"Chyba pocasi: {e}"

# --- FUNKCE PRO KURZY ---
def stahni_kurzy():
    vysledek = "=== KURZY (CZK) ===\n\n"
    try:
        res_cnb = requests.get("https://www.cnb.cz/cs/financni-trhy/devizovy-trh/kurzy-devizoveho-trhu/kurzy-devizoveho-trhu/denni_kurz.txt", timeout=10)
        usd_rate = 1.0
        eur_rate = "N/A"
        for line in res_cnb.text.split('\n'):
            if "|USD|" in line: usd_rate = float(line.split('|')[-1].replace(',', '.'))
            if "|EUR|" in line: eur_rate = line.split('|')[-1].strip()
        
        vysledek += f"Euro: {eur_rate} CZK\n"
        vysledek += f"Dolar: {usd_rate:.2f} CZK\n\n"

        try:
            btc = requests.get("https://api.binance.com/api/v3/ticker/price?symbol=BTCUSDT", timeout=5).json()
            vysledek += f"BTC: {(float(btc['price']) * usd_rate):,.0f} CZK\n".replace(',', ' ')
        except Exception: vysledek += "BTC: nedostupne\n"

        def get_metal(ticker):
            h = {"User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) Chrome/120.0.0.0 Safari/537.36"}
            data = requests.get(f"https://query2.finance.yahoo.com/v8/finance/chart/{ticker}", headers=h, timeout=5).json()
            if data and 'chart' in data and data['chart']['result']:
                return data['chart']['result'][0]['meta']['regularMarketPrice']
            return None

        gold = get_metal('GC=F')
        silver = get_metal('SI=F')

        if gold: vysledek += f"Zlato (1g): {(gold * usd_rate)/31.1035:.0f} CZK\n"
        else: vysledek += "Zlato: nedostupne\n"

        if silver: vysledek += f"Stribro (1g): {(silver * usd_rate)/31.1035:.2f} CZK\n"
        else: vysledek += "Stribro: nedostupne\n"

    except Exception as e: vysledek += "Kriticka chyba site.\n"
    return vysledek

# --- FUNKCE PRO ASTRO ---
def stahni_astro():
    try:
        r = requests.get("https://api.sunrisesunset.io/json?lat=50.088&lng=14.4208&timezone=Europe/Prague", timeout=10).json()['results']
        def to_24h(t): return datetime.strptime(t, "%I:%M:%S %p").strftime("%H:%M")
        
        diff = datetime.now() - datetime(2001, 1, 1)
        faze_raw = (0.20439731 + (diff.days + diff.seconds/86400.0) * 0.03386319269) % 1.0
        osvetleni = round((0.5 * (1 - math.cos(2 * math.pi * faze_raw))) * 100)
        idx = int(((faze_raw + 0.0625) % 1.0) * 8) 
        
        # Formát pro C++ ESP32 parser
        return f"{to_24h(r['sunrise'])}|{to_24h(r['sunset'])}|{to_24h(r['golden_hour'])}|{idx}|{osvetleni}"
    except Exception as e: return f"Chyba|Chyba|Chyba|0|0"

if __name__ == "__main__":
    print("Spoustim stahovani vsech dat...")
    with open("zpravy_svet.txt", "w", encoding="utf-8") as f: f.write(stahni_zpravy("https://ct24.ceskatelevize.cz/rss/svet"))
    with open("zpravy_cr.txt", "w", encoding="utf-8") as f: f.write(stahni_zpravy("https://ct24.ceskatelevize.cz/rss/domaci"))
    with open("zpravy_tech.txt", "w", encoding="utf-8") as f: f.write(stahni_zpravy("https://www.lupa.cz/rss/clanky/"))
    with open("pocasi.txt", "w", encoding="utf-8") as f: f.write(stahni_pocasi())
    with open("kurzy.txt", "w", encoding="utf-8") as f: f.write(stahni_kurzy())
    with open("astro.txt", "w", encoding="utf-8") as f: f.write(stahni_astro())
    print("Hotovo! Data pripravena.")
