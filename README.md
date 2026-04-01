# Voice Changer (WASAPI + DSP)

Application C++ Windows de transformation vocale en temps reel.

Le pipeline capture le son via WASAPI, applique une chaine d'effets DSP (Pitch, EQ, Gain, Clipper), puis renvoie le flux vers un peripherique de sortie.

## Fonctionnalites

- Capture/rendu WASAPI en mode partage event-driven.
- Chaine DSP temps reel:
  - Pitch shift (SoundTouch)
  - EQ 3 bandes (HP -> Presence -> LP)
  - Gain
  - Soft clipping
- Mode IA local (prototype) avec profils timbraux: `neutral`, `bright`, `dark`, `robot`.
- Presets predefinis: `girl`, `demon`, `robot`, `radio`.
- Monitoring runtime: frames capture/rendu, xruns, latence approximative.

## Build

Prerequis:

- Windows
- CMake 3.20+
- Compilateur C++20 (MSVC)

Commandes:

```powershell
cmake -B build
cmake --build build --config Release
```

Build debug:

```powershell
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug
```

## Tests unitaires

```powershell
ctest --test-dir build --output-on-failure
```

## Cible diagnostic CMake

```powershell
cmake --build build --target run_diagnostic_runtime --config Debug
```

## Utilisation

Lister les peripheriques:

```powershell
.\build\Release\voice_changer.exe --list-devices
```

Lancer avec indices explicites:

```powershell
.\build\Release\voice_changer.exe --in-index 0 --out-index 2 --preset girl
```

Mode IA local:

```powershell
.\build\Debug\voice_changer.exe --in-index 0 --out-index 2 --preset girl --mode ai --ai-profile bright --ai-blend 0.7 --ai-intensity 0.5
```

Lancer avec sous-chaines de noms:

```powershell
.\build\Release\voice_changer.exe --in Microphone --out Speakers --pitch-semitones 4 --gain 1.7
```

## Parametres CLI

- `--list-devices`
- `--in <substring>`
- `--out <substring>`
- `--in-index <index>`
- `--out-index <index>`
- `--gain <float>`
- `--clip-drive <float>`
- `--clip-out <float>`
- `--pitch-semitones <float>`
- `--mode <dsp|ai>`
- `--ai-profile <neutral|bright|dark|robot>`
- `--ai-blend <float>`
- `--ai-intensity <float>`
- `--st-seq-ms <int>`
- `--st-seek-ms <int>`
- `--st-overlap-ms <int>`
- `--preset <girl|demon|robot|radio>`
- `--fine-tune-audio <path>` (Nouveau)
- `--preset-name <name>` (Nouveau)
- `--load-preset <name>` (Nouveau)
- `--show-presets-dir` (Nouveau)
- `--ai-model <path>` (Nouveau, ONNX optionnel)

## Fine-tuning et Presets (Nouveau)

Entrainate un preset sur un echantillon de reference:

```powershell
.\build\Release\voice_changer.exe --fine-tune-audio "C:\samples\target_voice.wav" --preset-name "VoixCible_v1"
```

Charger et utiliser le preset:

```powershell
.\build\Release\voice_changer.exe --in-index 0 --out-index 2 --load-preset "VoixCible_v1"
```

Les presets sont sauvegardes sous `~/.voice-changer/presets/`.
Les presets sont sauvegardes sous `C:/Users/<user>/Documents/voice-changer/presets/`.

Afficher le dossier exact:

```powershell
.\build\Release\voice_changer.exe --show-presets-dir
```

Utiliser un modele ONNX (si binaire compile avec ONNX Runtime):

```powershell
.\build\Release\voice_changer.exe --in-index 0 --out-index 2 --mode ai --load-preset "VoixCible_v1" --ai-model "D:\models\tiny_vc.onnx"
```

## Bootstrap neuronal (scripts Python)

Les scripts de depart sont dans `scripts/neural`:

- `prepare_dataset.py`: decoupe les segments voix utiles
- `train_tiny_vc.py`: entraine un premier modele et exporte ONNX
- `infer_tiny_vc.py`: inference offline de validation
- `run_train_gpu.ps1`: lance un entrainement GPU avec checkpoints/reprise
- `capture_source_voice.py`: capture micro et fabrique automatiquement `data/source_chunks`
- `capture_source_voice.ps1`: wrapper PowerShell pour la capture source

Installation:

```powershell
pip install -r .\scripts\neural\requirements.txt
```

Lister les micros detectes:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\neural\capture_source_voice.ps1 -ListDevices
```

Enregistrer ta voix source (ex: 3 minutes) et generer les chunks source:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\neural\capture_source_voice.ps1 -Device 0 -Seconds 180 -OutputWav .\audio_samples\source-voice.wav -ChunksDir .\data\source_chunks
```

Exemple d'entrainement GPU avec checkpoints:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\neural\run_train_gpu.ps1 -SourceDir .\data\source_chunks -TargetDir .\data\target_chunks -OutModel .\models\tiny_vc.onnx -Epochs 40 -StepsPerEpoch 300
```

Important:

- Si tu mets a jour les scripts `train_tiny_vc.py` / `infer_tiny_vc.py`, relance un entrainement complet avant de juger la similarite vocale.
- Le binaire runtime C++ attend des features pseudo-mel specifiques; un modele entraine avec une autre featurization produira souvent une voix peu transformee.

Reprise depuis le dernier checkpoint:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\neural\run_train_gpu.ps1 -ResumeLast
```

## Structure

- `src/app`: point d'entree et presets.
- `src/audio`: capture/rendu WASAPI + utilitaires peripheriques.
- `src/core`: stats et ring buffer.
- `src/dsp`: effets audio.
- `src/ai`: conversion vocale locale et interface de backends IA.
- `src/ui`: UI desktop MVP Win32.
- `third_party/soundtouch`: dependance pitch shift.

## Contexte de navigation

Des fichiers de contexte techniques sont disponibles dans `docs/context`:

- `00-overview.md`
- `01-architecture.md`
- `02-operations.md`
- `03-improvements-backlog.md`
- `file-inventory.txt`

## Diagnostic runtime

Script: `scripts/diagnostic-runtime.ps1`

Exemple:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\diagnostic-runtime.ps1 -BuildConfig Debug -InIndex 0 -OutIndex 2 -Preset girl -DurationSec 15
```

Diagnostic mode IA local:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\diagnostic-runtime.ps1 -BuildConfig Debug -InIndex 0 -OutIndex 2 -Preset girl -Mode ai -AiProfile bright -AiBlend 0.7 -AiIntensity 0.5 -DurationSec 15
```

## UI desktop MVP

```powershell
.\build\voice_changer_ui.exe
```

L'UI propose:

- Selection de l'entree et de la sortie audio par nom (listes deroulantes).
- Bouton `Refresh devices` pour recharger les peripheriques detectes.

## CI

Workflow GitHub Actions: [.github/workflows/ci.yml](.github/workflows/ci.yml)
