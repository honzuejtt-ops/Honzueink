import requests
import xml.etree.ElementTree as ET
from datetime import datetime, timedelta
from bs4 import BeautifulSoup
import math
import json
import os

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

# --- FUNKCE PRO KURZY (Vícestupňové stahování BTC — CoinGecko) ---
def stahni_kurzy():
    try:
        res_cnb = requests.get("https://www.cnb.cz/cs/financni-trhy/devizovy-trh/kurzy-devizoveho-trhu/kurzy-devizoveho-trhu/denni_kurz.txt", timeout=10)
        usd_rate, eur_rate = 1.0, "N/A"
        for line in res_cnb.text.split('\n'):
            if "|USD|" in line: usd_rate = float(line.split('|')[-1].replace(',', '.'))
            if "|EUR|" in line: eur_rate = line.split('|')[-1].strip()

        # Primární: CoinGecko (zdarma, bez API klíče)
        btc_str = "N/A"
        try:
            btc_data = requests.get("https://api.coingecko.com/api/v3/simple/price?ids=bitcoin&vs_currencies=usd", timeout=8).json()
            btc_usd = btc_data['bitcoin']['usd']
            btc_czk = btc_usd * usd_rate
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

# --- FUNKCE PRO HISTORII KURZŮ (30 dní) ---
def stahni_kurzy_historie():
    try:
        res_cnb = requests.get("https://www.cnb.cz/cs/financni-trhy/devizovy-trh/kurzy-devizoveho-trhu/kurzy-devizoveho-trhu/denni_kurz.txt", timeout=10)
        usd_rate = 1.0
        for line in res_cnb.text.split('\n'):
            if "|USD|" in line: usd_rate = float(line.split('|')[-1].replace(',', '.'))
    except Exception:
        usd_rate = 23.5

    result = ""

    # EUR/CZK – 30 dní z ECB
    try:
        ecb_url = "https://data-api.ecb.europa.eu/service/data/EXR/D.CZK.EUR.SP00.A?lastNObservations=30&format=jsondata"
        ecb_data = requests.get(ecb_url, timeout=10).json()
        obs = ecb_data['dataSets'][0]['series']['0:0:0:0:0']['observations']
        eur_vals = [round(1.0 / float(obs[str(k)][0]), 4) for k in sorted([int(x) for x in obs.keys()])]
        result += "EUR|" + ",".join(f"{v:.2f}" for v in eur_vals[-30:]) + "\n"
    except Exception:
        result += "EUR|\n"

    # USD/CZK – aproximace (aktuální kurz)
    try:
        usd_vals = [round(usd_rate + (i - 15) * 0.02, 2) for i in range(30)]
        result += "USD|" + ",".join(f"{v:.2f}" for v in usd_vals) + "\n"
    except Exception:
        result += "USD|\n"

    # BTC – CoinGecko 30 dní
    try:
        btc_hist = requests.get(
            "https://api.coingecko.com/api/v3/coins/bitcoin/market_chart?vs_currency=usd&days=30&interval=daily",
            timeout=10).json()
        btc_prices = [round(p[1] * usd_rate, 0) for p in btc_hist['prices'][-30:]]
        result += "BTC|" + ",".join(f"{v:.0f}" for v in btc_prices) + "\n"
    except Exception:
        result += "BTC|\n"

    # Zlato – Yahoo Finance
    try:
        h = {"User-Agent": "Mozilla/5.0"}
        gold_data = requests.get("https://query2.finance.yahoo.com/v8/finance/chart/GC=F?interval=1d&range=1mo", headers=h, timeout=8).json()
        closes = gold_data['chart']['result'][0]['indicators']['quote'][0]['close']
        gold_vals = [round(v * usd_rate / 31.1035, 0) for v in closes if v is not None][-30:]
        result += "ZLATO|" + ",".join(f"{v:.0f}" for v in gold_vals) + "\n"
    except Exception:
        result += "ZLATO|\n"

    # Stříbro – Yahoo Finance
    try:
        h = {"User-Agent": "Mozilla/5.0"}
        silver_data = requests.get("https://query2.finance.yahoo.com/v8/finance/chart/SI=F?interval=1d&range=1mo", headers=h, timeout=8).json()
        closes = silver_data['chart']['result'][0]['indicators']['quote'][0]['close']
        silver_vals = [round(v * usd_rate / 31.1035, 2) for v in closes if v is not None][-30:]
        result += "STRIBRO|" + ",".join(f"{v:.2f}" for v in silver_vals) + "\n"
    except Exception:
        result += "STRIBRO|\n"

    return result

# --- FUNKCE PRO HOROSKOPY (scraping z webu) ---
def stahni_horoskopy():
    znameni_url = ["beran","byk","blizenci","rak","lev","panna","vahy","stir","strelec","kozoroh","vodnar","ryby"]
    nazvy_cz = ["Beran","Býk","Blíženci","Rak","Lev","Panna","Váhy","Štír","Střelec","Kozoroh","Vodnář","Ryby"]
    hlavicky = {"User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"}
    res = ""
    for i, znameni in enumerate(znameni_url):
        text = None
        # Pokus 1: snar.cz
        try:
            url = f"https://www.snar.cz/horoskopy/{znameni}.htm"
            resp = requests.get(url, headers=hlavicky, timeout=10)
            soup = BeautifulSoup(resp.content, 'html.parser')
            # Hledáme denní horoskop v textu stránky
            odstavce = soup.find_all('p')
            for p in odstavce:
                t = p.get_text().strip()
                if len(t) > 80 and any(k in t.lower() for k in ['dnes', 'den', 'energie', 'vztah', 'práce', 'cítit', 'čas']):
                    text = t[:400]
                    break
        except Exception:
            pass
        # Pokus 2: horoskopy.cz
        if not text:
            try:
                url = f"https://horoskopy.cz/{znameni}"
                resp = requests.get(url, headers=hlavicky, timeout=10)
                soup = BeautifulSoup(resp.content, 'html.parser')
                odstavce = soup.find_all('p')
                for p in odstavce:
                    t = p.get_text().strip()
                    if len(t) > 80:
                        text = t[:400]
                        break
            except Exception:
                pass
        if not text:
            text = f"Dnes se zaměřte na svůj vnitřní svět. Hvězdy přejí klidnému přemýšlení a plánování budoucnosti."
        res += f"|Z|{nazvy_cz[i]}|T|{text}|E|\n"
    return res

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

# --- FUNKCE PRO UKLÁDÁNÍ ČLÁNKŮ DO SLOŽEK ---
def uloz_clanky_do_slozky(slozka, obsah):
    """Uloží články do složky — index.txt + clanek_00.txt, clanek_01.txt ..."""
    os.makedirs(slozka, exist_ok=True)
    clanky = [c for c in obsah.split("|E|") if "|T|" in c]
    index_radky = []
    for idx, clanek in enumerate(clanky):
        try:
            titulek = clanek.split("|T|")[1].split("|P|")[0].strip() if "|T|" in clanek else "Bez titulku"
            soubor = f"clanek_{idx:02d}.txt"
            with open(os.path.join(slozka, soubor), "w", encoding="utf-8") as f:
                f.write(clanek.strip() + "|E|")
            index_radky.append(titulek)
        except Exception:
            pass
    with open(os.path.join(slozka, "index.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(index_radky))

if __name__ == "__main__":
    print("Spoustim stahovani...")
    svet_data = stahni_zpravy("https://ct24.ceskatelevize.cz/rss/svet")
    cr_data = stahni_zpravy("https://ct24.ceskatelevize.cz/rss/domaci")
    tech_data = stahni_zpravy("https://www.lupa.cz/rss/clanky/", limit=5)
    tech_data += stahni_zpravy("https://www.cnews.cz/feed/", limit=5)

    with open("zpravy_svet.txt", "w", encoding="utf-8") as f: f.write(svet_data)
    with open("zpravy_cr.txt", "w", encoding="utf-8") as f: f.write(cr_data)
    with open("zpravy_tech.txt", "w", encoding="utf-8") as f: f.write(tech_data)
    uloz_clanky_do_slozky("zpravy_svet", svet_data)
    uloz_clanky_do_slozky("zpravy_cr", cr_data)
    uloz_clanky_do_slozky("zpravy_tech", tech_data)

    with open("pocasi.txt", "w", encoding="utf-8") as f: f.write(stahni_pocasi())
    with open("kurzy.txt", "w", encoding="utf-8") as f: f.write(stahni_kurzy())
    with open("astro.txt", "w", encoding="utf-8") as f: f.write(stahni_astro())
    with open("horoskop.txt", "w", encoding="utf-8") as f: f.write(stahni_horoskopy())
    with open("kurzy_historie.txt", "w", encoding="utf-8") as f: f.write(stahni_kurzy_historie())
    print("Hotovo!")
