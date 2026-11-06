# AGENTS.md — istruzioni operative per l'agente AI

## Progetto

Plugin VST3 di reference matching audio (JUCE 8, C++) — contesto: `README.md`, `docs/GUIDA.md`.
Requisiti: `docs/SPEC.md`. Struttura tecnica: `docs/ARCHITECTURE.md`.

## Avvio e verifica

- Build: `cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --config Release --parallel`
- Test automatici (logica pura): `cmake --build build --target MASTEAL_Tests && ctest --test-dir build --output-on-failure`
- Verifica manuale: l'utente carica `MASTEAL.vst3` in **FL Studio** e segue i passi scritti nella PR. Review umana obbligatoria.

## Struttura e responsabilità

- `Source/Processor.*` — AudioProcessor: pipeline DSP, parametri, latenza dichiarata
- `Source/Editor.*` + `Source/ui/` — GUI (adattatore: legge lo stato, non contiene regole DSP)
- `Source/analysis/` — misurazioni (logica pura: solo buffer audio in ingresso, nessun I/O)
- `Source/dsp/` — processing (logica pura)
- `Source/reference/` — import file, trasporto A/B, Fingerprint (unico punto che tocca il disco)
- Regola di separazione: **`analysis/` e `dsp/` non includono mai header JUCE GUI né toccano file/rete**; tutto deve essere testabile con segnali sintetici offline.

## Convenzioni

- JUCE 8 via FetchContent; **nessun'altra dipendenza senza OK esplicito dell'utente**.
- Identificatori nel codice in inglese; testi visibili della GUI in inglese; documentazione in italiano.
- Commit in Conventional Commits (`feat:`, `fix:`, `docs:`, `chore:`, `refactor:`, `test:`).
- Realtime-safety nel `processBlock`: niente allocazioni, niente lock, niente I/O, niente `MessageManager`. Analisi aggiornate a ~30 Hz con lock-free (atomic / AbstractFifo).
- Latenza introdotta (lookahead limiter ecc.) sempre dichiarata con `setLatencySamples`.

## Cosa NON fare

- Mai commit/push diretti su `main` (protetto: solo via PR).
- Mai funzionalità fuori da `docs/SPEC.md`.
- Mai aggiungere tooling/build/dipendenze senza OK.
- Mai analizzare l'output già processato dal plugin stesso (l'analisi dell'input si prende **prima** della catena DSP).
- Mai confrontare spettri/dinamica senza prima normalizzare la loudness dei due segnali.
- Mai mergiare PR senza l'esito della review umana dell'utente.

## Definition of Done

1. Fa quello che dice la Issue, e solo quello.
2. Build pulita + test automatici passati, risultato riportato nella PR (es. `PASS: 15, FAIL: 0`).
3. Passi di test su FL Studio scritti nella PR ed eseguiti dall'utente con esito OK.
4. PR squash-mergiata in `develop`, branch eliminato, Issue chiusa a mano.
5. `docs/GUIDA.md` aggiornata (stato, cronologia, decisioni nuove).
