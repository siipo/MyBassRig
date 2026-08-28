First release built and validated on all three platforms, and the first with a
macOS AU. Everything here is produced by CI from the tagged commit: built,
unit tested, and put through pluginval at strictness 10, ten times, on each
platform before it is packaged.

## Fixed: bool parameters were saved wrong

**This is the reason to update from 0.1.0.**

`juce::AudioParameterBool` keeps whatever raw normalised value the host sends
and only thresholds it at 0.5 when read. Its range has an interval of 1, and
APVTS saves the *denormalised* value, so a switch sitting at 0.47 was written
to the session as 0 and the 0.47 was lost.

Hosts do not forget. A VST3 host caches the value it sent, so after saving and
reloading a session the host and the plugin disagreed about every switch in the
rig -- all seven pedal bypasses plus the master bypass. Wrong in an automation
lane, and wrong again the next time the host saved.

The plugin always *sounded* right, because every read thresholds. That is why
none of the 95 tests caught it: they all listen to the audio, and this was a
fault in the contract with the host, which is not observable from inside.
pluginval found it on its first ever run. See section 3p of `DESIGN.md` for the
diagnosis, including the two fixes that were wrong first.

## Fixed: Growl ran away with the level

Reported from playing: turning Growl up "garbles the sound". It was not the
divider and not aliasing -- both were checked and both were wrong. It was level.
Growl added up to +12 dB with nothing holding it down, reaching four and a half
times full scale on a 220 Hz note. The octaver sits ahead of the drive by
default, so that arrived at the clippers thirteen decibels hot and they did
exactly what clippers do.

There is now a +3 dB ceiling on the level Growl may add. It engages only above
the low strings, so the notes Growl exists for behave exactly as before. Peak at
full Growl drops from 4.52 to 1.80 at 220 Hz, and from 3.69 to 1.24 at 439 Hz.
See `DESIGN.md` 3r.

## Fixed: a crash under parameter automation

Found by `pluginval`, then by AddressSanitizer. The VST3 wrapper delivers
automation on the **audio thread**, and chowdsp's preset manager listens to
every parameter and marks the preset dirty from whatever thread the change
arrived on. That notification goes out through a `juce::ListenerList` built
without locking, so a host moving parameters from two threads at once corrupted
the heap.

It showed up about one run in eight, and only ever on the clean-to-dirty
transition, which is why it took a while to pin down. Any host automating
parameters while audio runs could hit it.

Contained by `SafePresetManager`, which keeps the dirty flag on the message
thread. Behaviour is unchanged for anything already on that thread. This is an
upstream bug and will be reported there; see `DESIGN.md` 3s for the diagnosis,
including the two attempts that were wrong first.

## Rebuilt: the octaver

The old octaver was an analogue-style flip-flop divider. It measured correctly
at every pitch and it sounded bad, in three ways that were all structural rather
than tuning: a hard square switched on the nearest sample boundary was gritty; a
flip-flop has memory, so one spurious crossing inverted the division and it
STAYED inverted; and the octave was literally `square x envelope`, so its
dynamics were the envelope's and nothing else's.

It is now a period tracker driving a phase-locked oscillator. Crossings are
timed rather than counted, so a lone glitch moves an estimate slightly instead
of flipping a state permanently, and there is no state left that can be stuck in
the wrong half. The waveform is a sine plus chosen harmonics, so every partial
sits at a frequency the plugin picked and nothing folds:

| note | before | after | |
|---|---|---|---|
| 111 Hz | -38.9 dB | **-59.7 dB** | 20.8 dB cleaner |
| 220 Hz | -35.6 dB | **-55.3 dB** | 19.7 dB cleaner |
| 439 Hz | -32.5 dB | **-49.3 dB** | 16.8 dB cleaner |

Growl now sets harmonic content directly instead of boosting a crossover band,
so the level compensation the previous design needed is gone -- the most Growl
can move the level is 2.3 dB, by construction.

Fixed alongside it: a crackle as notes faded out, caused by the octave's
amplitude being gated by a boolean. The old design had the identical gate and
nobody ever heard it, because a hard square is already nothing but
discontinuities. Cleaning up the waveform uncovered a fault that had been
shipping since the first release rather than introducing a new one.

All eleven octaver tests were written against behaviour rather than
implementation, so the entire architecture was replaced with every one of them
still passing.

## New

- **macOS and Linux builds**, alongside Windows.
- **Audio Unit** on macOS, in addition to VST3.
- **CI** on every push: build, tests, and pluginval strictness 10 with ten
  randomised repeats per platform. The repeat count matters -- pluginval's state
  test forgives a draw near a step, so a broken build passes about one run in
  five and a single green run proves very little.
- SHA256SUMS.txt for the archives.
- A regression test pinning the octaver's off-grid energy, so the number is
  visible if it ever moves, and another asserting it does not click as a note
  decays.

## Known limitations

- **Nothing here is code signed.** Windows SmartScreen and macOS Gatekeeper will
  both object on first launch.
- **The macOS and Linux builds have never been opened in a host by a person.**
  They compile and they pass pluginval; that is not the same as being used.
- The voicing has not been A/B'd against reference hardware. It has been played,
  and two bugs in `DESIGN.md` were found that way, but the values are chosen to
  be measurably distinct rather than demonstrably good.
- The octaver is monophonic and will stumble on chords and double stops. That is
  what an octaver of this kind does, and it is not a defect being worked around.
- **Two unexplained intermittents**, both rare, both on CI, neither reproducible
  locally. A Linux runner failed pluginval's state-restoration check once during
  this release's build ("Gate not restored", expected 0, read 0.41) and passed
  on a re-run; 40 equivalent local runs are clean. The suspicion is that
  pluginval sets the parameter on the HOST side, VST3 delivers it to the plugin
  with the next process call, and if no audio is processed in between the plugin
  never sees it -- making the host's cached value the stale one. That is a
  hypothesis and it is not proven.
- The older one: a Windows CI runner once failed the chain
  stability test and has not repeated it. It may have been the crash fixed
  above, since heap corruption can surface as a bad sample as easily as a
  segfault, but nothing proves that and it is still recorded as open.

## Licence

AGPL-3.0. The corresponding source is this repository at this tag; see
`THIRD_PARTY.md` in the archive for every dependency and its licence.
