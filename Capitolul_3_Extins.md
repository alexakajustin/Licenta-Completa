# Capitolul 3: Proiectarea și implementarea soluției (Extindere)

## 3.1 Arhitectura sistemului (Detaliere)

### 3.3.3 Structura Claselelor și Modele de Design (Design Patterns)

Pentru a asigura modularitatea cerută, s-a adoptat o arhitectură orientată pe obiecte (OOP) bazată pe câteva Modele de Design consacrate:

**Singleton Pattern** — Clasa principală `Application` și sistemul de gestionare a resurselor `AssetManager` sunt implementate ca Singletons. Acest lucru asigură un punct unic de acces la starea globală a aplicației (fereastra GLFW, contextul OpenGL) și previne duplicarea resurselor (aceeași textură sau shader nu va fi încărcată de mai multe ori în memoria video).

**Interface Pattern** — Interfața `IGenerator` este pilonul central al extensibilității. Aceasta definește o metodă pur virtuală `Execute(GeneratorData& data)`, care forțează orice generator nou să respecte contractul de intrare/ieșire al motorului. Astfel, `SceneManager` poate procesa o stivă de generatoare fără a cunoaște detaliile interne de implementare ale fiecăruia (zgomot, eroziune, etc.). Adaptarea unui nou algoritm PCG în RAMY durează, teoretic, doar timpul necesar implementării acestei interfețe.

**Component Pattern** — Obiectele din scenă (`GameObject`) urmează o arhitectură simplificată de tip entitate-componentă. Fiecare obiect are o listă de componente (`Transform`, `Mesh`, `Material`). Această structură permite manipularea independentă a proprietăților: de exemplu, putem scala un teren (via `Transform`) fără a afecta datele geometrice brute stocate în unitatea `Mesh`.

## 3.4 Implementarea Pipeline-ului de Randare (Secțiune Nouă)

Randarea hardware-accelerată în RAMY este un proces complex care implică sincronizarea CPU (C++) și GPU (GLSL).

### 3.4.5 Managementul Bufferelor: VAO, VBO și EBO

Pentru a reduce numărul de „draw calls", datele geometrice sunt stocate permanent pe GPU în obiecte de tip buffer:
1.  **Vertex Buffer Object (VBO):** Un tablou de date brute care conține pozițiile vertecșilor, normalele (necesare pentru calculul luminii), coordonatele UV (pentru texturare) și vectorii tangent/bitangent (pentru normal mapping).
2.  **Element Buffer Object (EBO):** Conține indicii care definesc modul în care vertecșii sunt conectați pentru a forma triunghiuri. Utilizarea EBO reduce consumul de memorie prin reutilizarea vertecșilor comuni între triunghiuri adiacente.
3.  **Vertex Array Object (VAO):** Un obiect de stare care „păstrează minte" atribuțiile bufferelor. În timpul buclei de randare, este suficient să apelăm `glBindVertexArray(vaoID)`, ceea ce este mult mai eficient decât reconfigurarea tuturor pointerilor de vertecși la fiecare cadru.

### 3.4.6 Shaders: Motorul Vizual (GLSL)

Sistemul de shadere din RAMY este compus din trei etape:
1.  **Vertex Shader:** Responsabil pentru transformările geometrice. Aplică matricea Model-View-Projection (MVP) pentru a proiecta vertecșii din spațiul 3D pe ecranul 2D. Tot aici se calculează spațiul TBN (Tangent, Bitangent, Normal) necesar pentru iluminarea avansată.
2.  **Geometry Shader (Opțional):** Utilizat pentru generarea de geometrie suplimentară în timp real (ex: fețe de iarbă sau wireframes de debug) direct pe GPU, fără a implica bus-ul de date către CPU.
3.  **Fragment Shader:** Inima vizuală a motorului. Aici se implementează modelul de iluminare **Blinn-Phong**. Pe lângă reflexia difuză și speculară, Fragment Shader-ul din RAMY suportă:
    -   **Multi-texturing:** Amestecarea mai multor texturi bazate pe panta terenului (ex: iarbă pe pante line, stâncă pe pante abrupte).
    -   **Normal Mapping:** Utilizarea hărților de normale pentru a simula detalii geometrice fine care nu există în mesh, prin perturbarea normalelor fragmentelor în funcție de o textură de normale.
    -   **Shadow Sampling:** Verificarea vizibilității fiecărui fragment față de sursa de lumină prin citirea din textura de adâncime (Shadow Map).

## 3.5 Optimizări Tehnice (Secțiune Nouă)

Pentru a atinge ținta de 60 FPS la rezoluții înalte, au fost implementate următoarele tehnici:

### 3.5.1 Frustum Culling

Înainte de a trimite un obiect către GPU, `SceneManager` verifică dacă acesta se află în interiorul „frustum-ului" camerei (volumul vizibil de formă piramidală). Orice obiect care se află în spatele camerei sau în afara câmpului vizual este ignorat complet, economisind resurse prețioase de procesare (Akenine-Möller et al., 2018).

### 3.5.2 Instanced Rendering

Pentru elemente numeroase și identice (ex: pinii dintr-o pădure generată cu generatorul Scatter), se folosește randarea instanțiată. În loc de 1.000 de „draw calls" individuale, se trimite o singură comandă către GPU împreună cu un tablou de matrice de transformare. Acest lucru reduce drastic overhead-ul comunicării între CPU și placa video, permițând afișarea a mii de obiecte fără degradarea vizibilă a performanței.

### 3.5.3 Gestiunea Memoriei prin Pool-uri (VRAM management)

În timpul generării procedurale, mesh-urile se schimbă frecvent. Pentru a evita fragmentarea VRAM (memoria plăcii video) prin alocări și dezalocări repetate, RAMY folosește un sistem de pre-alocare. Bufferele sunt create la o dimensiune maximă estimată, iar la fiecare actualizare a terenului, datele sunt trimise folosind `glBufferSubData`, o operație mult mai rapidă decât realocarea completă a bufferului.
