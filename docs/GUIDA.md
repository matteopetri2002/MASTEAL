# Guida progetto MASTEAL

## Obiettivo

Costruire **MASTEAL**, un plugin VST3 professionale di *reference matching* per mixing e
mastering: si carica un riferimento (canone master o singolo stem), il plugin misura
loudness (LUFS), true peak, immagine stereo, dinamica (compressione) e spettro, e applica
al canale dove è aperto una catena di processing che avvicina l'audio dell'utente al
riferimento. Include il workflow "copia/incolla": il fingerprint misurato si salva come
preset riutilizzabile su altri canali e progetti.

## Come usare questa guida

Documento vivo, aggiornato a ogni decisione. Copia canonica: questo file nel repo.
Metodo di lavoro: `checklist-agentica-completa.md` dell'utente (fasi 0→7, Issue → branch
`feature/*` da `develop` → PR → review umana → squash merge, `main` protetto).

## Stato attuale

- **Fase:** 5 — loop di sviluppo (Issue #1 e #2 chiuse; 12 rimanenti)
- **Repository:** https://github.com/matteopetri2002/MASTEAL
- **Prossimo passo:** Issue #3 — RefTransport: import file, waveform, trasporto, A/B.

## Procedura di bootstrap (sintesi)

1. Prerequisiti verificati: Xcode Command Line Tools (clang 17), git 2.50, gh 2.102 autenticato come `matteopetri2002`, Homebrew 7, identità git configurata. CMake installato via Homebrew.
2. `.gitignore` creato prima di qualsiasi commit (esclude anche i due file di appunti personali).
3. `git init -b main`, primo commit di bootstrap, repo pubblico creato con `gh repo create`.
4. Branch `develop` creato e pushato. Protezione di `main` attivata via API e verificata con push di prova rifiutato (GH006).

## Metodologia

- ✅ Fase 0 — Bootstrap tecnico
- ✅ Fase 1 — Fondamenta (README, AGENTS.md, GUIDA.md)
- ✅ Fase 2 — Requisiti (docs/SPEC.md)
- ✅ Fase 3 — Progettazione (docs/ARCHITECTURE.md)
- 🔄 Fase 4 — Backlog Issue GitHub (✅ approvate e create #1–#14 il 2026-11-06)
- 🔄 Fase 5 — Loop di sviluppo (in avvio con Issue #1)
- ⬜ Fase 6 — Release v1.0.0
- ⬜ Fase 7 — Memoria del progetto (continuo)

## Decisioni prese

- **Stack:** JUCE 8 (C++) con CMake, JUCE via FetchContent dentro `build/` — *perché:* standard dell'industria per plugin professionali, GUI ricca, VST3 nativo, nessuna installazione globale oltre a CMake.
- **Formato v1: solo VST3** — *perché:* decisione utente; FL Studio (DAW di test) lo supporta pienamente. AU recuperabile in fase 2 a costo quasi nullo (stesso toolchain). AAX escluso (licenza Avid).
- **AU/AAX fuori scope v1** — *perché:* come sopra, annotato in SPEC.
- **Caricamento del riferimento: import file audio nel plugin** (WAV/AIFF/FLAC/MP3) con waveform, trasporto, A/B — *perché:* standard della categoria (Metric AB, REFERENCE), analisi offline istantanea, niente routing sidechain.
- **Compressione: match dinamico applicato** (compressore interno auto-pilotato sul target stimato) + override manuali ratio/attack/release — *perché:* è il differenziante del prodotto rispetto ai meter passivi.
- **EQ matching incluso in v1** (fitting IIR su differenza a 1/3 d'ottava) — *perché:* senza il match spettrale il matching suona "a metà".
- **Workflow copia/incolla:** struct `Fingerprint` serializzabile in file `.mastealref` — *perché:* realizza il gesto richiesto dall'utente: misurare su un progetto, incollare su un altro canale/progetto senza ricaricare l'audio.
- **Due modalità d'uso:** FULL MATCH (master vs mix/canale) e STEM MATCH (stem vs singolo strumento), stesso motore DSP, soglie adattate in STEM — *perché:* secondo requisito fondante del prodotto.
- **Repo pubblico** — *perché:* protezione di `main` gratis su piano Free (come da checklist).
- **File di appunti personali esclusi dal repo** (`.gitignore`) — *perché:* note su OpenCode/modelli, non riguardano il plugin.
- **DAW per review umana: FL Studio** — *perché:* DAW dell'utente; ogni PR conterrà passi di test concreti su FL Studio.
- **Eccezione bootstrap:** il primo commit (repo vuoto) contiene `.gitignore` + README + AGENTS + docs/ direttamente su `main` — *perché:* eccezione consapevole prevista dalla checklist; da subito dopo `main` è protetto e si passa solo da PR.

## Da fare

### Step attuale
- [x] Issue #1 e #2 completate (PR #15 e #16, review umana FL Studio OK)
- [ ] Issue #3 — feat: RefTransport — import file, waveform, trasporto, A/B

### Backlog (ordine di lavoro = numero Issue)

1. Issue #1 — chore: skeleton CMake + JUCE 8, target VST3 + Tests
2. Issue #2 — feat: parametri APVTS e SharedState lock-free
3. Issue #3 — feat: RefTransport — import file, waveform, trasporto, A/B
4. Issue #4 — feat: LoudnessMeter BS.1770-4 e TruePeakMeter realtime
5. Issue #5 — feat: BandAnalyzer (stereo/dinamica per banda) e SpectrumAnalyzer
6. Issue #6 — feat: OfflineAnalysis + Fingerprint + FingerprintIO (.mastealref)
7. Issue #7 — feat: UI MeterBridge — delta card, SpectrumView, waveform
8. Issue #8 — feat: GainStage — match LUFS + limiter true peak
9. Issue #9 — feat: MatchEQ — fitting IIR sulla differenza spettrale
10. Issue #10 — feat: MidSideEQ — match stereo per banda
11. Issue #11 — feat: MatchComp — match dinamico auto-pilotato
12. Issue #12 — feat: MatchEngine completo — Amount, toggle, FULL/STEM
13. Issue #13 — feat: workflow copia/incolla — save/load Fingerprint, preset senza audio
14. Issue #14 — chore: polish v1 — CPU, validazione FL Studio, release readiness

Nota d'ordine (aggiustamento tecnico rispetto alla proposta approvata): i meter di analisi
(Issue #4–5) precedono OfflineAnalysis/Fingerprint (#6), perché l'analisi offline del
riferimento riusa gli stessi analyzer del realtime. Contenuto e dipendenze invariati.

## Aperto / da decidere

- **GUI definitiva "di livello professionale" (richiesta esplicita utente, 2026-11-06):** aspetto super moderno e comprensibile, visualizzazione spettro audio e simili, al pari dei plugin commerciali. Già coperta dal backlog: Issue #7 (LookAndFeel, delta card, SpectrumView) e Issue #14 (polish v1). Refinement visivo aggiuntivo va deciso dopo #7 sulla base di ciò che sarà funzionale.
- Nome commerciale definitivo e branding della GUI (il plugin si chiama MASTEAL; logo/font da decidere).
- Se il LUFS di match default sarà *integrated* o *short-term* (in SPEC entrambi selezionabili; default proposto: short-term).
- Distribuzione v1: solo `.vst3` buildato localmente o anche release GitHub con binario?

## Cronologia

- **2026-11-06** — Pianificazione completa in conversazione: concept, stack, DSP, UI, fasi. Decisioni utente: JUCE 8 C++, solo VST3, EQ matching sì, import file, match dinamico applicato, repo pubblico, FL Studio per i test, appunti esclusi dal repo.
- **2026-11-06** — Fase 0: prerequisiti verificati, CMake installato via Homebrew, `.gitignore` creato.
- **2026-11-06** — Fasi 1–3: scritti README.md, AGENTS.md, docs/SPEC.md, docs/ARCHITECTURE.md, docs/GUIDA.md. Bootstrap repo e protezione `main`.
- **2026-11-06** — Fase 4: backlog approvato dall'utente, create 14 Issue (#1–#14) con verifica numerazione. Aggiustamento tecnico d'ordine: meter (#4–5) prima di OfflineAnalysis/Fingerprint (#6). Avvio Issue #1.
- **2026-11-06** — Issue #1: prima build fallita per Command Line Tools corrotti (`'algorithm' file not found`, `usr/include/c++/v1` con soli 3 file, nessuna ricevuta pkgutil). Utente reinstallato i CLT (ora clang 21): toolchain verificato OK. Vedi "Lezioni apprese".
- **2026-11-06** — Issue #1 chiusa: PR #15 squash-mergiata in develop (`50fc8db`), branch eliminato, review umana OK in FL Studio (passthrough invariato, GUI placeholder, nessun crash). Fix lungo la strada: `LANGUAGES C CXX`, API `UnitTestRunner` (somma `getResult(i)->failures`), `add_test` con `$<TARGET_FILE:...>`, `BUNDLE_ID` esplicito senza spazi.
- **2026-11-06** — Issue #2 chiusa: PR #16 squash-mergiata (`8572ea0`). 10 parametri APVTS + SharedState lock-free + harness test SharedState. Feedback utente esplicito: GUI finale da portare a livello "plugin professionale" (assegnata a #7/#14, registrata in "Aperto / da decidere").

## Lezioni apprese

- **`'algorithm' file not found` con clang su macOS** → *causa:* installazione Command Line Tools corrotta/obsoleta dopo un aggiornamento di macOS (libreria C++ standard mancante). *Diagnosi:* `printf '#include <algorithm>\nint main(){}\n' > /tmp/t.cpp && clang++ /tmp/t.cpp` fallisce anche fuori progetto; `ls /Library/Developer/CommandLineTools/usr/include/c++/v1 | wc -l` ≈ 0. *Soluzione:* `sudo rm -rf /Library/Developer/CommandLineTools && xcode-select --install` (azione dell'utente, richiede sudo).
- **JUCE via CMake richiede `LANGUAGES C CXX`** nel `project()`, non solo CXX: errore di configure "A C compiler is required to build targets that depend on JUCE".
- **Non usare `| tail -N` sui comandi di build in diagnosi**: nasconde l'errore reale; rilanciare il solo configure con output completo.
