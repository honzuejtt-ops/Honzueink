import os
import shutil
import requests
import xml.etree.ElementTree as ET
from datetime import datetime
from bs4 import BeautifulSoup
import trafilatura
import re
import time
import hashlib
from datetime import datetime, timedelta

# Kořenová cesta k SD kartě (nebo lokální složce při spuštění v CI/locálně)
SD_CESTA = os.environ.get('SD_CESTA', './eindata')

# Sdileny HTTP klient + jednoduche retry
SESSION = requests.Session()

def get_with_retry(url, headers=None, timeout=10, retries=3, backoff=1.2):
    last_err = None
    for attempt in range(retries):
        try:
            return SESSION.get(url, headers=headers, timeout=timeout)
        except requests.RequestException as e:
            last_err = e
            if attempt < retries - 1:
                time.sleep(backoff * (attempt + 1))
    raise last_err

# --- FUNKCE PRO VYTĚŽENÍ TEXTU PŘÍMO Z ČLÁNKU (Trafilatura) ---
def stahni_text_clanku(url, perex):
    try:
        stazeno = trafilatura.fetch_url(url)
        obsah = trafilatura.extract(stazeno, include_comments=False, include_tables=False)
        
        if obsah:
            # --- DETEKCE PAYWALLU A REKLAM ---
            paywall_klicova_slova = [
                "vstup do clubu a získáš", "přístup k exkluzivnímu obsahu", 
                "předplatím si za", "čtení bez omezení", "idnes premium", 
                "kč / týden", "účtuje se jednou", "odemkněte článek",
                "kupte si předplatné", "zakoupením získáte"
            ]
            
            obsah_maly = obsah.lower()
            for slovo in paywall_klicova_slova:
                if slovo in obsah_maly:
                    return perex + "\n\n(Pozn: Článek je uzamčen za paywallem/předplatným)"
            
            if len(obsah) > 10000:
                return obsah[:10000] + "...\n(Pokracovani na webu)"
            return obsah
            
        return perex + "\n\n(Pozn: Z tohoto webu nelze obsah přímo vyčíst)"
    except Exception:
        return perex + "\n\n(Pozn: Chyba při stahování)"

# --- FUNKCE PRO ZPRÁVY (RSS) ---
def stahni_zpravy(rss_url, limit=15):
    from email.utils import parsedate_to_datetime
    hlavicky = {"User-Agent": "Mozilla/5.0"}
    try:
        odpoved = get_with_retry(rss_url, headers=hlavicky, timeout=10, retries=3)
        # Použijeme BeautifulSoup pro parsování XML — je mnohem tolerantnější k jmenným prostorům WP
        soup = BeautifulSoup(odpoved.content, 'xml')
        items = soup.find_all('item')
        
        vysledny_text = ""
        if not items: 
            return "|T|Žádné zprávy|D||P|Nebyly nalezeny.|X|Zkuste to později.|E|"
            
        for item in items[:limit]:
            titulek = item.find('title').get_text().strip() if item.find('title') else "Bez titulku"
            odkaz = item.find('link').get_text().strip() if item.find('link') else ""

            # Datum
            pub_date = item.find('pubDate')
            datum_cas = ""
            if pub_date and pub_date.get_text():
                try:
                    dt = parsedate_to_datetime(pub_date.get_text().strip())
                    datum_cas = dt.strftime("%d.%m.%Y %H:%M")
                except Exception:
                    datum_cas = datetime.now().strftime("%d.%m.%Y %H:%M")
            else:
                datum_cas = datetime.now().strftime("%d.%m.%Y %H:%M")

            # Perex (description)
            perex_el = item.find('description')
            perex_raw = perex_el.get_text().strip() if perex_el else ""
            perex = BeautifulSoup(perex_raw, "html.parser").get_text()
            perex_kratky = (perex[:150] + "...") if len(perex) > 150 else perex
            
            # Plný text
            text_clanku = stahni_text_clanku(odkaz, perex) if odkaz else perex
            vysledny_text += f"|T|{titulek}|D|{datum_cas}|P|{perex_kratky}|X|{text_clanku}|E|"
            
        return vysledny_text
    except Exception as e: 
        return f"|T|Chyba|D||P|Něco se pokazilo.|X|{e}|E|"

# --- FUNKCE PRO WEBY BEZ RSS (Refresher, Antiyoutuber atd.) ---
# --- FUNKCE PRO POČASÍ ---
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

# --- FUNKCE PRO PALIVA (Nafta, Benzin) ---
def stahni_paliva():
    """Stáhne aktuální ceny benzínu Natural 95 a nafty v ČR."""
    nafta, benzin = "N/A", "N/A"
    try:
        r = requests.get("https://www.mbenzin.cz/api/v1/prices", timeout=8)
        data = r.json()
        if "Nafta" in data:
            nafta = f"{data['Nafta']:.2f}"
        if "Natural95" in data:
            benzin = f"{data['Natural95']:.2f}"
    except Exception:
        pass

    if nafta == "N/A" or benzin == "N/A":
        try:
            h = {"User-Agent": "Mozilla/5.0"}
            r = requests.get("https://www.kurzy.cz/komodity/benzin-nafta-cena/", headers=h, timeout=8)
            from bs4 import BeautifulSoup
            soup = BeautifulSoup(r.content, "html.parser")
            rows = soup.select("table tr")
            for row in rows:
                cells = row.find_all("td")
                if len(cells) >= 3:
                    label = cells[0].get_text().strip().lower()
                    val = cells[2].get_text().strip().replace(",", ".").replace("Kč", "").strip()
                    try:
                        if "natural 95" in label and benzin == "N/A":
                            benzin = f"{float(val):.2f}"
                        if "nafta" in label and nafta == "N/A":
                            nafta = f"{float(val):.2f}"
                    except ValueError:
                        pass
        except Exception:
            pass

    if nafta == "N/A":
        nafta = "34.00"
    if benzin == "N/A":
        benzin = "36.00"

    return nafta, benzin

# --- FUNKCE PRO KURZY ---
def stahni_kurzy():
    try:
        res_cnb = requests.get("https://www.cnb.cz/cs/financni-trhy/devizovy-trh/kurzy-devizoveho-trhu/kurzy-devizoveho-trhu/denni_kurz.txt", timeout=10)
        usd_rate, eur_rate = 1.0, "N/A"
        for line in res_cnb.text.split('\n'):
            if "|USD|" in line: usd_rate = float(line.split('|')[-1].replace(',', '.'))
            if "|EUR|" in line: eur_rate = line.split('|')[-1].strip()

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

        nafta, benzin = stahni_paliva()

        return f"{eur_rate}|{usd_rate:.2f}|{btc_str}|{gold_str}|{silver_str}|{nafta}|{benzin}"
    except Exception: return "Chyba|Chyba|Chyba|Chyba|Chyba|Chyba|Chyba"

# --- FUNKCE PRO HISTORII KURZŮ ---
def stahni_kurzy_historie():
    try:
        res_cnb = requests.get("https://www.cnb.cz/cs/financni-trhy/devizovy-trh/kurzy-devizoveho-trhu/kurzy-devizoveho-trhu/denni_kurz.txt", timeout=10)
        usd_rate = 1.0
        for line in res_cnb.text.split('\n'):
            if "|USD|" in line: usd_rate = float(line.split('|')[-1].replace(',', '.'))
    except Exception:
        usd_rate = 23.5

    result = ""

    try:
        ecb_url = "https://data-api.ecb.europa.eu/service/data/EXR/D.CZK.EUR.SP00.A?lastNObservations=30&format=jsondata"
        ecb_data = requests.get(ecb_url, timeout=10).json()
        obs = ecb_data['dataSets'][0]['series']['0:0:0:0:0']['observations']
        eur_vals = [round(float(obs[str(k)][0]), 4) for k in sorted([int(x) for x in obs.keys()])]
        result += "EUR|" + ",".join(f"{v:.2f}" for v in eur_vals[-30:]) + "\n"
    except Exception:
        result += "EUR|\n"

    try:
        from datetime import timedelta
        end_d = datetime.now()
        start_d = end_d - timedelta(days=45)
        od_str = start_d.strftime("%d.%m.%Y")
        do_str = end_d.strftime("%d.%m.%Y")
        cnb_url = f"https://www.cnb.cz/cs/financni-trhy/devizovy-trh/kurzy-devizoveho-trhu/kurzy-devizoveho-trhu/vybrane.txt?od={od_str}&do={do_str}&mena=USD&format=txt"
        cnb_data = requests.get(cnb_url, timeout=10).text
        lines = cnb_data.strip().split('\n')[2:]
        usd_vals = [round(float(line.split('|')[1].replace(',', '.')), 2) for line in lines if '|' in line]
        result += "USD|" + ",".join(f"{v:.2f}" for v in usd_vals[-30:]) + "\n"
    except Exception:
        try:
            from datetime import timedelta
            start_d = (datetime.now() - timedelta(days=45)).strftime("%Y-%m-%d")
            usd_hist = requests.get(
                f"https://api.frankfurter.dev/v1/{start_d}..?base=USD&symbols=CZK",
                timeout=10).json()
            usd_vals = [round(v["CZK"], 2) for _, v in sorted(usd_hist["rates"].items())]
            result += "USD|" + ",".join(f"{v:.2f}" for v in usd_vals[-30:]) + "\n"
        except Exception:
            result += "USD|\n"

    try:
        btc_hist = requests.get(
            "https://api.coingecko.com/api/v3/coins/bitcoin/market_chart?vs_currency=usd&days=30&interval=daily",
            timeout=10).json()
        btc_prices = [round(p[1] * usd_rate, 0) for p in btc_hist['prices'][-30:]]
        result += "BTC|" + ",".join(f"{v:.0f}" for v in btc_prices) + "\n"
    except Exception:
        result += "BTC|\n"

    try:
        h = {"User-Agent": "Mozilla/5.0"}
        gold_data = requests.get("https://query2.finance.yahoo.com/v8/finance/chart/GC=F?interval=1d&range=1mo", headers=h, timeout=8).json()
        closes = gold_data['chart']['result'][0]['indicators']['quote'][0]['close']
        gold_vals = [round(v * usd_rate / 31.1035, 0) for v in closes if v is not None][-30:]
        result += "ZLATO|" + ",".join(f"{v:.0f}" for v in gold_vals) + "\n"
    except Exception:
        result += "ZLATO|\n"

    try:
        h = {"User-Agent": "Mozilla/5.0"}
        silver_data = requests.get("https://query2.finance.yahoo.com/v8/finance/chart/SI=F?interval=1d&range=1mo", headers=h, timeout=8).json()
        closes = silver_data['chart']['result'][0]['indicators']['quote'][0]['close']
        silver_vals = [round(v * usd_rate / 31.1035, 2) for v in closes if v is not None][-30:]
        result += "STRIBRO|" + ",".join(f"{v:.2f}" for v in silver_vals) + "\n"
    except Exception:
        result += "STRIBRO|\n"

    # NAFTA a BENZEN — historie se akumuluje z denních běhů (neexistuje veřejné API pro historii cen paliv v ČR)
    nafta_now, benzin_now = stahni_paliva()
    nafta_vals, benzin_vals = [], []
    cache_path = os.path.join(SD_CESTA, 'cache', 'kurzy_historie.txt')
    if os.path.exists(cache_path):
        with open(cache_path, 'r', encoding='utf-8') as f:
            for line in f:
                line = line.strip()
                if line.startswith("NAFTA|"):
                    nafta_vals = [v for v in line[len("NAFTA|"):].split(',') if v]
                elif line.startswith("BENZEN|"):
                    benzin_vals = [v for v in line[len("BENZEN|"):].split(',') if v]
    nafta_vals.append(nafta_now)
    benzin_vals.append(benzin_now)
    if len(nafta_vals) > 30: nafta_vals = nafta_vals[-30:]
    if len(benzin_vals) > 30: benzin_vals = benzin_vals[-30:]
    result += "NAFTA|" + ",".join(nafta_vals) + "\n"
    result += "BENZEN|" + ",".join(benzin_vals) + "\n"

    return result

def stahni_sync_kurzy(full_history, days=15):
    """Vytvoří synchronizační soubor pro posledních N dní pro Gap-Filling na ESP32."""
    lines = full_history.strip().split('\n')
    data_by_currency = {}
    for line in lines:
        if '|' in line:
            curr, vals_str = line.split('|')
            data_by_currency[curr] = vals_str.split(',')
    
    # Předpokládáme, že indexy v polích odpovídají dnům pozpátku od dnes
    # (ve skutečnosti ECB data mají různou délku, ale u nás v scriptu jsou ořezaná na 30)
    # Pro jednoduchost vytvoříme řádky pro posledních N záznamů
    sync_result = ""
    pocet_dostupnych = len(next(iter(data_by_currency.values())))
    start_idx = max(0, pocet_dostupnych - days)
    
    today = datetime.now()
    for i in range(start_idx, pocet_dostupnych):
        # Odhadneme datum (není to 100% přesné kvůli víkendům, ale pro sync to stačí, 
        # protože ESP32 kontroluje existenci datumu v lokálním souboru)
        # Lepší by bylo mít datum přímo v history, ale to by vyžadovalo změnu celého formátu.
        # Pro Gap-filling budeme používat 'dnes - offset'
        d = today - timedelta(days=(pocet_dostupnych - 1 - i))
        date_str = d.strftime("%Y-%m-%d")
        
        row_vals = []
        for curr in ["EUR", "USD", "BTC", "ZLATO", "STRIBRO", "NAFTA", "BENZEN"]:
            if curr in data_by_currency and i < len(data_by_currency[curr]):
                row_vals.append(data_by_currency[curr][i])
            else:
                row_vals.append("0")
        sync_result += f"{date_str}|" + "|".join(row_vals) + "\n"
    return sync_result

# --- FUNKCE PRO HOROSKOPY ---
def stahni_horoskopy():
    znameni_url = ["beran","byk","blizenci","rak","lev","panna","vahy","stir","strelec","kozoroh","vodnar","ryby"]
    nazvy_cz = ["Beran","Býk","Blíženci","Rak","Lev","Panna","Váhy","Štír","Střelec","Kozoroh","Vodnář","Ryby"]
    hlavicky = {"User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"}
    res = ""
    for i, znameni in enumerate(znameni_url):
        text = None
        try:
            url = f"https://www.snar.cz/horoskopy/{znameni}.htm"
            resp = requests.get(url, headers=hlavicky, timeout=10)
            soup = BeautifulSoup(resp.content, 'html.parser')
            odstavce = soup.find_all('p')
            for p in odstavce:
                t = p.get_text().strip()
                if len(t) > 80 and any(k in t.lower() for k in ['dnes', 'den', 'energie', 'vztah', 'práce', 'cítit', 'čas']):
                    text = t[:400]
                    break
        except Exception:
            pass
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

def stahni_zpravy_multi(zdroje, celkovy_limit=10, exclude_titulky=None, max_age_days=1):
    """Stáhne zprávy z více RSS zdrojů, deduplikuje podle titulku a zaloguje průběh.
    exclude_titulky: set titulků které se mají přeskočit (cross-category deduplikace).
    max_age_days: kolik dní zpětně se mají zprávy stahovat.
    """
    vybrane = []  # (dt, titulek, blok)
    # Inicializujeme titulky rovnou s vyloučenými - při shodě se blok přeskočí
    titulky = set(exclude_titulky) if exclude_titulky else set()
    pocet_unikat = 0
    for rss_url, limit_per_source in zdroje:
        if pocet_unikat >= celkovy_limit:
            break
        zbyvajici = celkovy_limit - pocet_unikat
        limit = min(limit_per_source, zbyvajici)
        data = stahni_zpravy(rss_url, limit=limit)
        bloky = extrahuj_bloky(data)
        pridano = 0
        duplikaty = 0
        for b in bloky:
            titulek = b["titulek"]
            if titulek in titulky:
                duplikaty += 1
                continue
            dt = parse_datum_na_datetime(b["datum_raw"])
            
            # Ignorovat zprávy starší než X dní
            if dt < datetime.now().replace(hour=0, minute=0, second=0, microsecond=0) - timedelta(days=max_age_days):
                continue
            
            vybrane.append((dt, titulek, b["blok"]))
            titulky.add(titulek)
            pridano += 1
            pocet_unikat += 1
            if pocet_unikat >= celkovy_limit:
                break
        print(f"  RSS {rss_url}: raw={len(bloky)} pridano={pridano} dup={duplikaty}")

    vybrane.sort(key=lambda x: x[0], reverse=True)
    
    # Segmentované ukládání: Vytvoříme Index a Detaily
    vysledny_index = ""
    vysledna_data_all = ""
    
    # Cesta pro detaily (bude v eindata/zpravy/aktualni/detail/)
    detail_dir = os.path.join(SD_CESTA, 'zpravy', 'aktualni', 'detail')
    os.makedirs(detail_dir, exist_ok=True)

    for dt, titulek, blok in vybrane[:celkovy_limit]:
        # Generujeme ID ze zkráceného heše titulku
        clanek_id = hashlib.md5(titulek.encode('utf-8')).hexdigest()[:12]
        
        # Extrahujeme text článku z bloku (původně mezi |X| a |E|)
        try:
            x_pos = blok.find("|X|")
            e_pos = blok.find("|E|", x_pos)
            text_clanku = blok[x_pos+3:e_pos]
            perex_blok = blok[:x_pos+3] # |T|...|D|...|P|...|X|
            
            # Uložíme text článku do samostatného souboru
            with open(os.path.join(detail_dir, f"{clanek_id}.txt"), 'w', encoding='utf-8') as f:
                f.write(text_clanku)
            
            # Do indexu dáme jen metadata + ID místo celého textu
            vysledny_index += f"{perex_blok}{clanek_id}|E|"
            vysledna_data_all += blok # Necháme i All-in-one pro archiv
        except Exception:
            vysledna_data_all += blok

    return vysledny_index, vysledna_data_all

def extrahuj_titulky(text):
    """Vrátí set titulků ze zpravodajského textu ve formátu |T|...|E|."""
    titulky = set()
    pos = 0
    while True:
        t_start = text.find("|T|", pos)
        if t_start == -1:
            break
        p_start = text.find("|P|", t_start + 3)
        if p_start == -1:
            break
        d_start = text.find("|D|", t_start + 3)
        konec = d_start if (d_start != -1 and d_start < p_start) else p_start
        titulek = text[t_start + 3:konec].strip()
        titulky.add(titulek)
        e_pos = text.find("|E|", konec)
        if e_pos == -1:
            break
        pos = e_pos + 3
    return titulky

def extrahuj_bloky(text):
    """Vrátí list dictů: titulek, datum_raw, blok."""
    bloky = []
    pos = 0
    while True:
        t_start = text.find("|T|", pos)
        if t_start == -1:
            break
        e_end = text.find("|E|", t_start)
        if e_end == -1:
            break
        e_end += 3
        blok = text[t_start:e_end]
        p_start = blok.find("|P|")
        if p_start == -1:
            pos = e_end
            continue
        d_start = blok.find("|D|")
        konec = d_start if (d_start != -1 and d_start < p_start) else p_start
        titulek = blok[3:konec].strip()
        datum_raw = ""
        if d_start != -1 and p_start > d_start + 3:
            datum_raw = blok[d_start + 3:p_start].strip()
        bloky.append({
            "titulek": titulek,
            "datum_raw": datum_raw,
            "blok": blok
        })
        pos = e_end
    return bloky

def parse_datum_na_datetime(datum_raw):
    """
    Převede datum článku na datetime.
    Podporuje např.: '2026-04-03 12:34', '3.4.2026 12:34', '3.4. 12:34', '3.4.'.
    Když chybí rok, použije aktuální.
    """
    if not datum_raw:
        return datetime.now().replace(second=0, microsecond=0)

    s = datum_raw.strip().replace(",", " ").replace("T", " ")
    s = re.sub(r"\s+", " ", s)

    m = re.search(r"(\d{4})-(\d{2})-(\d{2})(?:\s+(\d{1,2}):(\d{2}))?", s)
    if m:
        y, mo, d = int(m.group(1)), int(m.group(2)), int(m.group(3))
        hh, mm = int(m.group(4) or 0), int(m.group(5) or 0)
        try:
            return datetime(y, mo, d, hh, mm)
        except ValueError:
            pass

    m = re.search(r"(\d{1,2})\.(\d{1,2})\.(\d{4})?(?:\s+(\d{1,2}):(\d{2}))?", s)
    if m:
        d, mo = int(m.group(1)), int(m.group(2))
        y = int(m.group(3)) if m.group(3) else datetime.now().year
        hh, mm = int(m.group(4) or 0), int(m.group(5) or 0)
        try:
            return datetime(y, mo, d, hh, mm)
        except ValueError:
            pass

    return datetime.now().replace(second=0, microsecond=0)

def uloz_archiv_deduplikovane_po_dnech(archiv_dir, soubor, nova_data):
    """
    Uloží články do archiv/YYYY-MM-DD/{soubor} podle data článku.
    V cílovém souboru zachová deduplikaci podle titulku a řadí od nejnovějších.
    """
    os.makedirs(archiv_dir, exist_ok=True)
    existujici_titulky = set()
    for den in os.listdir(archiv_dir):
        p = os.path.join(archiv_dir, den, soubor)
        if os.path.exists(p):
            with open(p, 'r', encoding='utf-8') as f:
                existujici_titulky |= extrahuj_titulky(f.read())

    nove_bloky = extrahuj_bloky(nova_data)
    podle_dne = {}
    pridano = 0

    for item in nove_bloky:
        titulek = item["titulek"]
        if titulek in existujici_titulky:
            continue

        dt = parse_datum_na_datetime(item["datum_raw"])
        den = dt.strftime("%Y-%m-%d")
        item["dt"] = dt
        podle_dne.setdefault(den, []).append(item)
        existujici_titulky.add(titulek)
        pridano += 1

    for den, items in podle_dne.items():
        den_dir = os.path.join(archiv_dir, den)
        os.makedirs(den_dir, exist_ok=True)
        archiv_cesta = os.path.join(den_dir, soubor)

        exist = []
        if os.path.exists(archiv_cesta):
            with open(archiv_cesta, 'r', encoding='utf-8') as f:
                for old in extrahuj_bloky(f.read()):
                    old["dt"] = parse_datum_na_datetime(old["datum_raw"])
                    exist.append(old)

        merged = {}
        for old in exist:
            merged[old["titulek"]] = old
        for new in items:
            merged[new["titulek"]] = new

        serazene = sorted(merged.values(), key=lambda x: x["dt"], reverse=True)
        with open(archiv_cesta, 'w', encoding='utf-8') as f:
            for x in serazene:
                f.write(x["blok"] + "\n")

    return pridano

def oprav_existujici_archiv(archiv_dir):
    """
    Fixne špatně datované články v archivu:
    - článek přesune do správné YYYY-MM-DD složky dle |D|
    - v každém souboru setřídí od nejnovějšího
    """
    if not os.path.isdir(archiv_dir):
        return

    soubory = ('zpravy_svet.txt', 'zpravy_cr.txt', 'zpravy_tech.txt', 'zpravy_pozitivni.txt')
    agregace = {s: {} for s in soubory}

    for den in os.listdir(archiv_dir):
        den_dir = os.path.join(archiv_dir, den)
        if not os.path.isdir(den_dir):
            continue
        for soubor in soubory:
            cesta = os.path.join(den_dir, soubor)
            if not os.path.exists(cesta):
                continue
            with open(cesta, 'r', encoding='utf-8') as f:
                for item in extrahuj_bloky(f.read()):
                    dt = parse_datum_na_datetime(item["datum_raw"])
                    
                    # Pokud by the datum chybělo/bylo rozbité, vygeneruje to dnešek. 
                    # Zkusíme zachránit datum ze jména složky, v jaké zpráva byla.
                    if not item["datum_raw"]:
                        try:
                            z_slozky = datetime.strptime(den, "%Y-%m-%d")
                            dt = z_slozky.replace(hour=12)
                        except ValueError:
                            pass # Pokračujeme s dněškem
                        
                        uredne_datum = dt.strftime("%d.%m.%Y %H:%M")
                        item["blok"] = re.sub(r"\|D\|.*?\|P\|", f"|D|{uredne_datum}|P|", item["blok"], count=1)
                        item["datum_raw"] = uredne_datum

                    spravny_den = dt.strftime("%Y-%m-%d")
                    item["dt"] = dt
                    item["spravny_den"] = spravny_den
                    key = (spravny_den, item["titulek"])
                    agregace[soubor][key] = item

    # Přepis celé struktury jen pro zprávy soubory
    for den in os.listdir(archiv_dir):
        den_dir = os.path.join(archiv_dir, den)
        if os.path.isdir(den_dir):
            for soubor in soubory:
                p = os.path.join(den_dir, soubor)
                if os.path.exists(p):
                    os.remove(p)

    for soubor in soubory:
        by_day = {}
        for (_, _), item in agregace[soubor].items():
            by_day.setdefault(item["spravny_den"], []).append(item)

        for den, items in by_day.items():
            den_dir = os.path.join(archiv_dir, den)
            os.makedirs(den_dir, exist_ok=True)
            p = os.path.join(den_dir, soubor)
            items_sorted = sorted(items, key=lambda x: x["dt"], reverse=True)
            with open(p, 'w', encoding='utf-8') as f:
                for x in items_sorted:
                    f.write(x["blok"] + "\n")

if __name__ == "__main__":
    print("Spoustim stahovani z webu a RSS...")

    # --- Vytvoření adresářové struktury ---
    zpravy_dir   = os.path.join(SD_CESTA, 'zpravy', 'aktualni')
    cache_dir    = os.path.join(SD_CESTA, 'cache')
    archiv_dir   = os.path.join(SD_CESTA, 'zpravy', 'archiv')
    for d in (zpravy_dir, cache_dir, archiv_dir):
        os.makedirs(d, exist_ok=True)

    # 1. Běžné zprávy a tech/ai
    svet_data = stahni_zpravy_multi([
        ("https://ct24.ceskatelevize.cz/rss/svet", 100),
        ("https://www.novinky.cz/rss/zahranicni", 100),
    ], celkovy_limit=200)

    # Cross-category deduplikace: zprávy ze světa se neobjeví v ČR sekci
    svet_titulky = {b["titulek"] for b in extrahuj_bloky(svet_data[1])}
    print(f"  Světové zprávy: {len(svet_titulky)} unikátních titulků (budou vynechány v ČR)")

    cr_data = stahni_zpravy_multi([
        ("https://ct24.ceskatelevize.cz/rss/domaci", 100),
        ("https://www.novinky.cz/rss/domaci", 100),
    ], celkovy_limit=200, exclude_titulky=svet_titulky)
    tech_data = stahni_zpravy_multi([
        ("https://www.lupa.cz/rss/clanky/", 30),
        ("https://www.cnews.cz/feed/", 30),
        ("https://www.root.cz/rss/clanky/", 30),
        ("https://www.zive.cz/rss/sc-47/default.aspx", 30),
        ("https://venturebeat.com/category/ai/feed/", 30),
        ("https://openai.com/news/rss.xml", 20),
        ("https://blog.google/technology/ai/rss/", 20),
    ], celkovy_limit=200)

    # 3. Pozitivní zprávy (zde povolíme delší historii, 14 dní, protože nepíšou denně)
    pozit_data = stahni_zpravy_multi([
        ("https://pozitivni-zpravy.cz/feed/", 50),
    ], celkovy_limit=50, max_age_days=14)

    # 2. Archivace a ukládání zpráv do eindata/zpravy/aktualni/ a archiv/YYYY-MM-DD dle data článku
    oprav_existujici_archiv(archiv_dir)

    # Uložení aktuálních zpráv (přepis)
    for soubor, (index_data, full_data) in [
        ('zpravy_svet.txt', svet_data),
        ('zpravy_cr.txt',   cr_data),
        ('zpravy_tech.txt', tech_data),
        ('zpravy_pozitivni.txt', pozit_data),
    ]:
        # Uložíme index (pro rychlé ESP32)
        idx_name = soubor.replace('.txt', '.idx')
        with open(os.path.join(zpravy_dir, idx_name), 'w', encoding='utf-8') as f:
            f.write(index_data)
        
        # Uložíme i původní full verzi (pro archivaci)
        with open(os.path.join(zpravy_dir, soubor), 'w', encoding='utf-8') as f:
            f.write(full_data)
            
        pridano = uloz_archiv_deduplikovane_po_dnech(archiv_dir, soubor, full_data)
        print(f"  Archiv {soubor}: +{pridano} novych (trideni podle data clanku, idx vytvoren)")

    # Promazat archivní složky starší 7 dní
    for slozka in os.listdir(archiv_dir):
        try:
            datum_slozky = datetime.strptime(slozka, '%Y-%m-%d')
            if (datetime.now() - datum_slozky).days > 7:
                shutil.rmtree(os.path.join(archiv_dir, slozka))
                print(f"  Smazán starý archiv: {slozka}")
        except ValueError:
            pass

    # 3. Ukládání cache dat (počasí, kurzy, horoskopy)
    kh_data = stahni_kurzy_historie()
    h_data = stahni_horoskopy()
    
    with open(os.path.join(cache_dir, 'pocasi.txt'),         'w', encoding='utf-8') as f: f.write(stahni_pocasi())
    with open(os.path.join(cache_dir, 'kurzy.txt'),          'w', encoding='utf-8') as f: f.write(stahni_kurzy())
    with open(os.path.join(cache_dir, 'horoskop.txt'),       'w', encoding='utf-8') as f: f.write(h_data)
    with open(os.path.join(cache_dir, 'kurzy_historie.txt'), 'w', encoding='utf-8') as f: f.write(kh_data)

    # 4. Turbo Mode 3.0 - Sync a Latest soubory
    with open(os.path.join(cache_dir, 'kurzy_sync.idx'),  'w', encoding='utf-8') as f: 
        f.write(stahni_sync_kurzy(kh_data, 15))
    with open(os.path.join(cache_dir, 'horoskop_latest.idx'), 'w', encoding='utf-8') as f: 
        f.write(datetime.now().strftime("%Y-%m-%d") + "\n" + h_data)

    print("Vsechno uspesne stazeno a ulozeno!")
    print(f"  Zpravy: {zpravy_dir}")
    print(f"  Archiv: {archiv_dir}")
    print(f"  Cache:  {cache_dir}")
