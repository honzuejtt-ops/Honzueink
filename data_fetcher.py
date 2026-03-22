import os
import requests

# Zde si skript později načte tvoje tajné API klíče
WEATHER_API_KEY = os.environ.get("WEATHER_API_KEY")
NEWS_API_KEY = os.environ.get("NEWS_API_KEY")

def fetch_weather():
    # TADY POZDĚJI PŘIDÁME OPENWEATHERMAP API
    # Zatím to jen vrátí testovací text
    return "Pocasi v Praze\nTeplota: 15°C\nStav: Polojasno\nVitr: 5 m/s"

def fetch_news():
    # TADY POZDĚJI PŘIDÁME NEWS API NEBO RSS
    # Zatím to jen vrátí testovací text
    return "HLAVNI ZPRAVY\n\n1. Nova inovace ve svete AI...\n2. Politicka situace v CR...\n3. Zajimavost ze sveta vedy..."

if __name__ == "__main__":
    print("Spoustim stahovani dat...")
    
    # Získání dat
    weather_text = fetch_weather()
    news_text = fetch_news()

    # Uložení do textových souborů (bez diakritiky nebo s ní, podle fontů tvé čtečky)
    with open("pocasi.txt", "w", encoding="utf-8") as f:
        f.write(weather_text)

    with open("zpravy.txt", "w", encoding="utf-8") as f:
        f.write(news_text)
        
    print("Hotovo! Soubory pocasi.txt a zpravy.txt byly aktualizovany.")
