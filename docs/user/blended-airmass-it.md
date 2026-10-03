# Guida operativa ai modelli aria e al blending SD + Alpha-N

Questa guida documenta la revisione del 2026-09-30 di Speed Density, Alpha-N,
MAF e del blending SD + Alpha-N in TunerStudio. I nomi tra virgolette sono le
etichette inglesi dell'interfaccia. I nomi in `codice` identificano i parametri
MSQ e i canali di log.

Dal 2026-10-03 `capoworks` integra `feature/blended-airmass-pr` fino a
`6d51a9003e`. La nuova logica include fallback dei modelli e recupero automatico
sia standalone sia composito dopo una pubblicazione completa valida; non richiede
riarmo e il priming segue i controlli di avviamento ordinari. Le descrizioni dei
guasti, dei fallback e del riarmo riportate sotto sono storiche. Per il comportamento
corrente consultare la [guida aggiornata](blended-airmass.md).

Le mappe fornite come predefinite sono segnaposto. Prima di usare un modello sul
motore occorre calibrarlo. Il firmware controlla assi, valori, sensori e risultati
numerici, ma non può stabilire se una mappa descrive correttamente il motore.

## 1. Scelta del modello

Il controllo **Base Engine → Base engine → Fuel strategy** (`fuelAlgorithm`)
offre queste scelte:

| Scelta | Funzione |
| --- | --- |
| Speed Density | Usa la mappa SD e la MAP effettiva. |
| Alpha-N | Usa la mappa Alpha-N e il TPS. |
| SD + Alpha-N | Calcola le due masse e le combina con la tabella di autorità. |
| MAF Air Charge | Usa la massa misurata dal MAF e la sua mappa di correzione. |
| Lua | Conserva il comportamento Lua esistente. |

Le tre mappe SD, Alpha-N e MAF sono sempre indipendenti. Non esiste più il
controllo “Dedicated airmass tables” e non esistono dichiarazioni manuali
“map ready”. Selezionare la strategia è l'azione che mette il modello in uso.
MAF rimane standalone e non diventa un terzo ramo del blending.

Gli editor **Fuel → Speed Density VE**, **Alpha-N VE (reference filling)** e
**MAF correction** restano sempre accessibili quando l'iniezione è abilitata.
Questo permette di preparare una calibrazione prima di selezionarla. Il cursore
live di un editor è valido solo quando il relativo modello viene valutato.

## 2. Significato delle mappe

| Mappa | Dimensione | Asse di carico | Significato delle celle |
| --- | --- | --- | --- |
| Speed Density VE | 16 × 16 | MAP effettiva, kPa | Efficienza volumetrica del modello SD. |
| Alpha-N VE (reference filling) | 16 × 16 | TPS, % | Riempimento al riferimento di pressione e temperatura del modello Alpha-N. |
| MAF correction | 16 × 16 | Riempimento non corretto, % | Correzione della massa MAF; 100% è neutro. |
| % Alpha-N contribution | 8 × 8 | TPS, % | 0% usa solo SD; 100% usa solo Alpha-N. |

Le percentuali SD e Alpha-N rappresentano grandezze fisiche diverse. Il blending
non media le percentuali delle celle: combina le masse in grammi per cilindro.
Con autorità `w / 100`:

```text
massa = (1 - w) × massa_SD + w × massa_AlphaN
```

Le normali correzioni VE comuni vengono applicate una volta al risultato. A 0%
il ramo Alpha-N non viene calcolato; a 100% non viene calcolato il ramo SD.

## 3. Impostazioni del modello aria

Aprire **Fuel → Airmass model settings and blending**.

### Temperatura usata nella massa aria

**Air temperature source** (`airmassTemperatureSource`) offre:

- **Tcharge**: temperatura stimata dal modello charge-temperature configurato;
- **IAT**: temperatura misurata dal sensore IAT.

In SD + Alpha-N la temperatura viene acquisita una volta e passata a entrambi i
rami, con la stessa conversione in kelvin e lo stesso risultato di validità. IAT
selezionata richiede una misura valida e non ripiega automaticamente su Tcharge.
MAF non riceve un'altra correzione di densità da questo selettore.

Cambiare sorgente può cambiare la massa anche senza modificare le celle VE.
Rivedere quindi le mappe principali, Idle VE e le correzioni termiche. I canali
`airmassTemperature` e `airmassTemperatureSourceUsed` mostrano e registrano il
valore e la sorgente realmente usati.

### Permesso di usare la stima MAP

**Use MAP estimate table** (`useMapEstimateTable`) è un permesso operativo:

| Permesso | Use MAP estimate during transient | Comportamento |
| --- | --- | --- |
| Off | Inattivo | Usa MAP misurata dove richiesta; nessun fallback o confronto con la stima. |
| On | Off | Può usare una stima valida quando la MAP effettiva non può essere misurata. |
| On | On | Può inoltre eseguire il confronto transitorio già previsto. |

Il permesso resta disponibile con SD, Alpha-N, composito e MAF perché un
consumatore indipendente impostato su **Effective MAP** può richiederlo anche
quando il modello aria attivo non usa pressione. L'opzione transitoria è in
**Fuel → Acceleration enrichment** ed è inattiva quando il permesso generale
è off. Abilitare il permesso non certifica la tabella TPS/RPM: deve essere
calibrata e verificata sul motore.

Una funzione configurata per **Measured MAP** non accetta la stima. **Effective
MAP** invece segue la politica sopra. Se il permesso è off e manca la pressione
richiesta, il firmware segnala un errore invece di inventare un valore.

La normalizzazione barometrica della tabella di stima MAP non è implementata:
rimane uno studio separato.

### Multiply MAP in Alpha-N standalone

**Standalone Alpha-N: Multiply MAP** (`alphaNMultiplyMap`) agisce soltanto con
la strategia Alpha-N standalone:

```text
massa_ibrida = massa_AlphaN_pura × MAP_effettiva / 101,325 kPa
```

Nella formula, `massa_AlphaN_pura` è la massa prima della compensazione BARO
opzionale, che Multiply MAP disattiva. La mappa resta indicizzata da TPS e RPM.
Con l'opzione attiva serve una MAP
effettiva valida, secondo il permesso della stima. In SD + Alpha-N il controllo
è visibile ma inattivo: il ramo Alpha-N rimane puro anche a 100% di autorità.
La stessa mappa viene usata nei due casi, quindi il passaggio richiede una
verifica della calibrazione; le celle non vengono convertite.

### Compensazione barometrica Alpha-N

**Use barometric compensation for Alpha-N** (`alphaNBaroCompensation`) applica:

```text
coefficiente = pressione_ambiente / alphaNBaroReferencePressure
```

La compensazione vale per Alpha-N puro standalone e per il ramo Alpha-N del
composito, prima del blending. È inattiva nello standalone quando Multiply MAP
è attivo, perché la pressione è già presente nell'equazione. Non agisce su SD o
MAF e non introduce una dipendenza BARO quando Alpha-N ha contributo zero.

**Barometric reference pressure** è il riferimento fisso della calibrazione; non
va aggiornato a ogni avviamento. Un BARO mancante o non valido non viene sostituito
silenziosamente con 101,325 kPa. `alphaNBaroCoefficient` registra il rapporto
applicato; `airmassPressureFlags` permette di diagnosticare la provenienza delle
pressioni.

La precedente **Common barometric correction** rimane una correzione carburante
residua e può agire con tutti i modelli. Rivederla quando si abilita la nuova
compensazione Alpha-N, per non applicare due volte lo stesso effetto.

## 4. Sorgenti di carico indipendenti

Ogni tabella o controllo calibrato sceglie la propria sorgente. Cambiare una
sorgente non modifica altri consumatori, gli assi dei modelli aria o l'autorità.

Il selettore comune offre:

| Scelta | Unità e significato |
| --- | --- |
| Model default | SD/composito: MAP effettiva kPa; Alpha-N: TPS %; MAF: riempimento %. |
| Measured MAP | MAP misurata kPa, senza fallback. |
| TPS | Posizione farfalla %. |
| Acc Pedal | Posizione pedale %. |
| Cyl Filling % | Riempimento finale normalizzato %. |
| Effective MAP | MAP effettiva kPa, con stima solo se permessa e valida. |

Gli editor mostrano l'etichetta e il cursore della propria sorgente. Modificare
la sorgente non converte assi, celle o soglie: copiare valori kPa in una tabella
TPS cambiando soltanto l'etichetta produce una calibrazione errata.

Sono indipendenti almeno:

- fase iniezione;
- ogni trim carburante per cilindro;
- regioni STFT e relative soglie di carico;
- ogni trim accensione per cilindro;
- target lambda, tabella di deviazione lambda e soglie del monitor lambda;
- staged injection;
- trailing spark e correzione accensione IAT;
- massimo knock retard e ogni gain knock per cilindro;
- target HPFP;
- target VVT aspirazione e scarico tramite i rispettivi selettori;
- tabelle fan AC-off e AC-on;
- ogni correzione boost open-loop e closed-loop;
- GPPWM, che ora offre anche **Effective MAP**.

Per i trim per cilindro usare le voci **Load sources** nei gruppi Fuel e
Ignition. Le sorgenti knock sono in **Knock table load sources**. STFT, lambda
protection, staged injection, HPFP, fan, boost e VVT espongono il selettore nel
proprio pannello.

I selettori per cilindro sono indipendenti, ma i trim carburante, i trim
accensione e i gain knock conservano un solo vettore di breakpoint di carico
per ciascuna famiglia. Scegliere MAP per un cilindro e TPS per un altro richiede
quindi breakpoint numericamente compatibili per tutte le tabelle della famiglia;
non è possibile ridisegnare l'asse di un solo cilindro. Allo stesso modo, le
tabelle fan AC-off e AC-on hanno sorgenti indipendenti ma condividono i valori
numerici dell'asse X del relativo fan. Le dimensioni delle tabelle non cambiano.

`Measured MAP` ed `Effective MAP` non sono intercambiabili. Una selezione TPS
per una tabella non elimina la MAP richiesta da un modello SD attivo. In caso di
guasto sensore il firmware non cambia automaticamente sorgente.

## 5. Idle VE

**Idle → Idle settings → Use idle VE table** resta l'abilitazione esplicita.
Quando è attiva:

- **Idle VE target model** (`idleVeModel`) assegna l'unica tabella a SD oppure
  Alpha-N nel composito;
- **Idle VE load source** (`idleVeLoadSource`) sceglie indipendentemente la
  coordinata della tabella.

Nel composito, **Model default** segue il modello proprietario: MAP effettiva
per SD e TPS per Alpha-N. Negli standalone segue il carico naturale del modello.
Le scelte esplicite di Idle VE sono **Measured MAP**, **TPS** ed **Effective
MAP**; questo selettore non offre pedale o riempimento finale.

Un Idle VE assegnato a SD può usare TPS; uno assegnato ad Alpha-N può usare MAP.
Il selettore dell'asse non cambia il modello proprietario, il rilevamento del
minimo, il taper o l'autorità. La tabella sostituisce/interpola il parametro VE
del solo modello scelto prima del blending. Se quel modello ha autorità zero,
Idle VE non ha effetto.

Il raccordo idle/main, il ritorno basato su TPS e l'eventuale uso durante il
cranking taper restano attivi secondo le impostazioni Idle esistenti. Cambiare
modello proprietario o sorgente richiede ricalibrazione manuale: esiste una sola
tabella e il firmware non conserva due versioni o converte le celle.

## 6. VE Analyze

VE Analyze standalone continua a usare soltanto la mappa del modello selezionato,
incluso MAF. Se Idle VE è configurata, l'analisi delle mappe principali rimane
disabilitata perché il residuo lambda potrebbe provenire dalla tabella idle o dal
suo taper.

Nel composito VE Analyze è disponibile solo in due casi qualificati:

- tutta la tabella di autorità è esattamente 0%: scrive solo Speed Density VE;
- tutta la tabella è esattamente 100%: scrive solo Alpha-N VE.

La condizione deve restare uniforme per l'intera sessione motore. Una cella
mista, una modifica della configurazione mentre il motore gira o una transizione
di strategia invalida la qualifica fino a un nuovo ciclo fermo/avviato. Dopo
l'avviamento esiste inoltre un'esclusione iniziale di dieci secondi. Queste regole
impediscono di attribuire a un endpoint una misura lambda ritardata proveniente
da una zona mista. `blendedVeAnalyzeEndpoint` vale 0 non qualificato, 1 SD, 2
Alpha-N.

In TunerStudio 3.3.01 le schede e i pulsanti di VE Analyze possono restare
visibili anche quando il modello non è qualificato. La visibilità della scheda
non indica il consenso all’analisi: controllare il canale di qualifica e le
condizioni descritte sopra.

Non usare lo stesso residuo lambda per correggere entrambe le mappe nella zona
mista. Il target lambda usato dall'analizzatore segue la sorgente configurata
per la tabella target lambda.

## 7. Preparazione di una nuova calibrazione

1. Salvare un backup MSQ completo e conservarlo con il relativo INI e firmware.
2. Aprire i tre editor e verificare matrice e assi. Le mappe non usate possono
   restare ai segnaposto, purché abbiano assi validi.
3. Scegliere Tcharge o IAT e verificare le correzioni termiche esistenti.
4. Calibrare SD e Alpha-N separatamente nelle zone in cui contribuiranno.
5. Per ogni consumatore della sezione 4, scegliere la sorgente corrispondente
   alla calibrazione desiderata e rivedere assi e soglie nelle unità corrette.
6. Configurare deliberatamente permesso MAP estimate, Multiply MAP e BARO.
7. Se si usa Idle VE, scegliere modello proprietario e asse e calibrare la
   transizione idle/main.
8. Iniziare il composito con autorità 0% ovunque. Aggiungere gradualmente il
   contributo Alpha-N soltanto dove entrambi i modelli sono stati verificati.
9. Eseguire Burn, riavviare, rileggere l'MSQ e controllare che le scelte siano
   persistite prima di avviare il motore.

La calibrazione motore e la verifica dell'aria di bypass al minimo richiedono
prove reali. Una tabella Idle VE da sola non dimostra che una variazione della
portata di bypass a TPS fisso sia modellata correttamente.

## 8. Aggiornamento di una calibrazione precedente

Non viene fornito un convertitore automatico. La procedura è manuale perché un
cambio di asse o di modello può richiedere decisioni fisiche che uno script non
può dedurre.

1. Salvare il vecchio MSQ e schermate/esportazioni di matrici e assi.
2. Installare firmware e INI corrispondenti alla scheda.
3. Reimpostare o importare l'hardware secondo la normale procedura FOME.
4. Copiare manualmente **matrice e due assi** nella mappa proprietaria corretta:
   vecchia SD in Speed Density VE, vecchia Alpha-N nella mappa Alpha-N, vecchia
   correzione MAF nella mappa MAF.
5. Se il vecchio override VE usava un asse incompatibile con l'asse naturale del
   modello, non reinterpretare i numeri: ricostruire e ricalibrare la mappa.
6. Impostare ogni nuovo selettore di carico alla sorgente che il consumatore
   usava realmente nel vecchio firmware: staging eredita la vecchia sorgente
   lambda/AFR; deviazione e monitor lambda ereditano il vecchio carico carburante
   effettivo. Queste sorgenti potevano già essere diverse. I trim per cilindro
   hanno ora selettori indipendenti tra loro.
7. Configurare esplicitamente temperatura, MAP estimate, Idle VE, Multiply MAP e
   BARO. Il vecchio Alpha-N a 20 °C non ha un equivalente esatto tra Tcharge e IAT.
8. Controllare la common barometric correction per evitare una doppia correzione.
9. Confrontare offline tutte le tabelle e poi eseguire Burn, riavvio e rilettura.

Copiare soltanto le celle senza gli assi non conserva la calibrazione. Passare
da MAP a TPS, da Alpha-N puro a ibrido o da IAT a Tcharge richiede verifica e
possibile ricalibrazione anche quando la matrice non cambia.

## 9. Guasti standalone e latch del composito

In Speed Density, Alpha-N e MAF standalone un calcolo aria non valido inibisce
temporaneamente le nuove iniezioni. Gli impulsi già accettati terminano e il
firmware riprende automaticamente dopo una nuova pubblicazione valida: non
serve arrestare o premere il riarmo. Il priming standalone rimane consentito.

Il composito conserva il latch di sicurezza già previsto:

| Stato | Significato |
| --- | --- |
| Standalone | Strategia non composita. |
| Waiting for valid fuel | In attesa di un calcolo completo valido. |
| Ready | Il modello aria ammette nuove iniezioni, salvo altri limiter. |
| Fault latched | Nuove iniezioni bloccate fino al riarmo corretto. |

In caso di fault memorizzato nel composito:

1. registrare `blendedFault`, `blendedFlags` e il contesto del log;
2. correggere sensore, asse, sorgente o configurazione;
3. arrestare il motore e attendere la fine dei denti recenti e degli eventi
   iniezione pendenti;
4. premere **Rearm after stopping**;
5. riavviare e attendere un nuovo calcolo valido.

Prima di entrare nel composito o uscirne, arrestare il motore e attendere che
gli eventi pendenti siano terminati. Un cambio con motore in movimento o callback
pendenti genera esso stesso un fault `StrategyChange`, anche senza un guasto
sensore precedente.

Cambiare strategia non cancella un fault composito memorizzato. Gli impulsi già
accettati terminano normalmente; il latch impedisce nuovi eventi e il priming
composito resta disabilitato. A 0 RPM visualizzati il riarmo può ancora essere
rifiutato se il firmware rileva denti recenti o callback pendenti.

## 10. Canali utili nel log

| Canale | Significato |
| --- | --- |
| `blendedSdMass`, `blendedAlphaNMass` | Masse grezze dei due modelli. |
| `blendedRequestedAuthority` | Autorità richiesta dalla tabella. |
| `blendedEffectiveAuthority` | Autorità del calcolo valido. |
| `blendedSdLoad`, `blendedAlphaNLoad` | Coordinate catturate dei due modelli. |
| `blendedSdVe`, `blendedAlphaNVe` | Valori delle mappe prima delle correzioni comuni. |
| `blendedCorrection` | Prodotto delle correzioni VE comuni. |
| `blendedStatus`, `blendedFault`, `blendedFlags` | Stato, causa memorizzata e validità. |
| `airmassTemperature`, `airmassTemperatureSourceUsed` | Temperatura e sorgente realmente usate. |
| `alphaNBaroCoefficient` | Coefficiente BARO applicato ad Alpha-N. |
| `airmassPressureFlags` | Provenienza/validità delle pressioni del modello. |
| `blendedVeAnalyzeEndpoint` | 0 non qualificato, 1 SD, 2 Alpha-N. |
| canali `…Load` | Coordinata realmente pubblicata per ciascun consumatore. |

I bit storici di `blendedFlags` rimangono: 1 SD valutato, 2 Alpha-N valutato,
4 SD valido, 8 Alpha-N valido, 16 stima MAP usata, 32 calcolo valido, 64 stima
valutata. Un ramo non valutato può mostrare zero: significa indisponibile, non
una massa fisica nulla.

## 11. Problemi frequenti

| Sintomo | Verifica |
| --- | --- |
| Non vedo un editor aria | Controllare che l'iniezione sia abilitata; gli editor non dipendono più dalla strategia selezionata. |
| Il cursore di una mappa aria resta a zero | Il modello non è attualmente valutato; la mappa resta comunque modificabile. |
| MAP estimate non viene usata | Verificare il permesso generale, la validità di TPS/RPM/tabella e che il consumatore chieda Effective MAP. |
| MAP stimata valida ma fault di carico | Cercare consumatori configurati per Measured MAP. |
| Multiply MAP non cambia il composito | È corretto: l'opzione vale solo per Alpha-N standalone. |
| Compensazione BARO non agisce | Verificare Alpha-N con contributo non nullo, Multiply MAP inattivo nello standalone, sensore BARO e riferimento. |
| Cambiando sorgente il cursore non coincide con la tabella | Verificare di aver modificato il selettore del consumatore corretto e ricalibrato l'asse. |
| Idle VE non cambia il risultato | Il modello proprietario può avere autorità zero oppure il controllo idle/taper può essere inattivo. |
| VE Analyze non disponibile nel composito | Serve autorità uniforme 0% o 100% per l'intera sessione, Idle VE disabilitata e il periodo iniziale trascorso. |
| Sensore ripristinato ma iniezione standalone ancora tagliata | Attendere una nuova pubblicazione aria valida e verificare gli altri limiter; non serve il riarmo del composito. |
| Sensore ripristinato ma iniezione composita ancora tagliata | Un fault memorizzato richiede arresto, drenaggio degli eventi pendenti e riarmo. |

## 12. Limiti della qualifica

Usare sempre l'INI generato insieme al firmware della propria scheda. Le prove
software e al banco verificano il comportamento digitale, non la correttezza
della calibrazione su un motore. Temperatura della carica, compensazione BARO,
transitori MAP, aria di bypass e passaggi tra modelli devono essere verificati
con log e misure sul veicolo.

Riferimenti tecnici:

- [Requisiti della revisione](../development/blended-airmass-revision.md)
- [Inventario dei consumatori di carico](../development/blended-load-consumers.md)
- [Contratti degli ingressi aria](../development/airmass-input-contracts.md)
- [Studio BARO per la stima MAP, non implementato](../development/map-estimate-baro-study.md)
- [Template TunerStudio](../../firmware/tunerstudio/tunerstudio.template.ini)
- [Definizioni della configurazione](../../firmware/integration/fome_config.txt)
