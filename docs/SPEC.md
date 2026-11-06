# SPEC — requisiti MASTEAL (v1)

Fonte di verità su *cosa deve fare il progetto*. Se non è scritto qui, non si fa
(si propone prima come decisione in `docs/GUIDA.md`).

## Scopo

Plugin VST3 (macOS, arm64 e x86_64) che confronta l'audio del canale in cui è aperto con
un file audio di riferimento caricato dall'utente, ne misura le differenze (loudness, true
peak, immagine stereo, dinamica/compressione, spettro) e applica una catena di processing
che avvicina l'audio del canale al riferimento, con quantità regolabile.

## Modalità d'uso

- **FULL MATCH** — il riferimento è una canzone/master completa; il plugin è tipicamente sul
  mix bus o su un canale. Tutti i 5 moduli di match sono applicabili.
- **STEM MATCH** — il riferimento è il singolo stem di uno strumento (batteria, basso,
  voce…); il plugin è sul canale dello strumento dell'utente. Il motore è identico; il
  match EQ e dinamico usa soglie conservative adatte a materiale mono-strumentale.
- Selettore di modalità nell'UI: `FULL | STEM`.

## Caricamento del riferimento

- Import tramite drag&drop o file browser interno: formati **WAV, AIFF, FLAC, MP3**.
- Il file è decodificato e bufferizzato in RAM (fino a ~20 min stereo); l'analisi del
  riferimento è **offline e istantanea** all'import (non richiede il playback).
- Waveform del riferimento visualizzata, con selezione di una **regione di analisi**
  (default: intera canzone; nelle canzoni masterizzate l'utente può escludere intro/fade).
- Trasporto interno: play/stop/loop, sync opzionale al trasporto DAW.
- **A/B istantaneo**: ascolto del riferimento al posto dell'audio del canale, con crossfade
  di 10 ms (niente click). Livello del riferimento normalizzato al suo LUFS misurato per
  confronti onesti.
- Selezione regione da analizzare anche sull'input del canale (finestra temporale).

## Misure (sia sul riferimento sia sull'input del canale)

| Misura | Standard | Precisione richiesta |
|---|---|---|
| Loudness | ITU-R BS.1770-4: momentary (400 ms), short-term (3 s), integrated | ±0.1 LU rispetto a meter conformi (es. Youlean) |
| True peak | ITU-R BS.1770-4 (oversampling 4x) | ±0.1 dBTP |
| Immagine stereo | energia S/M e correlazione per banda (~10 bande a 1/3 d'ottava) + larghezza complessiva | tendenza e confronto coerenti, valore assoluto non normato |
| Dinamica/compressione | stima gain reduction: rapporto inviluppo breve (~10 ms) vs lungo (~1 s), LRA, dinamica per banda | confrontabile ref vs input sullo stesso segnale (stesso algoritmo sui due lati) |
| Spettro | FFT a 1/3 d'ottava, normalizzato in loudness prima del confronto | ±1 dB su segnale stazionario |

Tutte le misure sono mostrate side-by-side (riferimento vs input vs delta) nella UI.

## Processing (match applicato al canale)

Ordine catena: `input → analisi(pre-DSP) → MatchEQ → MidSideEQ → MatchComp → GainStage(trim LUFS + TP limiter) → output`.

1. **GainStage** — trim in dB verso il LUFS target (default: short-term del riferimento),
   smoothing; limiter true-peak con lookahead solo se il target TP è ecceduto.
2. **MidSideEQ** — per banda, guadagno differenziale M/S verso il rapporto S/M del
   riferimento, limitato a ±6 dB, con smoothing temporale (anti-pumping).
3. **MatchEQ** — curva differenza spettrale a 1/3 d'ottava (ref − input, entrambi
   normalizzati in loudness), approssimata con max 8 filtri IIR (peaking/shelf).
4. **MatchComp** — compressore feed-forward le cui soglie sono **calcolate automaticamente**
   per portare la dinamica dell'input verso quella stimata del riferimento; ratio/attack/
   release esposti come override manuali.
- **Amount globale 0–100%**: interpola tra "nessuna correzione" e "match completo" per
  ogni modulo; crossfade/bypass A-B del processing con fade 10 ms.
- **Toggle per modulo**: EQ / Stereo / Dyn / Loudness attivabili singolarmente.
- Ogni modulo mostra nella UI "cosa sta correggendo" (delta applicato in dB).

## Workflow copia/incolla (fingerprint)

- Il risultato dell'analisi del riferimento è un **Fingerprint** (misure loudness, TP,
  stereo per banda, dinamica, spettro + metadati: nome file, regione, modalità).
- **Salva/Carica** del Fingerprint come file `.mastealref` (JSON) → l'utente "copia" i
  parametri da un progetto e li "incolla" in un altro senza ricaricare l'audio.
- Il Fingerprint si salva anche nello stato del plugin (richiamo preset DAW senza file audio).

## UI

- Finestra ridimensionabile, HiDPI, dark theme.
- Zone: (1) waveform + trasporto + A/B; (2) delta card Loudness/Peak/Stereo/Dynamics +
  spettro sovrapposto ref/input; (3) Amount globale, toggle moduli, selettore FULL/STEM,
  save/load Fingerprint.
- Meter dell'input aggiornati in tempo reale (~30 Hz).

## Stati e schermate

- **Nessun riferimento caricato**: plugin trasparente (bypass), UI invita a caricare un file; meter dell'input comunque attivi.
- **Riferimento caricato, match OFF**: solo analisi e meter side-by-side.
- **Match ON**: processing attivo secondo Amount e toggle; delta card mostrano la correzione applicata.
- **A/B riferimento**: l'output passa al file di riferimento con crossfade.

## Criteri di accettazione (verifica manuale)

1. Il plugin VST3 si carica in FL Studio (arm64) senza crash e processa audio trasparente a match OFF. *Come si verifica:* progetto FL vuoto, MASTEAL su un canale audio, play di un loop → suono identico.
2. Import di un WAV di riferimento funzionante: waveform visibile, playback A/B con crossfade senza click. *Come si verifica:* drag&drop WAV nel plugin, premi A/B → senti il riferimento al posto dell'input.
3. LUFS/TP misurati coincidono entro ±0.1 con meter di riferimento esterni sullo stesso file. *Come si verifica:* stesso WAV in MASTEAL e in un meter conforme (o unit test con segnale sintetico a -23 LUFS).
4. Match loudness: attivando il solo modulo Loudness, l'output raggiunge il LUFS target del riferimento (±0.3 LU stazionario). *Come si verifica:* meter FL Studio / unit test con rumore a livello noto.
5. Match stereo: su riferimento più largo dell'input, MidSideEQ allarga; Amount 0% = nessuna modifica. *Come si verifica:* unit test + ascolto A/B.
6. Match EQ: su riferimento più brillante, l'output guadagna alte frequenze; delta ≤ ±curve target. *Come si verifica:* unit test con sweep/rumore rosa.
7. Match dinamico: su riferimento più compresso, l'output riduce il proprio dynamic range verso il target. *Come si verifica:* unit test con segnale a dinamica nota.
8. Fingerprint save/load: salvando `.mastealref` da un'istanza e caricandolo in un'altra (anche senza file audio), le misure target e il comportamento di match sono identici. *Come si verifica:* due progetti FL, save/load, confronto meter.
9. Latenza dichiarata correttamente (compensazione DAW attiva, nessun flam su transienti multi-canale). *Come si verifica:* stessa sorgente su due canali, uno con plugin → allineamento in FL.
10. CPU: ≤ ~15% di un core M-series con match completo attivo e buffer 256. *Come si verifica:* meter CPU di FL Studio.

## Dati persistenti

- File `.mastealref` (JSON, cartella scelta dall'utente).
- Stato del plugin nei preset della DAW (parametri + Fingerprint embedded).
- Il file audio di riferimento **non** è copiato nel progetto: nel Fingerprint resta solo il
  percorso originale + le misure (se il file non c'è più, il match funziona comunque —
  servono solo le misure; l'A/B del riferimento no).

## Fuori scope (v1) — NON si fa ora

- Formati AU e AAX (AU: recuperabile in fase 2 a basso costo).
- Windows (target v1: macOS arm64; x86_64 se viene gratis dal toolchain).
- Oversampling del processing (solo il true-peak meter ha oversampling interno 4x).
- Sidechain input dalla DAW come sorgente del riferimento.
- MIDI, qualsiasi forma di synthesis, multicanale oltre lo stereo.
- Analisi automatica di sezioni (verse/chorus) e matching per-sezione.
- Installer/pkg firmato e notarizzazione (il `.vst3` si carica localmente; distribuzione da decidere).
- Si aggiunge solo con una nuova decisione registrata in `docs/GUIDA.md`.
