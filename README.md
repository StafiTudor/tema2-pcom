## Stafi Tudor-Vasile - 324CA

1. Protocolul 
Protocolul folosit in aceasta tema defineste modul in care
serverul si clientii TCP comunica pentru gestionarea abonarilor la topicuri
si distribuirea mesajelor provenite de la clientii UDP. La initierea unei
conexiuni TCP, clientul trimite un sir care contine ID ul, iar serverul raspunde
fie cu OK\n (daca ID u este unic), fie cu ID already in use\n (daca alt client
cu acelasi ID este deja conectat), apoi inchide conexiunea. Toate mesajele
intre client si server sunt texte ASCII terminate cu newline, permitand practic
delimitarea usoara a fiecaruia.

La primirea unei comenzi de tip subscribe sau unsubscribe, serverul
actualizeaza lista de subscriptii a clientului si se raspunde cu 
Subscribed to topic X\n sau Unsubscribed from topic x\n. Abonarile sunt
pastrate in structura ClientInfo, astfel incat, daca un client se deconecteaza
si dupa se conecteaza iar, ii vor ramane automat toate subscribe urile
anterioare.

Pe de alta parte serverul, pe un socket UDP pentru mesaje binare in formatul:
primii 50 de octeti reprezinta topicul, urmat de un octet care indica tipul
de date si de payload-ul corespunzator. Serverul parseaza topicul, tipul si
valoarea, converteste datele sa se poate citi si construieste un mesaj text
de forma:
IP:PORT – TOPIC – TIP – VALARE\n
Apoi, pentru fiecare client TCP care are in lista sa de subscriptii un pattern
care se potriveste topicului (pattern ul include si wildcard-urile * +,
daca e cazul), serverul trimite acest mesaj text prin TCP.

2. server.c - explicatii:
-alocarile sunt dinamice(nu se pune limita la clienti sau abonamente)
-in acel switch se face conversia din binar in human-readable,
prin verificarea tipurilor si convertirea folosind si functii precum ntohs
de ex.
-cele 2 structuri client si client info sunt pentru a stoca clientii conectati
dar si istoricul celor care s-au deconectat
-folosim select pt a gestiona multiple conexiuni
-functia match este o functie care contine apelurile unei alte fct recursive
de verificare a topicului
-comenzile invalide nu trimit niciun mesaj, pur si simplu nu se executa nimic
si se trece mai departe

3. subscriber.c - explicatii:
-se configureaza socketul si se conecteaza la server
-se dezactiveaza nagle(prin setsockopt)
-se verifica raspunsul serverului(prin OK\n)
-daca e ok, se continua, se prelucreaza stdin, se trimite inapoi raspuns
