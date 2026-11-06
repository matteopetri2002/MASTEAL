# MASTEAL

Plugin VST3 professionale di *reference matching* per mixing e mastering: misura LUFS,
true peak, immagine stereo, compressione e spettro di un brano (o stem) di riferimento e
applica il match al canale dove è aperto.

## Stato

🚧 In sviluppo — v1.0 non ancora rilasciata. Metodo e decisioni: [`docs/GUIDA.md`](docs/GUIDA.md).

## Avvio (quando ci sarà codice)

1. `cmake -B build -DCMAKE_BUILD_TYPE=Release`
2. `cmake --build build --config Release --parallel`
3. Copia/symlink di `build/MASTEAL_artefacts/Release/MASTEAL.vst3` nella cartella VST3 del sistema, poi caricalo in FL Studio.

## Struttura

- `Source/` — codice del plugin (processor, editor, analisi, DSP, riferimento, UI)
- `Tests/` — unit test sulla logica pura (analisi e DSP)
- `docs/` — GUIDA (processo e decisioni), SPEC (requisiti), ARCHITECTURE (progetto tecnico)

## Sviluppo

- Istruzioni per l'agente: [`AGENTS.md`](AGENTS.md)
- Processo e decisioni: [`docs/GUIDA.md`](docs/GUIDA.md)
- Flusso: Issue → branch `feature/*` da `develop` → PR → review umana → squash merge. `main` protetto.
