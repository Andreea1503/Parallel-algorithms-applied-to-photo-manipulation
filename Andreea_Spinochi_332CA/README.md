Tema 1 - APD
===================

Pentru a paraleliza programul și a atinge scopul final al temei, care este de a
avea timpi de execuție mai buni decât cei obținuți cu execuția
secvențială, paralelizat funcțiile sample_grid, march, și rescale_image,
similar cu ceea ce am realizat într-un exercițiu de laborator numit
multiply_outer.c.

Am creat o structură pentru thread, care conține un identificator (ID) pentru
fiecare thread, un pointer către structura imaginii și numărul total de
thread-uri. Structura imaginii include imaginea inițială și toate transformările
prin care trece aceasta, pentru a facilita lucrul cu memoria și cu thread-urile
și pentru a evita conflictele.

Am implementat o barieră pentru a sincroniza thread-urile, astfel încât niciun
thread să nu înceapă execuția până când toate celelalte nu au terminat. Am
folosit această barieră la finalul fiecărei funcții paralelizate, astfel încât
thread-urile să nu avanseze în program până când imaginea nu este complet
transformată. Acest lucru este important deoarece toate thread-urile accesează
aceeași imagine și, dacă un thread ar continua execuția fără să sincronizeze,
ar putea modifica imaginea înainte ca celelalte thread-uri să termine de executat.

Am creat toate thread-urile înainte de a începe execuția, așa cum este specificat
în cerință, și le-am furnizat structura de thread ca parametru, pentru a permite
fiecărui thread să modifice imaginea. După ce funcțiile care modifică imaginea și
care au fost paralelizate s-au terminat de rulat, am așteptat ca toate thread-urile
să finalizeze execuția utilizând funcția pthread_join, pentru a evita probleme cu
memorie.

Apoi am distrus barierele, am returnat imaginea și am eliberat memoria alocată.

Această abordare asigură o execuție sincronizată a thread-urilor pentru a
îmbunătăți performanța programului, realizând operații paralele într-un mod
sigur.





