#ifndef HRY_WYR_H
#define HRY_WYR_H

// ===== WOULD YOU RATHER — 60 otázek s procentuálním rozdělením =====

// Plné otázky (pro zobrazení)
const char* wyrOtazky[] = {
  "Raději létání nebo neviditelnost?",
  "Raději umět číst myšlenky nebo cestovat časem?",
  "Raději žít bez hudby nebo bez filmů?",
  "Raději mít hodně peněz nebo hodně času?",
  "Raději být nejchytřejší nebo nejkrásnější?",
  "Raději žít na pláži nebo v horách?",
  "Raději umět všechny jazyky nebo hrát na všechny nástroje?",
  "Raději nikdy nestárnout nebo nikdy nebýt nemocný?",
  "Raději být slavný nebo být bohatý?",
  "Raději ztratit všechny vzpomínky nebo nikdy nevytvořit nové?",
  "Raději žít 1000 let v minulosti nebo v budoucnosti?",
  "Raději mít teleportaci nebo zastavit čas?",
  "Raději vědět, kdy zemřeš, nebo jak zemřeš?",
  "Raději mít neomezený internet nebo neomezené jídlo zdarma?",
  "Raději neumět lhát nebo nepoznat pravdu?",
  "Raději žít bez telefonu nebo bez auta?",
  "Raději mít oči vzadu hlavy nebo uši na kolenou?",
  "Raději být pilotem nebo kapitánem ponorky?",
  "Raději vždy mluvit pravdu nebo vždy lhát?",
  "Raději žít bez knih nebo bez hudby?",
  "Raději být neviditelný jeden den nebo létat jeden den?",
  "Raději mít superpamět nebo superrychlost?",
  "Raději bydlet v zámku nebo na lodi?",
  "Raději být nejlepší ve sportu nebo v umění?",
  "Raději umět mluvit se zvířaty nebo všemi jazyky?",
  "Raději žít 200 let v chudobě nebo 50 let v luxusu?",
  "Raději vidět budoucnost nebo změnit minulost?",
  "Raději mít neomezený benzín nebo neomezená data?",
  "Raději být nejsilnější nebo nejrychlejší?",
  "Raději nikdy nespat nebo nikdy nejíst?",
  "Raději být v komedii nebo v akčním filmu?",
  "Raději ztratit peněženku nebo telefon?",
  "Raději mít jeden den navíc v týdnu nebo hodinu navíc denně?",
  "Raději žít ve světě HP nebo Pána prstenů?",
  "Raději mít perfektní zrak nebo perfektní sluch?",
  "Raději být slavným sportovcem nebo slavným vědcem?",
  "Raději jíst jen sladké nebo jen slané?",
  "Raději vždy chodit pěšky nebo nikdy nechodit sám?",
  "Raději zapomenout heslo nebo zapomenout jméno?",
  "Raději žít na ostrově nebo na horské chatě?",
  "Raději být kouzelníkem nebo superhrdinou?",
  "Raději cestovat po světě nebo prozkoumat vesmír?",
  "Raději znát den své smrti nebo žít v nejistotě?",
  "Raději neumět plavat nebo neumět jezdit na kole?",
  "Raději mít robotického psa nebo živého papouška?",
  "Raději žít ve světě bez válek nebo bez nemocí?",
  "Raději mít schopnost léčit nebo schopnost ničit?",
  "Raději být velká ryba v malém rybníce nebo malá v oceánu?",
  "Raději vyhrát loterii nebo žít o 100 let déle?",
  "Raději mít dokonalou paměť nebo zapomenout špatné vzpomínky?",
  "Raději žít bez zrcadel nebo bez fotek?",
  "Raději mít jeden skutečný přítel nebo sto známých?",
  "Raději být alergický na slunce nebo na vodu?",
  "Raději mít nekonečnou trpělivost nebo nekonečnou energii?",
  "Raději žít v domě plném koček nebo psů?",
  "Raději dýchat pod vodou nebo přežít ve vesmíru?",
  "Raději mít každý den krásné počasí nebo skvělé jídlo?",
  "Raději umět ovládat oheň nebo vodu?",
  "Raději mít neviditelný plášť nebo létající koberec?",
  "Raději žít v době dinosaurů nebo v budoucnosti s roboty?"
};

// Možnost A pro každou otázku
const char* wyrMoznostiA[] = {
  "Létání", "Číst myšlenky", "Bez hudby", "Peníze", "Nejchytřejší",
  "Na pláži", "Jazyky", "Nestárnout", "Slavný", "Ztratit staré",
  "V minulosti", "Teleportace", "Kdy", "Internet", "Neumět lhát",
  "Bez telefonu", "Oči vzadu", "Pilot", "Pravdu", "Bez knih",
  "Neviditelný", "Superpamět", "Zámek", "Sport", "Se zvířaty",
  "200 let", "Vidět budoucnost", "Benzín", "Nejsilnější", "Nikdy nespat",
  "Komedie", "Peněženku", "Den navíc", "Harry Potter", "Zrak",
  "Sportovec", "Sladké", "Pěšky", "Heslo", "Ostrov",
  "Kouzelník", "Svět", "Znát den", "Neumět plavat", "Robot",
  "Bez válek", "Léčit", "Velká v rybníce", "Loterie", "Dokonalá paměť",
  "Bez zrcadel", "Jeden přítel", "Na slunce", "Trpělivost", "Kočky",
  "Pod vodou", "Počasí", "Oheň", "Plášť", "Dinosauři"
};

// Možnost B pro každou otázku
const char* wyrMoznostiB[] = {
  "Neviditelnost", "Cestovat časem", "Bez filmů", "Čas", "Nejkrásnější",
  "V horách", "Nástroje", "Nebýt nemocný", "Bohatý", "Nikdy nové",
  "V budoucnosti", "Zastavit čas", "Jak", "Jídlo", "Nepoznat pravdu",
  "Bez auta", "Uši na kolenou", "Kapitán ponorky", "Lhát", "Bez hudby",
  "Létat", "Superrychlost", "Loď", "Umění", "Všemi jazyky",
  "50 let luxusu", "Změnit minulost", "Data", "Nejrychlejší", "Nikdy nejíst",
  "Akční", "Telefon", "Hodina denně", "Pán prstenů", "Sluch",
  "Vědec", "Slané", "Nikdy sám", "Jméno", "Horská chata",
  "Superhrdina", "Vesmír", "Nejistota", "Neumět jezdit na kole", "Papoušek",
  "Bez nemocí", "Ničit", "Malá v oceánu", "100 let", "Zapomenout špatné",
  "Bez fotek", "Sto známých", "Na vodu", "Energie", "Psi",
  "Ve vesmíru", "Jídlo", "Vodu", "Koberec", "Roboti"
};

// Procento lidí volící možnost A (B = 100 - procentA)
const int wyrProcenta[] = {
  72, 32, 27, 41, 68,
  55, 62, 58, 17, 43,
  28, 38, 63, 46, 57,
  35, 44, 71, 85, 52,
  39, 54, 67, 48, 42,
  37, 45, 40, 43, 61,
  53, 36, 48, 55, 74,
  51, 38, 57, 72, 47,
  52, 42, 31, 34, 45,
  39, 89, 54, 56, 41,
  64, 82, 67, 36, 43,
  46, 41, 38, 53, 29
};

const int wyrPocet = 60;

#endif
