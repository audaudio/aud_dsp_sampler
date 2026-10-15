# aud_dsp_sampler

Der SFZ-Sampler der Audanika Audio Engine: Der Knoten `aud.sampler.sfz`
spielt SFZ-Instrumente auf der Engine von `aud_audio`. Er lädt sie auf
einem Worker-Thread der Engine, während das alte Instrument weiterspielt,
teilt ihre Samples zwischen allen Knoten des Prozesses im
Sample-Cache-Budget und rendert auf dem Audio-Thread, ohne zu allozieren.
Gebaut auf dem audaudio-Fork von sfizz.

Teil der Audanika Audio Engine; geplant in [aud_audio_pm](https://github.com/audaudio/aud_audio_pm).

## Ziele

- SFZ-Instrumente als Knoten der Engine, gespielt über UMP von MIDI 1.0
  und 2.0 und als Knoten-Presets in Graph-Dokumenten gespeichert (Ticket
  25, Schritt S9)
- Ladevorgänge, die weder Dart noch den Audio-Thread blockieren, mit IDs,
  Fortschritt und Abbruch; das alte Instrument spielt, bis das neue mit
  einer Überblendung von 5 ms übernimmt (sampler-003)
- Ein Sample-Speicher pro Prozess im Sample-Cache-Budget der Engine
  (lifecycle-002)
- Kräftiges Spielen überlastet den Audio-Thread nicht: ein Stimmenlimit
  pro Knoten, das sich in Echtzeit ändert, und ein Stimmenbudget für alle
  Knoten des Prozesses, gehalten durch das Stehlen von Stimmen mit einer
  Ausblendung von 10 ms (sampler-008)
- Eine Stimme kostet ein Drittel dessen, was sie vor Ticket 30 kostete: die
  Stimmenschleife besucht nur die belegten Stimmen, fasst ihre Gain- und
  Pan-Durchläufe zusammen, berechnet auf arm64 Interpolation und Hüllkurve
  für vier Frames auf einmal und rendert ganze Hüllkurvenabschnitte, bei
  Bit für Bit gleichen Renderings (sampler-009)
- Auf dem Audio-Thread wird nichts alloziert (interop-001); die nativen
  Tests lassen den Knoten unter dem Watchdog der Engine, dem
  RealtimeSanitizer und dem ThreadSanitizer laufen
- Die Message-API von sfizz auf den Routen des Knotens, beantwortet über
  die Engine (osc-002)
- Eine App, die den Sampler nicht nutzt, wird ohne seine native
  Bibliothek ausgeliefert (interop-001)
- Ein Fork von sfizz ausschließlich unter freizügigen Lizenzen
  (sampler-002, license-003)

## Installation

```yaml
dependencies:
  aud_dsp_sampler:
    git:
      url: git@github.com:audaudio/aud_dsp_sampler.git
      tag_pattern: "{{version}}"
    version: ^0.3.0
```

Eine App hängt von `aud_audio` und den DSP-Paketen ab, die sie nutzt, und
registriert sie bei ihrer Engine (family-001). Der Build-Hook kompiliert
den Knoten und den Fork von sfizz mit der C- und C++-Toolchain des Ziels;
das Paket selbst braucht kein Flutter. Die nativen Tests brauchen Node.js
und `clang++`; ihr RealtimeSanitizer-Build braucht ein clang, das ihn
mitbringt, z. B. LLVM aus Homebrew auf macOS (`brew install llvm`), und
entfällt ohne eines.

## Dokumentation

- [Der Plan von Ticket 25](https://github.com/audaudio/aud_audio_pm/blob/main/doc/2026-Q4/tickets/2026-10-11-25-build-the-sfz-sampler-on-sfizz-for-ios-and-android.md)
  mit dem Knoten, seinen Messungen und Erkenntnissen
- [Der Plan von Ticket 29](https://github.com/audaudio/aud_audio_pm/blob/main/doc/2026-Q4/tickets/2026-10-13-29-limit-the-voices-by-the-cpu-power-of-the-device.md)
  mit dem Stimmenlimit, dem Stimmenbudget und dem Stehlen
- [Der Plan von Ticket 30](https://github.com/audaudio/aud_audio_pm/blob/main/doc/2026-Q4/tickets/2026-10-13-30-render-the-sampler-voices-faster-on-the-big-cores.md)
  mit der schnelleren Stimmenschleife und ihren Messungen
- Die Entscheidungen hinter dem Paket:
  - [sampler-001](https://github.com/audaudio/aud_audio_pm/blob/main/doc/2026-Q4/concepts/decisions/sampler-001.md):
    sfizz, geforkt, als Kern des Samplers
  - [sampler-002](https://github.com/audaudio/aud_audio_pm/blob/main/doc/2026-Q4/concepts/decisions/sampler-002.md):
    das Paket ist der audaudio-Fork von sfizz
  - [sampler-003](https://github.com/audaudio/aud_audio_pm/blob/main/doc/2026-Q4/concepts/decisions/sampler-003.md):
    Ladevorgänge in einen Standby-Synth, mit einer Überblendung getauscht
  - [lifecycle-002](https://github.com/audaudio/aud_audio_pm/blob/main/doc/2026-Q4/concepts/decisions/lifecycle-002.md):
    die Ressourcen des Prozesses und das Sample-Cache-Budget
  - [sampler-008](https://github.com/audaudio/aud_audio_pm/blob/main/doc/2026-Q4/concepts/decisions/sampler-008.md):
    das Stimmenlimit pro Knoten und das Stimmenbudget des Prozesses
  - [sampler-009](https://github.com/audaudio/aud_audio_pm/blob/main/doc/2026-Q4/concepts/decisions/sampler-009.md):
    die schnellere Stimmenschleife des Forks
  - [osc-002](https://github.com/audaudio/aud_audio_pm/blob/main/doc/2026-Q4/concepts/decisions/osc-002.md):
    Nachrichten an Knoten und ihre Antworten
  - [license-003](https://github.com/audaudio/aud_audio_pm/blob/main/doc/2026-Q4/concepts/decisions/license-003.md):
    die Prüfung des von Faust erzeugten Codes
- [Das sfizz-Thema](https://github.com/audaudio/aud_audio_pm/blob/main/doc/2026-Q4/concepts/topics/sfizz.md)
  mit dem, was Ticket 25 in sfizz gefunden hat
- Die C-API: [`src/aud_dsp_sampler.h`](src/aud_dsp_sampler.h)
- Die Komponenten des Forks und ihre Lizenzen:
  [`src/third_party/NOTICES.md`](src/third_party/NOTICES.md)
- Die Guides in [`doc/guides`](doc/guides)

## Code-Beispiele

Der Sampler auf der Engine von `aud_audio` (`aud_audio_ffi.dart` auf
nativen Plattformen):

```dart
import 'package:aud_audio/aud_audio_ffi.dart';
import 'package:aud_dsp_sampler/aud_dsp_sampler_ffi.dart';

Future<void> main() async {
  final engine = AudEngine(
    packages: [
      AudNativeNodePackage(AudDspSampler.name, AudDspSampler.register),
    ],
  );
  engine.prepare();
  final graph = engine.graph;
  final piano = graph.createNode(
    AudSfzSampler.typeId,
    preset: AudSfzPreset.file('/path/to/piano.sfz', polyphony: 32),
  );
  graph.transaction((tx) => tx.connect(piano, graph.io));
  engine.start();

  // The piano plays on until the organ is loaded and swaps in.
  final id = graph.setString(
    piano,
    AudSfzSampler.sfzFile,
    '/path/to/organ.sfz',
  );
  final load = await graph.loadEnded(id);
  print('${load.resultName} ${load.message}'); // AUD_OK, what it could not read
  graph.setParam(piano, AudSfzSampler.volume, -6);
  graph.messages.listen((m) => print('${m.path} ${m.arguments}'));
  graph.sendMessage(piano, AudOscMessage('/num_regions'));
  print('${AudDspSampler.storeFiles} files, ${AudDspSampler.storeBytes} bytes');

  // At most 24 voices for the piano, and 48 for all samplers of the
  // process, each minus a reserve of 4 free voices.
  graph.setParam(piano, AudSfzSampler.maxVoices, 24);
  AudDspSampler.voiceBudget = 48;
  final voices = AudDspSampler.voiceStats;
  print('${voices.sounding} voices sound, ${voices.stolen} were stolen');
}
```

Ein Instrument als SFZ-Text, so wie ein Preset es ohne Datei trägt:

```dart
import 'package:aud_dsp_sampler/aud_dsp_sampler.dart';

void main() {
  final preset = AudSfzPreset.text(
    '<region> sample=c4.wav pitch_keycenter=60',
    virtualPath: '/path/to/sketch.sfz', // c4.wav lies next to it
    name: 'Sketch',
    tuningFrequency: 442,
    scalaFile: '/path/to/werckmeister.scl',
  );
  print(preset.toJson()); // aud_node_preset.schema.json of aud_audio_core
}
```

Tests und Beispiele spielen das erzeugte Testinstrument von
`testing.dart`, das keine Sample-Dateien mitbringt (scope-002):

```dart
import 'dart:io';

import 'package:aud_audio/aud_audio_ffi.dart';
import 'package:aud_dsp_sampler/aud_dsp_sampler_ffi.dart';
import 'package:aud_dsp_sampler/testing.dart';
import 'package:aud_midi_standard/aud_midi_standard.dart';

void main() {
  final graph = AudGraphFfi(maxFrames: 128, listen: false);
  AudDspSampler.registerWith(graph.hostApi); // throws AudDspSamplerException
  final directory = Directory.systemTemp.createTempSync('aud_instrument_');
  final instrument = AudSfzTestInstrument.write(directory.path);
  final node = graph.createNode(
    AudSfzSampler.typeId,
    preset: instrument.preset(polyphony: 16),
  );
  graph.transaction((tx) => tx.connect(node, graph.io));
  graph.start();
  graph.waitLoads(node: node); // blocks: tests and offline renders
  const note = MidiNoteOn(channel: 0, note: 60, velocity: 100);
  for (final event in AudUmpEvent.fromMessage(note)) {
    graph.sendEvent(node, event);
  }
  final wav = AudOfflineRenderer(graph).renderWav(frames: 48000);
  AudWavFile.write('${directory.path}/c4.wav', wav);
  graph.dispose();
}
```

Die Beispiel-App von `aud_audio` spielt den Sampler auf ihrer
Sampler-Seite; `example/` dieses Pakets zeigt die IDs des Knotens und ein
Preset und beweist den Link-Hook. Sein Integrationstest lädt ein
Instrument aus den Assets seiner APK auf einem Android-Gerät:

```bash
cd example && flutter test integration_test/apk_assets_test.dart -d <device>
```

## Instrumente aus Android-Assets laden

Auf Android liest der Sampler SFZ-Dateien und ihre Samples direkt aus den
Assets der APK, ohne sie herauszukopieren, so wie es die alte Audio-Engine
von Audanika tat. Ein Pfad, der mit `apk:/` beginnt, nennt ein Asset:

```dart
graph.setString(
  piano,
  AudSfzSampler.sfzFile,
  'apk:/content/sfz/presets/piano/main.sfz', // assets/content/sfz/... in der APK
);
```

- Der Pfad nach `apk:/` ist der Pfad innerhalb der Assets der APK:
  `apk:/content/sfz/x.sfz` für `android/app/src/main/assets/content/sfz/x.sfz`,
  `apk:/flutter_assets/assets/sfz/x.sfz` für das Flutter-Asset
  `assets/sfz/x.sfz`. Die relativen Pfade in der SFZ-Datei - `sample=`,
  `#include`, `default_path`, auch mit `..` - lösen sich ebenfalls in der
  APK auf, genauso eine `scala_file` oder der `sfz_virtual_path` eines
  SFZ-Texts.
- Samples werden über `AAssetManager` gelesen: Ein unkomprimiertes Asset
  wird ohne Kopie aus der APK eingeblendet, ein komprimiertes wird während
  des Lesens in den Speicher entpackt. Samples bleiben unkomprimiert;
  Android komprimiert `.wav`, `.ogg` und `.mp3` standardmäßig nicht, für
  `.flac` gehört es in `noCompress`. Ladevorgänge und die Streams der
  Noten laufen wie bei Dateien auf den Workern der Engine.
- Fehlt der genaue Name eines Samples, wird der Dateiname ohne Rücksicht
  auf Groß- und Kleinschreibung gesucht; die Ordner des Pfads müssen genau
  passen, denn eine APK listet die Dateien eines Ordners, nicht seine
  Unterordner. Eine Listendatei ist nicht nötig.
- Das Paket besorgt sich den `AssetManager` selbst: Sein Android-Plugin
  übergibt den der App an die native Bibliothek, wenn der Prozess startet,
  bevor Dart-Code läuft. Eine App mit eigenem JNI kann stattdessen
  `aud_dsp_sampler_set_android_assets` aufrufen. Auf iOS und den anderen
  Plattformen endet ein Ladevorgang eines `apk:/`-Pfads mit
  `AUD_ERROR_UNSUPPORTED` und einer Meldung, die das sagt; normale Pfade
  funktionieren überall.
- Der Speicher schlüsselt ein Sample nach seinem lexikalisch normalen Pfad,
  sodass Presets in verschiedenen Ordnern, die ein Sample über verschiedene
  relative Pfade erreichen, es teilen.

## Funktionsweise

### Der Knoten

- `aud.sampler.sfz` hat keinen Eingang und einen Ausgabebus `out` mit 2
  bis 16 Kanälen in Stereopaaren: die Ausgänge von sfizz, je ein Paar, die
  der SFZ-Opcode `output` pro Region wählt.
- Der Event-Eingang `midi` nimmt UMP von MIDI 1.0 und 2.0: Noten mit ihrer
  Velocity, Control Changes, Pitch Bend, Channel und Poly Pressure,
  Program Changes. Die übrigen Per-Note-Nachrichten von MIDI 2.0 haben in
  sfizz kein Gegenstück und zählen als `AUD_ERROR_UNSUPPORTED`. Numerische
  Controls setzen die CCs 0 bis 511 von sfizz über ihre ID; Nachrichten
  erreichen die Message-API von sfizz.
- Die Parameter, in der Reihenfolge von `AudSfzSampler.params`: `volume`
  (dB, -60 bis +6, mit Rampe), `polyphony` (1 bis 256), `sample_quality`
  (0 bis 10), `oscillator_quality` (0 bis 3), `tuning_frequency` (400 bis
  480 Hz), `stretch_tuning` (0 bis 1) und `max_voices` (1 bis 256, siehe
  Stimmen). Die gestuften runden auf ihre Stufe. Eine Änderung der
  Polyphonie oder der Stimmung lädt das Instrument neu, und die klingenden
  Noten blenden aus; `max_voices` wirkt ab dem nächsten Block ohne
  Ladevorgang.
- Der Tail des Knotens ist das längste Amplituden-Release seines
  Instruments, der ADSR oder des Flex-EG, der sie ersetzt (`egN_ampeg`).
  Der Transport erreicht sfizz - Tempo, Taktart, Position und
  Wiedergabezustand - für seine tempo-synchrone Modulation. Ein
  Offline-Render lässt sfizz in seiner besten Qualität rechnen.

### Stimmen

- In jedem Block, nach seinen Events und bevor er rendert, zählt ein
  Knoten seine klingenden Stimmen - sie spielen und sind weder abgeschaltet
  noch gestohlen - über den spielenden und den ausblendenden Synth, aus der
  Liste der aktiven Stimmen von sfizz. Über seinen Limits stiehlt er den
  Überschuss: losgelassene Stimmen (Taste und Pedal oben) vor gehaltenen,
  die ältesten zuerst, alle Layer einer Note zusammen. Eine gestohlene
  Stimme blendet ab dem Anfang des Blocks über 10 ms linear aus, überhört
  Note-Offs und die Pedale, startet keinen Release-Trigger und ist am Ende
  ihrer Ausblendung frei, ohne den Ausklang von 50 ms von sfizz. Neue Noten
  werden nie abgewiesen: Der Überschuss geht am Ende des Blocks, der sie
  startete.
- `max_voices` (1 bis 256, Standard 256) begrenzt einen Knoten in Echtzeit:
  Unter 256 hält er höchstens `max(1, max_voices - reserve)` Stimmen
  klingend; 256 setzt kein Limit über die Polyphonie hinaus. Es gilt auch
  für Offline-Renders.
- Ein Stimmenbudget pro Prozess (`AudDspSampler.voiceBudget`, Standard 0:
  keines) gilt für die Live-Renders aller Knoten aller Graphen: Zusammen
  halten sie höchstens `max(1, budget - reserve)` Stimmen klingend. Die
  Knoten veröffentlichen ihre klingenden und losgelassenen Stimmen in
  Atomics des Prozesses, und jeder nimmt seinen Anteil am Überschuss:
  zuerst losgelassene Stimmen, aufgeteilt nach den losgelassenen Stimmen
  jedes Knotens, dann gehaltene nach den gehaltenen Stimmen jedes Knotens;
  ein Knoten, dessen Anteil auf 0 abrundet, aber mindestens dem
  Durchschnitt der Knoten mit Stimmen entspricht, nimmt eine, damit der
  Überschuss mit jedem Block schrumpft. Offline-Renders zählen keine
  Stimmen und stehlen keine für das Budget. Ein Knoten zieht seine
  Stimmen zurück, wenn er geht, wenn der Graph stoppt oder ihn
  zurücksetzt, und solange er offline oder ohne Ausgang rendert.
- Die Reserve (`AudDspSampler.voiceReserve`, 0 bis 16, Standard 4) hält
  unter beiden Limits Stimmen frei, damit eine neue Note sofort eine freie
  Stimme findet, während eine gestohlene ausblendet.
- `AudDspSampler.voiceStats` liest das Budget, die Reserve, die Stimmen,
  die die Live-Knoten am Ende ihres letzten Blocks klingen ließen und
  losgelassen hatten, und zwei Zähler, die nur wachsen: die Stimmen, die
  `max_voices` und das Budget in Live-Blöcken stahlen, und die gerenderten
  Stimmen mal die
  Frames ihrer Live-Blöcke. Die eigenen Polyphonie-Limits von sfizz und
  ihre Ausblendungen von 6 ms bleiben, wie sie sind.

### Ladevorgänge

- `sfz_file` und `sfz_text` starten einen Ladevorgang auf einem
  Worker-Thread der Engine und geben sofort seine ID zurück; eine leere
  Datei oder ein leerer Text entlädt das Instrument, ebenso eines ohne
  Regionen, z. B. ein Text mit nur einem Kommentar. `sfz_virtual_path`
  nennt den Pfad, gegen den die Samples des nächsten Texts aufgelöst
  werden; `scala_file` und `scala_text` laden eine Stimmung mit dem
  Instrument.
- Der Ladevorgang füllt einen Standby-Synth, der wie der spielende
  konfiguriert ist. Der Audio-Thread übernimmt ihn an einem Blockanfang
  und blendet den alten Synth über 5 ms aus; der neue Synth startet ohne
  Noten und übernimmt die Controller, den Pitch Bend, den Druck und das
  Programm als wiederhergestellte Werte, sodass keine Region startet, die
  ein Controller auslöst.
- Ein neuerer Ladevorgang bricht einen laufenden ab; ein fehlgeschlagener
  behält das spielende Instrument. Ein Ladevorgang schlägt fehl, wenn sfizz
  das Instrument nicht lesen kann oder darin Fehler und keine Region
  findet (die Nachricht zitiert den ersten Fehler), wenn es keines seiner
  Samples lesen kann, oder mit
  `AUD_ERROR_CAPACITY`, wenn schon die Köpfe seiner Samples das
  Sample-Cache-Budget überschreiten. Ein erfolgreicher Ladevorgang nennt
  die Samples, die er nicht lesen konnte, und die Effekttypen, die sfizz
  nicht kennt und die das Signal durchreichen.
- Vor dem ersten Instrument fährt der Knoten die Lautstärke als Rampe und
  merkt sich die Controller dafür. Keine Exception überquert die C-ABI:
  Ein Ladevorgang, dem der Arbeitsspeicher ausgeht, endet mit
  `AUD_ERROR_OUT_OF_MEMORY`. Eine Host-API, der eine Funktion fehlt, die
  der Knoten ruft, oder deren Engine keine Caches mehr annimmt, bekommt
  keinen Knoten.

### Der Sample-Speicher

- Ein Speicher pro Prozess hält die Samples aller Knoten aller Engines,
  jede Datei einmal. Ein Ladevorgang liest den Kopf jedes Samples, 8192
  Frames, der bleibt, solange ein Instrument ihn nutzt; eine Note streamt
  die ganze Datei als Task der Engine.
- Hält der Prozess mehr als das Budget, bittet die Engine den Speicher zu
  trimmen: zuerst die gestreamten Daten, die keine Stimme liest, dann die
  Dateien, die kein Instrument hält, die am längsten ungenutzten zuerst.
  Eine Stimme, die ihrem Stream davonläuft, spielt Stille, behält ihre
  Position und zählt als `AUD_ERROR_UNDERFLOW`.
- Ein Pool ohne Host - die C-API von sfizz, die Tests des Forks ohne
  Host - liest ganze Dateien auf einmal. Ein Sample mit mehr als
  2^31 - 1 Frames oder eines, das weder mono noch stereo ist, wird
  abgewiesen und als fehlend genannt. Ein Sample, das der Arbeitsspeicher
  nicht fassen kann, wird verworfen und als fehlend genannt; ein Stream,
  den der Arbeitsspeicher nicht fassen kann, lässt seine Stimmen beim
  Kopf.
- SFZ-Instrumente gelten als vertrauenswürdige Eingabe: Eine SFZ-Datei
  kann jede Datei, die die App lesen kann, per `#include` einbinden oder
  abspielen - absolute Pfade und `../` funktionieren, so teilen
  SFZ-Bibliotheken ihre Samples. Die Sandbox der App begrenzt, was sie
  erreicht.
- Neben dem Dateisystem liest der Fork Dateien, die ein Backend bereitstellt,
  benannt durch ein Schema: `VirtualFile.h` hält die Backends, Parser,
  Speicher, Audio-Reader und die Scala-Stimmung fragen es zuerst. `apk:/`
  ist das Backend der Assets einer Android-App (`src/aud_apk_assets.cpp`);
  die nativen Tests stellen einen Ordner des Hosts als Assets bereit.

### Nachrichten

- Eine Nachricht unterhalb von `/graph/<g>/node/<n>/msg/` erreicht die
  Message-API von sfizz: `/graph/1/node/2/msg/region0/ampeg_attack f 0.1`
  wird zu `/region0/ampeg_attack` von sfizz. sfizz antwortet auf dem
  Audio-Thread über die Engine, und `AudGraph.messages` liefert die
  Antworten.
- Eine Nachricht mit einem String- oder Blob-Argument oder mit mehr als
  vier Argumenten wird abgewiesen und zählt als `AUD_ERROR_UNSUPPORTED`,
  da sfizz sie auf dem Audio-Thread kopieren oder parsen würde. Der Fork
  verwirft die Nachrichten, die seine Regionen wachsen ließen.

### Der Fork von sfizz

- `src/third_party/sfizz` begann als die Teilmenge von `audanika/sfizz`
  bei `824afdc`, die der Linker zieht, und wird an Ort und Stelle
  geändert: Die Historie dieses Pakets ist die Historie des Forks. Jede
  geänderte Datei sagt in einem Vermerk "Modified by Audanika" unter ihrem
  Lizenzkopf, was sich geändert hat. Audanikas Code im Fork - diese
  Änderungen und die neue Datei `RbjFilters.hpp` - steht wie sfizz unter
  BSD-2-Clause, damit der Fork eine Lizenz hat und seine Korrekturen
  upstream gehen können.
- Der Build-Hook kompiliert die Quellen aus `SOURCES.txt` mit den
  Include-Verzeichnissen aus `INCLUDES.txt`. `NOTICES.md` nennt die
  Komponenten, ihre Lizenzen und das Ergebnis der Faust-Prüfung: Die
  RBJ-Filter sind Audanikas eigener Code nach dem Audio EQ Cookbook, der
  verbleibende von Faust erzeugte Code trägt den STK-4.3-Hinweis, und
  fverb, das Metronom und cephes sind entfernt.
- Die Synths mehrerer Knoten laden gleichzeitig auf den Workern der
  Engine. Was sie im Prozess teilen - die Kurven der Flex-EG-Formen, die
  stille Wavetable, die Tabelle der Distortion, die CRC-Tabelle von
  stb_vorbis und die CPU-Fähigkeiten von dr_flac und dr_mp3 - wird einmal
  gebaut, ist atomar oder wird von einem Mutex geschützt, den nur
  Ladevorgänge nehmen; `NOTICES.md` listet die Prüfung (Ticket 28).
- Ein Host hält die Stimmen eines Synths unter einem eigenen Limit,
  während der Synth rendert (Ticket 29): `Synth::countVoices()` zählt die
  klingenden, losgelassenen und gerenderten Stimmen aus der Liste der
  aktiven Stimmen, `Synth::stealVoices()` stiehlt sie in der Reihenfolge
  oben, und `Voice::steal()` blendet eine Stimme linear aus und gibt sie
  am Ende frei. Die Polyphonie-Prüfungen von sfizz überspringen gestohlene
  Stimmen wie Stimmen, die es abgeschaltet hat, und sind sonst
  unverändert; eine gestohlene Stimme lässt keinen Release-Trigger
  starten. `VoiceCount.h` ist neu; `NOTICES.md` listet die geänderten
  Dateien.
- Die Hot Spots des Audio-Threads einer App sind schneller, mit Bit für Bit
  derselben Ausgabe (Ticket 28): Der MIDI-Zustand leert nur die
  Event-Vektoren, die Events bekamen; der Kompressor ruft `log10` und `pow`
  nur über seiner Schwelle und während er die Verstärkung senkt, und reicht
  Stille durch, solange er ruht; der Gain-Effekt rechnet seine Verstärkung
  einmal pro Block um; ein Bus mit stillem Eingang überspringt seine
  ruhenden Effekte; eine Stimme rechnet ihre Tonhöhe und ihre
  Lautstärkemodulation einmal pro Folge gleicher Werte um. `NOTICES.md`
  listet die geänderten Dateien.
- Die Bibliothek baut für jedes ABI von Android, auch für das 32-Bit-ABI
  `armeabi-v7a`, auf dem Abseils 128-Bit-Ganzzahl keinen Compilertyp hat
  und `int128.cc` braucht. Auf Android linkt sie mit `--no-undefined`: Ein
  Symbol, das der Auszug nicht enthält, bricht den Build ab statt das
  Laden in der App.

### Die Dart-API

- `aud_dsp_sampler.dart` ist plattformneutral und kompiliert für das Web:
  `AudSfzSampler` hält die IDs des Knotens, `AudSfzPreset.file` und
  `AudSfzPreset.text` bauen seine Presets, `AudSfzVoiceStats` hält die
  Stimmen des Prozesses.
- `aud_dsp_sampler_ffi.dart` ergänzt `AudDspSampler`: `register` ist die
  Registrierungsfunktion für die Paketliste einer Engine, `registerWith`
  registriert bei der Host-API eines Graphen und wirft eine
  `AudDspSamplerException` mit dem Ergebniscode, wenn die Engine ablehnt,
  `storeBytes` und `storeFiles` lesen den Speicher des Prozesses,
  `voiceBudget` und `voiceReserve` setzen das Stimmenbudget des Prozesses
  und seine Reserve auf jedem Thread, und `voiceStats` liest seine
  Stimmen.
- Das Paket ist ein Android-Plugin ohne Flutter-Abhängigkeit: Flutter baut
  den Java-Shim in `android/`, einen Content-Provider, der den
  `AssetManager` der App an die native Bibliothek übergibt, wenn der
  Prozess startet.
- Jedes Member von `AudDspSampler` ist mit `@RecordUse` aufgezeichnet: In
  einem Profile- oder Release-Build behält der Link-Hook die native
  Bibliothek nur, wenn die App eines davon nutzt.
- `testing.dart` ergänzt `AudSfzTestInstrument`: `write` schreibt
  Sägezahnwellen auf fünf Grundtönen und ihre SFZ-Datei, `preset` lädt
  sie.

### Tests

```bash
dart test
node scripts/test-native.js
```

- `dart test` führt die Dart-Tests, die Golden-Renders des
  Testinstruments (`test/goldens`, die `UPDATE_GOLDENS=1` schreibt) und
  die nativen Tests aus.
- `scripts/test-native.js` baut den Knoten, den Fork und die Engine von
  `aud_audio_graph` mit dem Address- und dem
  Undefined-Behaviour-Sanitizer, dem RealtimeSanitizer - mit einer Sonde,
  die beweist, dass er eine Allokation auf dem Audio-Thread fängt - und
  dem Thread-Sanitizer, unter dem Watchdog der Engine. Die Tests des Forks
  kompilieren wie sfizz als gnu++14.
- Die Tests von Ticket 28 vergleichen den schnelleren Code mit dem
  Original: den MIDI-Zustand mit dem ursprünglichen Leeren auf zufälligen
  Event-Strömen, den Kompressor und den Gain mit ihrem ursprünglichen Code
  auf lauten, leisen, stillen, transienten und einseitigen Signalen, und den
  Render eines Instruments wie dem einer App mit
  `test/goldens/sfizz_app_instrument.wav`, den Release 0.3.0 renderte.
- Die Tests von Ticket 29 prüfen, dass `max_voices` ab dem nächsten Block
  ohne Ladevorgang wirkt, dass eine gestohlene Stimme ohne Sprung
  ausblendet und nach 10 ms frei ist, die Reihenfolge des Stehlens und die
  Layer, die zusammen gehen, das Budget über die Knoten eines Graphen und
  über Graphen - auch während zwei Threads rendern und ein dritter das
  Budget setzt -, die Reserve, Offline-Renders, und dass ein Knoten, der
  geht, keine Stimmen in den Zählern hinterlässt.
- `--bench` baut den Mikro-Benchmark `test/native/bench/sfizz_bench.cpp`
  mit -O3 mit dem Fork von Release 0.3.2 (`AUD_BENCH_BASE` nennt eine
  andere Git-Ref) und mit dem Fork von jetzt, gibt aus, was das Leeren des
  MIDI-Zustands, der Kompressor, der Gain, ein ruhender Synth und vier
  Synths pro Aufruf vorher und nachher kosten, und seit Ticket 30 die
  Stimmen: einen Block mit 1, 8 und 32 gehaltenen Noten nach Interpolation
  (linear, Hermite, gefensterter Sinc mit 8 Punkten), nach Hüllkurve
  (ADSR, Flex-EG), mit Mono- und Stereo-Samples, mit und ohne bewegtes
  Pitch Bend, daraus die Kosten pro Stimme und den festen Teil eines
  Blocks, und den Start eines Bursts von 10 Note-ons. Er schlägt fehl,
  wenn die Renders beider um mehr als 1e-5 des Vollausschlags abweichen
  (sampler-007); mit `UPDATE_GOLDENS=1` schreibt er das Golden mit dem
  alten Fork. `--bench --android[=<adb serial>]` baut beide für arm64-v8a
  mit dem Android-NDK (`ANDROID_NDK_HOME`, sonst das neueste des SDK),
  führt sie über adb (`ADB`) in `/data/local/tmp/aud_bench` nacheinander
  auf jedem Cluster der Kerne des Geräts aus, mit `taskset` gebunden, und
  gibt eine Tabelle pro Cluster aus.
- `--help` listet die Optionen, `--only=asan|plain|rtsan|tsan` führt einen
  Build aus, und eine unbekannte Option oder `--android` ohne `--bench`
  beendet den Lauf mit Exit-Code 2.
  `AUD_TEST_FILTER` führt die Tests aus, deren Name ihn enthält;
  `AUD_SFIZZ_PRISTINE=<sfizz checkout>` vergleicht die RBJ-Filter
  zusätzlich mit den Faust-Originalen. Der Build-Cache in `.dart_tool`
  behält nur die Objekte und Binaries des letzten Laufs.
- Auf macOS nennt der Thread-Sanitizer die Dateien und Zeilen einer
  Meldung über `llvm-symbolizer` von LLVM und ein dSYM-Bundle des
  Testprogramms, das `dsymutil` von LLVM schreibt - aus LLVM von Homebrew
  oder der `llvm-symbolizer`, den `AUD_LLVM_SYMBOLIZER` nennt, mit
  `dsymutil` daneben; Apples `atos` hing bei Meldungen von Races.
  `AUD_RTSAN_CXX` nennt den Compiler des RealtimeSanitizer-Builds.

## Mitwirken

Tickets laufen über den `gg`-Workflow: siehe den
[Develop Guide](doc/guides/develop-guide.md) und den
[Review Guide](doc/guides/for-ai/ai-review-guide.md).
