# ARCHITECTURE — organizzazione del codice MASTEAL (v1)

## Idea in una frase

Il processor legge l'audio del canale, lo analizza **prima** di processarlo, confronta le
misure con il Fingerprint del riferimento e pilota una catena DSP (EQ → stereo → dinamica
→ loudness) i cui target derivano dalla differenza tra i due fingerprint.

```
file rif → RefTransport → analisi offline → Fingerprint (rif) ──────────┐
                                                                        ▼
audio canale → analisi realtime → Fingerprint (input) → MatchEngine → target DSP
                                                                        │
audio canale ──────────────────────────────────────────────────────► catena DSP → output
                    (MatchEQ → MidSideEQ → MatchComp → GainStage)
                                                     UI ◄── stato (lock-free, ~30 Hz)
```

## Struttura

```
MASTEAL/
├── CMakeLists.txt            # JUCE 8 (FetchContent), target MASTEAL (VST3) + MASTEAL_Tests
├── Source/
│   ├── Processor.{h,cpp}     # AudioProcessor: parametri (APVTS), pipeline, latenza, stato condiviso
│   ├── Editor.{h,cpp}        # AudioProcessorEditor, layout delle zone UI
│   ├── SharedState.h         # struct sola-lettura delle misure + atomics per la UI (lock-free)
│   ├── analysis/
│   │   ├── LoudnessMeter     # K-weighting + gating BS.1770-4 (momentary/short/integrated)
│   │   ├── TruePeakMeter     # oversampling 4x, ITU true peak
│   │   ├── BandAnalyzer      # filter bank ~10 bande 1/3 d'ottava: energia M/S, correlazione,
│   │   │                     #   dinamica per banda (inviluppi 10 ms / 1 s)
│   │   ├── SpectrumAnalyzer  # FFT → 1/3 d'ottava, normalizzazione loudness
│   │   └── OfflineAnalysis   # analisi completa di un buffer (usata per il file di riferimento)
│   ├── dsp/
│   │   ├── GainStage         # trim LUFS smooth + true-peak limiter lookahead
│   │   ├── MidSideEQ         # guadagno differenziale M/S per banda, ±6 dB, smoothing
│   │   ├── MatchEQ           # fitting ≤8 filtri IIR sulla curva differenza 1/3 d'ottava
│   │   └── MatchComp         # compressore feed-forward, threshold/ratio calcolati dal target dinamico
│   ├── matching/
│   │   ├── Fingerprint       # POD serializzabile: loudness, TP, bande S/M+dinamica, spettro, meta
│   │   ├── FingerprintIO     # save/load .mastealref (JSON) + stato plugin
│   │   └── MatchEngine       # diff(fingerprintRif, fingerprintInput) → TargetDsp (smooth, Amount, toggle, FULL/STEM)
│   ├── reference/
│   │   └── RefTransport      # AudioFormatManager/WAV,AIFF,FLAC,MP3 → buffer RAM, waveform,
│   │                         #   play/loop/sync, A/B crossfade 10 ms
│   └── ui/
│       ├── LookAndFeel       # dark theme custom
│       ├── WaveformComponent # waveform riferimento + selezione regione
│       ├── DeltaCards        # 4 card: Loudness / Peak / Stereo / Dynamics (rif vs input vs delta)
│       ├── SpectrumView      # spettro sovrapposto rif/input
│       └── ControlsBar       # Amount, toggle moduli, FULL/STEM, A/B, save/load fingerprint
└── Tests/                    # unit test JUCE: segnali sintetici → attese numeriche (±tolleranze da SPEC)
```

## Moduli (un file = un ruolo)

| Modulo | Ruolo | Può toccare I/O (file, GUI)? |
|---|---|---|
| `analysis/*` | misura segnali → numeri | **no** (logica pura, testabile offline) |
| `dsp/*` | processa buffer secondo target | **no** (logica pura) |
| `matching/*` | diff fingerprint → target DSP; serializzazione | FingerprintIO sì (file); resto no |
| `reference/RefTransport` | import/trasporto/A-B del file di riferimento | sì (disco/audio) |
| `Processor` | assembla tutto, real-time, parametri | sì (audio) — unico orchestratore DSP |
| `Editor` + `ui/*` | mostra lo stato, manda comandi | sì (GUI) — nessuna regola di business |

## Stato (unica fonte di verità)

`Processor` possiede: `Fingerprint rif` (immutable dopo l'analisi offline), misure realtime
dell'input (rinnovate a ~30 Hz), `TargetDsp` corrente (output di MatchEngine), parametri
APVTS. La UI legge una copia lock-free via `SharedState` (atomics + `AbstractFifo`); la UI
**non** chiama mai DSP direttamente, scrive solo parametri.

## Flussi principali

1. **Import riferimento** (thread separato): decode → `OfflineAnalysis` → `Fingerprint` →
   atomic swap in Processor → UI aggiornata. Nessun blocco dell'audio.
2. **processBlock**: copia input pre-DSP nel ring buffer di analisi (lock-free) → catena DSP
   con i `TargetDsp` correnti (smooth già applicato da MatchEngine) → se A/B attivo,
   crossfade verso il buffer del riferimento. Gli analyzer lavorano sul ring buffer a ~30 Hz
   da un timer/thread dedicato, **mai** dentro processBlock a frequenza audio.
3. **MatchEngine**: a ogni update (~30 Hz) calcola `diff(rif, input)`, applica Amount,
   toggle modulo, modalità FULL/STEM (limiti più conservativi), clamp di sicurezza
   (es. ±6 dB M/S per banda) → `TargetDsp` smoothed.

## Regole fisse (vincoli non negoziabili)

- **Analisi pre-DSP**: l'input si misura sempre prima della catena (niente feedback loop).
- **Normalizzazione loudness prima dei confronti spettrali/dinamici** (rif e input allo
  stesso LUFS, altrimenti MatchEQ insegue solo il volume).
- **Realtime-safety** in processBlock: zero allocazioni/lock/I/O (vedi AGENTS.md).
- **Latenza dichiarata** (`setLatencySamples`) = lookahead limiter + eventuali FFT.
- **Fail-safe**: senza riferimento caricato il plugin è trasparente; ogni modulo con
  toggle OFF è bit-exact bypass.

## Dettagli decisi

- **Filter bank a 1/3 d'ottava (~10 bande)** per stereo/dinamica/spettro — *perché:* stessa risoluzione per i tre domini, economica, sufficiente per il matching; no FFT per-blocco nel realtime.
- **MatchEQ come fitting IIR (≤8 filtri) e non convoluzione FIR** — *perché:* latenza zero/minima, CPU bassa, suono "analogico" prevedibile; la curva target viene lisciata prima del fitting.
- **MatchComp feed-forward con target di dynamic range per banda** — *perché:* la "vera" GR di un master è incognita; convergere la dinamica misurata verso il target è robusto e spiegabile all'utente (delta card).
- **Fingerprint come POD JSON** — *perché:* serializzazione stabile tra versioni, diff facile, nessuna dipendenza binaria.
- **Test con segnali sintetici** (tone a -23 LUFS, rumore rosa, sweep, segnali a dinamica nota) — *perché:* verifiche numeriche deterministiche sulle tolleranze SPEC, senza file audio nel repo.
