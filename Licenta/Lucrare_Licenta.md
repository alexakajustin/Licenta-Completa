# Lucrare de Licență: Sistem de Generare Procedurală de Conținut 3D (RAMY Procedural Engine)

**INTRODUCERE**

Jocurile pe calculator au fost, încă de mic, o pasiune și o înclinație naturală a mea, prin simplul fapt că așa m-am născut. Pentru mine, universul digital nu este doar un domeniu de studiu, ci o parte integrantă din parcursul meu de viață. La vârsta de 11 ani, experiența jocului *Minecraft* m-a fascinat și m-a surprins într-un mod profund; a fost locul unde m-am simțit acasă, unde mi-am făcut prieteni și amintiri, dar mai presus de toate, a fost scânteia care mi-a aprins curiozitatea față de ceea ce înseamnă generarea procedurală în jocurile video.

Personal, văd conținutul generat procedural ca fiind viitorul jocurilor de tip "Open World". Deși intervenția umană rămâne necesară pentru a menține echilibrul între detaliu și scară, viziunea mea este să ofer oamenilor o platformă, un motor grafic în care să își poată crea absolut orice tip de conținut procedural. Această lucrare nu este doar un exercițiu tehnic, ci o piesă de software care vine din suflet și care încearcă să rezolve o problemă pe care am resimțit-o personal: faptul că toate instrumentele de calitate pentru generare procedurală sunt extrem de scumpe și inaccesibile multora.

Am identificat o problemă clară pe piață: lipsa efectivă a unui motor grafic nișat pe generare procedurală. Motoarele *mainstream* precum Unreal sau Unity oferă această funcționalitate doar prin plugin-uri sau asset-uri costisitoare. Mai mult, consider că există o lipsă mare de "suflet" în jocurile video din ziua de zi. Prin **RAMY Procedural Engine**, sper să aduc acest suflet înapoi, creând o comunitate interconectată unde utilizatorii să își poată împărți propriile noduri și să le pună împreună ca pe niște piese de puzzle.

Inovația pe care o propun este modularitatea totală – un aspect pe care îl consider esențial. Lucrând timp de doi ani la un joc generat procedural în Unity, am simțit pe propria piele povara căutării unor asset-uri care să se potrivească. RAMY Engine oferă toată puterea în mâinile utilizatorului, oferind acces la orice parametru, care poate fi interconectat în moduri complexe, similar cu frumusețea matematică a mulțimii lui Mandelbrot.

Am structurat această lucrare pentru a acoperi toate aspectele acestui demers:
1. **Definirea problemei**, unde explorez contextul economic și nevoile pieței;
2. **Stadiul cunoașterii**, unde analizez soluțiile existente și lacunele acestora;
3. **Proiectarea și implementarea**, unde detaliez arhitectura tehnică (shadere, noduri, randare GPU);
4. **Implementarea din punct de vedere business**, unde analizez scalabilitatea și modelul de afaceri.

Țelul meu este ca, după ani de muncă, acest motor să devină un competitor real pentru soluțiile consacrate, rămânând însă fidel rădăcinilor sale open-source și oferind oricărui om curios posibilitatea de a crea lumi care să îl surprindă chiar și pe el, creatorul lor.

---

**1. CAPITOLUL 1: Definirea problemei**

**1.1. Din punct de vedere business**
**1.1. Din punct de vedere business**

**1.1.1. Descrierea contextului economic și a nevoilor pieței**

În contextul economic actual, consider că prezența unui modul de generare procedurală în orice motor grafic modern a devenit o necesitate absolută, nu doar o opțiune. Piața jocurilor video a evoluat către producții de o scară imensă, unde așteptările jucătorilor pentru lumi vaste și detaliate pun o presiune constantă pe timpul și bugetul de dezvoltare. Din cercetările mele, am observat că marile companii din industrie au adoptat deja PCG ca standard pentru a rămâne competitive. 

Un exemplu relevant este *Electronic Arts*, care folosește algoritmi de generare procedurală în procesul de producție al jocului *Skate 4* pentru a crea rapid structuri urbane și clădiri complexe. De asemenea, succesul titlului *Sons of The Forest* se bazează pe utilizarea PCG pentru a popula mediul cu vegetație și loot, optimizând astfel fluxul de lucru. Dezvoltatorii au nevoie de aceste soluții rapide deoarece ele reduc drastic costurile de producție și permit lansarea produselor într-un timp mult mai scurt, fără a sacrifica dimensiunea universului virtual.

**1.1.2. Identificarea stakeholderilor și a intereselor lor**

RAMY Engine este un proiect complet open-source, deoarece nu urmăresc obținerea unui profit financiar imediat din ceea ce fac. Principalul meu interes este de a pune bazele unei comunități pasionate și de lungă durată, formată din artiști grafici, designeri și programatori. Vreau să le ușurez munca acestora prin oferirea unei unelte care să elimine barierele financiare impuse de asset-urile scumpe de pe piețele consacrate.

Stakeholderii acestui proiect sunt, în primul rând, dezvoltatorii independenți (indie) care dispun de resurse limitate, dar și pasionații de informatică grafică dornici să exploreze noi metode de generare. Interesul meu fundamental este să transmit pasiunea mea și altora, stârnind curiozitatea tehnică și oferind o platformă unde colaborarea și schimbul de idei să primeze în fața intereselor comerciale.

**1.1.3. Impactul problemei asupra afacerii**

Am constatat că lipsa utilizării tehnicilor de PCG duce inevitabil la o creștere exponențială a costurilor și a timpului de producție. Într-o afacere din domeniul jocurilor video, fiecare lună de întârziere se traduce în pierderi financiare masive. Totuși, consider că PCG nu trebuie să înlocuiască complet munca manuală. Lumiile generate integral algoritmic, fără intervenție umană, tind să fie vaste dar lipsite de profunzime, fiind doar "gigantice și de suprafață".

Impactul pozitiv pe care RAMY Engine îl propune asupra procesului de dezvoltare este realizarea unei "colaborări elegante" între puterea algoritmilor și detaliul uman. Generarea procedurală se ocupă de partea masivă a muncii (macro-structura, distribuția resurselor), reducând efortul financiar, în timp ce creatorul uman se poate concentra pe adăugarea acelor detalii fine care oferă identitate și "suflet" lumii. Această simbioză este cheia pentru un model de business sustenabil în industria modernă.

**1.2. Din punct de vedere informatic**
**1.2. Din punct de vedere informatic**

**1.2.1. Specificații tehnice ale problemei**

Din perspectivă tehnică, am conceput RAMY Engine ca un sistem modular, alcătuit din mai multe componente interconectate care lucrează sincron pentru a permite generarea procedurală în timp real. Arhitectura motorului este împărțită în cinci module principale:

1.  **Motorul de randare:** Utilizează API-ul OpenGL 4.6, împreună cu bibliotecile GLEW și GLFW. Am ales OpenGL deoarece, fiind prima mea experiență în dezvoltarea de aplicații grafice complexe, am avut nevoie de un standard care să ofere un echilibru între simplitate și potențial de scalabilitate, fără complexitatea excesivă a unor tehnologii precum Vulkan sau DirectX.
2.  **Sistemul de noduri:** Reprezintă nucleul creativ al aplicației. Interfața de utilizator folosește *ImGui Nodes*, însă în spate am dezvoltat un sistem sofisticat de clase orientate-obiect. Acest sistem permite o scalabilitate teoretic infinită a logicii de generare, pornind de la principii simple de programare.
3.  **Asset Management:** Un modul care permite gestionarea resurselor externe prin funcționalități de *drag and drop*, facilitând importul modelelor 3D direct în scenă.
4.  **Serializare/Deserializare:** Asigură salvarea și încărcarea stării scenei și a configurațiilor de noduri.
5.  **Modulul "Game Engine":** Include elemente esențiale precum Inspectorul de obiecte, ierarhia scenei și instrumente de manipulare a obiectelor, fiind inspirat din fluxul de lucru consacrat al motorului Unity.

**1.2.2. Constrângeri și limitări**

Cea mai mare provocare pe care am întâmpinat-o, și la care lucrez constant, este optimizarea procesului de randare. Este extrem de dificil să găsesc acel "sweet spot" între performanță și calitatea vizuală atunci când sistemul trebuie să gestioneze procedural milioane de *asset*-uri simultan. 

O altă constrângere majoră este diversitatea hardware-ului utilizatorilor. Mi-am propus ca RAMY Engine să nu fie un software dedicat exclusiv dispozitivelor high-end. Pentru a depăși această limitare, am implementat un sistem de setări grafice granulare, permițând motorului să ruleze stabil și pe configurații hardware mai modeste sau pe plăci video integrate (AMD sau NVIDIA), asigurând o compatibilitate *out-of-the-box* cu puține detecții necesare la lansare.

**1.2.3. Tehnologii relevante**

Alegerea limbajului de programare **C++** a fost influențată de două motive majore. În primul rând, C++ este standardul absolut în industria motoarelor grafice datorită vitezei de execuție și controlului asupra resurselor. În al doilea rând, alegerea are o puternică încărcătură personală; am o apreciere nostalgică pentru C++, fiind limbajul care m-a introdus în lumea programării încă din liceu. Am simțit nevoia să rămân aproape de rădăcinile mele tehnice în realizarea acestui proiect de suflet.

În ceea ce privește bibliotecile externe, am optat pentru soluții precum **ImGui** și **Assimp** datorită ușurinței de utilizare și a funcționalității lor imediate. Deși sunt conștient că există alternative mai puternice sau mai optimizate pentru anumite sarcini specifice, scopul meu principal a fost construirea unui "schelet" funcțional și complet. Am prioritizat simplitatea și eficiența implementării, dorind să am un produs stabil pe care să pot construi ulterior module mai avansate.

---

**2. CAPITOLUL 2: Stadiul cunoașterii**

**2.1. Analiza pieței**

**2.1.1. Dimensiunea pieței și contextul actual**

Piața jocurilor video a atins o maturitate tehnologică în care hyperrealismul a devenit standardul pentru producțiile de tip AAA. Totuși, observ o tendință îngrijorătoare: pe măsură ce fidelitatea grafică crește, calitatea experienței brute și "sufletul" jocurilor par să scadă. Companiile de prestigiu investesc bugete colosale în detalii vizuale minuțioase, dar neglijează adesea mecanicile care stârnesc curiozitatea și dorința de explorare. 

În acest context, dimensiunea pieței nu se mai măsoară doar în venituri, ci și în capacitatea de a menține angajamentul jucătorilor. Analizând succesul fenomenal al jocului *Minecraft* — cel mai vândut titlu din istorie — am înțeles că simplitatea vizuală pusa într-un context infinit și interconectat este mult mai valoroasă decât realismul steril. *Minecraft* a demonstrat că PCG poate crea un sentiment de "acasă" și o curiozitate nesfârșită, oferind un *game loop* în care imaginația utilizatorului este singura limită.

**2.1.2. Tendințe și previziuni**

Tendința actuală în jocurile *Open World* este utilizarea generării procedurale ca un instrument de "umplutură" (filler). PCG este folosit pentru a popula spații vaste cu vegetație sau teren, dar fără a oferi personalitate lumii virtuale. Consider că viitorul aparține acelor instrumente care vor folosi PCG nu doar pentru scară, ci pentru a genera mecanici de joc emergente și lumi cu identitate proprie. Prevăd o reîntoarcere către jocurile care prioritizează curiozitatea în fața graficii, unde utilizatorul este surprins constant de natura lucrurilor, chiar dacă regulile sunt setate de el.

**2.1.3. Concurența**

În prezent, nu există pe piață un software de sine stătător, open-source, care să se nișeze strict pe viziunea pe care o propun prin RAMY Engine. Concurența este reprezentată în principal de:
*   **Plugin-uri integrate:** Soluții precum *Gaia* sau *MapMagic* pentru Unity și noul framework PCG din Unreal Engine 5. Acestea sunt puternice, dar limitează utilizatorul la ecosistemul motorului respectiv.
*   **Software-uri specializate:** *Houdini*, *World Creator* sau *Gaea*. Deși excepționale din punct de vedere tehnic, acestea sunt adesea prohibitve ca preț și au o curbă de învățare extrem de abruptă.

**2.2. Soluții existente**

**2.2.1. Prezentarea soluțiilor similare**

*   **Houdini (SideFX):** Standardul industriei pentru generare procedurală bazată pe noduri. Este folosit în aproape toate producțiile cinematografice și jocurile AAA, dar este o soluție "greoaie" pentru un dezvoltator independent și extrem de scumpă.
*   **Gaea (QuadSpinner):** Un instrument modern, axat pe generarea de terenuri prin simulări geologice de înaltă precizie. Oferă rezultate vizuale uluitoare, dar este specializat doar pe teren, nefiind un motor grafic complet.
*   **Gaia (Procedural Worlds):** Un sistem de tip "all-in-one" pentru Unity care facilitează crearea scenelor. Deși ușurează munca, este adesea perceput ca o "cutie neagră" (black box), unde intervenția profundă asupra algoritmilor este dificilă.

**2.2.2. Analiza critică (SWOT)**

Realizarea unei analize SWOT pentru soluțiile existente pe piață necesită o evaluare care depășește simpla listare a funcționalităților tehnice. În cercetarea mea, m-am concentrat pe modul în care aceste instrumente răspund nevoilor de flexibilitate și accesibilitate ale unui creator individual. Am observat o prăpastie tehnologică și filozofică între ceea ce oferă industria AAA și ceea ce are nevoie comunitatea indie.

Majoritatea soluțiilor consacrate funcționează pe o filosofie de tip "black box" (cutie neagră). Deși interfețele sunt moderne, algoritmii fundamentali sunt ascunși în spatele unor straturi de abstractizare care, deși oferă stabilitate, elimină controlul creativ granular. De exemplu, un artist care dorește să modifice modul în care un algoritm de eroziune hidraulică interacționează cu densitatea unui strat geologic se lovește adesea de imposibilitatea de a accesa sau modifica acea bucată de cod. Această rigiditate transformă procesul de generare dintr-o colaborare între om și mașină într-o simplă selecție de parametri predefiniți.

Din punct de vedere economic, am analizat bariera de intrare pe care o reprezintă costurile de licențiere. Un set de unelte profesionale (Houdini + Gaea + engine-ul gazdă) poate depăși bugetul total al unui proiect de mici dimensiuni. Această barieră nu este doar financiară, ci și educațională; curba de învățare pentru aceste software-uri este extrem de abruptă, necesitând luni de studiu specializat. În acest peisaj, am evaluat punctele forte, punctele slabe, oportunitățile și amenințările actuale:

*   **Puncte forte (Strengths):** Soluțiile comerciale beneficiază de zeci de ani de cercetare și dezvoltare (R&D) și de echipe masive de ingineri. Acestea oferă un grad de șlefuire (polish), documentație exhaustivă și o stabilitate pe care un proiect individual o poate atinge greu. De asemenea, integrarea cu alte pipeline-uri de producție este bine pusă la punct, fiind optimizate pentru fluxuri de lucru profesionale.
*   **Puncte slabe (Weaknesses):** Principala slăbiciune este lipsa cruntă de modularitate la nivel de cod pentru utilizatorul final. Majoritatea acestor instrumente sunt extrem de dependente de hardware high-end și tind să fie optimizate pentru randare offline, nu pentru interactivitate în timp real. Costurile ridicate și natura proprietară a codului îi lasă pe dezvoltatori la mila actualizărilor și deciziilor de business ale marilor corporații.
*   **Oportunități (Opportunities):** Consider că există un moment ideal pentru apariția unei alternative precum RAMY Engine. Comunitatea globală de dezvoltatori se îndreaptă tot mai mult către soluții open-source care oferă autonomie totală. Există o nevoie acută pentru o unealtă care să combine simplitatea sistemelor de noduri cu puterea accesului direct la codul C++ și OpenGL, fără a fi legat de un ecosistem comercial (platform-agnostic).
*   **Amenințări (Threats):** Cea mai mare amenințare vine din viteza cu care companii precum Epic Games (prin Unreal Engine PCG Framework) sau Unity își integrează propriile unelte procedurale "gratuite". Deși nu sunt open-source, acestea pot descuraja utilizatorii să exploreze soluții independente datorită confortului oferit de o soluție integrată.

**2.2.3. Lacune și oportunități: De ce RAMY Engine?**

Lucrând timp de doi ani în Unity la un proiect procedural, am resimțit lipsa unei unelte care să îmi ofere control total fără a mă taxa financiar sau tehnic. Lacuna majoră pe care am identificat-o este "rigiditatea parametrizării". În soluțiile existente, dacă un algoritm de eroziune sau de distribuție a vegetației nu dă rezultatul dorit, ești adesea blocat de implementarea producătorului. RAMY Engine umple această lacună prin modularitate extremă: orice nod este o piesă de puzzle pe care o poți rescrie, extinde sau înlocui, oferind astfel puterea de a pune "suflet" în generarea procedurală.

**2.3. Alegerea soluției optimale**
**2.3. Alegerea soluției optimale**

**2.3.1. Criterii de selecție**

În procesul de definire a soluției optime pentru acest proiect, am stabilit trei piloni fundamentali care au rămas nenegociabili pe parcursul întregii dezvoltări:

1.  **Performanța și accesibilitatea hardware:** Consider că performanța nu este opțională, ci o cerință tehnică de bază. Experiența mea personală din perioada în care am început să dezvolt în Unity, neavând la acea vreme un computer performant, m-a marcat profund. Din acest motiv, am decis ca RAMY Engine să fie optimizat pentru a rula pe majoritatea dispozitivelor, oferind oricărui utilizator acces la instrumente de creație de înaltă calitate, indiferent de hardware-ul de care dispune.
2.  **Natura Open Source:** Am ales să fac acest software complet transparent. Nu doresc să îmi ascund ideile sau algoritmii; dimpotrivă, vreau să îi motivez pe ceilalți prin munca mea și să facilitez formarea unei comunități unde codul este la vedere, permițând oricui să învețe și să contribuie.
3.  **Modularitatea ca formă de exprimare artistică:** Modularitatea este criteriul care asigură libertatea utilizatorului. Refuz ideea unui software rigid; consider că exprimarea artistică în universul digital constă în capacitatea creatorului de a-și defini singur regulile și conținutul. Nodurile interconectabile sunt modul meu de a asigura că RAMY nu este doar o unealtă, ci un mediu de expresie.

Alegerea de a dezvolta RAMY Engine ca o soluție de sine stătătoare, bazată pe criteriile de mai sus, se justifică prin dorința mea de a fi "sincer" atât în cod, cât și în motivația din spatele proiectului. În sutele de ore investite în acest software, nicio secundă nu m-am gândit la un preț sau la un câștig material personal. Singura mea sursă de satisfacție este și va rămâne utilizarea acestui motor de către alți oameni pasionați.

Această sinceritate este elementul care leagă toate deciziile tehnice luate. Prin optimizarea pentru dispozitive low-end, prin deschiderea codului către public și prin refuzul rigidității, am reușit să creez o platformă care nu doar generează date, ci are "suflet". RAMY Engine reprezintă soluția optimă deoarece nu este doar un răspuns tehnic la o nevoie de piață, ci un manifest pentru democratizarea procesului de creație digitală.

---

**3. CAPITOLUL 3: Proiectarea și implementarea soluției**

**3.1. Arhitectura sistemului**

**3.1.1. Diagrama de arhitectură și fluxul de date**

Arhitectura RAMY Engine este concepută pentru a maximiza throughput-ul de date între CPU și GPU. Nucleul sistemului este reprezentat de clasa `Application`, care gestionează ciclul de viață al ferestrei (via GLFW) și coordonează subsistemele printr-un *game loop* determinist. 

Fluxul de date urmează o cale liniară dar extrem de configurabilă:
1.  **Input Layer:** Capturarea evenimentelor de mouse și tastatură via `InputHandler`.
2.  **Logic Layer:** Actualizarea stării obiectelor din `SceneManager` și procesarea stivei de generare în `GenerationStack`.
3.  **Visual Layer:** Transmiterea datelor către `Renderer`, care organizează apelurile de desenare în funcție de materialele și shader-ele active.

**3.1.2. Descrierea componentelor și a interconexiunilor detaliate**

*   **InstancedGroup & Optimizarea Randării:** Pentru a permite afișarea a milioane de obiecte procedurale, am implementat clasa `InstancedGroup`. Aceasta utilizează *Instance Vertex Attributes* pentru a trimite matricile de transformare și datele specifice fiecărei instanțe (precum variații de culoare sau scară) într-un singur buffer GPU, reducând dramatic numărul de *draw calls*.
*   **SceneSerializer:** Pentru a asigura persistența datelor, am dezvoltat un sistem de serializare care traduce ierarhia complexă de `GameObjects` și configurațiile grafice de noduri într-un format structurat. Acest lucru permite utilizatorului să salveze "rețeta" unei lumi procedurale și să o reconstruiască identic la o dată ulterioară.
*   **Undo/Redo System:** Gestionat de `UndoManager`, acest modul urmărește modificările aduse în graful de noduri, permițând o experiență de editare non-distructivă, esențială în procesul creativ.

**3.1.3. Justificarea alegerii arhitecturii orientate pe componente**

Am optat pentru un sistem de tip *Component-Based* în locul unei ierarhii de moștenire rigide. Acest lucru înseamnă că un `GameObject` poate deveni o planetă, un soare sau un sistem de particule pur și simplu prin atașarea componentelor potrivite. Această flexibilitate este cea care permite RAMY Engine să fie atât de versatil în generarea de conținut divers.

**3.2. Tehnologii utilizate**

**3.2.1. Descrierea detaliată a stack-ului tehnologic**

*   **OpenGL 4.6 Core Profile:** Am ales profilul *Core* pentru a forța utilizarea practicilor moderne de randare, eliminând funcționalitățile de tip *fixed-function pipeline*. Folosesc intensiv *Vertex Buffer Objects* (VBO) pentru stocarea geometriei și *Uniform Buffer Objects* (UBO) pentru partajarea datelor între multiple programe shader (precum datele despre lumină sau cameră).
*   **Biblioteci de suport:**
    *   **GLM:** Utilizată pentru calcule de matrici 4x4 și vectori, fiind optimizată pentru a fi compatibilă cu alinierea memoriei cerută de shaderele GLSL.
    *   **Assimp:** Folosită pentru procesarea modelelor 3D. Am configurat Assimp cu *flags* de optimizare precum `aiProcess_Triangulate` și `aiProcess_GenSmoothNormals` pentru a asigura o geometrie curată la import.
    *   **ImGui & ImNodes:** Interfața este randată într-un pas separat, folosind un *overlay* care nu interferează cu buffer-ul principal de randare al scenei.

**3.2.2. Motivarea alegerilor tehnologice și a performanței**

C++ mi-a permis utilizarea pointerilor inteligenți (`std::unique_ptr`, `std::shared_ptr`) pentru a preveni scurgerile de memorie, oferind în același timp viteza necesară pentru a rula algoritmi de zgomot matematic complex pe CPU atunci când este nevoie de date pentru coliziuni sau logică de joc. Alegerea OpenGL a fost motivată și de accesibilitatea uneltelor de *debugging* precum *RenderDoc*, care mi-au fost vitale în vizualizarea pipeline-ului de randare pas cu pas.

**3.3. Implementarea funcționalităților cheie**

**3.3.1. Detalii tehnice aprofundate: Modulele de generare procedurală**

RAMY Engine nu este doar un vizualizator, ci o suită completă de algoritmi de generare care colaborează pentru a crea ecosisteme complexe. Am implementat următoarele module fundamentale:

1.  **Tessellation dinamic pentru Planete și Teren:**
    Implementarea folosește `Tessellation Control Shader` (TCS) și `Tessellation Evaluation Shader` (TES) pentru a genera detaliu geometric infinit la cerere. Pentru planete, pornesc de la un *Icosphere* subdivizat, iar pentru terenuri folosesc un *Grid* de bază. În ambele cazuri, deplasarea vîrfurilor este controlată de multiple octave de zgomot coerent, optimizate să ruleze integral pe GPU pentru a menține performanța.

2.  **Generarea Urbană și Arhitecturală (`CityGrid` & `BuildingGen`):**
    Am dezvoltat module capabile să genereze structuri urbane complexe. Modulul de tip `CityGrid` stabilește rețeaua stradală și lotizarea, în timp ce `BuildingGenNode` utilizează reguli de gramatică procedurală pentru a construi clădiri etajate, cu fațade generate dinamic. Fiecare clădire este unică, parametrii precum înălțimea, stilul ferestrelor și densitatea fiind controlați prin graful de noduri.

3.  **Simularea Eroziunii Hidraulice și Reliefului:**
    Pentru a obține munți cu aspect realist, am implementat un modul de eroziune hidraulică (`HydraulicErosionNode`). Acesta simulează curgerea apei peste teren, transportul sedimentelor și depunerea acestora în zonele joase. Acest proces transformă un zgomot matematic brut într-un relief natural, cu creste ascuțite și văi sedimentare.

4.  **Sistemul de Râuri și Rețele Hidrografice:**
    Modulul `RiverNode` generează trasee de râuri care respectă topologia terenului. Folosesc algoritmi de *pathfinding* care caută calea de minimă rezistență către nivelul mării, generând ulterior un mesh de apă cu coordonate UV animate pentru a simula curgerea.

5.  **Scatter și Distribuția Obiectelor:**
    Pentru popularea scenei cu vegetație, am implementat un sistem de *Scatter* care utilizează tehnici de *Poisson Disc Sampling*. Acest lucru asigură o distribuție naturală, evitând suprapunerile inestetice de obiecte. Datele rezultate sunt trimise către sistemul de *Instanced Rendering* descris anterior, permițând afișarea a sute de mii de copaci fără a degrada rata de cadre.

6.  **Shader-ul Cinematic de Soare:**
    Acesta reprezintă punctul culminant al sistemului vizual, simulând suprafața solară prin multiple straturi de zgomot *voro-noise* și deplasări geometrice de înaltă frecvență. Am integrat și un sistem de atmosferă (`PlanetAtmosphere`) care simulează dispersia Rayleigh pentru a oferi realism scenelor planetare.

7.  **Sistemul de Umbre Avansat (CSM & Omni):**
    Am implementat *Cascaded Shadow Maps* (CSM) cu 4 cascade pentru a gestiona umbrele la scară planetară. Pentru sursele de lumină locale, folosesc *Omni Shadow Maps* stocate în *Cube Maps*, asigurând o iluminare dinamică și corectă pentru toate obiectele generate.

**3.3.2. Interfața utilizatorului și fluxul de lucru bazat pe noduri**

Am dezvoltat un sistem unde fiecare nod reprezintă o operație matematică sau o transformare geometrică. Aceste noduri sunt interconectate prin "pini" de date. În spatele interfeței grafice, RAMY menține un graf de dependențe. Când un nod părinte este modificat, motorul marchează toate nodurile dependente ca fiind "murdare" (*dirty*) și forțează recalcularea lor doar în pasul următor de randare, evitând calculele inutile în fiecare cadru (*frame*).

**3.4. Testarea și validarea performanței**

**3.4.1. Metodologia de testare**

Am validat motorul pe trei configurații hardware diferite: un sistem cu GPU NVIDIA dedicat, un sistem cu AMD și un laptop cu placă grafică integrată Intel. Scopul a fost atingerea unei rate de cadre stabile de minimum 60 FPS pe configurații medii.

**3.4.2. Depanarea și optimizarea stării OpenGL**

Un punct critic a fost gestionarea corectă a contextului OpenGL. Am descoperit că scurgerile de stare (*state leakage*) de la un shader la altul provocau artefacte vizuale. Soluția a fost implementarea unui sistem de *State Caching* în clasa `Renderer`, care verifică dacă o setare (precum `GL_DEPTH_TEST` sau `GL_BLEND`) este deja activă înainte de a trimite o comandă nouă către GPU, economisind astfel cicluri prețioase de procesare.

**3.4.3. Validarea în raport cu obiectivele de business și tehnice**

Validarea finală a confirmat că RAMY Engine poate genera o planetă completă, cu atmosferă, soare și vegetație procedurală, menținând un consum de memorie RAM sub 2GB și o utilizare eficientă a resurselor GPU. Aceasta demonstrează că modularitatea și performanța pot coexista într-un software de licență construit cu rigoare și pasiune.
haderelor. De asemenea, am monitorizat consumul de memorie video (VRAM) pentru a preveni scurgerile de memorie în timpul regenerării repetate a mesh-urilor.

**3.4.2. Rezultatele testelor și remedierea defectelor**

O problemă majoră identificată a fost *z-fighting*-ul la distanțe mari în randarea planetelor. Am remediat acest lucru prin implementarea unei matrice de proiecție logaritmice. De asemenea, am optimizat sistemul de umbre prin introducerea unui algoritm de *frustum culling* pentru a nu randa obiectele care nu sunt vizibile pentru lumină.

**3.4.3. Validarea soluției în raport cu cerințele**

RAMY Engine îndeplinește toate criteriile stabilite inițial: este modular, oferă control total prin noduri și rulează stabil pe o gamă largă de hardware. Sinceritatea codului și pasiunea investită se reflectă în performanța fluidă a motorului, demonstrând că un sistem complex poate fi construit de o singură persoană dacă arhitectura este bine gândită.

---

**4. CAPITOLUL 4: Implementarea din punct de vedere business**

**4.1. Modelul de afaceri și sustenabilitatea**

Modelul de afaceri adoptat pentru RAMY Engine este unul atipic pentru industria software-ului comercial, fiind bazat pe principiile altruismului și al dezvoltării comunitare. Nu urmăresc obținerea unui profit material direct; scopul meu este de a oferi o unealtă celor care, din motive financiare, nu își permit licențe scumpe sau hardware de ultimă generație. Sustenabilitatea proiectului se va baza pe donații voluntare prin platforme dedicate, unde cei care apreciază munca depusă pot alege să susțină comunitatea. Această abordare elimină presiunea financiară asupra utilizatorului și transformă RAMY într-un bun comun.

**4.2. Strategia de marketing și vizibilitate**

Strategia de promovare pentru RAMY Engine este deja în plină desfășurare, bazându-se pe platformele unde comunitatea de dezvoltatori este cea mai activă: Reddit, YouTube și Instagram. Am postat deja primele rezultate pe subreddit-uri dedicate OpenGL și informaticii grafice, unde am primit sute de aprecieri și mii de vizualizări. Această validare timpurie îmi confirmă faptul că pasiunea investită în proiect este recunoscută. Pe viitor, planific să creez vlog-uri de dezvoltare, showcase-uri ale creațiilor utilizatorilor și chiar competiții de generare procedurală pentru a menține interesul comunității ridicat.

**4.3. Proiecția veniturilor și costurilor**

Din perspectiva mea personală, veniturile și profitul sunt nule. Costurile sunt minime, rezumându-se la timpul meu de dezvoltare și găzduirea pe platforme precum GitHub. Totuși, valoarea economică reală pe care o aduce RAMY este reducerea costurilor pentru utilizatorii săi. Prin optimizarea motorului pentru dispozitive low-end, utilizatorii economisesc resurse financiare pe care altfel le-ar fi investit în upgrade-uri hardware scumpe sau în asset-uri procedurale comerciale.

**4.4. Plan de scalabilitate și viziune de viitor**

Viziunea mea pe termen lung este ca RAMY Engine să evolueze dintr-un instrument de PCG într-un motor grafic complet, capabil să concureze cu giganți precum Unity, Godot sau Unreal Engine. Pentru a atinge acest nivel, planul de scalabilitate include implementarea unui limbaj de scripting (precum Lua sau Python) și a unui Play Manager pentru gestionarea logicii de joc. Scalabilitatea va fi susținută de comunitate; doresc ca utilizatorii să poată contribui cu propriile noduri și module, transformând motorul într-un ecosistem viu și în continuă creștere.

**4.5. Securitate și confidențialitate**

Fiind un proiect open-source, securitatea este garantată de transparența totală a codului. Utilizatorii pot audita sursele înainte de a utiliza software-ul, asigurându-se că nu există telemetrie sau procese ascunse. Totul se rulează local, protejând confidențialitatea datelor și a creațiilor utilizatorului. În eventualitatea unor contribuții externe malițioase, voi implementa detectoare specializate în procesul de *version control* pentru a menține integritatea motorului.

**4.6. Usability și User Experience (UX)**

Am conceput interfața RAMY Engine având ca principală sursă de inspirație simplitatea și fluxul de lucru din Unity. Obiectivul meu a fost să abstractizez complexitatea tehnică din spate (C++/OpenGL) într-o interfață intuitivă, accesibilă atât programatorilor, cât și artiștilor. Feedback-ul utilizatorilor va fi elementul central în evoluția UX; orice problemă întâmpinată de comunitate va fi tratată cu seriozitate și integrată în ciclul de îmbunătățire constantă a motorului.

---

**CONCLUZII ȘI PERSPECTIVE**

Lucrarea de față reprezintă materializarea unei pasiuni care a început acum mulți ani, odată cu primele lumi explorate în *Minecraft*. RAMY Engine nu este doar un proiect tehnic de absolvire, ci o piesă de software creată din dorința sinceră de a reda "sufletul" procesului de dezvoltare a jocurilor video. 

Prin implementarea unei arhitecturi modulare bazate pe noduri și utilizarea eficientă a API-ului OpenGL 4.6, am demonstrat că o singură persoană poate construi un sistem capabil să genereze ecosisteme întregi — de la planete și sori cinematici, până la orașe complexe și reliefuri erodate natural.

Perspectivele de viitor sunt ambițioase: doresc să transform RAMY într-un motor grafic complet, susținut de o comunitate de oameni pasionați care să colaboreze liber. Această călătorie, începută cu zeci de mii de linii de cod, este doar primul pas către o platformă care să democratizeze accesul la generarea procedurală de înaltă performanță. Sunt convins că, atâta timp cât motivația rămâne una sinceră și orientată către utilizator, RAMY Engine va reuși să surprindă și să inspire, oferind fiecărui creator puterea de a-și defini propriul univers virtual.

---

**BIBLIOGRAFIE**

[Se va completa conform stilului Harvard, minim 30 de surse]

---

**ANEXE**

[Cod sursă selectat, diagrame de arhitectură, capturi de ecran din motor]
