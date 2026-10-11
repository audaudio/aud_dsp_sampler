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
    version: ^0.2.0
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
- Die Entscheidungen hinter dem Paket:
  - [sampler-001](https://github.com/audaudio/aud_audio_pm/blob/main/doc/2026-Q4/concepts/decisions/sampler-001.md):
    sfizz, geforkt, als Kern des Samplers
  - [sampler-002](https://github.com/audaudio/aud_audio_pm/blob/main/doc/2026-Q4/concepts/decisions/sampler-002.md):
    das Paket ist der audaudio-Fork von sfizz
  - [sampler-003](https://github.com/audaudio/aud_audio_pm/blob/main/doc/2026-Q4/concepts/decisions/sampler-003.md):
    Ladevorgänge in einen Standby-Synth, mit einer Überblendung getauscht
  - [lifecycle-002](https://github.com/audaudio/aud_audio_pm/blob/main/doc/2026-Q4/concepts/decisions/lifecycle-002.md):
    die Ressourcen des Prozesses und das Sample-Cache-Budget
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
Preset und beweist den Link-Hook.

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
  480 Hz) und `stretch_tuning` (0 bis 1). Die gestuften runden auf ihre
  Stufe. Eine Änderung der Polyphonie oder der Stimmung lädt das
  Instrument neu, und die klingenden Noten blenden aus.
- Der Tail des Knotens ist das längste Amplituden-Release seines
  Instruments. Der Transport erreicht sfizz - Tempo, Taktart, Position und
  Wiedergabezustand - für seine tempo-synchrone Modulation. Ein
  Offline-Render lässt sfizz in seiner besten Qualität rechnen.

### Ladevorgänge

- `sfz_file` und `sfz_text` starten einen Ladevorgang auf einem
  Worker-Thread der Engine und geben sofort seine ID zurück; eine leere
  Datei oder ein leerer Text entlädt das Instrument. `sfz_virtual_path`
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
  das Instrument nicht parsen kann (die Nachricht zitiert den ersten
  Fehler), wenn es keines seiner Samples lesen kann, oder mit
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
  abgewiesen und als fehlend genannt; ein Stream, den der Arbeitsspeicher
  nicht fassen kann, lässt seine Stimmen beim Kopf.
- SFZ-Instrumente gelten als vertrauenswürdige Eingabe: Eine SFZ-Datei
  kann jede Datei, die die App lesen kann, per `#include` einbinden oder
  abspielen - absolute Pfade und `../` funktionieren, so teilen
  SFZ-Bibliotheken ihre Samples. Die Sandbox der App begrenzt, was sie
  erreicht.

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
  Lizenzkopf, was sich geändert hat.
- Der Build-Hook kompiliert die Quellen aus `SOURCES.txt` mit den
  Include-Verzeichnissen aus `INCLUDES.txt`. `NOTICES.md` nennt die
  Komponenten, ihre Lizenzen und das Ergebnis der Faust-Prüfung: Die
  RBJ-Filter sind Audanikas eigener Code nach dem Audio EQ Cookbook, der
  verbleibende von Faust erzeugte Code trägt den STK-4.3-Hinweis, und
  fverb, das Metronom und cephes sind entfernt.

### Die Dart-API

- `aud_dsp_sampler.dart` ist plattformneutral und kompiliert für das Web:
  `AudSfzSampler` hält die IDs des Knotens, `AudSfzPreset.file` und
  `AudSfzPreset.text` bauen seine Presets.
- `aud_dsp_sampler_ffi.dart` ergänzt `AudDspSampler`: `register` ist die
  Registrierungsfunktion für die Paketliste einer Engine, `registerWith`
  registriert bei der Host-API eines Graphen und wirft eine
  `AudDspSamplerException` mit dem Ergebniscode, wenn die Engine ablehnt,
  `storeBytes` und `storeFiles` lesen den Speicher des Prozesses.
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
- `--help` listet die Optionen, `--only=asan|plain|rtsan|tsan` führt einen
  Build aus, und eine unbekannte Option beendet den Lauf mit Exit-Code 2.
  `AUD_TEST_FILTER` führt die Tests aus, deren Name ihn enthält;
  `AUD_SFIZZ_PRISTINE=<sfizz checkout>` vergleicht die RBJ-Filter
  zusätzlich mit den Faust-Originalen. Der Build-Cache in `.dart_tool`
  behält nur die Objekte und Binaries des letzten Laufs.

## Mitwirken

Tickets laufen über den `gg`-Workflow: siehe den
[Develop Guide](doc/guides/develop-guide.md) und den
[Review Guide](doc/guides/for-ai/ai-review-guide.md).
