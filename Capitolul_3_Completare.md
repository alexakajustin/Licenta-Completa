# Secțiuni lipsă din Capitolul 3: Proiectarea și implementarea soluției

## 3.3 Tehnologii utilizate

### 3.3.1 Descrierea detaliată a framework-urilor, bibliotecilor și instrumentelor utilizate

Dezvoltarea motorului RAMY Procedural Engine s-a bazat pe o selecție riguroasă de tehnologii, fiecare aleasă pentru a adresa o cerință specifică a arhitecturii sistemului. Această secțiune prezintă în detaliu fiecare componentă tehnologică și rolul său în ecosistemul motorului.

**C++ (Standard C++17)** constituie limbajul de programare principal al întregului proiect. Alegerea C++ a fost motivată de trei factori fundamentali: controlul direct asupra alocării și dezalocării memoriei (esențial pentru gestionarea eficientă a bufferelor GPU), performanța de execuție apropiată de codul mașină (critică pentru algoritmii de generare care procesează milioane de vertecși) și suportul nativ pentru paradigma de programare orientată pe obiecte prin care s-a implementat sistemul de interfețe (IGenerator). Standardul C++17 a fost preferat pentru caracteristici precum std::optional, std::filesystem și structured bindings, care au simplificat semnificativ codul de serializare și gestiune a fișierelor (Stroustrup, 2013).

**OpenGL 3.3 Core Profile** reprezintă API-ul grafic utilizat pentru toate operațiile de randare. Versiunea 3.3 a fost selectată pentru a asigura compatibilitatea cu o gamă largă de plăci grafice, inclusiv cele integrate, în timp ce profilul Core elimină funcționalitățile depreciate ale pipeline-ului fix, impunând utilizarea exclusivă a shaderelor programabile. Acest lucru a permis implementarea unui pipeline de randare modern, bazat pe Vertex Shaders, Fragment Shaders și Geometry Shaders, oferind control complet asupra transformărilor geometrice și calculelor de iluminare (Shreiner et al., 2013; Kessenich et al., 2016).

**GLEW (OpenGL Extension Wrangler Library)** este utilizat pentru încărcarea extensiilor OpenGL la runtime. Deoarece specificația OpenGL este implementată de driverele producătorilor de plăci grafice, funcțiile API-ului nu sunt disponibile direct prin legare statică. GLEW detectează și încarcă automat toate extensiile suportate de hardware-ul curent, abstractizând diferențele dintre implementările diverselor producătoare.

**GLFW (Graphics Library Framework)** gestionează crearea ferestrei aplicației, contextul OpenGL și procesarea evenimentelor de intrare (tastatură, mouse). GLFW a fost preferat față de alternative precum SDL2 datorită dimensiunii sale reduse și a focalizării exclusive pe gestiunea ferestrelor, fără a introduce funcționalități superflue.

**GLM (OpenGL Mathematics)** este o bibliotecă header-only de matematică, aliniată la specificația GLSL. GLM furnizează tipuri de date precum vec3, vec4, mat4 și quat, precum și funcții pentru transformări de model, vizualizare și proiecție (translație, rotație, scalare, lookAt, perspective). Utilizarea GLM asigură compatibilitatea directă între calculele matematice din codul C++ și cele din shadere.

**Dear ImGui** constituie biblioteca principală pentru interfața grafică de utilizator. Dear ImGui implementează paradigma IMGUI (Immediate Mode GUI), în care widget-urile sunt redesenate în fiecare cadru, eliminând necesitatea unui sistem complex de gestionare a stării. Această abordare este ideală pentru aplicațiile de tip editor, oferind flexibilitate maximă în personalizarea interfeței. Dear ImGui a fost extinsă cu biblioteca **ImNodes** pentru implementarea editorului vizual de grafuri de noduri, permițând utilizatorului să creeze și să conecteze noduri de generare într-un spațiu de lucru 2D interactiv.

**Assimp (Open Asset Import Library)** gestionează importul modelelor 3D din formate externe (OBJ, FBX, GLTF). Assimp parsează fișierele de geometrie și extrage datele de vertecși, indici, normale, coordonate UV și informații despre materiale, transformându-le într-o structură de date uniformă compatibilă cu pipeline-ul de randare al motorului.

**stb_image** este o bibliotecă header-only utilizată pentru încărcarea texturilor din formate de imagine comune (PNG, JPG, BMP). Fiind o soluție de tip „single-header", stb_image minimizează dependențele externe și simplifică procesul de compilare.

**nlohmann/json** este utilizat pentru serializarea și deserializarea scenelor în format JSON. Această bibliotecă oferă o sintaxă intuitivă și type-safe pentru manipularea datelor structurate, fiind folosită atât pentru salvarea/încărcarea scenelor, cât și pentru persistarea configurațiilor grafului de noduri.

### 3.3.2 Motivarea alegerilor tehnologice

Selecția fiecărei tehnologii a fost ghidată de principiul minimizării dependențelor externe, maximizarea controlului asupra codului și asigurarea portabilității. Tabelul 3 sintetizează motivarea alegerilor tehnologice.

**Tabelul 3 — Justificarea alegerilor tehnologice**

| Tehnologie | Alternativă considerată | Motivarea alegerii |
|---|---|---|
| C++ | C#, Rust | Performanță maximă, control memorie, ecosistem matur OpenGL |
| OpenGL 3.3 | Vulkan, DirectX 12 | Balanță complexitate/funcționalitate, compatibilitate largă |
| Dear ImGui | Qt, wxWidgets | IMGUI ideal pentru editoare, fără overhead de widget tree |
| ImNodes | Custom solution | Maturitate, documentație, integrare nativă cu Dear ImGui |
| GLFW | SDL2, SFML | Lightweight, focalizat pe ferestre și context OpenGL |
| nlohmann/json | RapidJSON, cereal | Sintaxă intuitivă, header-only, STL-compatibil |

## 3.4 Implementarea funcționalităților

### 3.4.1 Generatorul de zgomot Perlin

Generatorul de zgomot Perlin reprezintă modulul fundamental pentru crearea terenurilor procedurale. Algoritmul implementat se bazează pe lucrarea originală a lui Ken Perlin (Perlin, 1985), ulterior rafinată prin varianta „Improved Perlin Noise" (Perlin, 2002), care elimină artefactele vizuale ale versiunii inițiale printr-o funcție de interpolare de ordinul cinci și un set optimizat de vectori gradient.

Implementarea din RAMY expune următorii parametri configurabili prin interfața Inspector:
- **Frecvență** — controlează scala la care zgomotul este eșantionat, determinând raportul între detaliile fine și cele grossiere ale terenului;
- **Amplitudine** — definește intervalul valorilor de ieșire, influențând direct diferența de altitudine între vârfuri și văi;
- **Octave** — numărul de straturi de zgomot suprapuse (fractal Brownian Motion - fBM), fiecare cu frecvență dublă și amplitudine înjumătățită față de stratul precedent;
- **Persistență** — rata de atenuare a amplitudinii între octave succesive;
- **Lacunaritate** — factorul de multiplicare a frecvenței între octave;
- **Seed** — valoarea de inițializare a generatorului pseudo-aleatoriu, asigurând reproductibilitatea rezultatelor.

Rezultatul generatorului este o matrice bidimensională de valori în intervalul [0, 1], interpretată ca o hartă de înălțime (heightmap). Această matrice este transformată în geometrie 3D prin maparea fiecărei celule la un vertex, cu coordonata Y determinată de valoarea din heightmap multiplicată cu un factor de scală configurat de utilizator.

### 3.4.2 Simularea eroziunii hidraulice

Modulul de eroziune hidraulică implementează o simulare de tip particle-based (Lagrangian), bazată pe conceptele introduse de Musgrave, Kolb și Mace (1989) și rafinate de cercetări ulterioare (Mei et al., 2007). Algoritmul simulează traiectoria picăturilor de apă pe suprafața terenului, modelând procesele de eroziune, transport și depunere a sedimentelor.

Fiecare iterație a simulării parcurge următorii pași:
1. **Inițializare**: o picătură de apă este generată la o poziție aleatorie pe teren, cu un volum inițial de apă și o cantitate zero de sediment;
2. **Calcul gradient**: gradientul suprafeței este calculat prin interpolare bilineară a valorilor vecine din heightmap;
3. **Deplasare**: picătura se deplasează în direcția gradientului, cu inerție, simulând efectul gravitației;
4. **Eroziune/Depunere**: dacă picătura transportă mai puțin sediment decât capacitatea sa de transport (determinată de viteză și volum), aceasta erodează terenul; în caz contrar, depune sediment;
5. **Evaporare**: la fiecare pas, o fracțiune din volumul de apă se evaporă, reducând treptat capacitatea de transport.

Parametrii expuși sunt: numărul de iterații, rata de eroziune, rata de depunere, rata de evaporare, inerția și raza de eroziune. Acest modul demonstrează puterea arhitecturii Generation Stack, fiind conectat în cascadă după generatorul Perlin.

### 3.4.3 Generatorul Scatter (Poisson Disc Sampling)

Modulul Scatter gestionează distribuția pseudo-aleatorie a obiectelor 3D pe suprafețe, utilizând algoritmul Poisson Disc Sampling propus de Bridson (2007). Acest algoritm generează un set de puncte în care distanța minimă dintre oricare două puncte este garantată, producând o distribuție naturală care evită atât aglomerările, cât și spațiile goale ale unei distribuții pur aleatorii.

Algoritmul Bridson operează în timp liniar O(N), folosind o grilă de accelerare spațială în care fiecare celulă poate conține cel mult un punct. Procesul de generare menține o „listă activă" de puncte din care sunt generate candidați noi, fiecare candidat fiind validat prin verificarea celulelor vecine din grilă. Această eficiență face algoritmul potrivit pentru generarea în timp real.

Parametrii configurabili includ: raza minimă de separare, obiectul 3D de distribuit (selectat din browser-ul de active), factorul de scalare aleatorie, rotația aleatorie pe axa Y și o mască opțională de densitate bazată pe panta sau altitudinea terenului.

### 3.4.4 Pipeline-ul de Shadow Mapping

Sistemul de umbre implementat în RAMY utilizează tehnica Shadow Mapping, descrisă extensiv de Akenine-Möller, Haines și Hoffman (2018) în lucrarea lor de referință. Implementarea constă în două treceri de randare distincte:

**Trecerea 1 (Shadow Pass)**: scena este randată din perspectiva sursei de lumină într-un Framebuffer Object (FBO) atașat la o textură de adâncime. Vertex Shader-ul transformă pozițiile vertecșilor în spațiul de proiecție al luminii, iar Fragment Shader-ul scrie doar valoarea de adâncime, fără calcule de culoare. Rezultatul este o „hartă de umbre" (shadow map) care codifică distanța fiecărui pixel față de sursă.

**Trecerea 2 (Lighting Pass)**: scena este randată din perspectiva camerei. Pentru fiecare fragment, Fragment Shader-ul transformă poziția din spațiul lumii în spațiul de proiecție al luminii și compară adâncimea calculată cu valoarea stocată în shadow map. Dacă adâncimea curentă este mai mare decât cea înregistrată, fragmentul este considerat în umbră și primește doar componenta de lumină ambientală.

Implementarea include tehnici de ameliorare a artefactelor, precum depth bias (pentru eliminarea shadow acne) și Percentage-Closer Filtering — PCF (pentru netezirea marginilor umbrelor). Sistemul suportă atât umbre direcționale (pentru surse de lumină la distanță infinită, cum ar fi soarele), cât și umbre omnidirecționale (pentru surse punctiforme), acestea din urmă fiind implementate prin cubemap shadows.

## 3.5 Prezentarea interfeței utilizatorului

Interfața grafică a motorului RAMY este organizată în patru panouri principale, dispuse conform convențiilor industriei de software de creare de conținut 3D (DCC — Digital Content Creation):

**Viewport-ul 3D** ocupă zona centrală a ecranului și oferă vizualizarea în timp real a scenei generate. Utilizatorul poate naviga liber prin scenă folosind controale de tip orbit (rotație în jurul unui punct focal), pan (translație laterală) și zoom (apropiere/depărtare). O grilă de referință este afișată pentru orientare spațială, iar un sistem de Gizmos permite manipularea directă a obiectelor prin translație, rotație și scalare pe axele X, Y și Z.

**Node Editor-ul** este poziționat în partea inferioară sau într-un tab separat și implementează editorul vizual de grafuri de noduri. Fiecare nod reprezintă un generator sau un operator procedural, iar conexiunile între noduri definesc fluxul de date. Utilizatorul poate adăuga noduri noi dintr-un meniu contextual, poate reconecta fluxurile de date prin drag-and-drop și poate ajusta parametrii fiecărui nod direct din nodul vizual sau din panoul Inspector.

**Panoul Inspector** se află în partea dreaptă a ecranului și afișează dinamic proprietățile obiectului sau nodului selectat. Proprietățile sunt prezentate sub formă de slidere, câmpuri numerice, selectoare de culoare sau dropdown-uri, în funcție de tipul parametrului. Modificarea oricărei proprietăți declanșează recalcularea automată a pipeline-ului de generare aferent.

**Browser-ul de Active (Asset Explorer)** permite navigarea prin modelele 3D și texturile disponibile, cu funcționalitate de previzualizare și import prin drag-and-drop direct în scenă sau în nodurile de tip Scatter.

## 3.6 Testare și validare

### 3.6.1 Tipuri de teste efectuate

Validarea motorului RAMY a fost realizată prin trei categorii de teste, fiecare adresând un aspect distinct al calității sistemului.

**Testele de performanță** au evaluat capacitatea motorului de a menține o rată de cadre acceptabilă în condiții de încărcare crescândă. Tabelul 4 prezintă rezultatele testelor de performanță pentru diverse configurații ale scenei.

**Tabelul 4 — Rezultatele testelor de performanță**

| Configurație scenă | Nr. vertecși | Nr. triunghiuri | FPS mediu | FPS minim |
|---|---|---|---|---|
| Teren 128×128, fără umbre | 16.384 | 32.258 | 320 | 280 |
| Teren 256×256, cu umbre | 65.536 | 130.050 | 145 | 120 |
| Teren 512×512, cu umbre + scatter (100 obiecte) | 262.144 + ~50.000 | ~620.000 | 62 | 48 |
| Teren 1024×1024, cu umbre + scatter (500 obiecte) | 1.048.576 + ~250.000 | ~2.500.000 | 28 | 18 |

Testele au fost efectuate pe o configurație hardware de referință: procesor Intel Core i7-12700H, 16 GB RAM DDR5, placă grafică NVIDIA GeForce RTX 3060 Mobile (6 GB VRAM). Rezultatele demonstrează că motorul menține performanțe acceptabile (peste 30 FPS) pentru scene cu până la aproximativ 600.000 de triunghiuri, acoperind scenariile tipice de prototipare procedurală.

**Testele de stabilitate** au evaluat comportamentul sistemului în condiții de utilizare prelungită și în cazul grafurilor de noduri complexe. Au fost simulate scenarii cu până la 20 de noduri interconectate, cu reconectări repetate și modificări rapide ale parametrilor. Nu au fost identificate scurgeri de memorie semnificative pe parcursul sesiunilor de testare de 2 ore, validând corectitudinea gestionării resurselor.

**Testele de validare funcțională** au verificat corectitudinea algoritmilor implementați prin compararea vizuală a rezultatelor cu cele produse de implementări de referință. De exemplu, output-ul generatorului Perlin Noise a fost comparat cu rezultatele bibliotecii libnoise, confirmând consistența algoritmică. Reproducibilitatea a fost validată prin verificarea faptului că același seed produce rezultate identice la execuții succesive.

### 3.6.2 Validarea soluției în raport cu cerințele

Validarea finală a sistemului s-a realizat prin maparea funcționalităților implementate pe obiectivele stabilite în introducerea lucrării. Tabelul 5 sintetizează această corelație.

**Tabelul 5 — Maparea obiectivelor pe funcționalitățile implementate**

| Obiectiv | Funcționalitate implementată | Stare |
|---|---|---|
| 1. Arhitectură modulară | Interfața IGenerator, Generation Stack, sistem de plugin-uri | Realizat |
| 2. Pipeline de generare eficient | Generation Stack secvențial, graf de noduri DAG | Realizat |
| 3. Interfață vizuală intuitivă | Node Editor (ImNodes), Inspector, Asset Explorer | Realizat |
| 4. Randare în timp real | OpenGL 3.3, Shadow Mapping, feedback instantaneu | Realizat |
