#ifndef GAMEBOOK_H
#define GAMEBOOK_H

// ===== GAMEBOOK ZAKLÍNAČ =====
// Hraješ za Geralta z Rivie.
// Každý uzel má text + 2 volby (krátký stisk / dlouhý stisk)
// Pokud je volba -1, hra končí (koncovka).

struct GBNode {
  const char* text;
  int volbaA;  // krátký stisk BTN21
  int volbaB;  // dlouhý stisk BTN0
  const char* popisA;  // text volby A
  const char* popisB;  // text volby B
};

const GBNode gamebook[] PROGMEM = {
  // === 0: ÚVOD ===
  { "Sedíš v hostinci U Šťastného prasete ve Viziمě. Venku lije. "
    "Hostinský ti nabídne dvě zakázky: ghúl na hřbitově nebo utopenci v řece.",
    1, 2, "Hřbitov (ghúl)", "Řeka (utopenci)" },

  // === 1: HŘBITOV - PŘÍCHOD ===
  { "Hřbitov je zahalený mlhou. Mezi náhrobky se mihají stíny. "
    "Slyšíš škrábání zespodu. Můžeš se plížit mezi hroby nebo zavolat ghúla.",
    3, 4, "Plížit se", "Vylákat ghúla" },

  // === 2: ŘEKA - PŘÍCHOD ===
  { "Řeka je temná a studená. Na břehu vidíš stopy. "
    "Můžeš jít podél břehu nebo se ponořit do vody s dýchacím lektvarem.",
    5, 6, "Podél břehu", "Do vody" },

  // === 3: HŘBITOV - PLÍŽENÍ ===
  { "Opatrně se plížíš. Ghúl je za velkým náhrobkem a žere mrtvolu. "
    "Můžeš zaútočit zezadu mečem nebo hodit bombu Šípový déšť.",
    7, 8, "Útok mečem", "Hodit bombu" },

  // === 4: HŘBITOV - VYLÁKÁNÍ ===
  { "Zavoláš a bouchneš mečem o náhrobek. Ghúl se vyřítí přímo na tebe! "
    "Je rychlejší než jsi čekal. Uhneš doleva nebo doprava?",
    9, 10, "Doleva", "Doprava" },

  // === 5: ŘEKA - BŘEH ===
  { "Jdeš po břehu a najdeš jeskyni. Uvnitř slyšíš bublání. "
    "U vchodu leží mrtvý rybář. Vstoupíš dovnitř nebo prozkoumáš tělo?",
    11, 12, "Vstoupit", "Prozkoumat tělo" },

  // === 6: ŘEKA - POTOPENÍ ===
  { "Ponoříš se do ledové vody. Lektvar funguje, vidíš pod hladinou. "
    "Tři utopenci plavou k tobě. Zaútočíš stříbrným mečem nebo upluješ k jeskyni?",
    13, 14, "Zaútočit", "Uplavat" },

  // === 7: HŘBITOV - ÚTOK MEČEM ===
  { "Tvůj meč se zabodne ghúlovi do zad! Zvíře řve a otočí se. "
    "Jeden úder nestačil. Ghúl po tobě hrabne drápy. Uskočíš nebo seš znaku?",
    15, 16, "Uskočit", "Znak Quen" },

  // === 8: HŘBITOV - BOMBA ===
  { "Bomba exploduje a ghúl je zasažený střepinami. Je zraněný ale zuřivý. "
    "Řítí se na tebe. Tasíš meč nebo použiješ znak Igni?",
    17, 18, "Tasit meč", "Znak Igni" },

  // === 9: HŘBITOV - DOLEVA ===
  { "Uskočíš doleva za náhrobek. Ghúl narazí do kamene a otřese se. "
    "Máš sekundu na reakci. Sekneš ho do hlavy nebo do nohou?",
    19, 20, "Do hlavy", "Do nohou" },

  // === 10: HŘBITOV - DOPRAVA ===
  { "Uhneš doprava, ale zakopneš o kořen! Padáš na zem. "
    "Ghúl se vrhá na tebe. Kopneš ho nebo se překulíš?",
    21, 22, "Kopnout", "Překulit se" },

  // === 11: JESKYNĚ - VSTUP ===
  { "Jeskyně je vlhká a temná. Zapálíš pochodeň. Na stěnách jsou škrábance. "
    "Chodba se rozděluje – vlevo je úzká, vpravo široká s vodou na podlaze.",
    23, 24, "Úzká chodba", "Široká chodba" },

  // === 12: ŘEKA - MRTVÝ RYBÁŘ ===
  { "Rybář má v ruce stříbrnou dýku a na krku amulet. "
    "V kapse najdeš zmačkaný dopis: 'Utopce vede vodní bába. Zabij ji.' "
    "Vezmeš si dýku nebo amulet?",
    25, 26, "Stříbrná dýka", "Amulet" },

  // === 13: ŘEKA - BOJ POD VODOU ===
  { "Stříbrný meč září pod vodou. Sekneš prvního utopce. "
    "Ale druzí dva tě obklíčí. Jeden tě chytí za nohu! Sekneš dolů nebo nahoru?",
    27, 28, "Seknout dolů", "Seknout nahoru" },

  // === 14: ŘEKA - ÚPRK K JESKYNI ===
  { "Pluješ k jeskyni, utopenci za tebou. Vplaveš dovnitř. "
    "Jeskyně má vzduchovou kapsu nahoře. Vynoříš se a vidíš oltář s lebkami. "
    "Prozkoumáš oltář nebo se schováš?",
    29, 30, "Prozkoumat oltář", "Schovat se" },

  // === 15: HŘBITOV - USKOČENÍ ===
  { "Uskočíš elegantně. Geralt přece jen nejlepší zaklínač. "
    "Ghúl se zapotácí. Máš čas na finální úder. Piroueta nebo přímý výpad?",
    31, 32, "Piroueta", "Přímý výpad" },

  // === 16: HŘBITOV - QUEN ===
  { "Znak Quen tě ochrání zlatým štítem. Ghúlovy drápy se odrazí. "
    "Zvíře je zmatené. Sekneš ho nebo použiješ Aard a odhodíš ho?",
    33, 34, "Seknout", "Znak Aard" },

  // === 17: HŘBITOV - MEČ PO BOMBĚ ===
  { "Tasíš stříbrný meč. Ghúl je zraněný z bomby a pomalý. "
    "Krouží kolem tebe. Zaútočíš hned nebo počkáš až zaútočí on?",
    35, 36, "Zaútočit hned", "Počkat" },

  // === 18: HŘBITOV - IGNI ===
  { "Plameny znaku Igni pohltí ghúla! Hoří a řve. "
    "Ale zuřivě se vrhá přímo na tebe, hořící. Uskočíš nebo ho dokončíš mečem?",
    37, 38, "Uskočit", "Dokončit mečem" },

  // === 19: HŘBITOV - SEKNUTÍ DO HLAVY ===
  { "Tvůj meč rozsekne ghúlovu lebku! Bestie padá k zemi mrtvá. "
    "V ghúlově doupěti najdeš pytel se zlaťáky a starou mapu. "
    "Vezmeš zlato nebo prozkoumáš mapu?",
    39, 40, "Vzít zlato", "Prozkoumat mapu" },

  // === 20: HŘBITOV - SEKNUTÍ DO NOHOU ===
  { "Sekneš ghúla do nohou. Padne na kolena. Ale chňapne ti po ruce! "
    "Drápy ti roztrhnou rukáv. Bolí to. Dokončíš ho levou rukou nebo hlavou?",
    41, 42, "Levou rukou", "Hlavičkou" },

  // === 21: HŘBITOV - KOPNUTÍ ===
  { "Kopneš ghúla do čelisti! Zvíře odletí. Vstaneš a tasíš meč. "
    "Ghúl je otřesený. Rychlý úder nebo pomalý silný?",
    43, 44, "Rychlý úder", "Silný úder" },

  // === 22: HŘBITOV - PŘEKULENÍ ===
  { "Překulíš se na stranu. Ghúl se zakousne do země místo tebe. "
    "Vstaneš mu za zády. Ideální pozice! Sekneš do páteře nebo do krku?",
    45, 46, "Do páteře", "Do krku" },

  // === 23: JESKYNĚ - ÚZKÁ CHODBA ===
  { "Úzkou chodbou se protáhneš do malé komůrky. Na zemi leží hnízdo z kostí. "
    "Vodní bába tu není, ale najdeš její lektvar. Vypiješ ho nebo zničíš?",
    47, 48, "Vypít lektvar", "Zničit lektvar" },

  // === 24: JESKYNĚ - ŠIROKÁ CHODBA ===
  { "Jdeš širokou chodbou. Voda ti sahá po kotníky. Najednou – šplouchnutí! "
    "Vodní bába stojí před tebou! Je obrovská. Zaútočíš nebo ustoupíš?",
    49, 50, "Zaútočit", "Ustoupit" },

  // === 25: ŘEKA - DÝKA ===
  { "Stříbrná dýka se ti bude hodit. Jdeš do jeskyně. "
    "Uvnitř je temno. Slyšíš šeptání. Jdeš za zvukem nebo zapálíš pochodeň?",
    51, 52, "Za zvukem", "Zapálit pochodeň" },

  // === 26: ŘEKA - AMULET ===
  { "Amulet na krku začne zářit modře. Chrání před utopením! "
    "S amueletem se potopíš do jeskyně. Vidíš podmořský tunel. "
    "Poplyveš tunelem nebo prozkoumáš dno?",
    53, 54, "Tunel", "Prozkoumat dno" },

  // === 27: POD VODOU - SEKNUTÍ DOLŮ ===
  { "Sekneš utopce do ruky – pustí tě! Dva zbývají. "
    "Vyplaveš na hladinu nebo budeš bojovat dál?",
    55, 56, "Na hladinu", "Bojovat" },

  // === 28: POD VODOU - SEKNUTÍ NAHORU ===
  { "Sekneš nahoru a zasáhneš druhého utopce do břicha. "
    "Ale ten třetí tě škrábe do zad! Otočíš se nebo pluješ pryč?",
    57, 58, "Otočit se", "Plavat pryč" },

  // === 29: JESKYNĚ - OLTÁŘ ===
  { "Oltář je pokrytý runami. Uprostřed leží lebka s drahokamem v oku. "
    "Vytáhneš drahokam nebo zničíš oltář mečem?",
    59, 60, "Vytáhnout drahokam", "Zničit oltář" },

  // === 30: JESKYNĚ - SCHOVÁNÍ ===
  { "Schováš se za skálu. Slyšíš jak se utopenci proplíží kolem. "
    "Za nimi vchází vodní bába. Zaútočíš ze zálohy nebo počkáš?",
    61, 62, "Útok ze zálohy", "Počkat" },

  // === 31: PIROUETA ===
  { "Zaklínačská piroueta! Meč krouží a zasahuje ghúla do krku. "
    "Hlava odlétá. Ghúl je mrtvý. Vítězství! "
    "Na těle najdeš zvláštní klíč. Vezmeš ho?",
    39, 40, "Vzít klíč a zlato", "Ignorovat klíč" },

  // === 32: PŘÍMÝ VÝPAD ===
  { "Přímý výpad přímo do srdce! Ghúl zachroptí a padne. "
    "Čistá práce. Hostinský bude spokojený. "
    "Prohledáš doupě nebo jdeš rovnou pro odměnu?",
    63, 64, "Prohledat doupě", "Jít pro odměnu" },

  // === 33: SEKNUTÍ PO QUENU ===
  { "Meč protne ghúlovo hrdlo. Hotovo. Další bestie na seznamu. "
    "V dáli vidíš světlo – na hřbitově je ještě někdo. Půjdeš se podívat?",
    65, 66, "Jít za světlem", "Odejít" },

  // === 34: AARD ===
  { "Znak Aard odhodí ghúla na náhrobek! Ghúl je napíchnutý na kříži. "
    "Škemrá. Dokončíš ho nebo ho necháš?",
    67, 68, "Dokončit", "Nechat" },

  // === 35: ÚTOK HNED ===
  { "Zaútočíš rychle. Ghúl nestihne reagovat. Tři seky a je po něm. "
    "Za hrobem najdeš díru do podzemí. Slézeš dolů?",
    69, 70, "Slézt dolů", "Nechat to" },

  // === 36: POČKAT NA ÚTOK ===
  { "Počkáš. Ghúl zaútočí – uskočíš a kontratakem mu usekneš ruku. "
    "Řve bolestí. Ještě žije. Poslední úder?",
    71, 72, "Úder do srdce", "Úder do hlavy" },

  // === 37: USKOČENÍ PŘED HOŘÍCÍM ===
  { "Uskočíš. Hořící ghúl narazí do stromu, který se vzplane. "
    "Ghúl padá a dohoří. Ale oheň se šíří! Uhasíš ho nebo utečeš?",
    73, 74, "Uhasit (Aard)", "Utéct" },

  // === 38: DOKONČENÍ HOŘÍCÍHO ===
  { "Probodneš hořícího ghúla mečem. Oba hoříte! Ale ghúl je mrtvý. "
    "Rychle uhasíš oblečení. Máš popáleniny, ale žiješ. Najdeš léčivé byliny?",
    75, 76, "Hledat byliny", "Jít do města" },

  // === 39: ZLATO / KLÍČ ===
  { "Bezeš zlato. 200 zlaťáků! Slušná odměna navíc k zakázce. "
    "Vracíš se do hostince. Hostinský je nadšený. Nabízí ti další zakázku – "
    "prý se v lese objevil vlkodlak. Přijmeš?",
    77, 78, "Přijmout vlkodlaka", "Odpočinout si" },

  // === 40: MAPA / IGNOROVÁNÍ ===
  { "Mapa ukazuje cestu k opuštěné elfí věži v lese. "
    "Prý tam je poklad. Půjdeš tam nebo to prodáš?",
    79, 80, "Jít k věži", "Prodat mapu" },

  // === 41: LEVOU RUKOU ===
  { "Přehodíš meč do levé a sekneš! Není to tak přesné, ale ghúl padá. "
    "Rána na ruce krvácí. Potřebuješ ošetření.",
    75, 76, "Hledat byliny", "Jít do města" },

  // === 42: HLAVIČKA ===
  { "Vážně? Hlavičkou na ghúla? Geralt, ty jsi blázen. "
    "Ale funguje to! Tvrdá zaklínačská lebka omráčí bestii. Dokončíš ho mečem. "
    "Legendární způsob zabití! Bardi o tom budou zpívat.",
    63, 64, "Prohledat doupě", "Jít slavit" },

  // === 43: RYCHLÝ ÚDER ===
  { "Rychlý sek! Ghúlova hlava letí. Čistá práce. "
    "Teď zpátky pro odměnu.",
    63, 64, "Prohledat okolí", "Jít pro odměnu" },

  // === 44: SILNÝ ÚDER ===
  { "Silný úder rozpoltí ghúla vedví! Brutální, ale účinné. "
    "Okolní hroby jsou pokryté krví. Alespoň dalšího ghúla to odradí.",
    63, 64, "Prohledat okolí", "Jít pro odměnu" },

  // === 45: DO PÁTEŘE ===
  { "Meč přerazí ghúlovi páteř. Bestie se zhroutí a už se nehýbe. "
    "Profesionální zásah. Vesemir by byl hrdý.",
    63, 64, "Prohledat doupě", "Jít pro odměnu" },

  // === 46: DO KRKU ===
  { "Sekneš do krku – hlava visí na kůži. Ghúl je definitivně mrtvý. "
    "V jeho tlamě najdeš zlatý prsten. Někdo ho pohřešuje...",
    81, 82, "Hledat majitele", "Nechat si prsten" },

  // === 47: VYPÍT LEKTVAR ===
  { "Vypiješ lektvar. Svět se zamlží... pak se rozjasní! "
    "Vidíš v temnotě jako kočka. A cítíš vodní bábu – je za zdí! "
    "Proseká se zeď nebo najdeš obejití?",
    83, 84, "Prosekat zeď", "Obejít" },

  // === 48: ZNIČIT LEKTVAR ===
  { "Rozbiješ lahvičku. Lepší nebrat cizí lektvary. "
    "Vrátíš se na rozcestí. Zkusíš širokou chodbu?",
    24, 11, "Široká chodba", "Zpět ven" },

  // === 49: ÚTOK NA VODNÍ BÁBU ===
  { "Vrhneš se na vodní bábu! Stříbrný meč jí rozpáře rameno. "
    "Řve jako sto koček a vrhne na tebe vodní proud! Uhneš nebo postavíš se?",
    85, 86, "Uhnout", "Quen a stát" },

  // === 50: ÚSTUP PŘED BÁBOU ===
  { "Ustoupíš. Bába tě sleduje ale neútočí. Šeptá něco. "
    "Nabízí ti dohodu – přestane zabíjet rybáře, když jí přineseš oběť. "
    "Přijmeš dohodu nebo zaútočíš?",
    87, 88, "Přijmout dohodu", "Zaútočit" },

  // === 51: ZA ZVUKEM ===
  { "Jdeš za šeptáním. V jeskyni najdeš malou holčičku! "
    "Pláče a říká, že ji unesla vodní bába. Je to past nebo pravda?",
    89, 90, "Věřit jí", "Je to past" },

  // === 52: POCHODEŇ ===
  { "Pochodeň osvítí jeskyni. Na stěnách jsou kresby – rituální symboly. "
    "Vodní bába tu provádí obřady. Najdeš její slabinu v kresbách?",
    91, 92, "Studovat kresby", "Jít dál" },

  // === 53: TUNEL ===
  { "Tunel vede hluboko pod řeku. Na konci je obrovská podmořská komora. "
    "Uprostřed trůní vodní bába na trůnu z kostí!",
    49, 50, "Zaútočit", "Vyjednávat" },

  // === 54: DNo ===
  { "Na dně řeky najdeš potopený poklad – truhlu plnou zlaťáků! "
    "Ale je příliš těžká. Otevřeš ji pod vodou nebo ji necháš?",
    93, 94, "Otevřít", "Nechat" },

  // === 55: NA HLADINU ===
  { "Vyplaveš na hladinu. Utopenci tě sledují ale nemohou na souš. "
    "Jsi v bezpečí. Ale zakázka není splněná – vodní bába žije. "
    "Vrátíš se do vody nebo najdeš jinou cestu?",
    6, 5, "Zpět do vody", "Jít po břehu" },

  // === 56: BOJOVAT DÁL ===
  { "Bojuješ jako lev! Dva utopci padnou pod tvým mečem. "
    "Třetí prchá – pluje do jeskyně. Pronásleduješ ho?",
    29, 55, "Pronásledovat", "Na hladinu" },

  // === 57: OTOČIT SE ===
  { "Otočíš se a probodneš utopce. Ale ten první tě znovu chytí! "
    "Jsi v kleštích. Použiješ Aard pod vodou?",
    95, 55, "Aard pod vodou", "Vyškubnout se" },

  // === 58: PLAVAT PRYČ ===
  { "Pluješ co nejrychleji. Utopenci jsou pomalejší. "
    "Dostaneš se do jeskyně. Jsi poškrábaný ale živý.",
    29, 30, "Prozkoumat jeskyni", "Odpočinout" },

  // === 59: DRAHOKAM ===
  { "Vytáhneš drahokam. Lebka se rozpadne a jeskyně se zatřese! "
    "Vodní bába řve někde v hlubinách – zničil jsi její zdroj moci! "
    "Utíkáš ven. Jeskyně se hroutí!",
    96, 97, "Běžet rychle", "Zachránit holčičku" },

  // === 60: ZNIČIT OLTÁŘ ===
  { "Rozsekáš oltář mečem. Kosti a lebky létají. "
    "Z trosek vyletí přízrak a řve. Ale pak zmizí. Oltář je zničený. "
    "Vodní bába ztratila moc. Uslyšíš ji řvát v dáli.",
    96, 98, "Jít zabít bábu", "Odejít" },

  // === 61: ZÁLOHA NA BÁBU ===
  { "Vyskočíš ze zálohy! Meč zasáhne vodní bábu do zad! "
    "Ale je tvrdší než jsi čekal – sotva ji to škrábne. Otočí se na tebe.",
    85, 86, "Uhnout", "Igni" },

  // === 62: ČEKAT NA BÁBU ===
  { "Čekáš. Vodní bába si sedne a začne zpívat. "
    "Utopenci se shromáždí kolem ní. Je jich tucet! "
    "Tohle je její armáda. Zaútočíš teď nebo odejdeš pro posily?",
    99, 98, "Zaútočit na všechny", "Odejít pro posily" },

  // === 63: PROHLEDAT DOUPĚ ===
  { "V doupěti najdeš 150 zlaťáků, stříbrný prsten a starou mapu. "
    "Slušný úlovek! Vracíš se do hostince jako vítěz.",
    39, 64, "Prozkoumat mapu", "Do hostince" },

  // === 64: JÍT PRO ODMĚNU ===
  { "Hostinský ti dá 100 zlaťáků a džbán medoviny. "
    "Sedíš u krbu a odpočíváš. Další den, další příšera. "
    "Ale dnes si zasloužíš klid. KONEC – Zaklínačská rutina.",
    -1, -1, "", "" },

  // === 65: SVĚTLO NA HŘBITOVĚ ===
  { "Za světlem najdeš čarodějku! Provádí nekromantský rituál. "
    "Chce oživit mrtvého šlechtice. Zastavíš ji nebo jí pomůžeš?",
    100, 101, "Zastavit", "Pomoci" },

  // === 66: ODEJÍT ZE HŘBITOVA ===
  { "Odcházíš se svou odměnou. Někdy je lepší se neplést do věcí. "
    "KONEC – Moudrý zaklínač.",
    -1, -1, "", "" },

  // === 67: DOKONČIT GHÚLA NA KŘÍŽI ===
  { "Probodneš ghúla srdcem. Je po něm. Zaklínač nezná slitování s netvory.",
    63, 64, "Prohledat okolí", "Jít pro odměnu" },

  // === 68: NECHAT GHÚLA ===
  { "Necháš ghúla napíchnutého. Bude trpět hodiny. "
    "To není zaklínačský způsob. Vesemir by nebyl hrdý. "
    "Ale aspoň žiješ. KONEC – Krutý zaklínač.",
    -1, -1, "", "" },

  // === 69: PODZEMÍ POD HŘBITOVEM ===
  { "Pod hřbitovem je katakomba! Stará elfí architektura. "
    "Najdeš starověký meč s runami a truhlu s knihami. "
    "Cenný nález! Vezmeš meč nebo knihy?",
    102, 103, "Runový meč", "Staré knihy" },

  // === 70: NECHAT PODZEMÍ ===
  { "Nejdeš tam. Kdo ví co je dole. Máš dost na dnešek.",
    63, 64, "Prohledat okolí", "Jít pro odměnu" },

  // === 71: ÚDER DO SRDCE ===
  { "Meč projde ghúlovým srdcem. Bestie padá. Perfektní zásah.",
    63, 64, "Prohledat okolí", "Jít pro odměnu" },

  // === 72: ÚDER DO HLAVY ===
  { "Rozsekneš ghúlovi hlavu. Drsné ale účinné.",
    63, 64, "Prohledat okolí", "Jít pro odměnu" },

  // === 73: UHASIT OHEŇ ===
  { "Znak Aard uhasí plameny. Hřbitov je zachráněn. "
    "Hrobník ti děkuje a dá ti bonus – 50 zlaťáků navíc!",
    63, 64, "Prohledat doupě", "Jít pro odměnu" },

  // === 74: UTÉCT OD OHNĚ ===
  { "Utíkáš. Za tebou hoří stromy a náhrobky. "
    "Hrobník bude zuřit. Ale ty žiješ.",
    64, 64, "Jít pro odměnu", "Rychle pryč" },

  // === 75: HLEDAT BYLINY ===
  { "Najdeš celandin a verbenu. Uvaříš léčivý odvar. "
    "Rána se zahojí. Můžeš pokračovat.",
    39, 64, "Prozkoumat doupě", "Jít pro odměnu" },

  // === 76: JÍT DO MĚSTA ===
  { "Felčar ve Vizimě tě ošetří za 20 zlaťáků. "
    "Bolí to ale přežiješ. Zaklínačský život.",
    64, 64, "Odpočinout", "Další zakázka" },

  // === 77: VLKODLAK ===
  { "Přijmeš zakázku na vlkodlaka. Je prý v lese za městem. "
    "Připravíš si olej na prokleté nebo půjdeš bez přípravy?",
    104, 105, "Připravit olej", "Jít hned" },

  // === 78: ODPOČINEK ===
  { "Zasloužíš si odpočinek. Koupíš si horkou koupel a dobré jídlo. "
    "Ráno se vydáš na cestu. Kam půjdeš? "
    "KONEC – Odpočatý zaklínač.",
    -1, -1, "", "" },

  // === 79: ELFÍ VĚŽ ===
  { "Elfí věž je nádherná i po staletích. Uvnitř najdeš elfí brnění "
    "a knihu zaklínadel. Neuvěřitelný nález! "
    "KONEC – Zaklínač archeolog. Našel jsi poklad!",
    -1, -1, "", "" },

  // === 80: PRODAT MAPU ===
  { "Prodáš mapu kupci za 80 zlaťáků. Slušný výdělek za kus papíru. "
    "Ale co když tam opravdu byl poklad? To se už nedozvíš. "
    "KONEC – Praktický zaklínač.",
    -1, -1, "", "" },

  // === 81: HLEDAT MAJITELE PRSTENU ===
  { "Najdeš vdovu, které prsten patřil. Pláče štěstím a dá ti "
    "jako odměnu stříbrný náhrdelník – má magickou ochranu! "
    "KONEC – Zaklínač se srdcem. Dobrý skutek.",
    -1, -1, "", "" },

  // === 82: NECHAT SI PRSTEN ===
  { "Prsten je zlatý a cenný. Prodáš ho za 150 zlaťáků. "
    "Nikdo se nic nedozví. Zaklínač musí jíst. "
    "KONEC – Pragmatický zaklínač.",
    -1, -1, "", "" },

  // === 83: PROSEKAT ZEĎ ===
  { "Proseká se tenkou stěnou. Za ní je vodní bába! "
    "Je překvapená – nemá čas reagovat. Teď nebo nikdy!",
    85, 86, "Zaútočit mečem", "Igni" },

  // === 84: OBEJÍT ===
  { "Obejdeš zeď a najdeš zadní vchod. Vodní bába sedí zády k tobě. "
    "Můžeš ji probodnout zezadu nebo jí hodit bombu.",
    106, 107, "Probodnout", "Bomba" },

  // === 85: UHNOUT BÁBĚ ===
  { "Uhneš vodnímu proudu. Bába je pomalá na souši. "
    "Obíháš ji a sekáš. Po třech zásazích padá. "
    "KONEC – Zaklínač lovec příšer. Vodní bába je mrtvá!",
    -1, -1, "", "" },

  // === 86: QUEN/IGNI PROTI BÁBĚ ===
  { "Oheň znaku Igni spaluje vodní bábu! Její vodní tělo se vaří. "
    "Řve a smršťuje se. Jeden finální sek a je po ní. "
    "KONEC – Zaklínač elementarista. Oheň porazil vodu!",
    -1, -1, "", "" },

  // === 87: DOHODA S BÁBOU ===
  { "Přijmeš dohodu. Bába přestane zabíjet, ale ty víš že nestvůrám "
    "nelze věřit. Vrátíš se a řekneš vesničanům ať jsou opatrní. "
    "KONEC – Diplomatický zaklínač. Ale jak dlouho dohoda vydrží?",
    -1, -1, "", "" },

  // === 88: ÚTOK PO ODMÍTNUTÍ ===
  { "Odmítneš a zaútočíš! Bába je zuřivá. Boj je těžký – "
    "vodní proud, drápy, utopenci. Ale nakonec ji porazíš. "
    "KONEC – Zaklínač bez kompromisů!",
    -1, -1, "", "" },

  // === 89: VĚŘIT HOLČIČCE ===
  { "Vezmeš holčičku za ruku a vedeš ji ven. "
    "Najednou se promění ve vodní bábu! Byla to past! "
    "Ale máš reflexy zaklínače – uskočíš a tasíš meč.",
    49, 85, "Zaútočit", "Uhnout a bojovat" },

  // === 90: NEVĚŘIT - PAST ===
  { "Správný instinkt! Holčička se promění v utopce. "
    "Ale jsi připravený. Jeden sek a je po něm. Jdeš dál.",
    24, 23, "Široká chodba", "Úzká chodba" },

  // === 91: STUDOVAT KRESBY ===
  { "Kresby ukazují, že vodní bába čerpá moc z oltáře v jeskyni. "
    "Znič oltář a ztratí sílu! Cenná informace.",
    29, 24, "Najít oltář", "Najít bábu" },

  // === 92: JÍT DÁL V JESKYNI ===
  { "Jdeš dál a narazíš na vodní bábu přímo! "
    "Nemáš výhodu překvapení ale jsi připravený.",
    49, 50, "Zaútočit", "Vyjednávat" },

  // === 93: OTEVŘÍT TRUHLU ===
  { "V truhle je 500 zlaťáků a magický amulet! "
    "S amueletem můžeš dýchat pod vodou napořád. Skvělý nález! "
    "KONEC – Zaklínač pokladů!",
    -1, -1, "", "" },

  // === 94: NECHAT TRUHLU ===
  { "Necháš truhlu – je příliš těžká. Možná se vrátíš s lodí. "
    "Zatím máš zakázku na vodní bábu.",
    29, 5, "Do jeskyně", "Na břeh" },

  // === 95: AARD POD VODOU ===
  { "Aard pod vodou vytvoří tlakovou vlnu! Utopci odlétají. "
    "Geniální tah. Cesta je volná.",
    29, 55, "Do jeskyně", "Na hladinu" },

  // === 96: BĚŽET Z JESKYNĚ ===
  { "Běžíš co nejrychleji. Kameny padají kolem tebe. "
    "Vyběhneš ven v poslední chvíli! Jeskyně se zhroutí. "
    "Vodní bába je pohřbená. KONEC – Zaklínač ničitel!",
    -1, -1, "", "" },

  // === 97: ZACHRÁNIT HOLČIČKU ===
  { "Chytíš holčičku (tentokrát je pravá!) a běžíš. "
    "Kameny padají ale vy utečete! Holčička je zachráněná. "
    "Její otec ti dá 300 zlaťáků. KONEC – Zaklínač hrdina!",
    -1, -1, "", "" },

  // === 98: ODEJÍT / PRO POSILY ===
  { "Odcházíš z jeskyně. Některé boje nejsou za to. "
    "Řekneš vesničanům ať se řece vyhýbají. "
    "KONEC – Opatrný zaklínač. Žiješ a to se počítá.",
    -1, -1, "", "" },

  // === 99: ÚTOK NA ARMÁDU UTONPCŮ ===
  { "Šílenství! Vrháš se na vodní bábu a tucet utonpců. "
    "Igni, Aard, meč, bomby – všechno! Boj trvá hodinu. "
    "Ale zaklínač z Rivie zvítězí! KONEC – Legendární zaklínač!",
    -1, -1, "", "" },

  // === 100: ZASTAVIT ČARODĚJKU ===
  { "Zastavíš nekromantku. Bojuje, ale tvůj stříbrný meč "
    "prolomí její štít. Přizná se – šlechtic byl její milenec. "
    "Předáš ji stráži nebo ji necháš jít?",
    108, 109, "Předat stráži", "Nechat jít" },

  // === 101: POMOCI ČARODĚJCE ===
  { "Pomůžeš s rituálem. Šlechtic ožije... ale jako ghúl! "
    "Čarodějka řve. Musíš ho zabít znovu. Smutný konec. "
    "KONEC – Zaklínač a následky špatných rozhodnutí.",
    -1, -1, "", "" },

  // === 102: RUNOVÝ MEČ ===
  { "Runový meč je nádherný. Elfí ocel s magickými runami. "
    "Je silnější než cokoliv cos měl. Skvělý nález! "
    "KONEC – Zaklínač s legendárním mečem!",
    -1, -1, "", "" },

  // === 103: STARÉ KNIHY ===
  { "Knihy obsahují zapomenutá zaklínadla a receptury. "
    "S těmito znalostmi budeš nejmocnější zaklínač na kontinentu! "
    "KONEC – Zaklínač učenec!",
    -1, -1, "", "" },

  // === 104: S OLEJEM NA VLKODLAKA ===
  { "Připravíš olej na prokleté. V lese najdeš vlkodlaka. "
    "S olejem je boj snadný – tři seky a leží. "
    "Ale pak se promění zpět v člověka... je to kovář z vesnice! "
    "KONEC – Zaklínačův paradox. Někdy příšera bývala člověkem.",
    -1, -1, "", "" },

  // === 105: BEZ PŘÍPRAVY NA VLKODLAKA ===
  { "Jdeš bez přípravy. Vlkodlak je rychlejší a silnější než čekáš. "
    "Boj je brutální. Nakonec ho porazíš, ale máš těžká zranění. "
    "KONEC – Přeživší zaklínač. Příště se lépe připrav!",
    -1, -1, "", "" },

  // === 106: PROBODNOUT BÁBU ZEZADU ===
  { "Stříbrný meč projde vodní bábou skrz! Řve a rozpadá se na vodu. "
    "Profesionální zásah ze zálohy. "
    "KONEC – Zaklínač zabiják. Tichý a smrtící!",
    -1, -1, "", "" },

  // === 107: BOMBA NA BÁBU ===
  { "Bomba Grapeshot exploduje! Vodní bába je roztrhaná na kusy. "
    "Jeskyně se otřásá ale drží. Zakázka splněna. "
    "KONEC – Zaklínač demoličář!",
    -1, -1, "", "" },

  // === 108: PŘEDAT ČARODĚJKU ===
  { "Předáš nekromantku strážím. Dostaneš 200 zlaťáků odměnu. "
    "Spravedlnost je spravedlnost. "
    "KONEC – Zaklínač a zákon. Správná volba.",
    -1, -1, "", "" },

  // === 109: NECHAT ČARODĚJKU JÍT ===
  { "Necháš ji jít. Někdy je lidský soucit důležitější než zákon. "
    "Odchází se slzami v očích a děkuje ti. "
    "KONEC – Zaklínač se srdcem. Ne vše je černobílé.",
    -1, -1, "", "" },
};

const int gamebookCount = 110;

#endif