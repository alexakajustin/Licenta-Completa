# Capitolul 2: Stadiul cunoașterii

## 2.1 Analiza pieței

### 2.1.1 Dimensiunea pieței

Piața globală a instrumentelor de dezvoltare a jocurilor video (game development tools) a fost evaluată la aproximativ 1,67 miliarde USD în anul 2024, cu o rată de creștere anuală compusă (CAGR) estimată între 10% și 13% pentru intervalul 2024–2030 (Global Growth Insights, 2024; Market Growth Reports, 2025). Această dinamică este alimentată de cererea tot mai mare pentru conținut 3D de înaltă fidelitate, nu doar în industria jocurilor video, ci și în domenii conexe precum vizualizarea arhitecturală, realitatea virtuală (VR), realitatea augmentată (AR) și producția cinematografică.

Segmentul specific al generării procedurale de conținut (PCG — Procedural Content Generation) nu dispune încă de evaluări de piață dedicate din partea firmelor de consultanță, însă relevanța sa poate fi dedusă indirect din adoptarea pe scară largă a motoarelor care integrează astfel de capabilități. Conform raportului GDC State of the Game Industry (2024), peste 60% dintre dezvoltatorii de jocuri utilizează cel puțin o formă de generare procedurală în proiectele lor, fie că este vorba de generarea terenurilor, plasarea vegetației sau crearea de structuri arhitecturale. Această statistică evidențiază o tranziție de la metodele tradiționale de modelare manuală către fluxuri de lucru automatizate, bazate pe algoritmi.

Un factor determinant al creșterii pieței este explozia cererii pentru lumi virtuale de tip open-world. Titluri precum „The Elder Scrolls" (Bethesda), „No Man's Sky" (Hello Games) sau „Minecraft" (Mojang Studios) au demonstrat valoarea comercială a universurilor generate procedural, stabilind așteptări ridicate din partea consumatorilor. Conform raportului Newzoo Global Games Market (2024), industria jocurilor video a generat venituri globale de aproximativ 187,7 miliarde USD, iar segmentul PC și console — cele mai relevante pentru aplicațiile PCG — a reprezentat aproximativ 40% din această valoare.

### 2.1.2 Tendințe și previziuni

Evoluția pieței instrumentelor de generare procedurală este puternic influențată de trei tendințe majore care redefinesc paradigmele de producție digitală.

Prima tendință semnificativă este integrarea inteligenței artificiale (AI) în pipeline-urile de creație. Modelele generative bazate pe rețele neuronale, precum cele de tip difuzie sau rețele adversariale generative (GAN), sunt tot mai frecvent utilizate pentru generarea de texturi, materiale și chiar geometrii 3D (Karras et al., 2020). Această convergență între AI și PCG deschide orizonturi noi, însă ridică și provocări legate de controlabilitate și determinism — domenii în care abordările algoritmice clasice, precum cele implementate în RAMY, oferă un avantaj competitiv clar prin reproductibilitatea și predictibilitatea rezultatelor. RAMY se poziționează ca un complement al acestor tehnologii emergente, oferind o bază deterministă și extensibilă pe care pot fi ulterior integrate module bazate pe AI.

A doua tendință este democratizarea accesului la instrumente profesionale. Tendința industriei este de a migra de la modele de licențiere prohibitive către modele freemium sau open-source (Kings Research, 2025). Exemple relevante includ adoptarea Blender ca standard de facto pentru modelarea 3D open-source, precum și decizia Epic Games de a elimina redevențele pentru venituri sub un milion de dolari pentru Unreal Engine 5. Această tendință validează modelul de business propus de RAMY, care se bazează pe un nucleu open-source gratuit, cu posibilitatea de a oferi module avansate prin licențiere.

A treia tendință este extinderea aplicațiilor 3D în afara industriei de gaming. Motoarele de randare în timp real sunt adoptate cu rapiditate în sectoare precum arhitectura, designul industrial, simularea autonomă pentru vehicule și antrenarea sistemelor de inteligență artificială prin date sintetice (NVIDIA, 2023). Această diversificare a cazurilor de utilizare amplifică cererea pentru instrumente modulare și extensibile de generare procedurală, care nu sunt limitate la un singur tip de activ grafic.

### 2.1.3 Concurența

Peisajul competitiv al instrumentelor de generare procedurală poate fi segmentat în trei categorii principale: soluții dedicate de generare a terenurilor, motoare grafice cu funcționalități procedurale integrate și framework-uri specializate de PCG.

În prima categorie, cele mai importante soluții sunt World Machine (dezvoltat de Stephen Schmitt) și Gaea (dezvoltat de QuadSpinner). World Machine este un instrument matur, bazat pe o arhitectură de grafuri de noduri, recunoscut pentru stabilitatea sa și capacitățile avansate de generare a terenurilor la scară largă prin sistemul de „Tiled Builds". Gaea, lansat mai recent, se diferențiază prin interfața modernă, accelerarea GPU și un set puternic de algoritmi de eroziune, fiind orientat către artiști care doresc iterație rapidă și rezultate vizuale de înaltă calitate. Ambele instrumente sunt însă limitate la generarea de terenuri, neputând gestiona alte tipuri de active procedurale precum structuri arhitecturale sau vegetație distribuită.

În a doua categorie, motoarele grafice comerciale precum Unreal Engine 5 (Epic Games) și Unity (Unity Technologies) au integrat funcționalități de generare procedurală. Unreal Engine 5 a introdus în 2023 un framework PCG dedicat, care permite crearea de reguli de distribuție a obiectelor folosind un sistem vizual de grafuri. Unity oferă plugin-uri de tip Gaia Pro (Procedural Worlds), care automatizează generarea de terenuri, biome și vegetație direct în editor. Aceste soluții au avantajul integrării native în ecosistemele respective, însă dezavantajul major rezidă în dependența totală de motorul gazdă, lipsa portabilității și costurile asociate licențierii (în cazul plugin-urilor precum Gaia Pro).

În a treia categorie, Houdini (SideFX) reprezintă standardul industrial pentru generarea procedurală avansată. Este un instrument extrem de versatil, capabil să gestioneze orice tip de activ procedural — de la terenuri la orașe întregi — prin intermediul unui limbaj de programare vizuală (VEX) și al unui sistem de operatori nodulari. Cu toate acestea, Houdini prezintă o curbă de învățare extrem de abruptă și un model de licențiere costisitor (licența comercială Houdini FX depășește 4.000 USD anual), făcându-l inaccesibil pentru dezvoltatorii independenți și pentru studiourile mici.

## 2.2 Soluții existente

### 2.2.1 Prezentarea soluțiilor similare

**World Machine** este unul dintre cele mai longevive instrumente de generare procedurală a terenurilor, fiind utilizat în producții AAA precum „Horizon Zero Dawn" (Guerrilla Games) și „Far Cry" (Ubisoft). Funcționează pe baza unui graf de noduri în care operatorii de zgomot, eroziune și filtrare sunt conectați secvențial. Punctul forte al World Machine este sistemul de „Tiled Builds", care permite generarea de terenuri masive, segmentate în plăci, fără a depăși limitele memoriei. Limitarea principală este aria de aplicabilitate restrânsă: instrumentul generează exclusiv hărți de înălțime (heightmaps) și texturi asociate, fără capacitate de a gestiona obiecte 3D sau structuri arhitecturale.

**Gaea (QuadSpinner)** reprezintă o evoluție modernă a conceptului de generator de terenuri. Accelerat GPU, Gaea oferă o previzualizare în timp real a modificărilor, cu un set impresionant de noduri de eroziune care produc rezultate fotorealiste. Interfața sa intuitivă și fluxul de lucru orientat către artiști l-au transformat rapid într-o alternativă populară la World Machine. Cu toate acestea, Gaea rămâne un instrument specializat pe terenuri, iar documentația sa a fost frecvent criticată de comunitate ca fiind insuficientă.

**Houdini (SideFX)** se diferențiază fundamental de celelalte soluții prin versatilitatea sa. Folosind paradigma de programare vizuală bazată pe noduri și limbajul VEX, Houdini poate genera orice tip de conținut procedural: de la terenuri și efecte vizuale la orașe procedurale și simulări fizice complexe. Integrarea cu motoare de jocuri se realizează prin Houdini Engine, un plugin disponibil pentru Unreal Engine și Unity. Dezavantajele sale sunt curba de învățare extrem de abruptă, costul ridicat al licenței și overhead-ul unui instrument generalist care nu este optimizat pentru scenarii specifice.

**Unreal Engine PCG Framework** este un sistem relativ recent (introdus în versiunea 5.2), care permite dezvoltatorilor să creeze reguli de distribuție a obiectelor folosind un editor vizual de grafuri. Deși puternic, acest framework este limitat la ecosistemul Unreal Engine și necesită cunoștințe avansate ale motorului pentru a fi utilizat eficient.

**Gaia Pro (Unity)** este un plugin comercial pentru Unity care automatizează generarea de terenuri, plasarea vegetației și crearea bioclimelor. Oferă un flux de lucru integrat direct în editorul Unity, dar este complet dependent de acest motor și prezintă limitări în ceea ce privește extinderea către tipuri noi de generatoare.

### 2.2.2 Analiza SWOT a soluțiilor existente

Tabelul 1 prezintă o sinteză a analizei SWOT comparative a principalelor soluții de generare procedurală existente pe piață.

**Tabelul 1 — Analiza SWOT a soluțiilor existente de generare procedurală**

| Criteriu | World Machine | Gaea | Houdini | UE5 PCG | Gaia Pro |
|---|---|---|---|---|---|
| **Puncte tari** | Stabilitate, Tiled Builds, maturitate | GPU-accelerat, eroziune excelentă, UI modern | Versatilitate totală, VEX, integrare pipeline | Integrat nativ în UE5, gratuit | Integrat în Unity, ușor de folosit |
| **Puncte slabe** | Doar terenuri, UI învechit | Doar terenuri, documentație slabă | Cost ridicat, curbă de învățare abruptă | Doar în UE5, relativ nou | Doar în Unity, extensibilitate limitată |
| **Oportunități** | Export multi-format | Expansiune către vegetație | AI-integration | Ecosistem UE vast | Piața Unity indie |
| **Amenințări** | Gaea ca alternativă modernă | Houdini la preț redus | Instrumente AI generative | Plugin-uri terțe | Alternative gratuite |

### 2.2.3 Lacune și oportunități

Analiza soluțiilor existente relevă trei lacune majore pe care RAMY le adresează direct:

Prima lacună este fragmentarea instrumentelor. În prezent, un artist care dorește să genereze un mediu complet (teren, vegetație, structuri) trebuie să jongleze între multiple instrumente specializate, fiecare cu propriul format de date și flux de lucru. RAMY propune un ecosistem unificat, în care generatorii de tipuri diferite comunică prin aceeași interfață (IGenerator) și partajează același flux de date.

A doua lacună este dependența de ecosisteme închise. Soluțiile precum Gaia Pro sau UE5 PCG sunt legate iremediabil de motorul gazdă, limitând portabilitatea și libertatea creativă. RAMY, fiind construit ca un motor independent în C++/OpenGL, elimină această dependență, oferind control total asupra codului sursă și posibilitatea de integrare în orice pipeline de producție.

A treia lacună este bariera economică. Instrumentele profesionale precum Houdini impun costuri de licențiere prohibitive pentru dezvoltatorii independenți. RAMY adoptă o filosofie open-source pentru nucleul său, democratizând accesul la tehnologia de generare procedurală.

## 2.3 Alegerea soluției optimale

### 2.3.1 Criterii de selecție

Selecția soluției optimale s-a realizat pe baza unui set de cinci criterii ponderate, definite în funcție de obiectivele tehnice și de business ale proiectului:

1. **Modularitate și extensibilitate** (pondere 30%) — capacitatea de a adăuga noi tipuri de generatoare fără modificarea nucleului;
2. **Cost și accesibilitate** (pondere 25%) — modelul de licențiere și bariera de intrare;
3. **Performanță în timp real** (pondere 20%) — capacitatea de vizualizare imediată a rezultatelor;
4. **Independență de platformă** (pondere 15%) — lipsa dependenței de un motor grafic specific;
5. **Control la nivel scăzut** (pondere 10%) — acces direct la resurse hardware și memorie video.

### 2.3.2 Justificarea alegerii

Tabelul 2 prezintă evaluarea comparativă a soluțiilor pe baza criteriilor definite, folosind o scală de la 1 (slab) la 5 (excelent).

**Tabelul 2 — Evaluare comparativă a soluțiilor pe baza criteriilor de selecție**

| Criteriu (pondere) | World Machine | Gaea | Houdini | UE5 PCG | Gaia Pro | RAMY |
|---|---|---|---|---|---|---|
| Modularitate (30%) | 2 | 2 | 5 | 3 | 2 | 5 |
| Cost (25%) | 3 | 3 | 1 | 4 | 2 | 5 |
| Performanță RT (20%) | 2 | 4 | 3 | 5 | 4 | 4 |
| Independență (15%) | 4 | 4 | 4 | 1 | 1 | 5 |
| Control scăzut (10%) | 2 | 2 | 4 | 2 | 1 | 5 |
| **Scor ponderat** | **2,45** | **2,85** | **3,35** | **3,05** | **2,25** | **4,85** |

Analiza comparativă evidențiază faptul că dezvoltarea unui motor independent (RAMY) obține cel mai ridicat scor ponderat (4,85 din 5), depășind semnificativ soluțiile existente. Avantajul principal rezidă în combinația unică de modularitate maximă (prin interfața IGenerator), cost zero pentru nucleu, independență completă de orice motor comercial și control total la nivel scăzut prin C++ și OpenGL.

Alegerea limbajului C++ a fost motivată de necesitatea controlului direct asupra memoriei și performanței, eliminând overhead-ul unui garbage collector sau al unui runtime interpretat. API-ul OpenGL 3.3 a fost preferat față de Vulkan datorită balanței optime între puterea expresivă și complexitatea de implementare, OpenGL oferind un nivel de abstractizare suficient pentru un proiect de cercetare fără a impune complexitatea excesivă a sincronizării explicite cerute de Vulkan (Sellers et al., 2016).

Decizia de a nu utiliza un motor comercial existent (Unreal Engine sau Unity) a fost fundamentată pe trei argumente: transparența totală a codului (esențială pentru scopuri didactice și de cercetare), eliminarea dependenței de ecosisteme proprietare și posibilitatea de a optimiza fiecare componentă pentru scenariul specific al generării procedurale, fără a transporta supraîncărcarea unui motor generalist.
