# aud_dsp_sampler

SFZ-Sampler-Knoten der Audanika Audio Engine auf dem audaudio-Fork von
sfizz.

Teil der Audanika Audio Engine; geplant in [aud_audio_pm](https://github.com/audaudio/aud_audio_pm).

Der Knoten `aud.sampler.sfz` spielt SFZ-Instrumente auf der Engine von
`aud_audio`. Er lädt auf einem Worker-Thread der Engine, spielt das alte
Instrument weiter, bis das neue geladen ist, und blendet in 5 ms über. Die
Samples liegen in einem Speicher pro Prozess, den alle Knoten aller Engines
teilen, im Sample-Cache-Budget der Engine. Auf dem Audio-Thread laufen nur
die Echtzeitfunktionen von sfizz, ohne zu allozieren.

`src/third_party/sfizz` ist der audaudio-Fork von sfizz
(Entscheidung sampler-002 in aud_audio_pm); `src/third_party/NOTICES.md`
nennt Herkunft, Änderungen und Lizenzen. Details stehen in der englischen
[README](README.md).
