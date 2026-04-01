#ifndef HRY_KVIZ_H
#define HRY_KVIZ_H

// ===== KATEGORIE A OBTÍZNOST =====
const int kvizKategoriiCount = 6;
const char* kvizKategorieNazvy[] = { "Kultura", "Věda", "Všeobecný", "Osobnosti", "Sport", "Příroda" };
const int kvizObtiznosti[] = { 100, 200, 300 };

// ===== OTÁZKY (180 celkem, 30 na kategorii, 10 za 100/200/300) =====
// Pořadí: Kultura 0-29, Věda 30-59, Všeobecný 60-89, Osobnosti 90-119, Sport 120-149, Příroda 150-179

const char* kvizOtazky[] = {
  // ---- KULTURA ZA 100 (0-9) ----
  "Kdo napsal Malého prince?",
  "Kdo namaloval Monu Lisu?",
  "Kolik strun má kytara?",
  "Kdo napsal Hamleta?",
  "Ve kterém městě je Louvre?",
  "Kdo režíroval film Titanic (1997)?",
  "Co je to sonet?",
  "Kdo složil Devátou symfonii?",
  "Jaký žánr je Harry Potter?",
  "Kdo vytvořil sochu Davida?",
  // ---- KULTURA ZA 200 (10-19) ----
  "Ve kterém roce vyšel první díl Pána prstenů?",
  "Kdo namaloval Hvězdnou noc?",
  "Z jaké země pochází manga?",
  "Kdo napsal román 1984?",
  "Ve kterém městě se nachází Sagrada Família?",
  "Kdo napsal Proměnu?",
  "Kolik Oscarů získal film Ben-Hur?",
  "Kdo je autorem Malé mořské víly?",
  "Co je to freska?",
  "Jaký nástroj hraje smyčcem?",
  // ---- KULTURA ZA 300 (20-29) ----
  "Ve kterém roce vznikla opera La Traviata?",
  "Kdo napsal Canterburské povídky?",
  "Jaký styl je Guernica od Picassa?",
  "Který český skladatel napsal Novosvětskou symfonii?",
  "Kdo napsal Odysseu?",
  "Ve kterém roce vznikl kubismus?",
  "Kdo napsal Bratry Karamazovy?",
  "Jaký tanec pochází z Argentiny?",
  "Kdo je autorem Farmy zvířat?",
  "Který český autor napsal R.U.R.?",

  // ---- VĚDA ZA 100 (30-39) ----
  "Kolik planet je ve Sluneční soustavě?",
  "Jaký plyn dýcháme?",
  "Kolik kostí má lidské tělo?",
  "Jaká je chemická značka vody?",
  "Co měří teploměr?",
  "Kolik má rok dní?",
  "Co je to DNA?",
  "Jaký je nejtvrdší minerál?",
  "Kolik má člověk zubů?",
  "Co je to atom?",
  // ---- VĚDA ZA 200 (40-49) ----
  "Kdo formuloval teorii relativity?",
  "Jaká je přibližná rychlost světla?",
  "Co je to fotosyntéza?",
  "Kolik chromozomů má člověk?",
  "Jaký prvek má chemickou značku Fe?",
  "Kdo objevil penicilin?",
  "Co je to mitóza?",
  "Jaká planeta je nejbližší Slunci?",
  "Co je to pH?",
  "Kdo vynalezl žárovku?",
  // ---- VĚDA ZA 300 (50-59) ----
  "Jaké je Avogadrovo číslo?",
  "Co je to Heisenbergův princip neurčitosti?",
  "Kdo objevil strukturu DNA?",
  "Co je to kvark?",
  "Jaký je absolutní nulový bod?",
  "Co je to CRISPR?",
  "Kdo formuloval 3 zákony pohybu?",
  "Co je to neutronová hvězda?",
  "Jaký plyn tvoří většinu atmosféry?",
  "Co je to Schrödingerova kočka?",

  // ---- VŠEOBECNÝ ZA 100 (60-69) ----
  "Jaké je hlavní město Francie?",
  "Kolik je kontinentů?",
  "Jaká je nejdelší řeka světa?",
  "Kolik má hodina minut?",
  "Jaká měna se používá v Japonsku?",
  "Jaký je nejvyšší hora světa?",
  "Kolik je měsíců v roce?",
  "Kde sídlí OSN?",
  "Kolik je písmen v české abecedě?",
  "Jaký je symbol míru?",
  // ---- VŠEOBECNÝ ZA 200 (70-79) ----
  "Ve kterém roce padla Berlínská zeď?",
  "Jaký je nejmenší stát světa?",
  "Kolik hvězd je na vlajce EU?",
  "Jaká je nejlidnatější země světa?",
  "Ve kterém roce byla založena ČR?",
  "Kolik časových pásem má Rusko?",
  "Ve kterém roce přistáli lidé na Měsíci?",
  "Jaký je největší oceán?",
  "Kde se nachází Machu Picchu?",
  "Jaký je nejdelší most na světě?",
  // ---- VŠEOBECNÝ ZA 300 (80-89) ----
  "Ve kterém roce začala 1. světová válka?",
  "Kolik zemí tvoří Spojené království?",
  "Ve kterém roce byl vynalezen internet?",
  "Jaká je rozloha Ruska?",
  "Kdo vynalezl knihtisk?",
  "Ve kterém roce skončila studená válka?",
  "Kolik obyvatel má svět přibližně?",
  "Kde se nachází Angkor Wat?",
  "Jaký stát má nejvíce jazyků?",
  "Jaký je nejstarší universidad?",

  // ---- OSOBNOSTI ZA 100 (90-99) ----
  "Kdo je zakladatel Microsoftu?",
  "Kdo byl první prezident USA?",
  "Kdo je zakladatel Applu?",
  "Kdo byl první člověk ve vesmíru?",
  "Kdo napsal Romeo a Julii?",
  "Kdo vynalezl telefon?",
  "Kdo je autorem E=mc²?",
  "Kdo byl Nikola Tesla?",
  "Kdo byla Matka Tereza?",
  "Kdo namaloval Monu Lisu?",
  // ---- OSOBNOSTI ZA 200 (100-109) ----
  "Kdo je zakladatel Tesly?",
  "Kdo vynalezl dynamit?",
  "Kdo byl Mahátma Gándhí?",
  "Kdo objevil Ameriku?",
  "Kdo byla Kleopatra?",
  "Kdo založil Facebook?",
  "Kdo byl Napoleon Bonaparte?",
  "Kdo je Nelson Mandela?",
  "Kdo byl Charles Darwin?",
  "Kdo napsal Mein Kampf?",
  // ---- OSOBNOSTI ZA 300 (110-119) ----
  "Kdo byl Čingischán?",
  "Ve kterém roce zemřel Leonardo da Vinci?",
  "Kdo je Alan Turing?",
  "Kdo byl Ramesse II?",
  "Kdo založil Osmanskou říši?",
  "Kdo je Rosalind Franklin?",
  "Kdo byl Konfucius?",
  "Kdo je Marie Curie?",
  "Kdo byl Archimedes?",
  "Kdo je Ada Lovelace?",

  // ---- SPORT ZA 100 (120-129) ----
  "Kolik hráčů má fotbalový tým?",
  "Ve kterém sportu se používá raketa?",
  "Co je to hat-trick?",
  "Jaký sport hraje NBA?",
  "Kde se koná Wimbledon?",
  "Co je to Tour de France?",
  "Ve kterém městě byly OH 2024?",
  "Kolik setů se hraje v tenise (muži Grand Slam)?",
  "Kolik kol má závod F1 obvykle?",
  "Jaký je nejrychlejší sport (rychlost míčku)?",
  // ---- SPORT ZA 200 (130-139) ----
  "Ve kterém roce byly první novodobé OH?",
  "Kolik metrů je maratón?",
  "Kdo je nejlepší střelec historie fotbalu?",
  "Jaký sport pochází z Japonska?",
  "Kolik bodů má touchdown v americkém fotbale?",
  "Kdo je Usain Bolt?",
  "Kolik trvá hokejová třetina?",
  "Kdo vyhrál MS ve fotbale 2022?",
  "Kdo má nejvíc titulů ve F1?",
  "Ve kterém sportu se hraje florbal?",
  // ---- SPORT ZA 300 (140-149) ----
  "Kolik olympijských zlat má Michael Phelps?",
  "Ve kterém roce se hrál první Super Bowl?",
  "Kdo je nejúspěšnější tenista v historii Grand Slamů?",
  "Co zahrnuje Ironman triathlon?",
  "Kdo je Don Bradman?",
  "Jaký je rekord v maratonu?",
  "Kdo založil novodobé olympijské hry?",
  "Co je to Dakar Rally?",
  "Kde vznikl baseball?",
  "Ve kterém roce se poprvé hrálo rugby?",

  // ---- PŘÍRODA ZA 100 (150-159) ----
  "Jaké je největší zvíře na světě?",
  "Kolik nohou má pavouk?",
  "Co produkují včely?",
  "Jaký strom dává žaludy?",
  "Jaký je nejrychlejší zvíře na souši?",
  "Kolik nohou má hmyz?",
  "Jaký je největší prales?",
  "Jaké zvíře je symbol Austrálie?",
  "Co je to tsunami?",
  "Co je to fotosyntéza (stručně)?",
  // ---- PŘÍRODA ZA 200 (160-169) ----
  "Kolik srdcí má chobotnice?",
  "Jaký je nejstarší strom na světě?",
  "Jaký pták neumí létat a žije na Antarktidě?",
  "Kolik procent Země pokrývá voda?",
  "Co je to symbióza?",
  "Jaký je největší vodopád světa?",
  "Kolik zubů má žralok za život?",
  "Co je to permafrost?",
  "Co je to korálový útes?",
  "Jaký je největší hadí druh?",
  // ---- PŘÍRODA ZA 300 (170-179) ----
  "Co je to endosymbiotická teorie?",
  "Kolik procent tvoří deštné pralesy kyslíku?",
  "Jaký je nejjedovatější živočich?",
  "Co je to bioluminiscence?",
  "Jaký je nejhlubší bod oceánu?",
  "Co je to ENSO?",
  "Kolik druhů hmyzu je známo?",
  "Co je to mykorhiza?",
  "Jaký je nejstarší živý organismus?",
  "Jaký je rozdíl mezi prokaryoty a eukaryoty?"
};

const char* kvizOdpovedi[] = {
  // KULTURA 100
  "Antoine de Saint-Exupéry",
  "Leonardo da Vinci",
  "6",
  "William Shakespeare",
  "Paříž",
  "James Cameron",
  "Báseň o 14 verších",
  "Ludwig van Beethoven",
  "Fantasy",
  "Michelangelo",
  // KULTURA 200
  "1954",
  "Vincent van Gogh",
  "Japonsko",
  "George Orwell",
  "Barcelona",
  "Franz Kafka",
  "11",
  "H. C. Andersen",
  "Malba na mokré omítce",
  "Housle",
  // KULTURA 300
  "1853",
  "Geoffrey Chaucer",
  "Kubismus",
  "Antonín Dvořák",
  "Homér",
  "1907",
  "Fjodor Dostojevskij",
  "Tango",
  "George Orwell",
  "Karel Čapek",

  // VĚDA 100
  "8",
  "Kyslík",
  "206",
  "H2O",
  "Teplotu",
  "365",
  "Deoxyribonukleová kyselina",
  "Diamant",
  "32",
  "Základní částice hmoty",
  // VĚDA 200
  "Albert Einstein",
  "300 000 km/s",
  "Přeměna CO2 na kyslík",
  "46",
  "Železo",
  "Alexander Fleming",
  "Buněčné dělení",
  "Merkur",
  "Míra kyselosti",
  "Thomas Edison",
  // VĚDA 300
  "6.022 × 10²³",
  "Princip neurčitosti",
  "Watson a Crick",
  "Subatomární částice",
  "-273,15 °C",
  "Editace genů",
  "Isaac Newton",
  "Zbytek supernovy",
  "Dusík (78%)",
  "Myšlenkový experiment kvant. mechaniky",

  // VŠEOBECNÝ 100
  "Paříž",
  "7",
  "Nil",
  "60",
  "Jen",
  "Mount Everest",
  "12",
  "New York",
  "42",
  "Holubice",
  // VŠEOBECNÝ 200
  "1989",
  "Vatikán",
  "12",
  "Indie",
  "1993",
  "11",
  "1969",
  "Tichý oceán",
  "Peru",
  "Danyang-Kunshan (Čína)",
  // VŠEOBECNÝ 300
  "1914",
  "4",
  "1969 (ARPANET)",
  "17,1 mil. km²",
  "Johannes Gutenberg",
  "1991",
  "8 miliard",
  "Kambodža",
  "Papua-Nová Guinea",
  "Bologna (1088)",

  // OSOBNOSTI 100
  "Bill Gates",
  "George Washington",
  "Steve Jobs",
  "Jurij Gagarin",
  "William Shakespeare",
  "Alexander Graham Bell",
  "Albert Einstein",
  "Vynálezce a fyzik",
  "Řeholnice a misionářka",
  "Leonardo da Vinci",
  // OSOBNOSTI 200
  "Elon Musk",
  "Alfred Nobel",
  "Vůdce indické nezávislosti",
  "Kryštof Kolumbus",
  "Egyptská královna",
  "Mark Zuckerberg",
  "Francouzský císař",
  "Jihoafrický prezident",
  "Autor teorie evoluce",
  "Adolf Hitler",
  // OSOBNOSTI 300
  "Zakladatel Mongolské říše",
  "1519",
  "Otec počítačové vědy",
  "Egyptský faraon",
  "Osman I.",
  "Spoluobjevitelka DNA",
  "Čínský filozof",
  "Dvojnásobná nobelistka",
  "Řecký matematik a fyzik",
  "První programátorka",

  // SPORT 100
  "11",
  "Tenis",
  "3 góly jedním hráčem",
  "Basketbal",
  "Londýn",
  "Cyklistický závod",
  "Paříž",
  "5",
  "~50-70",
  "Badminton",
  // SPORT 200
  "1896",
  "42 195 m",
  "Cristiano Ronaldo",
  "Judo",
  "6",
  "Nejrychlejší člověk",
  "20 minut",
  "Argentina",
  "Lewis Hamilton / Schumacher (7)",
  "Florbal",
  // SPORT 300
  "23",
  "1967",
  "Novak Djokovič (24)",
  "3,8km plav. / 180km kolo / 42km běh",
  "Nejlepší kriketový hráč",
  "Pod 2 hodiny (Kipchoge)",
  "Pierre de Coubertin",
  "Terénní automobilový závod",
  "USA (1840s)",
  "1823",

  // PŘÍRODA 100
  "Plejtvák obrovský",
  "8",
  "Med",
  "Dub",
  "Gepard",
  "6",
  "Amazonský prales",
  "Klokan",
  "Obří vlna",
  "Výroba kyslíku rostlinami",
  // PŘÍRODA 200
  "3",
  "Borovice osinatá (~5000 let)",
  "Tučňák",
  "71%",
  "Soužití dvou organismů",
  "Angel (Venezuela)",
  "Až 30 000",
  "Trvale zmrzlá půda",
  "Podmořský ekosystém",
  "Krajta síťovaná",
  // PŘÍRODA 300
  "Mitochondrie byly bakterie",
  "~28%",
  "Medúzka čtyřhranná",
  "Světélkování organismů",
  "Marianský příkop (11 034 m)",
  "El Niño – jižní oscilace",
  "~1 milion",
  "Soužití hub a kořenů",
  "Posidonia oceanica (~100 000 let)",
  "Přítomnost jádra buňky"
};

// Kategorie pro každou otázku (0=Kultura, 1=Věda, 2=Všeobecný, 3=Osobnosti, 4=Sport, 5=Příroda)
const int kvizKategoriePole[] = {
  0,0,0,0,0,0,0,0,0,0, // Kultura 100
  0,0,0,0,0,0,0,0,0,0, // Kultura 200
  0,0,0,0,0,0,0,0,0,0, // Kultura 300
  1,1,1,1,1,1,1,1,1,1, // Věda 100
  1,1,1,1,1,1,1,1,1,1, // Věda 200
  1,1,1,1,1,1,1,1,1,1, // Věda 300
  2,2,2,2,2,2,2,2,2,2, // Všeobecný 100
  2,2,2,2,2,2,2,2,2,2, // Všeobecný 200
  2,2,2,2,2,2,2,2,2,2, // Všeobecný 300
  3,3,3,3,3,3,3,3,3,3, // Osobnosti 100
  3,3,3,3,3,3,3,3,3,3, // Osobnosti 200
  3,3,3,3,3,3,3,3,3,3, // Osobnosti 300
  4,4,4,4,4,4,4,4,4,4, // Sport 100
  4,4,4,4,4,4,4,4,4,4, // Sport 200
  4,4,4,4,4,4,4,4,4,4, // Sport 300
  5,5,5,5,5,5,5,5,5,5, // Příroda 100
  5,5,5,5,5,5,5,5,5,5, // Příroda 200
  5,5,5,5,5,5,5,5,5,5  // Příroda 300
};

// Obtížnost pro každou otázku
const int kvizObtiznostPole[] = {
  100,100,100,100,100,100,100,100,100,100, // Kultura 100
  200,200,200,200,200,200,200,200,200,200, // Kultura 200
  300,300,300,300,300,300,300,300,300,300, // Kultura 300
  100,100,100,100,100,100,100,100,100,100, // Věda 100
  200,200,200,200,200,200,200,200,200,200, // Věda 200
  300,300,300,300,300,300,300,300,300,300, // Věda 300
  100,100,100,100,100,100,100,100,100,100, // Všeobecný 100
  200,200,200,200,200,200,200,200,200,200, // Všeobecný 200
  300,300,300,300,300,300,300,300,300,300, // Všeobecný 300
  100,100,100,100,100,100,100,100,100,100, // Osobnosti 100
  200,200,200,200,200,200,200,200,200,200, // Osobnosti 200
  300,300,300,300,300,300,300,300,300,300, // Osobnosti 300
  100,100,100,100,100,100,100,100,100,100, // Sport 100
  200,200,200,200,200,200,200,200,200,200, // Sport 200
  300,300,300,300,300,300,300,300,300,300, // Sport 300
  100,100,100,100,100,100,100,100,100,100, // Příroda 100
  200,200,200,200,200,200,200,200,200,200, // Příroda 200
  300,300,300,300,300,300,300,300,300,300  // Příroda 300
};

const int kvizPocet = 180;

#endif
#ifndef HRY_KVIZ_H
#define HRY_KVIZ_H

#include <Arduino.h>

const int kvizPocet = 10;

const char* kvizOtazky[] = {
  "Ktera planeta slunecni soustavy je nejblize Slunci?",
  "Kdo napsal hru Romeo a Julie?",
  "Jak se jmenuje nejvyssi hora sveta?",
  "V jakem roce skoncila druha svetova valka?",
  "Ktery prvek ma chemickou znacku O?",
  "Jaka je hlavni mena v Japonsku?",
  "Kdo namaloval obraz Mona Lisa?",
  "Jak se nazyva nas kontinent?",
  "Ktery ocean je nejvetsi?",
  "Kolik barev ma duha?"
};

const char* kvizOdpovedi[] = {
  "Merkur",
  "William Shakespeare",
  "Mount Everest",
  "1945",
  "Kyslik",
  "Jen",
  "Leonardo da Vinci",
  "Evropa",
  "Tichy ocean",
  "Sedm"
};

#endif
