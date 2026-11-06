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

- **Fase:** 4 (bootstrap e documenti completi — backlog Issue in attesa di approvazione utente)
- **Repository:** https://github.com/matteopetri2002/MASTEAL
- **Prossimo passo:** approvazione del backlog Issue (fase 4) e avvio del loop di sviluppo (fase 5) con la Issue dello skeleton CMake+JUCE.

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
- 🔄 Fase 4 — Backlog Issue GitHub (proposto, in attesa di approvazione utente)
- ⬜ Fase 5 — Loop di sviluppo
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
- [ ] Utente approva il backlog Issue (fase 4)
- [ ] Creazione Issue con `gh issue create` e verifica con `gh issue list`
- [ ] Avvio Issue #1: skeleton CMake+JUCE, plugin che si carica in FL Studio

### Backlog
(in attesa di approvazione — elenco proposto in chat, sarà riportato qui con i numeri Issue)

## Aperto / da decidere

- Nome commerciale definitivo e branding della GUI (il plugin si chiama MASTEAL; logo/font da decidere).
- Se il LUFS di match default sarà *integrated* o *short-term* (in SPEC entrambi selezionabili; default proposto: short-term).
- Distribuzione v1: solo `.vst3` buildato localmente o anche release GitHub con binario?

## Cronologia

- **2026-11-06** — Pianificazione completa in conversazione: concept, stack, DSP, UI, fasi. Decisioni utente: JUCE 8 C++, solo VST3, EQ matching sì, import file, match dinamico applicato, repo pubblico, FL Studio per i test, appunti esclusi dal repo.
- **2026-11-06** — Fase 0: prerequisiti verificati, CMake installato via Homebrew, `.gitignore` creato.
- **2026-11-06** — Fasi 1–3: scritti README.md, AGENTS.md, docs/SPEC.md, docs/ARCHITECTURE.md, docs/GUIDA.md. Bootstrap repo e protezione `main`.
