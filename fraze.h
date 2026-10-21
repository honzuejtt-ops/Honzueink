#ifndef FRAZE_H
#define FRAZE_H

struct Fraze {
  const char* cz;
  const char* en;
};

// ===== NAKUPOVÁNÍ =====
const Fraze frazeNakup[] PROGMEM = {
  {"Kolik to stojí?","How much does it cost?"},
  {"Mohu platit kartou?","Can I pay by card?"},
  {"Kde je nejbližší obchod?","Where is the nearest shop?"},
  {"Máte to ve větší velikosti?","Do you have it in a bigger size?"},
  {"Máte to v jiné barvě?","Do you have it in another color?"},
  {"Mohu si to vyzkoušet?","Can I try it on?"},
  {"Kde jsou šatny?","Where are the fitting rooms?"},
  {"Je to v akci?","Is it on sale?"},
  {"Máte slevu?","Do you have a discount?"},
  {"Potřebuji účtenku.","I need a receipt."},
  {"Mohu to vrátit?","Can I return it?"},
  {"Toto je příliš drahé.","This is too expensive."},
  {"Máte něco levnějšího?","Do you have something cheaper?"},
  {"Hledám dárek.","I'm looking for a gift."},
  {"Kde najdu pečivo?","Where can I find bread?"},
  {"Kde je pokladna?","Where is the checkout?"},
  {"Máte igelitovou tašku?","Do you have a plastic bag?"},
  {"Kolik to stojí celkem?","How much is it in total?"},
  {"Mohu dostat slevu?","Can I get a discount?"},
  {"Je tohle čerstvé?","Is this fresh?"},
  {"Máte bezlepkové výrobky?","Do you have gluten-free products?"},
  {"Kde je oddělení ovoce?","Where is the fruit section?"},
  {"Přijímáte hotovost?","Do you accept cash?"},
  {"Chci reklamovat.","I want to make a complaint."},
  {"To je všechno, děkuji.","That's all, thank you."},
  {"Dejte mi prosím dva.","Give me two, please."},
  {"Máte otevřeno v neděli?","Are you open on Sunday?"},
  {"V kolik zavíráte?","What time do you close?"},
  {"Hledám supermarket.","I'm looking for a supermarket."},
  {"Máte věrnostní kartu?","Do you have a loyalty card?"},
};
const int frazeNakupCount = 30;

// ===== UBYTOVÁNÍ =====
const Fraze frazeUbyt[] PROGMEM = {
  {"Mám rezervaci.","I have a reservation."},
  {"Chtěl bych se ubytovat.","I would like to check in."},
  {"Na kolik nocí?","For how many nights?"},
  {"Kolik stojí pokoj na noc?","How much is a room per night?"},
  {"Máte volný pokoj?","Do you have a room available?"},
  {"Chtěl bych jednolůžkový pokoj.","I would like a single room."},
  {"Chtěl bych dvoulůžkový pokoj.","I would like a double room."},
  {"Je v ceně snídaně?","Is breakfast included?"},
  {"Kde je jídelna?","Where is the dining room?"},
  {"V kolik je snídaně?","What time is breakfast?"},
  {"Můžete mi zavolat taxi?","Can you call me a taxi?"},
  {"Kde je nejbližší restaurace?","Where is the nearest restaurant?"},
  {"Nefunguje WiFi.","The WiFi is not working."},
  {"Jaké je heslo na WiFi?","What is the WiFi password?"},
  {"Potřebuji ručník.","I need a towel."},
  {"Klimatizace nefunguje.","The air conditioning is not working."},
  {"Mohu nechat zavazadlo?","Can I leave my luggage?"},
  {"Chtěl bych se odhlásit.","I would like to check out."},
  {"Máte parkoviště?","Do you have a parking lot?"},
  {"Kde je výtah?","Where is the elevator?"},
  {"Můj pokoj je příliš hlučný.","My room is too noisy."},
  {"Mohu změnit pokoj?","Can I change rooms?"},
  {"Kde je nejbližší bankomat?","Where is the nearest ATM?"},
  {"Potřebuji prodloužit pobyt.","I need to extend my stay."},
  {"Je tu trezor?","Is there a safe?"},
  {"Chtěl bych pokoj s výhledem.","I would like a room with a view."},
  {"Kde mohu zaparkovat?","Where can I park?"},
  {"Máte prádelnu?","Do you have a laundry service?"},
  {"Kolik stojí minibar?","How much is the minibar?"},
  {"Prosím probuďte mě v 7 hodin.","Please wake me up at 7 o'clock."},
};
const int frazeUbytCount = 30;

// ===== ZDRAVÍ =====
const Fraze frazeZdravi[] PROGMEM = {
  {"Potřebuji doktora.","I need a doctor."},
  {"Kde je nemocnice?","Where is the hospital?"},
  {"Kde je lékárna?","Where is the pharmacy?"},
  {"Bolí mě hlava.","I have a headache."},
  {"Bolí mě břicho.","I have a stomach ache."},
  {"Bolí mě v krku.","I have a sore throat."},
  {"Mám horečku.","I have a fever."},
  {"Je mi špatně.","I feel sick."},
  {"Mám průjem.","I have diarrhea."},
  {"Mám alergii.","I have an allergy."},
  {"Jsem alergický na penicilin.","I'm allergic to penicillin."},
  {"Potřebuji lék proti bolesti.","I need a painkiller."},
  {"Zlomil jsem si ruku.","I broke my arm."},
  {"Pořezal jsem se.","I cut myself."},
  {"Potřebuji obvaz.","I need a bandage."},
  {"Volejte záchranku!","Call an ambulance!"},
  {"Mám cukrovku.","I have diabetes."},
  {"Mám vysoký tlak.","I have high blood pressure."},
  {"Jsem těhotná.","I am pregnant."},
  {"Potřebuji recept.","I need a prescription."},
  {"Mám astma.","I have asthma."},
  {"Nemohu dýchat.","I can't breathe."},
  {"Bolí mě zub.","I have a toothache."},
  {"Kde je zubař?","Where is the dentist?"},
  {"Omdlel jsem.","I fainted."},
  {"Mám vyrážku.","I have a rash."},
  {"Štípl mě hmyz.","I was stung by an insect."},
  {"Ztratil jsem léky.","I lost my medication."},
  {"Mám nevolnost.","I feel nauseous."},
  {"Potřebuji pomoc!","I need help!"},
};
const int frazeZdraviCount = 30;

// ===== ZÁBAVA =====
const Fraze frazeZabava[] PROGMEM = {
  {"Kde je dobrá restaurace?","Where is a good restaurant?"},
  {"Stůl pro dva, prosím.","A table for two, please."},
  {"Mohu vidět jídelní lístek?","Can I see the menu?"},
  {"Co doporučujete?","What do you recommend?"},
  {"Jsem vegetarián.","I am a vegetarian."},
  {"Účet, prosím.","The bill, please."},
  {"Bylo to výborné.","It was excellent."},
  {"Kde je nejbližší bar?","Where is the nearest bar?"},
  {"Dvě piva, prosím.","Two beers, please."},
  {"Jedno víno, prosím.","One wine, please."},
  {"Na zdraví!","Cheers!"},
  {"Kde je kino?","Where is the cinema?"},
  {"Dva lístky, prosím.","Two tickets, please."},
  {"V kolik začíná představení?","What time does the show start?"},
  {"Kde je divadlo?","Where is the theater?"},
  {"Kde je muzeum?","Where is the museum?"},
  {"Kolik stojí vstupné?","How much is the entrance fee?"},
  {"Je tu někde dobrý klub?","Is there a good club around here?"},
  {"Kde se dá tancovat?","Where can I go dancing?"},
  {"Máte živou hudbu?","Do you have live music?"},
  {"Kde je pláž?","Where is the beach?"},
  {"Půjčujete tu kola?","Do you rent bikes here?"},
  {"Kde se dá plavat?","Where can I swim?"},
  {"Je tu nějaká prohlídka?","Is there a guided tour?"},
  {"Kde je informační centrum?","Where is the information center?"},
  {"Fotit se tu smí?","Is photography allowed?"},
  {"Je to vhodné pro děti?","Is it suitable for children?"},
  {"Máte program na tento týden?","Do you have a program for this week?"},
  {"Kde koupím suvenýry?","Where can I buy souvenirs?"},
  {"Byla to skvělá zábava!","It was great fun!"},
};
const int frazeZabavaCount = 30;

#endif