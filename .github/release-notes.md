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

## New

- **macOS and Linux builds**, alongside Windows.
- **Audio Unit** on macOS, in addition to VST3.
- **CI** on every push: build, tests, and pluginval strictness 10 with ten
  randomised repeats per platform. The repeat count matters -- pluginval's state
  test forgives a draw near a step, so a broken build passes about one run in
  five and a single green run proves very little.
- SHA256SUMS.txt for the archives.
- A regression test pinning the octaver's off-grid energy, so the number is
  visible if it ever moves.

## Known limitations

- **Nothing here is code signed.** Windows SmartScreen and macOS Gatekeeper will
  both object on first launch.
- **The macOS and Linux builds have never been opened in a host by a person.**
  They compile and they pass pluginval; that is not the same as being used.
- The voicing has not been A/B'd against reference hardware. It has been played,
  and two bugs in `DESIGN.md` were found that way, but the values are chosen to
  be measurably distinct rather than demonstrably good.
- The octaver synthesises its square by hard switching at base rate, with no
  band limiting. Measured this release: -32 to -44 dB of non-harmonic content
  depending on the note and the tone setting, worsening about 3 dB per octave up
  the neck. It improves only 6 dB per doubling of sample rate, so oversampling
  is a poor trade; see `DESIGN.md` 3q. Whether any of it is audible under a dry
  bass is still an open question, and a listening one.
- One unexplained intermittent: a Windows CI runner once failed the chain
  stability test and has not repeated it. It predates this work, does not
  reproduce anywhere, and three candidate explanations were ruled out by
  measurement. Recorded as open in `DESIGN.md` rather than quietly closed.

## Licence

AGPL-3.0. The corresponding source is this repository at this tag; see
`THIRD_PARTY.md` in the archive for every dependency and its licence.
