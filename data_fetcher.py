import requests
import xml.etree.ElementTree as ET
from datetime import datetime
from bs4 import BeautifulSoup
import math
import json

# --- FUNKCE PRO VYTĚŽENÍ TEXTU PŘÍMO Z ČLÁNKU ---
def stahni_text_clanku(url, perex):
    hlavicky = {"User-Agent": "Mozilla/5.0"}
    try:
        odpoved = requests.get(url, headers=hlavicky, timeout=10)
        soup = BeautifulSoup(odpoved.content, 'html.parser')
        odstavce = soup.find_all('p')
        obsah = ""
        for p in odstavce:
            text = p.get_text().strip()
            if len(text) > 40: obsah += text + "\n"
        if "Enable JavaScript" in obsah or len(obsah) < 100: return perex + "\n\n(Pozn: Celý text web zablokoval)"
        if len(obsah) > 1500: obsah = obsah[:1500] + "...\n(Pokracovani na webu)"
        return obsah
    except Exception: return perex + "\n\n(Pozn: Chyba při stahování)"

# --- FUNKCE PRO ZPRÁVY (RSS) ---
def stahni_zpravy(rss_url, limit=10):
    hlavicky = {"User-Agent": "Mozilla/5.0"}
    try:
        odpoved = requests.get(rss_url, headers=hlavicky, timeout=10)
        root = ET.fromstring(odpoved.content)
        items = root.findall('.//item')
        vysledny_text = ""
        if not items: return "|T|Žádné zprávy|P|Nebyly nalezeny.|X|Zkuste to později.|E|"
        for item in items[:limit]:
            titulek = item.find('title').text.strip() if item.find('title') is not None else "Bez titulku"
            odkaz = item.find('link').text.strip() if item.find('link') is not None else ""
            perex_raw = item.find('description').text.strip() if item.find('description') is not None else ""
            perex = BeautifulSoup(perex_raw, "html.parser").get_text()
            perex_kratky = perex[:100] + "..." if len(perex) > 100 else perex
            text_clanku = stahni_text_clanku(odkaz, perex) if odkaz else perex
            vysledny_text += f"|T|{titulek}|P|{perex_kratky}|X|{text_clanku}|E|"
        return vysledny_text
    except Exception as e: return f"|T|Chyba|P|Něco se pokazilo.|X|{e}|E|"

# --- FUNKCE PRO POČASÍ (Obohaceno o Východ a Západ slunce) ---
def stahni_pocasi():
    url = "https://api.open-meteo.com/v1/forecast?latitude=50.088&longitude=14.4208&daily=weathercode,temperature_2m_max,temperature_2m_min,precipitation_sum,windspeed_10m_max,sunrise,sunset&timezone=Europe/Berlin"
    try:
        data = requests.get(url, timeout=10).json()['daily']
        res = ""
        for i in range(7):
            dt = datetime.strptime(data['time'][i], "%Y-%m-%d").strftime("%d.%m.")
            ikona = "OBLACNO"
            kod = data['weathercode'][i]
            if kod in [0, 1]: ikona = "SLUNCE"
            elif kod in [2, 3]: ikona = "MRAKY"
            elif kod in [61, 63, 65, 80, 81, 82]: ikona = "DEST"
            elif kod in [95, 96, 99]: ikona = "BOURKA"
            elif kod in [71, 73, 75, 85, 86]: ikona = "SNIH"
            
            tmax = round(data['temperature_2m_max'][i])
            tmin = round(data['temperature_2m_min'][i])
            srazky = data['precipitation_sum'][i]
            vitr = round(data['windspeed_10m_max'][i]/3.6)
            vychod = data['sunrise'][i].split('T')[1]
            zapad = data['sunset'][i].split('T')[1]
            
            res += f"{dt}|{tmax}|{tmin}|{srazky}|{ikona}|{vitr}|{vychod}|{zapad}\n"
        return res
    except Exception as e: return f"Chyba pocasi: {e}"

# --- FUNKCE PRO KURZY (Vícestupňové stahování BTC) ---
def stahni_kurzy():
    try:
        res_cnb = requests.get("https://www.cnb.cz/cs/financni-trhy/devizovy-trh/kurzy-devizoveho-trhu/kurzy-devizoveho-trhu/denni_kurz.txt", timeout=10)
        usd_rate, eur_rate = 1.0, "N/A"
        for line in res_cnb.text.split('\n'):
            if "|USD|" in line: usd_rate = float(line.split('|')[-1].replace(',', '.'))
            if "|EUR|" in line: eur_rate = line.split('|')[-1].strip()

        # Dvojitá kontrola BTC (nejdřív CoinDesk, pak Binance)
        btc_str = "N/A"
        try:
            btc_data = requests.get("https://api.coindesk.com/v1/bpi/currentprice.json", timeout=5).json()
            btc_czk = float(btc_data['bpi']['USD']['rate_float']) * usd_rate
            btc_str = f"{btc_czk:,.0f}".replace(',', ' ')
        except Exception:
            try:
                btc_data = requests.get("https://api.binance.com/api/v3/ticker/price?symbol=BTCUSDT", timeout=5).json()
                btc_czk = float(btc_data['price']) * usd_rate
                btc_str = f"{btc_czk:,.0f}".replace(',', ' ')
            except Exception:
                pass

        def get_metal(ticker):
            h = {"User-Agent": "Mozilla/5.0"}
            data = requests.get(f"https://query2.finance.yahoo.com/v8/finance/chart/{ticker}", headers=h, timeout=5).json()
            return data['chart']['result'][0]['meta']['regularMarketPrice']

        try: gold_str = f"{(get_metal('GC=F') * usd_rate)/31.1035:.0f}"
        except: gold_str = "N/A"

        try: silver_str = f"{(get_metal('SI=F') * usd_rate)/31.1035:.2f}"
        except: silver_str = "N/A"

        return f"{eur_rate}|{usd_rate:.2f}|{btc_str}|{gold_str}|{silver_str}"
    except Exception: return "Chyba|Chyba|Chyba|Chyba|Chyba"

# --- FUNKCE PRO ASTRO (TEXT NA 7 DNÍ) ---
def stahni_astro():
    url = "https://api.open-meteo.com/v1/forecast?latitude=50.088&longitude=14.4208&daily=sunrise,sunset&timezone=Europe/Berlin"
    try:
        data = requests.get(url, timeout=10).json()['daily']
        res = "ASTROLOGICKA PREDPOVED\n\n"
        
        for i in range(7):
            dt_str = data['time'][i]
            dt_zobrazeni = datetime.strptime(dt_str, "%Y-%m-%d").strftime("%d. %m.")
            vychod = data['sunrise'][i].split('T')[1]
            zapad = data['sunset'][i].split('T')[1]
            
            target_date = datetime.strptime(dt_str, "%Y-%m-%d")
            diff = target_date - datetime(2001, 1, 1)
            faze_raw = (0.20439731 + (diff.days) * 0.03386319269) % 1.0
            osvetleni = round((0.5 * (1 - math.cos(2 * math.pi * faze_raw))) * 100)
            
            if faze_raw < 0.03 or faze_raw > 0.97: faze_nazev = "Nov"
            elif faze_raw < 0.22: faze_nazev = "Dorustajici srpek"
            elif faze_raw < 0.28: faze_nazev = "Prvni ctvrt"
            elif faze_raw < 0.47: faze_nazev = "Dorustajici Mesic"
            elif faze_raw < 0.53: faze_nazev = "Uplnek"
            elif faze_raw < 0.72: faze_nazev = "Couvajici Mesic"
            elif faze_raw < 0.78: faze_nazev = "Posledni ctvrt"
            else: faze_nazev = "Couvajici srpek"
            
            res += f"[{dt_zobrazeni}]\n"
            res += f"Slunce: {vychod} - {zapad}\n"
            res += f"Mesic: {faze_nazev} ({osvetleni}%)\n"
            res += "-" * 20 + "\n"
            
        return res
    except Exception as e: return f"Chyba pri stahovani astro dat: {e}"

if __name__ == "__main__":
    print("Spoustim stahovani...")
    with open("zpravy_svet.txt", "w", encoding="utf-8") as f: f.write(stahni_zpravy("https://ct24.ceskatelevize.cz/rss/svet"))
    with open("zpravy_cr.txt", "w", encoding="utf-8") as f: f.write(stahni_zpravy("https://ct24.ceskatelevize.cz/rss/domaci"))
    with open("zpravy_tech.txt", "w", encoding="utf-8") as f: f.write(stahni_zpravy("https://www.lupa.cz/rss/clanky/"))
    with open("pocasi.txt", "w", encoding="utf-8") as f: f.write(stahni_pocasi())
    with open("kurzy.txt", "w", encoding="utf-8") as f: f.write(stahni_kurzy())
    with open("astro.txt", "w", encoding="utf-8") as f: f.write(stahni_astro())
    print("Hotovo!")
