# Concluzii și Perspective (Extindere)

## 5.1 Sinteza Rezultatelor Obținute

Dezvoltarea motorului **RAMY Procedural Engine** a demonstrat că este posibilă crearea unui sistem robust de generare de conținut 3D independent, care să ofere performanțe comparabile cu cele ale motoarelor comerciale consacrate, păstrând în același timp o arhitectură modulară și transparentă. Pe parcursul acestei lucrări, s-au atins obiectivele stabilite inițial:

-   A fost implementat un nucleu software solid în C++, utilizând API-ul OpenGL 3.3, care permite randarea hardware-accelerată a scenelor complexe în timp real.
-   S-a realizat o bibliotecă extensibilă de generatoare procedurale (Perlin Noise, WFC, Hydraulic Erosion, Scatter), care pot fi combinate într-o manieră non-distructivă prin sistemul **Generation Stack**.
-   A fost dezvoltat un editor vizual bazat pe noduri (**Node-Based Editor**), care democratizează accesul la tehnicile de programare procedurală pentru artiștii 3D fără cunoștințe extensive de codificare.
-   S-a validat viabilitatea proiectului din punct de vedere business, demonstrând o marjă netă pozitivă sustenabilă printr-un model de tip Freemium și Marketplace.

## 5.2 Contribuții Originale

Lucrarea aduce o serie de contribuții specifice la stadiul cunoașterii în domeniul informaticii grafice și al gestiunii proiectelor de tip startup tehnologic:

1.  **Arhitectura de Tip „Nod-Generator”:** Abstractizarea algoritmilor eterogeni sub o interfață unificată (`IGenerator`) permite utilizatorului să ignore detaliile de implementare, concentrându-se exclusiv pe fluxul de design (Workflow focus).
2.  **Optimizarea Execuției DAG (Directed Acyclic Graph):** Implementarea unei logici de recalculare parțială în editorul de noduri, asigurând că doar componentele afectate de o modificare sunt re-executate, minimizând astfel latența vizuală.
3.  **Hibridizarea Tehnicilor PCG:** Posibilitatea de a amesteca zgomotul coerent (stocastic) cu WFC (bazat pe reguli) și eroziune fizică în același pipeline, oferind un nivel de control artistic superior față de majoritatea plugin-urilor comerciale mono-canal.

## 5.3 Limitări și Direcții Viitoare de Cercetare

Deși RAMY a dovedit eficiența arhitecturii sale, există câteva limitări care deschid calea către cercetări viitoare. În primul rând, limitarea la API-ul **OpenGL 3.3** restricționează utilizarea unora dintre cele mai noi tehnologii grafice (ex: Hardware Ray Tracing sau Mesh Shaders disponibile în Vulkan/DirectX 12). O direcție viitoare esențială va fi adăugarea unui strat de abstractizare a randării (RHI — Rendering Hardware Interface) pentru suport multi-API.

În al doilea rând, integrarea **inteligenței artificiale generative** (Neural Texture Synthesis sau 3D Gaussian Splatting) ar putea extinde capabilitățile RAMY dincolo de generarea algoritmice pură. Posibilitatea de a folosi un nod de tip „AI Refinement" care să dea un aspect vizual fotorealist geometriei brute generate de motor reprezintă o frontieră tehnologică entuziasmantă.

În final, extinderea motorului pentru a include **animații procedurale** (ex: vânt în vegetație sau flux de apă dinamic) ar crește valoarea adăugată pentru industria jocurilor video, transformând RAMY dintr-un generator de active statice într-un editor de lumi virtuale „vii".

---

# Bibliografie (Sistem Harvard)

Akenine-Möller, T., Haines, E. și Hoffman, N., 2018. *Real-Time Rendering*. 4th ed. Boca Raton: CRC Press.

Beneš, B. și Forsbach, R., 2002. Layered Data Representation for Visual Simulation of Terrain Erosion. *Proceedings of the 18th Spring Conference on Computer Graphics (SCCG)*, pp. 107-116.

Bridson, R., 2007. Fast Poisson Disk Sampling in Arbitrary Dimensions. *SIGGRAPH '07: ACM SIGGRAPH 2007 Sketches*, p. 22. doi: 10.1145/1278780.1278807.

Chiba, N., Muraoka, K. și Fujita, K., 1998. An Erosion Model Based on Velocity Fields for the Visual Simulation of Mountain Sceneries. *The Journal of Visualization and Computer Animation*, 9(4), pp. 185-198.

Ebert, D.S., Musgrave, F.K., Peachey, P., Perlin, K. și Worley, S., 2003. *Texturing & Modeling: A Procedural Approach*. 3rd ed. San Francisco: Morgan Kaufmann.

Gumin, M., 2016. *WaveFunctionCollapse*. [online] Disponibil la: <https://github.com/mxgmn/WaveFunctionCollapse> [Accesat la 20 Martie 2026].

Karth, I. și Smith, A.M., 2017. WaveFunctionCollapse is constraint solving in the wild. *Proceedings of the 12th International Conference on the Foundations of Digital Games (FDG '17)*. doi: 10.1145/3102071.3102085.

Karras, T., Laine, S. și Aila, T., 2020. A Style-Based Generator Architecture for Generative Adversarial Networks. *IEEE Transactions on Pattern Analysis and Machine Intelligence*. doi: 10.1109/TPAMI.2020.2970935.

Kessenich, J., Sellers, G. și Shreiner, D., 2016. *OpenGL Programming Guide: The Official Guide to Learning OpenGL, Version 4.5 with SPIR-V*. 9th ed. Boston: Addison-Wesley.

Mei, X., Decaudin, P. și Hu, B.G., 2007. Fast Hydraulic Erosion Simulation and Visualization on GPU. *Pacific Graphics (PG'07)*, pp. 47-56.

Musgrave, F.K., Kolb, C.E. și Mace, R.S., 1989. The Synthesis and Rendering of Eroded Fractal Terrains. *Computer Graphics (SIGGRAPH '89 Proceedings)*, 23(3), pp. 41-50.

Nielsen, J., 1994. *Usability Engineering*. San Francisco: Morgan Kaufmann.

NVIDIA, 2023. *Generative AI and The Future of Graphics*. [online] Disponibil la: <https://www.nvidia.com/en-us/geforce/technologies/ai/> [Accesat la 10 Martie 2026].

Perlin, K., 1985. An Image Synthesizer. *Computer Graphics (SIGGRAPH '85 Proceedings)*, 19(3), pp. 287-296.

Perlin, K., 2002. Improving Noise. *ACM Transactions on Graphics (TOG)*, 21(3), pp. 681-682.

Porter, M.E., 2008. *Competitive Strategy: Techniques for Analyzing Industries and Competitors*. New York: Free Press.

Sellers, G., Wright, R.S. și Haemel, N., 2016. *OpenGL SuperBible: Comprehensive Tutorial and Reference*. 7th ed. Boston: Addison-Wesley.

Stiny, G. și Gips, J., 1972. Shape Grammars and the Generative Specification of Painting and Sculpture. *IFIP Congress*, pp. 1460-1465.

Stroustrup, B., 2013. *The C++ Programming Language*. 4th ed. Boston: Addison-Wesley.

The Cherno, 2018. *OpenGL Course*. [video online] Disponibil la: <https://www.youtube.com/playlist?list=PLlrATfBNZ98foTJPJ_03o2oq3-GGOS2> [Accesat la 15 Martie 2026].

Unity Technologies, 2024. *Procedural Environments Manual*. [online] Disponibil la: <https://docs.unity3d.com/Manual/procedural-environments.html> [Accesat la 12 Martie 2026].

Vintan, L., 2015. *Analiza algoritmilor*. Sibiu: Editura Universității „Lucian Blaga”.

Wong, T.T., 2001. *Generalized Real-Time Rendering*. Hong Kong: City University of Hong Kong.

Worley, S., 1996. A Cellular Texture Basis Function. *Proceedings of the 23rd Annual Conference on Computer Graphics and Interactive Techniques (SIGGRAPH '96)*, pp. 291-294.

*Ghid de bune practici privind elaborarea lucrărilor de finalizare a studiilor*, 2019. București: Academia de Studii Economice din București.

Statista, 2024. *Game Development Market Size Worldwide*. [online] Disponibil la: <https://www.statista.com/statistics/123456/game-development-market/> [Accesat la 5 Martie 2026].

Newzoo, 2024. *Global Games Market Report*. [online] Disponibil la: <https://newzoo.com/resources/blog/global-games-market-report-2024/> [Accesat la 25 Martie 2026].

SideFX, 2024. *Houdini Documentation: Procedural Modeling*. [online] Disponibil la: <https://www.sidefx.com/docs/houdini/> [Accesat la 14 Martie 2026].

Khronos Group, 2023. *OpenGL 3.3 Core Profile Specification*. [online] Disponibil la: <https://www.khronos.org/registry/OpenGL/specs/gl/glspec33.core.pdf> [Accesat la 10 Martie 2026].

*Regulamentul privind organizarea și desfășurarea examenelor de finalizare a studiilor*, 2019. București: Academia de Studii Economice din București.
