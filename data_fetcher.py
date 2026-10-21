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
import unicodedata
import calendar
from datetime import datetime, timedelta, timezone, date
from zoneinfo import ZoneInfo
from urllib.parse import urlparse, urljoin

TZ_PRAHA = ZoneInfo("Europe/Prague")

def praha_now():
    return datetime.now(TZ_PRAHA).replace(tzinfo=None)

# Normalizace titulku pro deduplikaci — bez diakritiky, interpunkce a velikosti písmen.
def norm_titulek(t):
    t = unicodedata.normalize('NFKD', t)
    t = ''.join(c for c in t if not unicodedata.combining(c))
    t = t.lower()
    t = re.sub(r'[^a-z0-9 ]', ' ', t)
    return re.sub(r'\s+', ' ', t).strip()

# Kořenová cesta k SD kartě (nebo lokální složce při spuštění v CI/locálně)
SD_CESTA = os.environ.get('SD_CESTA', './eindata')

# Sdileny HTTP klient + jednoduche retry
SESSION = requests.Session()

# Krátké štítky zdrojů zobrazované vedle data článku na zařízení.
MEDIA_ALIASES = {
    "ct24.ceskatelevize.cz": "ČT",
    "ceskatelevize.cz": "ČT",
    "irozhlas.cz": "iROZ",
    "novinky.cz": "Novinky",
    "lupa.cz": "Lupa",
    "cnews.cz": "Cnews",
    "root.cz": "Root",
    "zive.cz": "Živě",
    "ejaj.cz": "EJAJ",
    "svethardware.cz": "SvětHW",
    "digimanie.cz": "Digimanie",
    "blog.aditivni.cz": "Aditivní",
    "aditivni.cz": "Aditivní",
    "pozitivni-zpravy.cz": "PozZpr",
    "blesk.cz": "Blesk",
    "expres.cz": "Expres",
    "super.cz": "Super",
    "extra.cz": "Extra",
    "denikalarm.cz": "Alarm",
    "respekt.cz": "Resp",
    "denikn.cz": "N",
    "kudyznudy.cz": "Kudy",
    "moviezone.cz": "MZone",
    "serialzone.cz": "SZone",
    "games.tiscali.cz": "Games",
}

def kratky_nazev_media(rss_url):
    host = (urlparse(rss_url).hostname or "").lower()
    for domena, alias in MEDIA_ALIASES.items():
        if host == domena or host.endswith("." + domena):
            return alias
    return host.removeprefix("www.")[:10] or "RSS"

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

# --- ČIŠTĚNÍ TEXTU ČLÁNKU OD BALASTU ---
# Řádky, které samy o sobě nic neznamenají (navigace, galerie, reklamy...)
_JUNK_LINES = {
    "článek", "clanek", "téma", "tema", "témata", "temata",
    "související", "souvisejici", "přečtěte si také", "prectete si take",
    "reklama", "inzerce", "sdílet", "sdilet", "sdílet galerii",
    "komentáře", "komentare", "diskuze",
    "předchozí", "predchozi", "následující", "nasledujici", "další", "dalsi",
    "zavřít reklamu", "zavrit reklamu", "další galerie", "dalsi galerie",
    "všechny články", "vsechny clanky", "nejnovější články", "nejnovejsi clanky",
}
# Fráze, které se mohou vyskytnout kdekoliv v textu
_JUNK_PHRASES = [
    "video se připravuje",
    "článek je uzamčen",
    "za paywallem",
    "paywallem/předplatným",
    "z tohoto webu nelze obsah přímo vyčíst",
    "chyba při stahování",
    "pokračování na webu",
    "pokracovani na webu",
    "zavřít reklamu",
    "fotogalerie bude pokračovat",
]

def ocisti_text(t):
    if not t:
        return t
    vystup = []
    for radek in t.splitlines():
        s = radek.strip()
        if not s:
            vystup.append("")
            continue
        klic = s.lower().strip(" .,:;|·-–—/")
        if klic in _JUNK_LINES:
            continue
        if any(p in klic for p in _JUNK_PHRASES):
            continue
        if klic.startswith("(pozn"):            # naše vlastní poznámky
            continue
        if re.match(r"^(téma|témata|článek|rubrika|štítky|tagy|zdroj)\s*:", s, re.IGNORECASE):
            continue                            # meta řádky typu "Téma: ..."
        if re.fullmatch(r"\d+\s*s", klic):      # odpočty, např. "5 s"
            continue
        if re.fullmatch(r"[^A-Za-z0-9]+", s):   # řádky jen z interpunkce, např. "/"
            continue
        vystup.append(s)
    txt = "\n".join(vystup)
    return re.sub(r"\n{3,}", "\n\n", txt).strip()

# --- FUNKCE PRO VYTĚŽENÍ TEXTU PŘÍMO Z ČLÁNKU (Trafilatura) ---
def stahni_text_clanku(url, perex):
    try:
        odpoved = SESSION.get(
            url,
            headers={"User-Agent": "Mozilla/5.0 (compatible; HonzueINK/1.0)"},
            timeout=(4, 10),
        )
        if odpoved.status_code != 200 or not odpoved.content:
            return None
        stazeno = odpoved.text
        html_text = BeautifulSoup(stazeno, "html.parser").get_text(" ", strip=True).lower()
        obsah = trafilatura.extract(stazeno, include_comments=False, include_tables=False)
        
        if not obsah:
            return None

        host = (urlparse(url).hostname or "").lower()
        obsah = obsah.strip()

        # EJAJ přidává na stránky katalogových nástrojů newsletter a celý blok
        # doporučených produktů. Pokud extraktor zachytí jen tento společný
        # obsah, článek raději přeskočíme, než abychom jej uložili jako zprávu.
        if host == "ejaj.cz" or host.endswith(".ejaj.cz"):
            radky = obsah.splitlines()
            koncove_bloky = re.compile(
                r"^\s*(AI novinky do e-mailu|Podobné nástroje|Podobne nastroje)\s*$",
                re.IGNORECASE,
            )
            pozice = next((i for i, r in enumerate(radky) if koncove_bloky.match(r)), None)
            if pozice is not None:
                obsah = "\n".join(radky[:pozice]).strip()
            # Typická stránka nástroje bez skutečného článku: kategorie,
            # newsletter a SEO katalog podobných nástrojů.
            if re.search(r"(?im)^\s*Kategorie\s*$", obsah) and len(obsah) < 500:
                return None

        # MovieZone může při nedostupném článku vrátit přihlašovací/registr.
        # obrazovku. Jednotlivá slova mohou být legitimní, proto požadujeme
        # několik charakteristických navigačních frází současně.
        obsah_maly = obsah.lower()
        if host == "moviezone.cz" or host.endswith(".moviezone.cz"):
            prihlasovaci_fraze = (
                "komentovat a hodnotit filmy",
                "sestavovat si žebříčky oblíbených filmů",
                "zapomenuté heslo",
                "přihlášení",
                "registrace",
                "soutěžit o filmové",
            )
            if sum(frase in obsah_maly for frase in prihlasovaci_fraze) >= 2:
                return None

        paywall_klicova_slova = (
            "odemkněte článek", "odemknout článek", "přístup k prémiovým článkům",
            "číst ho můžeš jen s refresher+", "co se dozvíš po odemčení",
        "dočíst článek", "článek je dostupný pouze předplatitelům",
        "článek je zamčen", "pouze pro předplatitele", "článek pro předplatitele",
            "přečtěte si celý článek po přihlášení", "předplaťte si deník n",
            "předplaťte si respekt", "přístup k tomuto článku mají předplatitelé",
            "tento článek je určen předplatitelům", "přístup k exkluzivnímu obsahu",
            "vstup do clubu a získáš", "předplatím si za", "čtení bez omezení",
            "idnes premium", "kč / týden", "účtuje se jednou",
            "kupte si předplatné", "zakoupením získáte",
        )
        if any(slovo in obsah_maly for slovo in paywall_klicova_slova):
            return None
        html_paywall_klicova_slova = (
            "číst ho můžeš jen s refresher+", "co se dozvíš po odemčení",
            "článek je zamčen", "pouze pro předplatitele", "článek pro předplatitele",
            "přečtěte si celý článek po přihlášení",
        )
        if any(slovo in html_text for slovo in html_paywall_klicova_slova):
            return None

        # Neukládat perex jako náhradu celého textu ani potichu oříznutý článek.
        obsah = ocisti_text(obsah)
        if len(obsah) < 100 or len(obsah) > 14000:
            return None
        perex_norm = re.sub(r"\s+", " ", ocisti_text(perex or "")).strip().lower()
        obsah_norm = re.sub(r"\s+", " ", obsah).strip().lower()
        if len(perex_norm) >= 120 and obsah_norm.startswith(perex_norm[:120]) and len(obsah_norm) <= len(perex_norm) + 100:
            return None
        return obsah
    except Exception:
        return None

# --- FUNKCE PRO ZPRÁVY (RSS/Atom) ---
def _parse_rss_datum(item, event_mode=False):
    """Vrátí datum RSS/Atom položky v pražském čase, nebo None."""
    from email.utils import parsedate_to_datetime

    nazvy_udalosti = {"startdate", "eventdate", "begindate", "start_date", "event_date"}
    nazvy_bezne = {"pubdate", "published", "updated", "date"}
    datumove_elementy = [
        element for element in item.find_all()
        if element.name and element.name.lower().split(":")[-1] in nazvy_udalosti | nazvy_bezne
    ]
    if event_mode:
        datumove_elementy.sort(
            key=lambda element: element.name.lower().split(":")[-1] not in nazvy_udalosti
        )
    for element in datumove_elementy:
        raw = element.get_text(" ", strip=True)
        if not raw:
            continue
        try:
            dt = parsedate_to_datetime(raw)
        except (TypeError, ValueError, OverflowError):
            try:
                dt = datetime.fromisoformat(raw.replace("Z", "+00:00"))
            except ValueError:
                continue
        if dt.tzinfo is None:
            dt = dt.replace(tzinfo=TZ_PRAHA)
        return dt.astimezone(TZ_PRAHA)
    return None


def stahni_zpravy(rss_url, limit=15, vyzadovana_slova=(), exclude_titulky=None, event_mode=False):
    hlavicky = {"User-Agent": "Mozilla/5.0"}
    try:
        odpoved = get_with_retry(rss_url, headers=hlavicky, timeout=10, retries=3)
        odpoved.raise_for_status()
        soup = BeautifulSoup(odpoved.content, 'xml')
        items = soup.find_all(["item", "entry"])
        
        vysledny_text = ""
        if not items: 
            return "|T|Žádné zprávy|D||P|Nebyly nalezeny.|X|Zkuste to později.|E|"
            
        media = kratky_nazev_media(rss_url)
        pridano = 0
        dnes = praha_now().date()
        vyloucene = {norm_titulek(t) for t in (exclude_titulky or ())}
        for item in items:
            if pridano >= limit:
                break
            title_el = item.find('title')
            titulek = title_el.get_text(" ", strip=True) if title_el else "Bez titulku"
            link_elements = item.find_all('link')
            odkaz = ""
            if link_elements:
                def je_alternativni_odkaz(element):
                    rel = element.get("rel") or []
                    if isinstance(rel, str):
                        rel = rel.split()
                    return not rel or "alternate" in rel
                link_el = next((x for x in link_elements if je_alternativni_odkaz(x)), link_elements[0])
                odkaz = link_el.get("href", "").strip() or link_el.get_text(" ", strip=True)
            odkaz = urljoin(rss_url, odkaz) if odkaz else ""
            datum = _parse_rss_datum(item, event_mode=event_mode)

            # Nejprve zahodíme staré a duplicitní položky, teprve pak stahujeme HTML článku.
            if not event_mode and (not datum or datum.date() != dnes):
                continue
            if norm_titulek(titulek) in vyloucene:
                continue
            if vyzadovana_slova:
                titulek_norm = norm_titulek(titulek)
                if not any(norm_titulek(slovo) in titulek_norm for slovo in vyzadovana_slova):
                    continue

            # Některá kalendářová RSS nemají datum publikace. Kudy se proto
            # neomezuje na den zprávy; při chybějícím datu použijeme den stažení.
            if event_mode and datum is None:
                datum = praha_now()
            if datum is None:
                continue
            datum_cas = datum.strftime("%d.%m.%Y %H:%M")

            # Perex (description) — očištěný a zploštělý na jeden řádek
            perex_el = item.find(['description', 'summary', 'content'])
            perex_raw = perex_el.get_text().strip() if perex_el else ""
            perex = ocisti_text(BeautifulSoup(perex_raw, "html.parser").get_text())
            perex = re.sub(r"\s+", " ", perex).strip()
            perex_kratky = (perex[:150] + "...") if len(perex) > 150 else perex
            
            # Uložíme jen skutečně dostupný celý text; teaser/placený začátek přeskočíme.
            text_clanku = stahni_text_clanku(odkaz, perex) if odkaz else None
            if not text_clanku:
                continue
            vysledny_text += f"|T|{titulek}|D|{datum_cas}|P|{perex_kratky}|M|{media}|X|{text_clanku}|E|"
            pridano += 1
            vyloucene.add(norm_titulek(titulek))
            
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
    """Stáhne aktuální ceny benzínu Euro 95 a nafty v ČR (Kč/l).

    Zdroj: fuel-prices.eu — oficiální týdenní průměr Evropské komise
    (Weekly Oil Bulletin). Markdown export je malý a obsahuje cenu
    rovnou v Kč/l, takže není potřeba nic přepočítávat.

    Pokud se cenu nepodaří získat, vrací prázdné řetězce — ESP32 pak
    řádek s palivy vůbec nezobrazí (místo aby ukazoval vymyšlenou cenu).
    """
    import re
    nafta, benzin = "", ""

    try:
        h = {"User-Agent": "Mozilla/5.0"}
        r = requests.get("https://www.fuel-prices.eu/Czechia/?format=md", headers=h, timeout=12)
        txt = r.text
        m = re.search(r'Euro\s*95[^\n]*?local:\s*([\d.,]+)\s*CZK', txt)
        if m: benzin = f"{float(m.group(1).replace(',', '.')):.2f}"
        m = re.search(r'Diesel[^\n]*?local:\s*([\d.,]+)\s*CZK', txt)
        if m: nafta = f"{float(m.group(1).replace(',', '.')):.2f}"
    except Exception as e:
        print(f"  Paliva: stažení selhalo ({e})")

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
        eur_rate = 24.0
        for line in res_cnb.text.split('\n'):
            if "|USD|" in line: usd_rate = float(line.split('|')[-1].replace(',', '.'))
            if "|EUR|" in line: eur_rate = float(line.split('|')[-1].replace(',', '.'))
    except Exception:
        usd_rate = 23.5
        eur_rate = 25.0

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

    # NAFTA a BENZEN — historie z fuel-prices.eu (oficiální data Evropské komise, týdenní)
    try:
        h = {"User-Agent": "Mozilla/5.0"}
        r = requests.get("https://www.fuel-prices.eu/Czechia/", headers=h, timeout=10)
        soup = BeautifulSoup(r.content, "html.parser")
        tables = soup.find_all("table")
        nafta_hist, benzin_hist = [], []
        candidate_table = None
        for tbl in tables:
            rows = tbl.find_all("tr")
            for tr in rows:
                tds = tr.find_all("td")
                texts = [td.get_text().strip() for td in tds]
                if len(tds) >= 3 and any("€" in t for t in texts):
                    candidate_table = tbl
                    break
            if candidate_table:
                break
        if candidate_table:
            rows = candidate_table.find_all("tr")
            for tr in rows[1:]:
                tds = tr.find_all("td")
                if len(tds) >= 3:
                    try:
                        diesel_text = tds[2].get_text().strip().replace("€", "").replace(",", ".").replace("\xa0", "")
                        benzin_text = tds[1].get_text().strip().replace("€", "").replace(",", ".").replace("\xa0", "")
                        diesel_eur = float(diesel_text)
                        benzin_eur = float(benzin_text)
                        diesel_czk = round(diesel_eur * eur_rate, 2)
                        benzin_czk = round(benzin_eur * eur_rate, 2)
                        nafta_hist.append(diesel_czk)
                        benzin_hist.append(benzin_czk)
                    except (ValueError, IndexError):
                        continue
        nafta_hist = list(reversed(nafta_hist))
        benzin_hist = list(reversed(benzin_hist))
        if len(nafta_hist) >= 2:
            nafta_daily = []
            benzin_daily = []
            n = len(nafta_hist)
            for i in range(30):
                t = i / 29.0 if 29 > 0 else 0
                idx_f = t * (n - 1)
                idx_lo = int(idx_f)
                idx_hi = min(idx_lo + 1, n - 1)
                frac = idx_f - idx_lo
                nafta_daily.append(f"{nafta_hist[idx_lo] + (nafta_hist[idx_hi] - nafta_hist[idx_lo]) * frac:.2f}")
                benzin_daily.append(f"{benzin_hist[idx_lo] + (benzin_hist[idx_hi] - benzin_hist[idx_lo]) * frac:.2f}")
            result += "NAFTA|" + ",".join(nafta_daily) + "\n"
            result += "BENZEN|" + ",".join(benzin_daily) + "\n"
        else:
            result += "NAFTA|\nBENZEN|\n"
    except Exception:
        result += "NAFTA|\nBENZEN|\n"

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

def stahni_zpravy_multi(zdroje, celkovy_limit=10, exclude_titulky=None, event_mode=False):
    """Stáhne zprávy z více RSS zdrojů, deduplikuje podle titulku a zaloguje průběh.
    exclude_titulky: set titulků které se mají přeskočit (cross-category deduplikace).
    Běžné aktuality obsahují jen dnešní články; event_mode povolí položky
    kalendářových RSS bez omezení dne publikace.
    """
    vybrane = []  # (dt, titulek, blok)
    # Inicializujeme titulky rovnou s vyloučenými - porovnáváme normalizovaně
    titulky = set(norm_titulek(t) for t in exclude_titulky) if exclude_titulky else set()
    fronty_zdroju = []
    zakladni_kvota = max(1, (celkovy_limit + max(1, len(zdroje)) - 1) // max(1, len(zdroje)))
    for zdroj in zdroje:
        rss_url, limit_per_source = zdroj[:2]
        vyzadovana_slova = tuple(norm_titulek(x) for x in zdroj[2]) if len(zdroj) > 2 else ()
        # Každý zdroj dostane šanci naplnit zhruba dvojnásobek férového podílu.
        # Přebytek rozdělíme v dalších kolech, pokud některé zdroje nabídnou málo.
        limit = min(limit_per_source, celkovy_limit, zakladni_kvota * 2)
        data = stahni_zpravy(
            rss_url, limit=limit, vyzadovana_slova=vyzadovana_slova,
            exclude_titulky=titulky, event_mode=event_mode,
        )
        bloky = extrahuj_bloky(data)
        pridano = 0
        duplikaty = 0
        fronta = []
        for b in bloky:
            titulek = b["titulek"]
            # Placeholder z neúspěšného zdroje nepatří mezi skutečné články
            if titulek in ("Žádné zprávy", "Chyba"):
                continue
            if vyzadovana_slova:
                titulek_norm = norm_titulek(titulek)
                if not any(slovo in titulek_norm for slovo in vyzadovana_slova):
                    continue
            t_klic = norm_titulek(titulek)
            if t_klic in titulky:
                duplikaty += 1
                continue
            datum_raw = b["datum_raw"].strip()
            if not datum_raw or (not event_mode and not re.search(r"(?:\d{4}-\d{2}-\d{2}|\d{1,2}\.\d{1,2}\.)", datum_raw)):
                continue
            dt = parse_datum_na_datetime(b["datum_raw"])

            # Aktuality jsou denní výběr, ne pohyblivé časové okno.
            # Při neznámém datu parsování vrací pražský dnešek; jinak musí
            # datum článku přesně odpovídat dnešnímu dni v Europe/Prague.
            if not event_mode and dt.date() != praha_now().date():
                continue
            
            fronta.append((dt, titulek, b["blok"]))
            titulky.add(t_klic)
            pridano += 1
        fronta.sort(key=lambda x: x[0], reverse=True)
        fronty_zdroju.append(fronta)
        print(f"  RSS {rss_url}: raw={len(bloky)} pridano={pridano} dup={duplikaty}")

    # Nejdřív přidělíme každému zdroji stejnou základní kvótu, pak volná místa
    # rozdělíme po jednom článku mezi zdroje, které mají další obsah.
    for fronta in fronty_zdroju:
        vybrane.extend(fronta[:zakladni_kvota])
        del fronta[:zakladni_kvota]
    while len(vybrane) < celkovy_limit:
        pridano_kolo = False
        for fronta in fronty_zdroju:
            if fronta and len(vybrane) < celkovy_limit:
                vybrane.append(fronta.pop(0))
                pridano_kolo = True
        if not pridano_kolo:
            break

    vybrane.sort(key=lambda x: x[0], reverse=True)

    # Když se z žádného zdroje nic nestáhlo, vrátíme aspoň informativní placeholder
    if not vybrane:
        prazdny = "|T|Žádné zprávy|D||P|Nebyly nalezeny.|X|Zkuste to později.|E|"
        return prazdny, prazdny
    
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
        return praha_now().replace(second=0, microsecond=0)

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
        y = int(m.group(3)) if m.group(3) else praha_now().year
        hh, mm = int(m.group(4) or 0), int(m.group(5) or 0)
        try:
            return datetime(y, mo, d, hh, mm)
        except ValueError:
            pass

    return praha_now().replace(second=0, microsecond=0)

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

    soubory = ('zpravy_svet.txt', 'zpravy_cr.txt', 'zpravy_tech.txt', 'zpravy_pozitivni.txt', 'zpravy_kultura.txt', 'zpravy_akce.txt')
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
                        
                        uredne_datum = dt.strftime("%d.%-m. %H:%M")
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

# ===================================================================
# OFFLINE JÍZDNÍ ŘÁDY PID (GTFS) — jen vybrané linky a zastávky
# ===================================================================
JR_LINKY = [
    ("7",   "Otakarova", "1", "Kotlářka"),      # z Otakarovy na Kotlářku
    ("7",   "Kavalírka", "0", "Bílá labuť"),    # z Kavalírky do města
    ("123", "Průchova",  "1", "Na Knížecí"),    # z Průchovy do města
    ("123", "Kavalírka", "0", "Šmukýřka"),      # z Kavalírky na Šmukýřku
]
JR_GTFS_URL = "https://data.pid.cz/PID_GTFS.zip"
JR_OBNOVA_DNI = 7     # GTFS PID má ~50 MB — generujeme nejvýš jednou za 7 dní


def _jr_slug(s):
    tab = str.maketrans("áčďéěíňóřšťúůýž", "acdeeinorstuuyz")
    return s.lower().translate(tab).replace(" ", "_")


def _velikonoce(rok):
    # Anonymní gregoriánský algoritmus (Good Friday / Easter Monday)
    a = rok % 19; b = rok // 100; c = rok % 100
    d = b // 4; e = b % 4; f = (b + 8) // 25; g = (b - f + 1) // 3
    h = (19 * a + b - d - g + 15) % 30
    i = c // 4; k = c % 4
    l = (32 + 2 * e + 2 * i - h - k) % 7
    m = (a + 11 * h + 22 * l) // 451
    mesic = (h + l - 7 * m + 114) // 31
    den = ((h + l - 7 * m + 114) % 31) + 1
    return datetime(rok, mesic, den)


def _svatky_rok(rok):
    e = _velikonoce(rok)
    return [
        datetime(rok, 1, 1), e - timedelta(days=2), e + timedelta(days=1),
        datetime(rok, 5, 1), datetime(rok, 5, 8),
        datetime(rok, 7, 5), datetime(rok, 7, 6),
        datetime(rok, 9, 28), datetime(rok, 10, 28), datetime(rok, 11, 17),
        datetime(rok, 12, 24), datetime(rok, 12, 25), datetime(rok, 12, 26),
    ]


def stahni_jizdni_rady():
    """Stáhne GTFS PID a uloží malé offline jízdní řády pro vybrané zastávky."""
    import zipfile
    import io
    import csv

    jr_dir = os.path.join(SD_CESTA, 'jr')
    os.makedirs(jr_dir, exist_ok=True)

    # Jízdní řády se mění jen zřídka — stahování 50MB GTFS přeskočíme,
    # pokud byla data vygenerovaná v posledních JR_OBNOVA_DNI dnech.
    znacka = os.path.join(jr_dir, 'aktualizace.txt')
    if os.path.exists(znacka) and os.path.exists(os.path.join(jr_dir, 'index.txt')):
        try:
            with open(znacka, encoding='utf-8') as f:
                posledni = datetime.strptime(f.read().strip(), '%Y-%m-%d').date()
            if (datetime.now().date() - posledni).days < JR_OBNOVA_DNI:
                print("  Jizdni rady jsou cerstve, GTFS se nestahuje.")
                return
        except ValueError:
            pass

    print("  Stahuji GTFS PID (muze to chvilku trvat)...")
    try:
        r = requests.get(JR_GTFS_URL, timeout=180)
        r.raise_for_status()
        z = zipfile.ZipFile(io.BytesIO(r.content))
    except Exception as ex:
        print(f"  GTFS se nepovedlo stahnout: {ex}")
        return

    def rows(name):
        with z.open(name) as f:
            for r in csv.DictReader(io.TextIOWrapper(f, encoding='utf-8-sig')):
                yield r

    # kalendář → které dny služba jezdí (WD / SA / SU)
    cal = {r['service_id']: r for r in rows('calendar.txt')}
    cd = {(r['service_id'], r['date']): r['exception_type'] for r in rows('calendar_dates.txt')}

    def aktivni_servisy(datum):
        ds = datum.strftime('%Y%m%d')
        den = ('monday', 'tuesday', 'wednesday', 'thursday', 'friday', 'saturday', 'sunday')[datum.weekday()]
        s = set()
        for sid, r in cal.items():
            if r['start_date'] <= ds <= r['end_date'] and r[den] == '1':
                s.add(sid)
        for (sid, d), typ in cd.items():
            if d == ds:
                if typ == '1':
                    s.add(sid)
                else:
                    s.discard(sid)
        return s

    # Reprezentativní den pro každý typ (bere ohled na svátky) — plán se pak
    # negeneruje z více období najednou, takže v něm nejsou duplicitní časy.
    svatky = set()
    for rok in (datetime.now().year, datetime.now().year + 1):
        svatky |= {d.strftime('%Y-%m-%d') for d in _svatky_rok(rok)}
    rep = {}
    dnes = datetime.now().date()
    for i in range(15):
        d = dnes + timedelta(days=i)
        ds = d.strftime('%Y-%m-%d')
        if d.weekday() < 5 and 'WD' not in rep and ds not in svatky:
            rep['WD'] = d
        if d.weekday() == 5 and 'SA' not in rep:
            rep['SA'] = d
        if d.weekday() == 6 and 'SU' not in rep:
            rep['SU'] = d
    aktiv = {ty: aktivni_servisy(d) for ty, d in rep.items()}

    chtene_zastavky = {s for _, s, _, _ in JR_LINKY}
    stop_ids = {}                     # stop_id -> název zastávky
    zastavky_ids = {}                 # název -> [stop_id, ...]
    for r in rows('stops.txt'):
        nm = r.get('stop_name', '')
        if nm in chtene_zastavky:
            stop_ids[r['stop_id']] = nm
            zastavky_ids.setdefault(nm, []).append(r['stop_id'])

    line_ids = {}                     # route_id -> číslo linky
    for r in rows('routes.txt'):
        sn = r.get('route_short_name', '')
        if any(sn == l for l, _, _, _ in JR_LINKY):
            line_ids[r['route_id']] = sn

    trip_info = {}                    # trip_id -> (linka, service_id, direction_id, headsign)
    for r in rows('trips.txt'):
        if r['route_id'] in line_ids:
            trip_info[r['trip_id']] = (line_ids[r['route_id']], r['service_id'],
                                       r.get('direction_id', '0'), r.get('trip_headsign', ''))

    # stop_times je velký → čteme po řádcích a bereme jen naše linky/zastávky
    maxseq = {}
    hits = []
    for r in rows('stop_times.txt'):
        tid = r['trip_id']
        if tid not in trip_info:
            continue
        sq = int(r['stop_sequence'])
        if sq > maxseq.get(tid, 0):
            maxseq[tid] = sq
        nm = stop_ids.get(r['stop_id'])
        if nm:
            hits.append((tid, nm, r['stop_id'], sq, r.get('departure_time') or r.get('arrival_time')))

    # Jedna zastávka je v GTFS často rozdělená na víc nástupišť (platform)
    # a spoj pak má v datech víc odjezdových časů lišících se o minutu.
    # Pro každý spoj proto bereme jen jedno nástupiště — to, na kterém staví
    # nejvíc spojů daného směru. Tím zmizí zdvojené časy.
    per_trip = {}                     # (linka, zastávka, směr, trip_id) -> [(čas, stop_id)]
    heads = {}                        # (linka, zastávka, směr) -> {headsign: počet}
    platformy = {}                    # (linka, zastávka, směr) -> {stop_id: počet}
    for tid, nm, stop_id, sq, dep in hits:
        if sq >= maxseq.get(tid, 0):
            continue                  # poslední zastávka = jen výstup, ne odjezd
        line, sid, di, headsign = trip_info[tid]
        try:
            h, m, s = map(int, dep.split(':'))
        except ValueError:
            continue
        if h >= 24:
            continue                  # po půlnoci vynecháme
        key = (line, nm, di)
        per_trip.setdefault((key, tid), []).append((dep, stop_id))
        heads.setdefault(key, {})
        heads[key][headsign] = heads[key].get(headsign, 0) + 1
        platformy.setdefault(key, {})
        platformy[key][stop_id] = platformy[key].get(stop_id, 0) + 1

    groups = {}                       # (linka, zastávka, směr) -> časy
    for (key, tid), lst in per_trip.items():
        line, nm, di = key
        pref = max(platformy[key].items(), key=lambda kv: (kv[1], kv[0]))[0]
        dep = None
        for cas, pid in lst:
            if pid == pref:
                dep = cas
                break
        if dep is None:
            dep = lst[0][0]
        try:
            h, m, s = map(int, dep.split(':'))
        except ValueError:
            continue
        sid = trip_info[tid][1]
        g = groups.setdefault(key, {'times': {'WD': set(), 'SA': set(), 'SU': set()}})
        for ty, sset in aktiv.items():
            if sid in sset:
                g['times'][ty].add(f"{h:02d}:{m:02d}")

    pozadovany = {(l, s): d for l, s, d, _ in JR_LINKY}
    popisky = {(l, s): lab for l, s, _, lab in JR_LINKY}

    per_file = {}
    for (line, nm, di), g in groups.items():
        if pozadovany.get((line, nm)) != di:
            continue                   # bereme jen požadovaný směr
        per_file.setdefault((line, nm), []).append((di, g))

    nove_soubory = {'index.txt', 'svatky.txt', 'aktualizace.txt',
                    'golemio_key.txt', 'posledni_kontrola.txt'}
    index_lines = []
    poradi = {(l, s): i for i, (l, s, _, _) in enumerate(JR_LINKY)}
    for (line, nm), lst in sorted(per_file.items(), key=lambda kv: poradi.get(kv[0], 99)):
        fname = f"{line}_{_jr_slug(nm)}.txt"
        nove_soubory.add(fname)
        ids_all = sorted({max(platformy[(line, nm, di)].items(), key=lambda kv: (kv[1], kv[0]))[0]
                          for di, _ in lst})
        out = [f"# {line}|{nm}", f"# ids={','.join(ids_all)}"]
        for di, g in sorted(lst):
            hlavni = popisky.get((line, nm), max(heads[(line, nm, di)].items(), key=lambda kv: kv[1])[0])
            for ty in ('WD', 'SA', 'SU'):
                casy = sorted(g['times'][ty])
                out.append(f"{di}|{hlavni}|{ty}|" + " ".join(casy))
        with open(os.path.join(jr_dir, fname), 'w', encoding='utf-8') as f:
            f.write("\n".join(out) + "\n")
        index_lines.append(f"{fname}|{line} {nm}")
        print(f"  JR: {line} {nm} -> {fname}")

    with open(os.path.join(jr_dir, 'index.txt'), 'w', encoding='utf-8') as f:
        f.write("\n".join(index_lines) + "\n")

    rok = datetime.now().year
    with open(os.path.join(jr_dir, 'svatky.txt'), 'w', encoding='utf-8') as f:
        for r in (rok, rok + 1):
            for d in _svatky_rok(r):
                f.write(d.strftime('%Y-%m-%d') + "\n")

    # smazat staré soubory, které už nejsou v seznamu
    for fn in os.listdir(jr_dir):
        if fn.endswith('.txt') and fn not in nove_soubory:
            os.remove(os.path.join(jr_dir, fn))

    # datum generování — podle něj se příště pozná, že se GTFS nemá stahovat
    with open(znacka, 'w', encoding='utf-8') as f:
        f.write(datetime.now().strftime('%Y-%m-%d') + "\n")


# ===================================================================
# GOOGLE KALENDÁŘ — tajná iCal adresa → malý soubor pro zařízení
# ===================================================================
# Výstup (eindata/kalendar/kalendar.txt), jeden řádek = jedna událost v daném dni:
#   RRRR-MM-DD|HH:MM|HH:MM|název|místo|popis
# Prázdné časy = celodenní událost (nebo pokračování vícedenní akce).
KALENDAR_HORIZONT_DNI = 92   # ~3 měsíce dopředu

try:
    from zoneinfo import ZoneInfo
    _TZ_PRAHA = ZoneInfo('Europe/Prague')
except Exception:
    _TZ_PRAHA = None

_DNY_ICS = {'MO': 0, 'TU': 1, 'WE': 2, 'TH': 3, 'FR': 4, 'SA': 5, 'SU': 6}


def _ics_text(s):
    return (s or '').replace('\\n', ' ').replace('\\N', ' ') \
        .replace('\\,', ',').replace('\\;', ';').replace('\\\\', '\\')


def _ocisti_hodnotu(s, maxlen):
    s = re.sub(r'\s+', ' ', s or '').strip().replace('|', '/')
    return s[:maxlen]


def _den(x):
    return x.date() if isinstance(x, datetime) else x


def _klic_casu(x):
    if x is None:
        return ''
    if isinstance(x, datetime):
        if x.tzinfo is not None:
            x = x.astimezone(timezone.utc)
        return x.strftime('%Y%m%dT%H%M%S')
    return x.strftime('%Y%m%d')


def _ics_parsuj_dt(hodnota, parametry=''):
    """Vrátí (datetime/date, celodenni). Časy převádí do Europe/Prague."""
    h = (hodnota or '').strip()
    try:
        if 'T' in h:
            dt = datetime.strptime(h[:15], '%Y%m%dT%H%M%S')
            if h.endswith('Z'):
                dt = dt.replace(tzinfo=timezone.utc)
                if _TZ_PRAHA:
                    dt = dt.astimezone(_TZ_PRAHA)
                return dt, False
            m = re.search(r'TZID=([^;:]+)', parametry or '', re.IGNORECASE)
            if m and _TZ_PRAHA:
                try:
                    return dt.replace(tzinfo=ZoneInfo(m.group(1))).astimezone(_TZ_PRAHA), False
                except Exception:
                    pass
            return (dt.replace(tzinfo=_TZ_PRAHA) if _TZ_PRAHA else dt), False
        return datetime.strptime(h[:8], '%Y%m%d').date(), True
    except Exception:
        return None, False


def _ics_rrule(text):
    r = {}
    for cast in (text or '').split(';'):
        if '=' in cast:
            k, v = cast.split('=', 1)
            r[k.strip().upper()] = v.strip()
    return r


def _ics_vyskty(start, rrule, od, do):
    """Výskyty události (včetně opakování) v rozmezí od–do."""
    if start is None:
        return []
    je_dt = isinstance(start, datetime)
    cas = start.timetz() if je_dt else None
    zaklad = start.date() if je_dt else start
    if not rrule:
        return [start] if od <= zaklad <= do else []

    freq = rrule.get('FREQ', '').upper()
    interval = max(1, int(rrule.get('INTERVAL', '1') or '1'))
    count = int(rrule['COUNT']) if str(rrule.get('COUNT', '')).isdigit() else None
    until = None
    if rrule.get('UNTIL'):
        until, _ = _ics_parsuj_dt(rrule['UNTIL'], '')
    until_den = _den(until) if until is not None else None

    vysledek = []

    def pridej(d):
        if zaklad <= d <= do:
            vysledek.append(datetime.combine(d, cas) if je_dt else d)

    if freq == 'WEEKLY':
        byday = rrule.get('BYDAY', '')
        wds = sorted({_DNY_ICS[p[-2:]] for p in byday.split(',') if p[-2:] in _DNY_ICS})
        if not wds:
            wds = [zaklad.weekday()]
        tyden0 = zaklad - timedelta(days=zaklad.weekday())
        n = 0
        for t in range(600):
            tyden = tyden0 + timedelta(weeks=t * interval)
            for wd in wds:
                d = tyden + timedelta(days=wd)
                if d < zaklad:
                    continue
                if until_den is not None and d > until_den:
                    return vysledek
                if count is not None and n >= count:
                    return vysledek
                if d > do:
                    return vysledek
                pridej(d)
                n += 1
        return vysledek

    if freq == 'MONTHLY':
        bymd = rrule.get('BYMONTHDAY', '')
        dny = [int(x) for x in bymd.split(',') if x.isdigit()] or [zaklad.day]
        # "2TU" = druhé úterý v měsíci, "-1FR" = poslední pátek
        byday = rrule.get('BYDAY', '')
        ord_wd, ord_n = None, 1
        mday = re.match(r'^([+-]?\d+)?(MO|TU|WE|TH|FR|SA|SU)$', byday.split(',')[0]) if byday else None
        if mday:
            ord_wd = _DNY_ICS[mday.group(2)]
            ord_n = int(mday.group(1)) if mday.group(1) else 1
        n = 0
        rok, mes = zaklad.year, zaklad.month
        for _ in range(400):
            kandidati = []
            if ord_wd is not None:
                if ord_n > 0:
                    first = date(rok, mes, 1)
                    delta = (ord_wd - first.weekday()) % 7
                    kandidati = [first + timedelta(days=delta + 7 * (ord_n - 1))]
                else:
                    last = date(rok, mes, calendar.monthrange(rok, mes)[1])
                    delta = (last.weekday() - ord_wd) % 7
                    kandidati = [last - timedelta(days=delta + 7 * (-ord_n - 1))]
                kandidati = [d for d in kandidati if d.month == mes]
            else:
                for dd in dny:
                    try:
                        kandidati.append(date(rok, mes, dd))
                    except ValueError:
                        pass
            for d in sorted(kandidati):
                if d < zaklad:
                    continue
                if until_den is not None and d > until_den:
                    return vysledek
                if count is not None and n >= count:
                    return vysledek
                if d > do:
                    return vysledek
                pridej(d)
                n += 1
            mes += interval
            while mes > 12:
                mes -= 12
                rok += 1
        return vysledek

    if freq == 'YEARLY':
        bym = rrule.get('BYMONTH', '')
        bymd = rrule.get('BYMONTHDAY', '')
        mes = int(bym.split(',')[0]) if bym.split(',')[0].isdigit() else zaklad.month
        den = int(bymd.split(',')[0]) if bymd.split(',')[0].isdigit() else zaklad.day
        n = 0
        for t in range(200):
            try:
                d = date(zaklad.year + t * interval, mes, den)
            except ValueError:
                continue
            if d < zaklad:
                continue
            if until_den is not None and d > until_den:
                break
            if count is not None and n >= count:
                break
            if d > do:
                break
            pridej(d)
            n += 1
        return vysledek

    # DAILY (a výchozí případ)
    d = zaklad
    n = 0
    for _ in range(5000):
        if until_den is not None and d > until_den:
            break
        if count is not None and n >= count:
            break
        if d > do:
            break
        pridej(d)
        n += 1
        d += timedelta(days=interval)
    return vysledek


def stahni_kalendar():
    """Stáhne tajnou iCal adresu Google kalendáře a vytvoří data pro zařízení."""
    url = os.environ.get('KALENDAR_ICS_URL', '').strip()
    if len(url) < 10:
        print("  Kalendar: KALENDAR_ICS_URL neni nastavena, preskakuji.")
        return

    print("  Stahuji Google kalendar (iCal)...")
    try:
        r = get_with_retry(url, timeout=30)
        r.raise_for_status()
        text = r.text
    except Exception as ex:
        print(f"  Kalendar se nepovedlo stahnout: {ex}")
        return

    # Složení pokračovacích řádků (ICS je láme mezerou na začátku)
    text = re.sub(r'\r?\n[ \t]', '', text)

    udalosti = []
    cur = None
    for radek in text.splitlines():
        radek = radek.rstrip('\r')
        if radek == 'BEGIN:VEVENT':
            cur = {}
        elif radek == 'END:VEVENT':
            if cur is not None:
                udalosti.append(cur)
            cur = None
        elif cur is not None and ':' in radek:
            levy, hodnota = radek.split(':', 1)
            casti = levy.split(';')
            cur.setdefault(casti[0].upper(), []).append((';'.join(casti[1:]), hodnota))

    if not udalosti:
        print("  Kalendar: zadne udalosti v ICS.")
        return

    dnes = datetime.now(_TZ_PRAHA).date() if _TZ_PRAHA else datetime.now().date()
    od = dnes.replace(day=1)
    do = dnes + timedelta(days=KALENDAR_HORIZONT_DNI)

    # Přepisy opakovaných událostí (Google je posílá jako samostatné VEVENTy)
    prekryty = set()
    for ev in udalosti:
        if 'RECURRENCE-ID' in ev and 'UID' in ev:
            rid, _ = _ics_parsuj_dt(ev['RECURRENCE-ID'][0][1], ev['RECURRENCE-ID'][0][0])
            if rid is not None:
                prekryty.add((ev['UID'][0][1].strip(), _klic_casu(rid)))

    radky = []
    for ev in udalosti:
        if 'DTSTART' not in ev:
            continue
        if 'STATUS' in ev and ev['STATUS'][0][1].strip().upper() == 'CANCELLED':
            continue
        start, celodenni = _ics_parsuj_dt(ev['DTSTART'][0][1], ev['DTSTART'][0][0])
        if start is None:
            continue
        uid = ev['UID'][0][1].strip() if 'UID' in ev else ''

        if 'DTEND' in ev:
            konec, _ = _ics_parsuj_dt(ev['DTEND'][0][1], ev['DTEND'][0][0])
        elif celodenni:
            konec = start + timedelta(days=1)
        else:
            konec = start + timedelta(hours=1)
        if konec is None or konec < start:
            konec = start + (timedelta(days=1) if celodenni else timedelta(hours=1))

        rrule = _ics_rrule(ev['RRULE'][0][1]) if 'RRULE' in ev else {}

        vyjimky = set()
        for params, hodnota in ev.get('EXDATE', []):
            for jeden in hodnota.split(','):
                x, _ = _ics_parsuj_dt(jeden, params)
                if x is not None:
                    vyjimky.add(_klic_casu(x))

        nazev = _ocisti_hodnotu(_ics_text(ev['SUMMARY'][0][1]) if 'SUMMARY' in ev else '(bez nazvu)', 80)
        misto = _ocisti_hodnotu(_ics_text(ev['LOCATION'][0][1]) if 'LOCATION' in ev else '', 40)
        popis = _ocisti_hodnotu(_ics_text(ev['DESCRIPTION'][0][1]) if 'DESCRIPTION' in ev else '', 220)

        delka = konec - start
        for occ in _ics_vyskty(start, rrule, od, do):
            klic = _klic_casu(occ)
            if klic in vyjimky:
                continue
            if uid and (uid, klic) in prekryty:
                continue

            occ_konec = occ + delka
            d0 = _den(occ)
            if isinstance(occ, datetime):
                d1 = (occ_konec - timedelta(microseconds=1)).date()
                cas_od = occ.strftime('%H:%M')
                cas_do = occ_konec.strftime('%H:%M')
            else:
                d1 = _den(occ_konec) - timedelta(days=1)
                cas_od = cas_do = ''
            if d1 < d0:
                d1 = d0
            if d1 > d0:
                # vícedenní akce — konec se vztahuje až k poslednímu dni
                cas_do = ''

            d = d0
            while d <= d1:
                if d >= od and d <= do:
                    # časy uvádíme jen u prvního dne
                    radky.append((d.strftime('%Y-%m-%d'),
                                  cas_od if d == d0 else '',
                                  cas_do if d == d0 else '',
                                  nazev, misto, popis))
                d += timedelta(days=1)

    radky.sort(key=lambda x: (x[0], x[1]))
    radky = radky[:800]

    kal_dir = os.path.join(SD_CESTA, 'kalendar')
    os.makedirs(kal_dir, exist_ok=True)
    with open(os.path.join(kal_dir, 'kalendar.txt'), 'w', encoding='utf-8') as f:
        for r in radky:
            f.write('|'.join(r) + '\n')
    print(f"  Kalendar: {len(radky)} udalosti -> eindata/kalendar/kalendar.txt")


if __name__ == "__main__":
    print("Spoustim stahovani z webu a RSS...")

    # --- Vytvoření adresářové struktury ---
    zpravy_dir   = os.path.join(SD_CESTA, 'zpravy', 'aktualni')
    cache_dir    = os.path.join(SD_CESTA, 'cache')
    archiv_dir   = os.path.join(SD_CESTA, 'zpravy', 'archiv')
    for d in (zpravy_dir, cache_dir, archiv_dir):
        os.makedirs(d, exist_ok=True)

    # 1. Běžné zprávy a tech/ai — s cross-category deduplikací napříč všemi kategoriemi
    # Priorita: Svět > ČR > Tech > Kultura > Bulvár.
    
    svet_data = stahni_zpravy_multi([
        ("https://ct24.ceskatelevize.cz/rss/svet", 60),
        ("https://www.novinky.cz/rss/zahranicni", 60),
    ], celkovy_limit=100)

    svet_titulky = {b["titulek"] for b in extrahuj_bloky(svet_data[1])}
    print(f"  Světové zprávy: {len(svet_titulky)} unikátních titulků")

    cr_data = stahni_zpravy_multi([
        ("https://ct24.ceskatelevize.cz/rss/domaci", 60),
        ("https://www.novinky.cz/rss/domaci", 60),
    ], celkovy_limit=100, exclude_titulky=svet_titulky)

    # Obojsměrná deduplikace Svět↔ČR — nenecháme stejný článek ve dvou rubrikách
    cr_titulky = {b["titulek"] for b in extrahuj_bloky(cr_data[1])}
    cr_titulky_norm = {norm_titulek(t) for t in cr_titulky}
    svet_bloky = extrahuj_bloky(svet_data[1])
    svet_filtrovane = [b for b in svet_bloky if norm_titulek(b["titulek"]) not in cr_titulky_norm]
    if len(svet_filtrovane) < len(svet_bloky):
        print(f"  Dedup Svět←ČR: odebráno {len(svet_bloky) - len(svet_filtrovane)} duplikátů")
        # Přebudujeme index i plná data, aby byly konzistentní (původní index měl všechny články)
        detail_dir = os.path.join(SD_CESTA, 'zpravy', 'aktualni', 'detail')
        os.makedirs(detail_dir, exist_ok=True)
        novy_index = ""
        svet_filtrovany_text = ""
        for b in svet_filtrovane:
            titulek = b["titulek"]
            blok = b["blok"]
            clanek_id = hashlib.md5(titulek.encode('utf-8')).hexdigest()[:12]
            svet_filtrovany_text += blok
            try:
                x_pos = blok.find("|X|")
                e_pos = blok.find("|E|", x_pos)
                text_clanku = blok[x_pos+3:e_pos]
                perex_blok = blok[:x_pos+3]  # |T|...|D|...|P|...|X|
                with open(os.path.join(detail_dir, f"{clanek_id}.txt"), 'w', encoding='utf-8') as f:
                    f.write(text_clanku)
                novy_index += f"{perex_blok}{clanek_id}|E|"
            except Exception:
                novy_index += blok  # fallback
        svet_data = (novy_index, svet_filtrovany_text)
    # Vždy přepočítáme svet_titulky podle aktuálního stavu dat (pro správný claimed set)
    svet_titulky = {b["titulek"] for b in extrahuj_bloky(svet_data[1])}
    print(f"  Svět po dedup: {len(svet_titulky)}, ČR: {len(cr_titulky)}")

    # Global claimed set: Svět + ČR
    claimed = svet_titulky | cr_titulky

    tech_data = stahni_zpravy_multi([
        ("https://www.lupa.cz/rss/clanky/", 20),
        ("https://www.cnews.cz/feed/", 20),
        ("https://www.root.cz/rss/clanky/", 20),
        ("https://www.zive.cz/rss/sc-47/default.aspx", 20),
        ("https://www.ejaj.cz/feed.xml", 30),
        ("https://www.svethardware.cz/rss", 25),
        ("https://digimanie.cz/rss/all", 12),
        ("https://blog.aditivni.cz/rss.xml", 10),
    ], celkovy_limit=100, exclude_titulky=claimed)

    tech_titulky = {b["titulek"] for b in extrahuj_bloky(tech_data[1])}
    print(f"  Tech zprávy: {len(tech_titulky)} unikátních titulků")

    # Global claimed: Svět + ČR + Tech
    claimed = claimed | tech_titulky

    # Kultura: články a recenze her/knih/filmů/seriálů. Kalendáře akcí jsou zvlášť.
    kultura_data = stahni_zpravy_multi([
        ("https://www.irozhlas.cz/rss/irozhlas/section/kultura", 25),
        ("http://www.moviezone.cz/rss/recenze/", 20),
        ("http://www.serialzone.cz/rss/clanky/", 15),
        ("https://games.tiscali.cz/rss2.xml", 25, ("recenze",)),
        ("https://denikalarm.cz/feed/", 20, ("recenze", "kniha", "literatura", "film", "seriál", "divadlo", "kultura", "hudba", "výstava")),
        ("https://denikn.cz/kultura/feed", 20),
        ("https://www.respekt.cz/api/rss?type=articles&unlocked=1", 20,
         ("film", "seriál", "divadlo", "kniha", "literatura", "kultura", "hudba", "výstava", "festival", "recenze")),
    ], celkovy_limit=100, exclude_titulky=claimed)
    kultura_titulky = {b["titulek"] for b in extrahuj_bloky(kultura_data[1])}
    print(f"  Kulturní zprávy: {len(kultura_titulky)} unikátních titulků")
    claimed = claimed | kultura_titulky

    # Akce: samostatné kalendáře Kudy z nudy pro Prahu a Plzeňský kraj.
    akce_data = stahni_zpravy_multi([
        ("https://www.kudyznudy.cz/rss/events.aspx?region=1", 30),
        ("https://www.kudyznudy.cz/rss/events.aspx?region=4", 30),
    ], celkovy_limit=80, exclude_titulky=claimed, event_mode=True)
    akce_titulky = {b["titulek"] for b in extrahuj_bloky(akce_data[1])}
    print(f"  Akce Praha a Plzeň: {len(akce_titulky)} unikátních titulů")
    claimed = claimed | akce_titulky

    # Bulvár: ponecháváme Pozitivní zprávy a české zdroje celebrit/reality show.
    pozit_data = stahni_zpravy_multi([
        ("https://pozitivni-zpravy.cz/feed/", 15),
        ("https://www.blesk.cz/rss", 12),
        ("https://servis.expres.cz/rss.aspx", 10),
        ("https://www.super.cz/rss", 8),
        ("https://www.extra.cz/rss", 5),
    ], celkovy_limit=30, exclude_titulky=claimed)

    pozit_titulky = {b["titulek"] for b in extrahuj_bloky(pozit_data[1])}
    print(f"  Bulvár: {len(pozit_titulky)} unikátních titulků")

    # 2. Archivace a ukládání zpráv do eindata/zpravy/aktualni/ a archiv/YYYY-MM-DD dle data článku
    oprav_existujici_archiv(archiv_dir)

    # Uložení aktuálních zpráv (přepis)
    for soubor, (index_data, full_data) in [
        ('zpravy_svet.txt', svet_data),
        ('zpravy_cr.txt',   cr_data),
        ('zpravy_tech.txt', tech_data),
        ('zpravy_pozitivni.txt', pozit_data),
        ('zpravy_kultura.txt', kultura_data),
        ('zpravy_akce.txt', akce_data),
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

    # 5. Offline jízdní řády PID (jen vybrané linky/zastávky)
    print("Stahuji jízdní řády PID...")
    try:
        stahni_jizdni_rady()
    except Exception as ex:
        print(f"  Jizdni rady se nepovedlo vytvorit: {ex}")

    # 6. Google kalendář (tajná iCal adresa)
    try:
        stahni_kalendar()
    except Exception as ex:
        print(f"  Kalendar se nepovedlo vytvorit: {ex}")

    print("Vsechno uspesne stazeno a ulozeno!")
    print(f"  Zpravy: {zpravy_dir}")
    print(f"  Archiv: {archiv_dir}")
    print(f"  Cache:  {cache_dir}")
