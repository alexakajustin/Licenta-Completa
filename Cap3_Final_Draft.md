# CAPITOLUL 3: PROIECTAREA ȘI IMPLEMENTAREA SOLUȚIEI

## 3.1 Arhitectura sistemului

RAMY Procedural Generation Studio este un motor grafic 3D conceput pentru generarea procedurală de conținut tridimensional. Arhitectura sistemului este organizată pe două ramuri principale care converg către un nucleu comun, Scene Manager, responsabil cu gestionarea obiectelor și resurselor din scenă.

Ramura de randare pornește de la API-ul OpenGL 3.3, accesat prin intermediul bibliotecii GLEW, care asigură încărcarea extensiilor OpenGL. Această ramură alimentează Viewport Window, fereastra de vizualizare 3D în care scena este redată în timp real prin multiple treceri de randare (umbre direcționale, umbre omnidirecționale și trecerea finală cu iluminare, texturi și skybox).

Ramura de configurare este construită pe baza bibliotecii Dear ImGui, care furnizează interfața grafică de utilizator. Aceasta include Node-Based Editor Window, un editor vizual de grafuri de noduri implementat cu librăria imnodes, prin care utilizatorul definește fluxuri de generare procedurală conectând noduri precum Scene Input, Perlin Noise, Scatter, Merge Mesh și Output.

Clasele generatoare C++ implementează logica procedurală prin sistemul extensibil de operații, organizate pe categorii: generare de forme, transformări, zgomot, operații pe mesh, selecție și utilitare. Acestea pot fi executate prin Generator Singleton Stack (o stivă secvențială) sau prin graful de noduri (execuție topologică a unui DAG). Rezultatele converg în Mesh Results, datele CPU ale mesh-urilor procedurale, care sunt apoi încărcate pe GPU și redate în viewport.

**Diagrama 1 – Diagrama de arhitectură**

### 3.1.2 Structura claselor și modele de design

Pentru a asigura modularitatea cerută, s-a adoptat o arhitectură orientată pe obiecte (OOP) bazată pe câteva Modele de Design consacrate:

**Singleton Pattern** — Clasa principală `Application` și sistemul de gestionare a resurselor `AssetManager` sunt implementate ca Singletons. Acest lucru asigură un punct unic de acces la starea globală a aplicației (fereastra GLFW, contextul OpenGL) și previne duplicarea resurselor (aceeași textură sau shader nu va fi încărcată de mai multe ori în memoria video).

**Interface Pattern** — Interfața `IGenerator` este pilonul central al extensibilității. Aceasta definește o metodă pur virtuală `Execute(GeneratorData& data)`, care forțează orice generator nou să respecte contractul de intrare/ieșire al motorului. Astfel, `SceneManager` poate procesa o stivă de generatoare fără a cunoaște detaliile interne de implementare ale fiecăruia (zgomot, eroziune, etc.). Adaptarea unui nou algoritm PCG în RAMY durează, teoretic, doar timpul necesar implementării acestei interfețe.

**Component Pattern** — Obiectele din scenă (`GameObject`) urmează o arhitectură simplificată de tip entitate-componentă. Fiecare obiect are o listă de componente (`Transform`, `Mesh`, `Material`). Această structură permite manipularea independentă a proprietăților: de exemplu, putem scala un teren (via `Transform`) fără a afecta datele geometrice brute stocate în unitatea `Mesh`.

## 3.2 Diagrama de secvenţe

Diagrama de secvență ilustrează fluxul complet de execuție al pipeline-ului de generare procedurală, de la interacțiunea utilizatorului cu interfața grafică până la randarea finală în viewport. Procesul se desfășoară în șapte pași principali și implică colaborarea dintre cinci componente: Node Editor (Dear ImGui), Generator Stack, Scene Manager, IGenerator și Viewport (OpenGL).

Fluxul începe când utilizatorul configurează generatorii prin intermediul Node Editor-ului (pasul 1), ajustând parametrii precum frecvența zgomotului Perlin, numărul de instanțe pentru scatter sau dimensiunile formelor primitive. Editorul trimite configurația către Generator Stack (pasul 2), care construiește pipeline-ul de execuție prin ordonarea generatoarelor conform dependențelor definite de utilizator.

Generator Stack-ul apelează metoda `Execute()` pe fiecare implementare `IGenerator` (pasul 3), care procesează datele și returnează rezultatul sub formă de `MeshData` (pasul 4), conținând vertecși (poziție, coordonate UV, normale, tangente și bitangente) și indici. Ieșirea fiecărui generator poate deveni intrarea următorului, permițând transformări incrementale ale geometriei.

Odată ce toate generatoarele au fost executate, rezultatele sunt transmise către Scene Manager prin apelul `UpdateScene(meshes)` (pasul 5), care actualizează sau creează obiectele din scenă cu noile date geometrice. Scene Manager-ul declanșează apoi procesul de randare (pasul 6) prin Viewport (OpenGL), care execută trecerile de umbre și trecerea finală de iluminare. Rezultatul 3D este afișat utilizatorului în fereastra viewport (pasul 7), completând ciclul de feedback vizual.

**Diagrama 2 – Diagrama de secvenţe**

## 3.3 Tehnologii utilizate

### 3.3.1 Descrierea detaliată a framework-urilor, bibliotecilor și instrumentelor utilizate

Dezvoltarea motorului **RAMY Procedural Engine** s-a bazat pe o selecție riguroasă de tehnologii, fiecare aleasă pentru a adresa o cerință specifică a arhitecturii sistemului.

**C++ (Standard C++17)** constituie limbajul de programare principal al întregului proiect. Alegerea C++ a fost motivată de trei factori fundamentali: controlul direct asupra alocării și dezalocării memoriei (esențial pentru gestionarea eficientă a bufferelor GPU), performanța de execuție apropiată de codul mașină (critică pentru algoritmii de generare care procesează milioane de vertecși) și suportul nativ pentru paradigma de programare orientată pe obiecte prin care s-a implementat sistemul de interfețe (`IGenerator`). Standardul C++17 a fost preferat pentru caracteristici precum `std::optional`, `std::filesystem` și `structured bindings`, care au simplificat semnificativ codul de serializare și gestiune a fișierelor (Stroustrup, 2013).

**OpenGL 3.3 Core Profile** reprezintă API-ul grafic utilizat pentru toate operațiile de randare. Versiunea 3.3 a fost selectată pentru a asigura compatibilitatea cu o gamă largă de plăci grafice, inclusiv cele integrate, în timp ce profilul Core elimină funcționalitățile depreciate ale pipeline-ului fix, impunând utilizarea exclusivă a shaderelor programabile. Acest lucru a permis implementarea unui pipeline de randare modern, bazat pe Vertex Shaders, Fragment Shaders și Geometry Shaders, oferind control complet asupra transformărilor geometrice și calculelor de iluminare (Shreiner et al., 2013).

**GLEW (OpenGL Extension Wrangler Library)** este utilizat pentru încărcarea extensiilor OpenGL la runtime. Deoarece specificația OpenGL este implementată de driverele producătorilor de plăci grafice, funcțiile API-ului nu sunt disponibile direct prin legare statică. GLEW detectează și încarcă automat toate extensiile suportate de hardware-ul curent.

**GLFW (Graphics Library Framework)** gestionează crearea ferestrei aplicației, contextul OpenGL și procesarea evenimentelor de intrare (tastatură, mouse). GLFW a fost preferat față de alternative precum SDL2 datorită dimensiunii sale reduse și a focalizării exclusive pe gestiunea ferestrelor.

**GLM (OpenGL Mathematics)** furnizează tipuri de date precum `vec3`, `vec4`, `mat4` și `quat`, asigurând compatibilitatea directă între calculele matematice din codul C++ și cele din shadere.

**Dear ImGui** constituie biblioteca principală pentru interfața grafică de utilizator (IMGUI). Aceasta a fost extinsă cu biblioteca ImNodes pentru implementarea editorului vizual de grafuri de noduri, permițând utilizatorului să creeze și să conecteze noduri de generare într-un spațiu de lucru 2D interactiv.

**Assimp (Open Asset Import Library)** gestionează importul modelelor 3D din formate externe (OBJ, FBX, GLTF), extrăgând datele de vertecși, indici, normale și coordonate UV.

**stb_image** este o bibliotecă header-only utilizată pentru încărcarea texturilor din formate de imagine comune (PNG, JPG, BMP), minimizând dependențele externe.

**nlohmann/json** este utilizat pentru serializarea și deserializarea scenelor în format JSON, oferind o sintaxă intuitivă pentru salvarea/încărcarea scenelor și a configurațiilor grafului de noduri.

### 3.3.2 Motivarea alegerilor tehnologice

Selecția fiecărei tehnologii a fost ghidată de principiul minimizării dependențelor externe, maximizarea controlului asupra codului și asigurarea portabilității.

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

### 3.4.1 Pipeline-ul de randare

Randarea hardware-accelerată în RAMY implică sincronizarea CPU (C++) și GPU (GLSL). Datele geometrice sunt stocate permanent pe GPU în obiecte de tip buffer:
1. **Vertex Buffer Object (VBO):** date brute (poziții, normale, UVs, tangenți).
2. **Element Buffer Object (EBO):** indici pentru formarea triunghiurilor.
3. **Vertex Array Object (VAO):** obiect de stare care „păstrează minte" configurația bufferelor.

Sistemul de shadere este compus din Vertex Shader (proiecție MVP, spațiu TBN) și Fragment Shader (iluminare Blinn-Phong, multi-texturing, normal mapping și shadow sampling).

### 3.4.2 Optimizări tehnice

1. **Frustum Culling:** `SceneManager` ignoră obiectele aflate în afara volumului vizibil al camerei, economisind procesare GPU.
2. **Instanced Rendering:** Pentru obiecte numeroase (păduri, iarbă), se trimite o singură comandă de randare cu un tablou de matrice, reducând drastic overhead-ul comunicării CPU-GPU.
3. **Gestiunea memoriei (VRAM):** Se folosește pre-alocarea și `glBufferSubData` pentru a evita fragmentarea VRAM la actualizarea frecventă a terenurilor procedurale.

### 3.4.3 Generatorul de zgomot Perlin

Implementarea se bazează pe "Improved Perlin Noise" (Perlin, 2002). Acesta expune parametrii: frecvență, amplitudine, octave (fBM), persistență, lacunaritate și seed. Rezultatul este un heightmap [0, 1] transformat în geometrie 3D.

### 3.4.4 Simularea eroziunii hidraulice

Simulare Lagrangian (particle-based) care modelează traiectoria picăturilor de apă. Procesul implică inițializare, calcul gradient, deplasare cu inerție, eroziune/depunere de sediment și evaporare. Aceasta demonstrează puterea stivei de generare, fiind aplicată de obicei peste zgomotul Perlin.

### 3.4.5 Generatorul Scatter (Poisson Disc Sampling)

Utilizează algoritmul lui Bridson (2007) pentru a distribui obiecte 3D cu o distanță minimă garantată. Algoritmul operează în timp real O(N), folosind o grilă de accelerare spațială. Parametrii includ raza de separare, scala/rotația aleatorie și masca de densitate (panta sau altitudine).

### 3.4.6 Pipeline-ul de Shadow Mapping

Sistemul realizează două treceri de randare:
1. **Shadow Pass:** Crearea unui depth map din perspectiva luminii.
2. **Lighting Pass:** Comparația adâncimii curente cu cea din shadow map (Shadow Sampling).
Include **depth bias** (eliminare shadow acne) și **PCF** (îndulcirea marginilor umbrei).

## 3.5 Prezentarea interfeței utilizatorului

Interfața este organizată în patru panouri DCC:
1. **Viewport-ul 3D:** Vizualizarea în timp real cu navigare orbit/pan/zoom și Gizmos pentru manipulare.
2. **Node Editor:** Spațiu vizual pentru definirea fluxurilor de date prin conectarea nodurilor de generare.
3. **Panoul Inspector:** Editează dinamic proprietățile obiectului sau nodului selectat.
4. **Browser-ul de Active:** Navigare și previzualizare modele/texturi.

## 3.6 Testare și validare

### 3.6.1 Tipuri de teste efectuate

**Testele de performanță** au fost evaluate pe o configurație hardware de referință: **procesor Intel Core i7-12700H, 16 GB RAM DDR5, placă grafică NVIDIA GeForce RTX 3060 Mobile (6 GB VRAM)**.

**Tabelul 4 — Rezultatele testelor de performanță**

| Configurație scenă | Nr. vertecși | Nr. triunghiuri | FPS mediu | FPS minim |
|---|---|---|---|---|
| Teren 128×128, fără umbre | 16.384 | 32.258 | 320 | 280 |
| Teren 256×256, cu umbre | 65.536 | 130.050 | 145 | 120 |
| Teren 512×512, cu umbre + scatter (100 obiecte) | 262.144 + ~50k | ~620k | 62 | 48 |
| Teren 1024×1024, cu umbre + scatter (500 obiecte) | 1.04M + ~250k | ~2.5M | 28 | 18 |

**Testele de stabilitate** au confirmat absența scurgerilor de memorie în timpul sesiunilor prelungite de testare (până la 2 ore) cu grafuri de până la 20 de noduri.

### 3.6.2 Validarea soluției în raport cu cerințele

**Tabelul 5 — Maparea obiectivelor pe funcționalitățile implementate**

| Obiectiv | Funcționalitate implementată | Stare |
|---|---|---|
| 1. Arhitectură modulară | Interfața IGenerator, Generation Stack, sistem de plugin-uri | Realizat |
| 2. Pipeline de generare eficient | Generation Stack secvențial, graf de noduri DAG | Realizat |
| 3. Interfață vizuală intuitivă | Node Editor (ImNodes), Inspector, Asset Explorer | Realizat |
| 4. Randare în timp real | OpenGL 3.3, Shadow Mapping, feedback instantaneu | Realizat |
