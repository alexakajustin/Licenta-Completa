# Capitolul 4: Implementarea din punct de vedere business

## 4.1 Modelul de afaceri

Modelul de afaceri al motorului RAMY Procedural Engine se bazează pe o strategie de tip Freemium, adaptată specificului pieței instrumentelor de dezvoltare software. Această abordare presupune oferirea gratuită a nucleului motorului (motorul de randare, sistemul de generare și editorulNode-Based) sub o licență open-source de tip MIT, în timp ce funcționalitățile avansate și modulele specializate sunt disponibile prin intermediul unor licențe comerciale.

Structura veniturilor este organizată pe trei piloni:

**Licențe pentru module avansate:** Generatoarele specializate de înaltă complexitate (precum simulările avansate de eroziune termică, generatoarele de rețele stradale sau sistemele de creație a vegetației fotorealiste) sunt oferite ca module comerciale, cu prețuri între 15 și 50 EUR per modul. Acest model urmează exemplul unor ecosisteme similare din industrie, precum marketplace-urile Unreal Engine și Unity, care au demonstrat viabilitatea comercială a vânzării de module individuale.

**Marketplace pentru comunitate:** O platformă de tip marketplace permite dezvoltatorilor terți să creeze și să comercializeze propriile module de generare, RAMY reținând un comision de 20% din fiecare tranzacție. Acest model stimulează creșterea ecosistemului prin contribuții externe și creează un efect de rețea care amplifică valoarea platformei.

**Licențe enterprise:** Pentru studiourile de jocuri și companiile de vizualizare care necesită suport tehnic dedicat, integrare personalizată și garanții de performanță, se oferă licențe enterprise cu abonament anual, incluzând acces la toate modulele premium, suport prioritar și actualizări anticipate.

## 4.2 Strategia de marketing și vânzări

Strategia de marketing este construită pe principiul „Community-First", exploatând canalele preferate de comunitatea dezvoltatorilor și artiștilor 3D:

**Prezență open-source pe GitHub:** Publicarea codului sursă al nucleului pe GitHub sub licență MIT servește drept principal canal de achiziție a utilizatorilor. Proiectele open-source de calitate atrag organic contribuitori și utilizatori prin vizibilitate în căutări, recomandări peer-to-peer și articole tehnice. Documentația completă, exemplele de utilizare și tutorialele video sunt esențiale pentru reducerea barierei de intrare.

**Marketing de conținut:** Crearea regulată de conținut educațional (tutoriale video pe YouTube, articole tehnice pe blogul proiectului, postări pe comunități precum Reddit r/gamedev și r/proceduralgeneration) demonstrează capabilitățile motorului și atrage utilizatori potențiali. Showcases vizuale ale rezultatelor procedurale sunt deosebit de eficiente în acest domeniu, unde impactul vizual dictează interesul comunității.

**Participarea la evenimente:** Prezentarea motorului la conferințe și game jam-uri (precum Global Game Jam, Ludum Dare sau conferința GDC) oferă vizibilitate și oportunități de networking cu potențiali utilizatori și contribuitori.

**Program de ambasadori:** Identificarea și sprijinirea creatorilor de conținut (YouTuberi, streameri, autori de tutoriale) care utilizează și promovează motorul, oferindu-le acces anticipat la funcționalități noi și suport tehnic dedicat.

## 4.3 Proiecția veniturilor și costurilor

**Tabelul 6 — Proiecția financiară pe primii trei ani**

| Categorie | Anul 1 | Anul 2 | Anul 3 |
|---|---|---|---|
| **Venituri** | | | |
| Module premium | 2.000 EUR | 8.000 EUR | 20.000 EUR |
| Comision marketplace | 500 EUR | 3.000 EUR | 12.000 EUR |
| Licențe enterprise | 0 EUR | 5.000 EUR | 15.000 EUR |
| **Total venituri** | **2.500 EUR** | **16.000 EUR** | **47.000 EUR** |
| **Costuri** | | | |
| Hosting și infrastructură | 600 EUR | 1.200 EUR | 2.400 EUR |
| Marketing și promovare | 1.000 EUR | 2.000 EUR | 3.000 EUR |
| Dezvoltare (ore proprii valorificate) | 5.000 EUR | 8.000 EUR | 10.000 EUR |
| **Total costuri** | **6.600 EUR** | **11.200 EUR** | **15.400 EUR** |
| **Rezultat net** | **-4.100 EUR** | **+4.800 EUR** | **+31.600 EUR** |

Proiecția indică atingerea punctului de break-even în cursul celui de-al doilea an de operare, cu o creștere exponențială a veniturilor datorată efectului de rețea al marketplace-ului și a scalabilității inerente a modelului digital.

## 4.4 Securitate și confidențialitate

### 4.4.1 Măsuri de securizare a datelor și a accesului la sistem

Întrucât RAMY funcționează ca o aplicație desktop standalone, riscurile de securitate sunt diferite față de cele ale aplicațiilor web. Principalele măsuri implementate includ:

**Protecția proprietății intelectuale:** Modulele premium sunt distribuite ca biblioteci compilate (DLL pe Windows, .so pe Linux), protejând codul sursă al algoritmilor comerciali. Sistemul de licențiere verifică validitatea cheii de activare la pornirea aplicației prin comparare cu un hash stocat local, fără a necesita conectivitate permanentă la internet.

**Integritatea datelor:** Fișierele de scenă sunt serializate în format JSON cu o sumă de control (checksum) atașată, permițând detectarea coruperii datelor. Sistemul de undo/redo oferă o protecție suplimentară împotriva pierderilor accidentale de date.

**Conformitate GDPR:** Motorul nu colectează, stochează sau transmite date personale ale utilizatorilor. Singurele date transmise extern sunt verificările opționale de actualizare, care pot fi dezactivate din setări. Această abordare asigură conformitatea nativă cu Regulamentul General privind Protecția Datelor (GDPR — Regulamentul UE 2016/679).

## 4.5 Usability și User Experience

### 4.5.1 Testarea utilizabilității și a experienței utilizatorului

Principiile de design ale interfeței RAMY au fost ghidate de euristicile de utilizabilitate ale lui Nielsen (1994), adaptate la contextul specific al aplicațiilor DCC:

**Vizibilitatea stării sistemului:** Modificarea oricărui parametru declanșează recalcularea imediată a scenei, oferind feedback vizual instantaneu. Barra de progres afișată în timpul operațiilor intensive (precum eroziunea hidraulică la rezoluții mari) informează utilizatorul despre stadiul procesării.

**Corelarea între sistem și lumea reală:** Terminologia utilizată în interfață (frecvență, amplitudine, eroziune, scatter) corespunde vocabularului domeniului, familiar artiștilor 3D și dezvoltatorilor de jocuri.

**Controlul și libertatea utilizatorului:** Sistemul de undo/redo pe mai multe niveluri permite experimentarea liberă, fără riscul pierderii ireversibile a muncii anterioare.

**Consecvență și standarde:** Dispunerea panourilor (viewport central, inspector lateral, editor de noduri inferior) urmează convențiile stabilite de instrumente precum Blender, Houdini și Unreal Engine, minimizând curba de învățare pentru utilizatorii familiari cu aceste instrumente.

### 4.5.2 Îmbunătățiri aduse în urma feedback-ului

Pe parcursul dezvoltării, feedback-ul informal obținut de la colegi și de la membri ai comunității online a condus la următoarele îmbunătățiri:
- Adăugarea tooltip-urilor descriptive pentru fiecare parametru din Inspector;
- Implementarea unui sistem de preseturi pentru generatoarele frecvent utilizate;
- Optimizarea timpilor de răspuns prin recalculare parțială (doar nodurile afectate de o modificare sunt re-executate);
- Îmbunătățirea vizuală a grafului de noduri prin codificarea prin culori a tipurilor de conexiuni.
