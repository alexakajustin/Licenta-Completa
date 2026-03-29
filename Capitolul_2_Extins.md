# Capitolul 2: Stadiul cunoașterii (Extindere)

## 2.1 Analiza pieței (Adăugări)

### 2.1.2 Tendințe globale în industria uneltelor de dezvoltare (Continuare)

Pe lângă influența AI-ului, piața este marcată de o nevoie acută de interoperabilitate. Standardele deschise, cum ar fi formatul **Universal Scene Description (USD)** dezvoltat de Pixar și promovat intens de NVIDIA prin platforma Omniverse, devin fundamentale. Aceste standarde permit colaborarea în timp real între artiști care folosesc software-uri diferite (Blender, Maya, Houdini, RAMY). În acest context, RAMY își propune să integreze exportul în formate standardizate, oferind flexibilitate maximă utilizatorilor care doresc să combine generarea procedurală proprie cu pipeline-uri profesionale.

Mai mult, importanța **Digital Twins** în industrie (replici digitale ale obiectelor fizice) extinde piața către simulări industriale. RAMY, prin capacitatea sa de a genera structuri geometrice precise pe baza unor parametri de intrare din lumea reală, poate servi ca instrument de prototipare rapidă pentru simulări de mediu urban sau infrastructură critică (Kaufmann et al., 2023).

### 2.1.3 Analiza concurenței (Detaliere)

**Houdini (SideFX)** — Deși este un concurent indirect din cauza barierei de preț, Houdini rămâne etalonul tehnic. Sistemul său bazat pe „SOPs" (Surface Operators) este analog cu arhitectura de generatoare din RAMY, ambele urmând conceptul de flux de date imuabil. Diferența principală este că RAMY este optimizat pentru execuție în timp real pe GPU cu un set specific de reguli deterministe, în timp ce Houdini este un sistem generalist de simulare care adesea necesită timpi de „caching" sau „baking" pentru scene complexe.

**Unity PCG & Unreal PCG** — Recentele framework-uri integrate în motoarele Unreal și Unity ridică miza pentru motoarele independente. Totuși, acestea suferă de o „over-specialization". Unreal PCG, de exemplu, este extrem de puternic pentru distribuția de vegetație pe scară largă (megascans), dar mult mai rigid în ceea ce privește generarea de geometrie procedurală de la zero (ex: extrudarea formelor gramaticale). RAMY, prin implementarea unei interfețe generice `IGenerator`, permite utilizatorului să definească logici de generare arbitrară, nu doar distribuție de instanțe.

## 2.2 Algoritmi Fundamentali în Generarea Procedurală (Secțiune Nouă)

În această secțiune sunt analizați algoritmii care stau la baza motorului RAMY, oferind o perspectivă teoretică asupra modului în care matematica este transpusă în geometrie 3D.

### 2.2.1 Zgomotul Coerent: De la Valoare la Gradient (Perlin & Simplex)

Zgomotul procedural este „cărămida de bază" a oricărui sistem PCG. Spre deosebire de un număr pur aleatoriu (care produce „zgomot alb" — discontinuu și vizual haotic), zgomotul coerent returnează valori care se schimbă gradual în spațiu. Ken Perlin a revoluționat industria grafică prin introducerea zgomotului bazat pe gradient în 1985, pentru care a primit un Premiu de Merit de la Academia de Film (Perlin, 1985).

Algoritmul funcționează astfel:
1.  **Grila de gradienți:** Se definește o grilă de puncte întregi în spațiul 2D sau 3D. Fiecărui punct îi este asociat un vector gradient unitar, determinat pseudaleatoriu pe baza unui „seed".
2.  **Vectorii de distanță:** Pentru orice punct (x, y) de eșantionat, se calculează vectorii care pornesc de la cele 4 colțuri ale „chiliei" grilei către punctul respectiv.
3.  **Produsul scalar (Dot Product):** Se calculează produsul scalar între vectorul gradient al fiecărui colț și vectorul de distanță corespunzător. Această operație determină influența fiecărui gradient asupra punctului evaluat.
4.  **Interpolarea:** Valorile obținute sunt interpolate folosind o funcție de netezire (S-curve). Perlin a îmbunătățit funcția inițială de interpolare cubică ($3t^2 - 2t^3$) cu una de ordinul 5 ($6t^5 - 15t^4 + 10t^3$), asigurând continuitatea derivatei secunde, ceea ce elimină artefactele vizuale vizibile sub formă de linii drepte pe suprafața terenului (Perlin, 2002).

În RAMY, implementarea acestui algoritm suportă straturi multiple de zgomot (fractali), permițând utilizatorului să construiască hărți complexe unde frecvențe joase (munți) se combină cu frecvențe înalte (bolovani, micro-relief), un proces cunoscut sub numele de **Fractal Brownian Motion (fBM)**.

### 2.2.2 Wave Function Collapse (WFC): Sinteza bazată pe Exemple

WFC este unul dintre cei mai fascinanți algoritmi contemporani din PCG, inspirat de mecanica cuantică și soluționarea de constrângeri (Constraint Satisfaction Problems). Creat de Maxim Gumin în 2016, acesta permite generarea unei lumi care aderă la regulile extrase dintr-un set mic de date de intrare (Gumin, 2016).

Funcționarea în RAMY urmează pașii:
1.  **Entropia:** Inițial, fiecare celulă a lumii se află într-o „suprapoziție" de toate stările posibile (toate tipurile de tile-uri sau obiecte).
2.  **Observația (Colapsul):** Se alege celula cu cea mai mică entropie (cea mai constrânsă) și se alege forțat o stare validă pentru ea (se „colapsează" starea).
3.  **Propagarea:** Această decizie trimite un „val" de schimbări către celulele vecine, eliminând stările care devin invalide conform regulilor de adiacență.
4.  **Repetiția:** Procesul continuă până când întreaga lume este colapsată sau până când apare o contradicție (caz în care algoritmul trebuie să facă „backtrack" sau să repornească).

WFC în RAMY este utilizat în special pentru generarea structurilor arhitecturale modulare sau a labirinturilor, oferind un nivel de logică structurală imposibil de atins prin zgomot simplu.

### 2.2.3 Eroziunea Hidraulică: Simularea Geologică

Pentru a obține un aspect realist al munților, zgomotul fractal nu este suficient. Eroziunea hidraulică transformă formele „umflate" ale zgomotului Perlin în reliefuri ascuțite, brăzdate de albii de râuri. Simularea implementată în RAMY se bazează pe abordarea Lagrangiană: mii de particule de apă sunt „aruncate" pe teren și își croiesc drum către bazinul hidrografic cel mai apropiat.

Ecuațiile fundamentale ale simulării includ:
-   **Capacitatea de sedimentare ($C$):** $C = V \cdot S \cdot W \cdot K$, unde $V$ este viteza, $S$ este panta, $W$ este volumul de apă, iar $K$ este constanta de capacitate.
-   **Transportul:** Dacă picătura transportă mai puțin decât capacitatea $C$, ea „sapă" în teren (eroziune). Dacă transportă mai mult, ea depune sedimentul (depunere).
-   **Netezirea (Thermal Weathering):** O sub-rutină care simulează prăbușirea materialului nisipos sub influența gravitației, asigurând că pantele nu devin instabile matematic.

Această abordare transformă o problemă pur informatică într-una de simulare fizică, adăugând o valoare estetică superioară prin imitarea proceselor naturale geologice (Beneš & Forsbach, 2002).
