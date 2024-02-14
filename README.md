# TEMA 3 APD - PROTOCOLUL BITTORRENT

    - Student: Ciorgan Andrei-Florian
    - Grupa: 332CD

---------- Descriere ----------

In cadrul acestei teme am avut de implementat o varianta simplificata a protocolului BitTorrent prin simularea unei retele de seeds si peeri coordonati de catre un tracker. In implementare, am folosit logica de MPI pentru a lucra cu sisteme distribuite.
Codul in sine este impartit in 2 bucati: peer si tracker. In peer se trateaza logica clientilor iar in tracker logica coordonatorului retelei.

---------- Initializare ----------

La pornirea programului, clienti isi vor citi hashurile pe care le detin din fisierele de input. Apoi, ii vor trimite trackerului datele despre fiser si toate hashurile pe care le are. Trackerul creaza swarmurile (sau face update la swarmurile) asociate fiecerui fisier in care tine date despre cine ce fisiere are.
Cand clientii termina de trimis fiserele detinute, acestia trimit un mesaj de finalizare catre tracker ca sa marcheze ca au terminat pregatirile initiale.
Dupa ce trackerul primeste mesaj de finalizare de initializare de la toti clientii, acesta le trimite un mesaj de start care sa ii anunte ca pot sa incepe sa ceara sa descarce fisiere.

---------- Cerere de fisiere ----------

Fiecare client are 2 threaduri, unul de download si unul de upload. Cel de download are rolul de a comunica cu trackerul pentru eventuale update-uri in retea si de a cere de la peeri segmentele dorite, in timp ce threadul de upload are rolul de a raspunde la cereri trimise de la alti peeri.
Un client incepe prin solicitarea datelor despre swarmul fiserului dorit apoi alege la intamplare peeri de la care cere segmentele dorite. Periodic, clientul trimite trackerului un mesaj de update care sa il anunte ca si el este un peer pentru fisierul partial(sau total) descarcat.
 
---------- Finalizare ----------

Cand un client termina de descarcat toate fisierele dorite, isi inchide threadul de download si anunta trackerul de acest lucru. Cand toti clienti termina ce au de descarcat, trackerul le trimite tuturor niste mesaje de inchidere care se ocupa de oprirea threadurilor de upload. In fine, trackerul se inchide.

---------- Sincronizare ----------

Metoda principala de sincronizare se reduce la natura blocanta a operatilor de Recv din MPI, acestea fiind folosite strategic ca sa simuleze o bariera. Pentru threaduri, ca sa poata comunica corect, ne-am folosit de campul de tag din cadrul operatilor MPI, mesajele trimise cu tagul 0 fiind directionate threadului de download, iar cele cu 1 catre cel de upload.
