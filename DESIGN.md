# BassRig — design

A VST3 bass pedal. One fully-built pedal (a preamp/drive), a pedal abstraction
with a registry so more can be dropped in later, and a custom pedal-face UI.

Working name only — the product name is one variable in `CMakeLists.txt`.

## Guiding rule

Do not invent DSP that already exists in a readable, tested, permissively
licensed form. Every block below names where it comes from. We write the
voicing, the UI, and the glue; we borrow the maths.

---

## 1. Prior art we build on

| What | Where | License | How we use it |
|---|---|---|---|
| Plugin framework | JUCE 8 | AGPLv3 **or** commercial | Host, VST3 wrapper, `juce::dsp` |
| Circuit modelling | [chowdsp_wdf](https://github.com/Chowdhury-DSP/chowdsp_wdf) | **BSD-3** | Tone stack, Sallen-Key, bridged-T as real component models |
| ADAA waveshapers | [chowdsp_waveshapers](https://github.com/Chowdhury-DSP/chowdsp_utils) | **GPLv3** | Anti-aliased clippers — see §6, licence decision |
| Oversampling, LR crossover, SVF | `juce::dsp` | with JUCE | Already good, no reason to rewrite |
| Build / CI / test scaffold | [Pamplejuce](https://github.com/sudara/pamplejuce) | MIT | CMake layout, Catch2 + pluginval in CI |
| UI debugging | melatonin_inspector | MIT | Live layout inspection while building the pedal face |

### Read-but-don't-copy references

These are **AGPLv3**, so lifting code makes BassRig AGPLv3. We read them for
stage topology, calibration method and sanity-checking our numbers:

- [Obsidian-B7000](https://github.com/tehguitarist/Obsidian-B7000) — Darkglass
  B7K Ultra solved as a WDF from traced component values. Thirteen DSP stages,
  null-tested against real pedal captures (−26.5 dB clean, −9.4 dB max drive),
  and published CPU/latency numbers per oversampling factor. This is the
  reference standard for what "done properly" looks like.
- [NoAmp-Low-Rider-DI](https://github.com/tehguitarist/NoAmp-Low-Rider-DI) —
  three reverse-engineered generations of the Tech 21 SansAmp Bass Driver DI.
- [Open-Source-Bassman-Preamp](https://github.com/flubber2077/Open-Source-Bassman-Preamp)
  — uses ADAA specifically to get away with 2× oversampling instead of 8×.
  That trade is the one we want.

Since a circuit-exact B7K clone already exists and is done well, BassRig is
**not** a clone. We take the topology that the whole B7K/BDDI family shares —
it is the reason bass drives work at all — and voice it ourselves.

---

## 2. What the pedal is

A bass preamp/overdrive. The defining feature of the class, and the thing a
generic distortion plugin gets wrong, is that **the low end never enters the
clipper at full strength**. Clipping a low B turns it to mud; the fix is a
parallel clean path at unity plus a bass-content control on what feeds the
drive.

### Signal chain

```
in ─ DC block ─ trim ─┬─ CLEAN ─────── delay(N) ──────────────┐
                      │                                        ├─ BLEND ─ EQ ─ master ─ out
                      └─ GRUNT hpf ─ ATTACK shelf ─ ↑2× ─ drive ─ shaper A ─ shaper B ─ recovery lpf ─ ↓2×
```

- **Grunt** (3-way): how much low end reaches the clipper. The single most
  important control and the one most plugins omit.
- **Attack** (3-way): pre-emphasis treble shelf — boost/flat/cut before drive.
- **Drive**: gain into the shaper cascade.
- **Shaper A**: soft, asymmetric, JFET-ish. **Shaper B**: harder, CMOS-ish.
  Cascading two mild stages sounds far better than one aggressive one.
- **Recovery LPF**: kills the fizz that any clipper produces above ~4 kHz.
- **Blend**: clean at unity against independently-levelled drive.
- **EQ**: Baxandall bass + treble, two semi-parametric mids with switchable
  centres (250/500/1k and 750/1.5k/3k) — modelled as WDF, not biquads copied
  off a chart.

### Controls

Master · Blend · Drive · Level · Bass · Lo-Mid · Hi-Mid · Treble
+ Attack (3-way) · Grunt (3-way) · two mid-frequency selectors · bypass.

---

## 3. The blend phase trap

The drive path is oversampled and filtered; the clean path is not. If we blend
them without compensating, the clean path arrives early and the blend comb
filters. **The clean path carries a delay line matched to the oversampler's
latency plus the ADAA cascade's**, and the total is reported to the host via
`setLatencySamples()`.

**Where the damage lands, corrected after measuring.** The first draft of this
document said the comb hurts "exactly where bass lives". That is wrong, and the
first version of the test was aimed there and consequently passed with the delay
line deliberately deleted. At 2x oversampling the latency is 49 samples, which
at 41 Hz is only ~15 degrees of phase, inaudible. The nulls sit at
`fs / (2 * latency)` and its odd multiples: ~490 Hz, ~1.5 kHz, ~2.4 kHz. The
mids, where the drive character actually lives. With compensation removed,
490 Hz drops 13 dB.

The test now probes those null frequencies, derived from the reported latency so
it stays correct if the oversampling changes, and it is verified to fail when
the delay line is removed. A guard that has never been seen to fail is not yet a
guard.

### A second trap, found while building

`juce::dsp::ProcessorDuplicator` hands its `state` to each per-channel filter
when it constructs them inside `prepare()`. Assigning `.state` afterwards, the
obvious way to switch between precomputed coefficient sets without allocating on
the audio thread, updates the duplicator's pointer and not the filters'. They
silently keep running default coefficients, which output pure silence.
`DrivePedal` therefore owns its shelf filters directly and assigns
`filter.coefficients` per channel: still just a refcount bump, no allocation, and
it actually works.

## 3b. What step 4 measured

The drive path is now `[ADAA tanh, biased] -> coupling HP -> gain -> [ADAA soft
clipper, degree 5] -> output HP -> recovery LP`, all inside 2x oversampling.

**ADAA earns its place.** Same chain, same 2x oversampling, ADAA stages swapped
for direct evaluation (`-DBASSRIG_NAIVE_SHAPERS=ON`), aliasing measured as
energy below the fundamental, where a memoryless nonlinearity fed a sine cannot
legitimately put anything:

| f0 | drive | ADAA | naive | benefit |
|---|---|---|---|---|
| 1493 Hz | 1.0 | -78.0 dB | -58.9 dB | 19 dB |
| 2999 Hz | 1.0 | -64.7 dB | -42.1 dB | 23 dB |
| 4001 Hz | 1.0 | -67.7 dB | -51.8 dB | 16 dB |

Total harmonic content is unchanged in both (-11 / -19 / -23 dB), so this is
aliasing being suppressed, not distortion being filtered away. That is the whole
argument for 2x instead of 8x.

### Four things that were wrong, and what the measurements said

1. **Stacked high-passes wrecked the low end.** Coupling at 30 Hz plus an output
   DC blocker at 20 Hz, against a clean path with neither, put the two ~90
   degrees apart at 41 Hz. Blend coherence measured **0.23 at 31 Hz and 0.30 at
   41 Hz**: the blend was eating the fundamental of the low B and low E, the
   exact failure this pedal exists to avoid. Corners moved to 5 Hz and Grunt
   "Full" to 10 Hz; coherence is now 0.94-0.99 everywhere.

2. **Fixed bias after the gain is not asymmetry.** With the signal already
   scaled 13x, a 0.18 offset gave 0.08% peak asymmetry. But scaling the bias
   with the gain is worse: at full drive the bias alone reaches 3.6 and pins the
   stage. Correct answer is fixed bias, post-gain, with the asymmetry showing up
   as **even harmonics** (a duty-cycle shift) rather than peak height. Peak
   asymmetry is meaningless once a stage saturates, since both halves reach the
   same magnitude.

3. **Odd functions do generate DC.** Stage B is an odd function fed a DC-free
   signal, which sounds like it cannot produce an offset. But DC-free is not
   symmetric: stage A hands it a zero-mean waveform whose halves differ in
   shape, and clipping that leaves **-52 dBFS of constant DC**. It needed its
   own blocker.

4. **The bias must go inside the oversampled region.** At base rate it is a DC
   step ramping through the oversampling filter, which reintroduced the -21 dBFS
   startup thump that `settleDcPath()` exists to prevent. Inside, the
   oversampler never sees DC and silence settles to 1.3e-7, about one LSB at
   24-bit.

Two of the failures along the way were the tests being wrong, not the DSP:
measuring DC over 18.78 cycles of a 110 Hz tone reports the leftover partial
cycle as an offset, and asserting bit-exact silence from a biased stage is an
overclaim. Both are now measured over whole cycles and against a documented
floor.

---


## 3c. What step 6 measured

The EQ section is a Baxandall bass/treble stack solved as a wave digital filter,
adapted from the BaxandallEQ example in jatinchowdhury18/WaveDigitalFilters
(GPL-3.0), plus two semi-parametric mid bands.

**The mids are peaking biquads, deliberately.** A semi-parametric mid in this
class of pedal is an active section around an op-amp: self-contained, sharing no
components with the other controls, and its response IS a second-order peak. A
peaking filter is the equivalent circuit there, not an approximation of one.
Measured, they land on their nominal gain exactly: +6.00 dB and -6.00 dB.

**The interaction argument for WDF does not survive measurement.** The
justification originally written for using a WDF was that bass and treble
interact through the shared network in a way independent biquads cannot express.
Tested: bass swings 21.45 dB at 60 Hz with treble at minimum and 21.49 dB with
treble at maximum. **0.04 dB.** At 250 Hz and 1 kHz the difference is 0.3 dB and
0.2 dB. The controls are effectively independent in this circuit and that claim
was wrong.

What the WDF did buy is two facts that only fell out of modelling the circuit,
both of which turned out to matter a lot:

| | |
|---|---|
| Flat is at pot **0.90**, not 0.50 | Centring the pot linearly measures +6.5 dB at 40 Hz relative to 1 kHz. A knob at noon would not have been neutral. |
| Boost and cut are **asymmetric** | +16.4 dB against -9.1 dB at 40 Hz. |
| Insertion loss is **-20.98 dB** | The network is passive. Without makeup the EQ section is a large volume drop. |

Well-fitted shelving biquads could probably match the result. They would just
have to be fitted against something, and this is that something.

### The taper, which took three attempts

1. **Upstream's mapping has a dead zone.** `clamp(1 - knob^3.333, 0.01, 0.99)`
   puts flat at noon correctly, but `1 - knob^3.333` exceeds 0.99 for every knob
   below 0.251, so it clamps. Measured: knob 0.00 and knob 0.25 gave identical
   output. The bottom quarter of both controls did nothing.
2. **A single taper exponent cannot fix it.** The two halves of the knob need
   opposite curvature. Flat sits at pot 0.90, near one end of the travel, so cut
   runs across the narrow span 0.99..0.90 where decibels change steeply with pot
   position, while boost runs across the wide span 0.90..0.01 where they change
   gently. An exponent that spread one side evenly bunched the other.
3. **Separate exponents**, solved from the measured dB-versus-pot curve: 1.35 on
   cut, 0.6 on boost. Bass now reads -9.1 / -3.4 / +0.2 / +9.3 / +16.4 dB across
   the travel, and flat at noon holds to +-0.25 dB from 40 Hz to 8 kHz.

Still uneven: **treble is top-heavy**, most of its boost living in the last
quarter of the knob, because its dB-versus-pot curve differs from bass's and the
two share a taper. Separate treble exponents are step 5 work.

### Cost

Re-solving the WDF tree recomputes a six-port scattering matrix. Upstream does
that per sample; here it happens every 32 samples and only while a control is
actually moving, so static knobs cost nothing and a sweep costs a sixteenth of
upstream. Also note the tone stack runs at base rate, not oversampled, so its
treble response carries some bilinear frequency warping near 10 kHz. Upstream
oversamples to avoid that. Not yet measured -- listed under outstanding.

---


## 3d. The pedal face

Drawn entirely in `juce::Graphics`, no image assets, so it scales cleanly and
the repository stays text.

**Looking at it, rather than guessing.** `tools/Snapshot.cpp` builds a small
console app that constructs the editor and renders it to PNG headlessly, with no
window and no host. That turns "does the UI look right" into something that can
actually be checked during an ordinary build, and it is how the layout problems
below were found:

```bash
cmake --build build --config Release --target BassRigSnapshot && ./build/BassRigSnapshot_artefacts/Release/BassRigSnapshot.exe snapshots
```

**Fixed design size, scaled by transform.** The face is laid out at 460x648 and
scaled with an `AffineTransform`, with the aspect ratio locked. A pedal is a
physical object: the control positions carry meaning, and reflowing them into a
different arrangement at a different window size would make it a different
pedal.

### What the first render showed

- A large dead band across the bottom third, and `TRIM` stranded to one side
  leaving a hole in the middle of its row. Enclosure shortened from 720 to 648
  and the voicing row rebuilt on even thirds.
- Every value arc grew from the far left, including the EQ and gain knobs whose
  neutral point is the middle of their range. Knobs now take an arc origin: the
  drive and blend knobs read from zero, while the EQ, trim, level and master
  knobs grow their arc from the default and carry a tick mark there. Double
  click returns any knob to that point.
- The faceplate said "MICROTUBE DRIVE / DI". Microtube is Darkglass's
  product-line trademark, and section 1 says explicitly that BassRig is not a
  clone of their pedal. Replaced with "BASS PREAMP / DRIVE". Borrowing GPL code
  with attribution is one thing; wearing someone else's brand is another.

### Tested, not just eyeballed

`tests/EditorTests.cpp` renders the face and asserts it is not blank, that it
survives being scaled, and -- the one that matters -- that changing each
parameter actually changes pixels. That last test is what stops a dropped
attachment from silently turning a control into a picture of a control.
Verified by deliberately dropping the three-way switch attachment: the attack
switch went to **0 differing pixels** and the test failed, as it should.

---


## 3e. What step 5 measured

Voicing, with one honest limitation stated up front: nobody listened to any of
this. Every value below is chosen so a control is measurably distinct, correctly
ranged and free of dead travel. Final taste is still an open question.

### Three controls had inert travel

Each was found the same way, by sweeping and plotting rather than sampling at
quarter points.

**The drive knob wasted its top half.** Gain was `jmap(knob^2, 1, 20)`, and
harmonic content rose 17.7 dB over the lower half of the travel against 1.6 dB
over the upper half: the cascade saturates long before the gain runs out.
Measured harmonics against gain, inverted the curve, and tabulated it, so
distortion now rises in even 2.3 dB steps from -26.2 dB to -7.8 dB. Gain is also
capped at 8 instead of 20 on the same evidence -- going 8 to 20 buys under 1 dB
of extra harmonic content while making the last eighth of the knob swing gain by
15 instead of 4.

**The EQ taper had a step at the centre detent.** Linearising decibels needs a
power below 1, and every power below 1 has infinite slope at zero. The fitted
exponent moved the bass control 1.1 dB within one percent of knob travel just
above noon; the exponent the optimiser actually wanted moved it 4.8 dB.
Quarter-point sampling hid this completely, which is how it survived step 6.
Both tapers are now the exact inverse of the measured curve: 0.00 dB deviation
from linear, no singularity, and separate tables for bass and treble because
their curves differ.

**Drive changed the volume.** The 1/sqrt(gain) makeup held level within 0.7 dB
over the first sixty percent of the knob and then sagged 5.1 dB. No single
exponent fixes it -- correcting the top over-compensates the middle -- so the
makeup is a measured table too. It is also normalised: the drive path measured
4.0 dB hotter than clean, which meant a 50/50 Blend was not a balance at all,
just the driven signal dominating.

### Two measurements of mine were wrong before the DSP was

**Attack looked like a dead control.** All three positions measured identically.
The control was fine; the measurement was not. Attack is pre-emphasis BEFORE the
clipper, so it can only act on content the input already carries above 2 kHz,
and a pure sine has none -- the harmonics do not exist until after the clipper.
On a band-limited sawtooth it works. The shelf was then widened from +-6 dB to
+-9 dB, because correcting the drive taper had reduced the gain at a given knob
position and with it the cascade that was amplifying the shelf: the cut
direction had dropped to 0.9 dB of effect. It now spans 4.4 dB.

**Level matching made the blend look worse.** After normalising the drive path
to unity, blend coherence at 30.9 Hz fell from 0.944 to 0.897 and failed its
test. The phase error had not changed. Matched levels simply expose it: the
blend ratio goes to 1 when one path dominates but to cos(theta/2) when they are
equal, so the old 4 dB imbalance had been masking it. Grunt "Full" moved from
10 Hz to 5 Hz in response, which lifts coherence to 0.943 at 30.9 Hz and 0.966
at the low E.

### Grunt

Left at 5 / 100 / 250 Hz. Measured on a low E at drive 0.7, harmonic content is
-8.1 dB at Full, -19.0 dB at Mid and -38.6 dB at Tight: three clearly separated
behaviours. Whether those are the right three is an ear question.

---


## 3f. Presets

`chowdsp::PresetManager` (GPLv3) takes a plain `juce::AudioProcessorValueTreeState`,
so it dropped in without migrating the state layer. It brings file IO, versioning,
dirty tracking and user preset paths. chowdsp also ships a presets UI component
and this deliberately does not use it: the backend logic is worth borrowing, the
visual identity is not, so the preset strip is drawn in the pedal theme.

Thirteen factory presets, built from the processor parameter list rather than from
a captured snapshot, so a preset can never silently omit a parameter added later
-- anything a preset does not name lands on its documented default.

**"Default" carries no overrides at all.** It is exactly the parameter defaults,
which means the name in the preset box agrees with what a knob does when you
double click to reset it. Without that, resetting a control would quietly move
the plugin away from the preset it still claims to be on.

### The restore order is a trap

`PresetManager::loadXmlState` does not merely record which preset was selected.
It calls `loadPreset`, which writes that preset over every parameter. So the
obvious ordering in `setStateInformation` -- restore the parameters, then restore
the preset -- throws the parameters away. Measured: a session saved after editing
"Mid Push" reopened on the unedited "Mid Push", with the edit silently gone.

The saved parameters are the truth, so the preset state is restored first and the
parameters land last. Both directions are now tested: an edited preset comes back
edited and still marked dirty, and an unedited one comes back clean rather than
falsely wearing a dirty marker. Reversing the two lines fails both.

### A crash found by a bad test

The preset audio test first segfaulted. That one was the test being wrong -- it
handed a one-channel buffer to a processor configured stereo, and JUCE guarantees
a host supplies `max(numIn, numOut)` channels. The test was fixed, and
`processBlock` also gained a `jmin` on the channel count, because a broken
contract should not be a hard crash in a plugin someone else is hosting.

### Also wired

Presets are exposed through the host program API (`getNumPrograms`,
`setCurrentProgram`, `getProgramName`), so a DAW sees them as programs, not just
the plugin UI. Every factory preset is rendered in a test and checked for finite,
non-silent, non-exploding output -- they are hand-typed numbers, and a bad one
would otherwise surface only in use.

---


## 3g. Pedal #2, and what the abstraction was worth

A compressor, ahead of the drive. Built on `chowdsp::compressor` (GPLv3) rather
than hand-rolled -- its level detector and gain computer are tested DSP, and the
interesting decisions for a BASS compressor are elsewhere.

### The sidechain filter is the whole point

Feed a compressor the full signal and the fundamental of a low B dominates the
level detector, so every note pumps the whole band in sympathy with the bottom
octave. Filtering the key input means it responds to the note instead.

Measured on a loud 41 Hz fundamental under a quiet note at 330 Hz:

| Sidechain | Gain reduction |
|---|---|
| Off | -19.98 dB |
| 80 Hz | -11.16 dB |
| 160 Hz | -5.57 dB |

Twenty decibels of gain reduction pulled by a fundamental that should not be
driving the detector at all. It defaults to 80 Hz rather than Off, because for a
bass compressor the filtered key is the correct behaviour rather than an
advanced option.

### The registry earned its keep, with one change

The claim in section 5 was that a second pedal costs a header, a source file and
one line in `PedalRegistry`. That was almost true. The gap was parameters: they
lived centrally in `Parameters.cpp`, so a new pedal also had to remember to
register its controls somewhere else entirely.

Each `Entry` now carries an `addParameters` function, `Params::createLayout()`
just asks the registry, and each pedal owns its own layout. A pedal added
without its parameters now fails to compile rather than shipping dead knobs.
Parameter IDs still live in one list in `Parameters.h`, prefixed per pedal, so
two pedals cannot collide.

The processor holds a chain rather than a single pedal, and every pedal runs on
every block. There is still deliberately no runtime pedal swapping: that needs
either a lock shared with the audio callback or a lock-free handover, and a rig
does not need one. A pedal that should be out of circuit has its own bypass,
which costs one atomic load.

### Two judgement calls

**The compressor defaults to off.** A drive at its default setting announces
itself the moment you play; a compressor quietly changing dynamics and level
does not, and a plugin that squashes on insert without being asked is a
surprise. Presets that want it turn it on.

**Gain reduction is measured either side of the compressor** rather than read
out of its internals, so the meter shows the change the listener actually gets.
The value crosses to the UI as a relaxed atomic polled on a timer, so nothing in
the meter can block the audio thread.

---


## 3h. Pedal #3: the envelope filter

In the spirit of the EBS BassIQ. Not a clone -- no schematic was traced -- but
the control set and the three voices come from that pedal, because it is the one
that got envelope filtering right for bass.

Three voices, and they measure as three different things (spectral centroid of a
110 Hz note, quiet against hard):

| Mode | Quiet | Hard |
|---|---|---|
| Up | 219 Hz | 567 Hz |
| Down | 298 Hz | 123 Hz |

Hi-Q is a band-pass rather than a low-pass, and carries under 70% of the low-end
energy that Up does -- that is where the quack comes from.

**The high-pass mix-in is the bass-specific part.** A resonant low-pass swallows
string definition, so a high-passed copy of the dry signal is blended back over
the top. On the hardware this is an internal trim; here it is a knob, because a
plugin does not need a screwdriver. Measured, turning it up multiplies the
energy above 2 kHz by more than four when the filter is otherwise closed.

The filter is chowdsp's ARP 4072 emulation rather than a plain SVF, used in
**limit mode**: resonance soft-limits instead of running away. With Q at maximum
in Hi-Q mode over a full-scale low E, output peaks below 12x rather than
diverging.

The envelope is monophonic on purpose. A bass is a mono source, and letting the
channels sweep independently would smear the effect across the stereo image
instead of moving it as one thing.

## 3i. The rack, and why the enclosure stopped growing

Three fixed sections had already reached 870 pixels. Adding the envelope filter
the same way would have made it 1080, and the chorus and phaser below would take
it past 1500 -- a plugin window taller than most screens.

So the face is now split. **EQ and drive stay permanently visible**, because
they are the pedal's identity rather than effects you switch between.
**Everything else lives in a tabbed rack**, one panel at a time, and the
enclosure stays the same height however many pedals go in it. Each tab carries a
lamp showing whether that pedal is running, so a rack pedal working in the
background is visible without selecting its tab.

Two things this cost, both worth paying: you can no longer see every control at
once, and the snapshot tool had to learn to render each tab (it now writes one
PNG per tab, so a panel cannot be quietly broken behind an unselected one).

## 3j. Planned: chorus and phaser

Neither is built. This is the design, not a description of code.

### Chorus

The bass-specific problem is the same one the drive pedal has: **modulating the
low end turns it to soup**. A guitar chorus applied to a bass makes the
fundamental wander, and the note stops sitting still in the mix.

The answer is the one this project keeps arriving at -- split the band and only
process the top:

```
in ---+--- low-pass (crossover) ------------------------- unmodulated --+--- out
      |                                                                 |
      +--- high-pass -- [delay 1 modulated] -- +------------------------+
                        [delay 2 modulated] ---+
```

- **Crossover at 150-250 Hz**, exposed as a control. Below it, nothing moves.
  This is what an EBS UniChorus does with its "bass filter", and what makes a
  bass chorus usable at all.
- **Two modulated voices** rather than one, LFOs offset in phase, so it thickens
  rather than simply detunes.
- Delay range 5-25 ms, depth in milliseconds rather than a percentage.
- `chowdsp_dsp_utils` has the delay lines with proper fractional interpolation;
  a modulated delay read at the wrong interpolation order is where chorus
  artefacts come from, so this is not the place to hand-roll one.
- Crossover must be **all-pass complementary** (Linkwitz-Riley), or recombining
  the bands will notch the crossover region. `juce::dsp::LinkwitzRileyFilter`
  gives that; a plain pair of filters does not.
- Controls: Rate, Depth, Mix, Crossover, and a two-way for Chorus / Vibrato
  (mix locked wet).

Risk to watch: the same phase-coherence trap as the drive blend. The dry low
band and the modulated high band recombine, so their relative delay matters. The
existing coherence test formulation transfers directly.

### Phaser

Simpler DSP, and a different bass problem.

```
in --+-- [all-pass x N, modulated] --+-- (+/-) --> out
     |                               |
     +------------- dry -------------+
```

- **4 / 6 / 8 stage switch.** Stage count is the character control: 4 is a gentle
  sweep, 8 is vocal and resonant.
- Feedback control, and it must be **soft-limited** the way the envelope filter
  is -- a phaser with high feedback and a low sweep is another resonance that
  can run away.
- **Notch placement matters more than on guitar.** The default sweep range wants
  to sit above ~200 Hz, or the notches land on the fundamental and the note
  hollows out. Same reasoning as the chorus crossover, arrived at differently.
- Invert switch: summing dry and wet in antiphase moves the notches, and the two
  sound genuinely different.
- `chowdsp_filters` has first-order all-pass sections; modulating them wants the
  same care as the envelope filter cutoff, so per-sample coefficient updates on
  a TPT structure rather than a biquad.
- Controls: Rate, Depth, Feedback, Stages, Mix, Invert.

### Both

- They belong in the rack, as two more tabs. Signal order: compressor, envelope
  filter, phaser, chorus, drive -- modulation before distortion, so the drive
  grinds a moving signal rather than smearing an already distorted one.
- Both need an LFO. One shared, tested LFO with a rate-sync option is better than
  two; `chowdsp_sources` has oscillators, though a chorus LFO wants triangle or
  sine rather than the band-limited ones aimed at audio rates.
- Neither adds latency, so no change to the latency arithmetic.

---


## 3k. Chorus and phaser, and a plan that did not survive contact

Both built to the section 3j design. One half of it was right and one half was
wrong.

### The chorus crossover works exactly as planned

Modulation depth measured on tones whose period divides the sample rate exactly:

| Tone | Modulation depth |
|---|---|
| 46.9 Hz (below crossover) | 0.0005 |
| 1200 Hz (above it) | 0.106 |

A separation of over 200x. Below the crossover the low end simply does not move,
which is the whole difference between a bass chorus and a guitar chorus pointed
at a bass.

### The phaser plan was wrong

Section 3j said a sweep floor above 200 Hz would keep the notches off the
fundamental. It does not, and the reason is not obvious: **phase shift
accumulates across the all-pass chain**. Six stages with the sweep floored at
220 Hz still swing about 134 degrees at 47 Hz. Measured, the fundamental
modulated MORE than the midrange:

| | 46.9 Hz | 750 Hz |
|---|---|---|
| Sweep floor only, as planned | **0.99** | 0.79 |
| With a crossover | **0.008** | 1.76 |

So the phaser got the crossover the chorus has. Same conclusion, reached from a
different direction, and only because it was measured rather than reasoned about.

### Two measurement mistakes, again the same one

The first readings were nonsense in both pedals, and for the reason that has now
caught this project three times: **a fixed measurement window is not a fixed
number of cycles.** Level measured over 2048 samples covers 1.9 cycles of a
45 Hz tone, and the leftover fraction moves the reading far more than the effect
does. Then, once the window was made a whole number of cycles, it was still
4096 samples long -- a third of an LFO cycle -- so it averaged across the
modulation and flattened it.

Windows are now a whole number of cycles AND short relative to the LFO, and the
test tones are chosen so their period divides 48 kHz exactly.

## 3l. Reordering the chain

The design said, three times and in writing, that there would deliberately be no
runtime pedal swapping, because it needs either a lock shared with the audio
callback or a lock-free handover. Asked for it, that reasoning turned out to be
answering the wrong question.

**The set of pedals never changes.** Every one is built once and lives for the
life of the plugin. Reordering changes only the order they are visited in, and
that fits in a single word: four bits per slot, packed into one atomic. The
audio thread does one acquire load per block, the message thread one release
store. No lock, no allocation, and the order can only ever be seen whole rather
than half applied.

The order lives as a property on the APVTS tree rather than as a parameter,
because a permutation is not something a host should be able to automate into an
invalid state. Anything that is not a clean permutation is discarded rather than
patched up: a half-valid order would silently drop or double a pedal, which is
much harder to notice than reverting to the default.

### The bug that found

Presets can carry an order, which is what makes a preset like "Backwards Rig" --
drive first, compressor levelling an already distorted signal -- possible at
all. That silently did not work.

`AudioProcessorValueTreeState::replaceState` assigns the tree (`state =
newState`), which **redirects** it. No property-changed callbacks fire for the
new contents. Preset loading calls `replaceState` directly, so a listener
watching only for property changes never heard about a preset order and quietly
ignored it. Handling `valueTreeRedirected` as well fixes it, and there is now a
test that loads the preset and checks the order actually applied.

### The chain strip

Order that lives only in a saved property is order nobody will ever discover, so
the face grew a strip of chips in signal order, dragged to rearrange, each with
a lamp for whether that pedal is on. Dragging commits nothing until the mouse is
released -- partly so a drag that changes its mind costs nothing, and partly so
the audio thread is not handed a new order on every mouse move.

---


## 3m. Octaver and noise gate

### The octaver divides rather than tracks

Frequency division -- a comparator into two flip-flops -- rather than pitch
detection, and that is a deliberate choice rather than the easy way out. A
tracker needs a couple of cycles before it is confident, and at 31 Hz that is
60 ms of latency. This plugin reports latency honestly, so it could not have
been hidden. A divider responds on the next zero crossing and costs nothing.

Measured, with Direct at zero so the reading is unambiguous:

| Note | Energy one octave down | Energy at the original pitch |
|---|---|---|
| 55 Hz | 81.3% | 1.4e-7 |
| 110 Hz | 81.6% | 2.1e-7 |
| 220 Hz | 82.0% | 1.2e-6 |

The price of a divider is that it works on one note at a time and stumbles on
chords and the first milliseconds of a note. That is not a defect to be fixed --
it is what an OC-2 does, and most of why people like them.

Two bass-specific pieces. The **tracking low-pass is steep and exposed**,
because what the comparator sees has to be the fundamental and nothing else, and
where that sits depends on how far up the neck you are. And a **squelch**:
without it the comparator triggers on noise between notes and the flip-flops
free-run, warbling an octave out of silence.

### The gate

Three things separate a usable gate from an irritating one, and all three are
about not being fooled by the signal: **hysteresis** (a single threshold makes
it chatter as a note decays across it), **hold** (bass envelopes ripple, and
without hold the gate slams shut in the dips), and the **sidechain filter** --
the same one the compressor has, for the same reason.

**Range rather than a hard mute**, because a gate that slams to silence draws
more attention than the noise it removes.

### One thing worth knowing about the threshold

The threshold is measured on the FILTERED key, so the level that actually opens
the gate depends on the note. At the 80 Hz setting a 110 Hz note reaches the
detector at 0.81 of its real level, so it opens about 2 dB later than the dial
says.

This surfaced as a failing test -- a tone set just above the threshold never
opened the gate -- and the first instinct was to call it a bug. It is not. It is
the filter doing exactly what it is there for, and it is true of any
sidechain-filtered gate. The test was rewritten to target hysteresis directly
with the filter switched off, and the behaviour is documented on the pedal.

### Default placement

The gate goes first, so hum is dealt with before the compressor lifts it. Drag
it to the end instead if what needs gating is drive hiss rather than pickup hum
-- which is the whole point of having a chain strip.

### Two strips that stopped fitting

Seven chips and six tabs across a 460-pixel face clipped every label, because
both were sizing their indicator lamp from the row height. At two or four items
that looked fine; at seven the lamp swallowed the width. Both now use a
fixed-width lamp column.

---


## 3n. The octave that vanished on the low strings

Reported from playing, not from a test: the octave drops out at the A on the E
string and below. Three hypotheses, two of them wrong, and the answer was not a
bug at all.

**Wrong: harmonics confusing the divider.** The tracking low-pass is fixed at
250 Hz, so for a 55 Hz note its 2nd, 3rd and 4th harmonics all pass to the
comparator, and the lower the note the more harmonics get under the cutoff. It
was a good story. Measured with a realistic harmonic series, tracking held at
0.78 even at E1 -- barely worse than the 0.82 a pure sine gives.

**Wrong: the note decaying below the squelch.** The envelope follower ripples
much more deeply at low frequencies (67% per cycle at 41 Hz against 8% at
220 Hz), so a decaying low note could plausibly flicker the squelch. Measured
across a two-second plucked note: the octave survived 100% of it at every pitch.

**Right: the octave is simply below what a speaker reproduces.** Sub output
level is flat across the neck -- the divider is perfect. What changes is where
that energy sits:

| Note | Octave | Below 40 Hz | Audible |
|---|---|---|---|
| E1 41.2 Hz | 20.6 Hz | **79%** | 21% |
| A1 55.0 Hz | 27.5 Hz | **80%** | 20% |
| C2 65.4 Hz | 32.7 Hz | 80% | 20% |
| E2 82.4 Hz | 41.2 Hz | 9% | 91% |
| A2 110 Hz | 55.0 Hz | 0% | 100% |

The cliff falls between C2 and E2, which is exactly where it was reported. The
original test used a pure sine and measured the octave as a SHARE of total
energy -- normalised, so it hid the fact that four fifths of that share was
inaudible.

### Growl

Physics is not an acceptable place to stop, so the octave now has a Growl
control that lifts its harmonics. A square at 27.5 Hz has harmonics at 82, 137
and 192 Hz; they carry the same pitch and they are comfortably audible.

**It adds rather than trades, and that distinction was earned.** The first
version crossfaded between the fundamental and the harmonics against a 90 Hz
crossover. Measured, that gained up to 8 dB on the low strings but cost 6.6 dB
on E2 and A2 -- it was discarding exactly the fundamental those notes needed.
Boosting the upper half of an all-pass complementary crossover instead means
Growl at zero is the untouched octave, and turning it up can only help:

| Note | Octave | Audible energy at full Growl |
|---|---|---|
| E1 | 20.6 Hz | +10.3 dB |
| A1 | 27.5 Hz | +11.2 dB |
| C2 | 32.7 Hz | +11.6 dB |
| E2 | 41.2 Hz | +7.0 dB |
| A2 | 55.0 Hz | +8.4 dB |

Most help where the problem is, and never negative anywhere.

---


## 3o. The octave that jumped up as notes decayed

A second report from playing: the octave jumps UP as a note gets quieter, worse
on lower notes. Reproduced at 41.2 Hz, where the sub went 20.5 -> 20.5 -> 41.7
-> 164.8 Hz across a decaying note; at 55 Hz it went 27.8 -> 27.8 -> 55.7.

**The cause.** The squelch was a single absolute threshold, and the envelope
follower was a plain one-pole. At 41 Hz that follower ripples 67% per cycle,
against 8% at 220 Hz. Near the threshold the squelch therefore flickered every
cycle, and each flicker reset the flip-flops mid-note. A divider that gets reset
partway through loses its place, and the division comes out wrong. The
frequency-dependent ripple is precisely why it was worse the lower the note.

Three changes: a hold on the envelope longer than one cycle of the lowest note,
hysteresis on the squelch, and resetting the flip-flops only after the input has
been quiet for a while rather than instantly.

**They are redundant, and that was verified rather than assumed.** Reverting
them one at a time, the test passes every time; only reverting all three brings
the fault back. Any one of the three prevents it. None is "the" fix, which is
recorded in the code so none of them is later removed as apparently unnecessary.

### The regression test did not work the first time

Written, run against the fixed code, green. Then run against the reverted code
-- also green. It was not testing anything.

The note decayed too slowly and the test was too short: the fault only appears
once the level reaches the squelch region around 2.75 s, and the test stopped at
2.73 s. It measured only the healthy part of the decay. Fixed by decaying faster
so the fault falls inside the measured windows, and by skipping windows where
the octave is legitimately silent rather than asking what frequency silence is.

This is the second time on this project that a guard has been written, looked
correct, and proved to be testing nothing. Both were caught the same way, and it
is the only reason to keep doing it.

### Three wrong diagnoses first

Worth recording, because the wrong ones were all plausible:

1. Harmonics inside the tracking filter confusing the comparator. Measured with
   a realistic harmonic series: tracking held at 0.78 even at E1.
2. The note decaying below the squelch. Measured across a plucked note: the
   octave survived 100% of it at every pitch -- because that test, too, was not
   quiet enough to reach the fault.
3. Zero-crossing counting said the sub ran at 3x its correct frequency at every
   note including ones already verified spectrally. That was the growl
   crossover ringing on a square edge, not the divider.

---


## 3p. The bug the tests could not see

pluginval at strictness 10, run for the first time after fifteen build steps and
94 green tests, failed:

```
!!! Test 30 failed: Phaser not restored on setStateInformation
      -- Expected value within 0.1 of: 0, Actual value: 0.374501
!!! Test 38 failed: Chorus not restored on setStateInformation
      -- Expected value within 0.1 of: 0, Actual value: 0.104727
```

Nothing in this project could have found it. Every test here listens to the
output, and the output was correct. What was wrong was the host's view of a
parameter.

### The mechanism

`juce::AudioParameterBool` stores whatever raw normalised float it is handed and
only thresholds at 0.5 when read, so it will sit at 0.47 while reporting false.
That much is fine. The problem is its `NormalisableRange`, which is
`{ 0.0f, 1.0f, 1.0f }` — interval 1. APVTS saves the **denormalised** value, and
denormalising 0.47 through an interval of 1 snaps it to 0. **The save is lossy.**

The host does not forget. A VST3 host caches the value it sent, so after a save
and restore its cache says 0.47 and the plugin says 0. That divergence is wrong
in an automation lane and wrong again the next time the host saves.

The fix is one character — a continuous range, `{ 0.0f, 1.0f }`. Normalised and
denormalised then agree and the save is exact. The parameter still reports two
steps and still reads as a boolean, so hosts still draw a switch, and `get()`
thresholds exactly as before. `Params::LosslessBool` is otherwise a
transcription of JUCE's class, written out rather than subclassed because
`AudioParameterBool` declares `setValue`, `getValue` and its backing float all
private and `get()` is non-virtual.

### Two fixes before it, both wrong the same way

Worth recording because both looked right and one of them passed.

1. **Force every parameter to match the tree after `replaceState`.** APVTS skips
   any parameter whose denormalised value is unchanged, so the restore was a
   no-op; re-applying it afterwards fixed the failing case. It also made the
   round trip land on exactly 0 or 1, which is not what the host sent, so
   pluginval kept failing — with a different message. Treating the divergence
   rather than its cause.

2. **Snap on assignment**, the way `AudioParameterChoice` does. This made the
   plugin silently alter values the host had sent, which is the same divergence
   with the sign flipped. pluginval carried on failing at about the same rate:
   3 runs in 8.

Both were abandoned only after reading pluginval's own source, which settled in
thirty seconds what two rounds of plausible reasoning had got wrong:

```cpp
const auto originalValue = parameter->getValue();
parameter->setValue (r.nextFloat());
callSetStateInformationOnMessageThreadIfVST3 (instance, originalState);
ut.expectWithinAbsoluteError (parameter->getValue(), originalValue, 0.1f);
```

It asks for the value the host last saw. Not something equivalent to it.

### Why it took ten runs to believe

The test draws a uniform random value, and the 0.1 tolerance forgives a draw
that lands near a step. So a broken build passes whenever the draw falls in
`[0, 0.1]` or `[0.9, 1]` — about one run in five, and independently per
parameter. The first run failed on parameters 30 and 38; later runs failed on
parameter 1 and nothing else.

A single green pluginval run would have been meaningless. The evidence is:

| Build | pluginval runs | Result |
|---|---|---|
| Broken (`{ 0, 1, 1 }`) | 4 | 4 failed |
| Fixed (`{ 0, 1 }`) | 10 | 10 passed |

CI therefore runs `--repeat 10 --randomise`, not a single pass.

### What was deleted

A test asserting the same fault for `AudioParameterChoice` was written, run
against the **broken** build, and passed. Choice parameters run assignment
through their range, which snaps to whole indices, so they cannot hold a stale
fraction and never had the bug. It was deleted rather than kept green — this is
the third time in this project a test has passed against code it was supposed to
catch, and the rule stands: a test that passes with the fix removed is testing
nothing.

The first fix was deleted on the same grounds. Once the range was continuous the
whole suite passed without it, so it was doing nothing but looking careful.

### The lesson worth keeping

94 tests, every DSP claim measured, every guard validated by sabotage — and a
plugin that misreported its own parameters to every host that loaded it. The
tests were all pointed at the audio. Nothing was pointed at the contract with
the host, because that contract is not observable from inside.

---


## 3q. How dirty the octaver actually is

The octaver was on the outstanding list as "aliasing is structural, unmeasured".
It is now measured, because "the ADAA discipline does not reach this pedal" is
an argument, not a number, and the fix it implies is expensive.

### The measurement

f0 is chosen so that f0/2 lands exactly on FFT bin m, which puts every
legitimate partial of the generated octave exactly on a multiple of m. Aliased
images land at (N - k*m), and 32768 is not a multiple of any m used, so they can
never fold back onto the grid. Off-grid energy is then a clean measure of what
should not be there.

Tone wide open, which is the worst case — the default 700 Hz tone low-pass takes
5 to 7 dB off every figure:

| f0 | octave | off-grid |
|---|---|---|
| 111 Hz | 56 Hz | −38.9 dB |
| 220 Hz | 110 Hz | −35.6 dB |
| 439 Hz | 220 Hz | −32.5 dB |
| 879 Hz | 439 Hz | −29.3 dB |

About 3 dB worse per octave up the neck.

### What it costs to fix

Same note, three sample rates:

| f0 | 48 kHz | 96 kHz | 192 kHz |
|---|---|---|---|
| ~220 Hz | −35.6 | −41.7 | −48.0 |
| ~439 Hz | −32.5 | −38.5 | −44.6 |

**Six decibels per doubling, consistently, in both rows.** That is the number
that decides the design. Oversampling the divider buys 6 dB a doubling, so
matching the −60 dB the drive manages from −32.5 dB would take about 32x. That
is not a reasonable price for one pedal.

Six dB per doubling is also the signature of an artefact dominated by *when* the
edge happens rather than how sharp it is: the comparator can only flip on a
sample boundary, so the square's period wobbles by up to one sample, and halving
the sample period halves that error. Which points at sub-sample edge placement —
computing the fractional zero-crossing time and offsetting the transition — as
the cheap fix, rather than brute-force oversampling. Not implemented, and not
attempted here.

### Two wrong readings on the way

Both worth recording, because both looked like findings.

1. At 879 Hz with tracking left at its 250 Hz default, off-grid measured **+33
   dB** — the artefact louder than the signal. That is not aliasing. The
   comparator was being fed a note two octaves above its tracking filter and the
   divider was not dividing. Setting tracking to suit the note turns +33 dB into
   −29.3 dB. The regression test now asserts `isTracking()` so this cannot be
   misread again.

2. The first sample-rate sweep fixed the FFT bin index rather than the
   frequency, so raising the rate raised f0 with it. At 192 kHz that put f0 at
   1758 Hz, far outside tracking range, and produced another +27 dB reading. The
   sweep now picks the bin per rate to hold the note constant.

Both are the same mistake as sections 3b and 3k: measuring something adjacent to
the thing of interest and believing the number.

### Is it good enough

Unknown. −32 to −44 dB of non-harmonic content, mostly below 700 Hz after the
tone control, under a dry bass that is usually louder — that is a listening
question, not a measurement one, and nobody has listened for it specifically.
The regression test records where the number is so a change is visible. It does
not claim the number is right.

---


## 3r. Growl garbled the sound, and it was not the divider

Reported from playing: "if I set growl too high it tends to garble sound". The
third fault found this way, and the third that no test had.

### Two wrong guesses first

The obvious suspect was section 3q's aliasing: Growl lifts the band above 60 Hz,
and the off-grid content lives there too, so Growl should be amplifying the
grit. Measured, and **no** — the off-grid ratio barely moves with Growl. At a
220 Hz note it goes from −42.78 dB to −42.83 dB across the whole control. The
hypothesis was tidy and wrong.

The second guess was that the crossover stopped summing flat. Also no.

### What it was

Level. `sub = rumble + pitch * (1 + growl * boost)` with `boost = 3`, and
nothing holding the total down:

| f0 | peak, growl 0 | peak, growl 1 |
|---|---|---|
| 111 Hz | 0.95 | 2.52 |
| 220 Hz | 1.18 | **4.52** |
| 439 Hz | 0.93 | 3.69 |

Four and a half times full scale. In the default order the octaver sits ahead of
the drive, so that arrives at the ADAA clippers thirteen decibels hot, and a
clipper handed a signal thirteen decibels hot does what it is told. The garbling
was the drive doing its job on a signal that had no business being that loud.

### A ceiling, not a level match

Holding the power exactly constant was implemented first and it is the wrong
answer, which the existing audibility test caught immediately. If Growl may only
redistribute energy, then on a note whose octave already sits mostly above 40 Hz
there is nothing to redistribute from. Measured audible gain at a 110 Hz note
fell to **−0.004 dB** — very slightly negative, breaking the one promise the
control makes, that it can only ever help. Raising `boost` did not rescue it:
at boost 6, 9 and 12 the number stayed pinned at zero, because normalising the
power afterwards cancels exactly what the boost added.

So Growl is allowed to add level and simply not allowed to add much. Below the
ceiling nothing happens at all; the control behaves as it always did.

The ceiling was chosen by measuring both requirements. Audible gain at full
Growl, against the 6 dB the low strings must get:

| ceiling | 41.2 Hz | 55 Hz | 82.4 Hz | 110 Hz |
|---|---|---|---|---|
| 1.2 (+1.6 dB) | 6.38 | 7.01 | 1.89 | 1.58 |
| **1.4 (+3 dB)** | **7.50** | **8.35** | **3.23** | **2.92** |
| 1.6 (+4 dB) | 7.50 | 9.06 | 4.37 | 3.60 |

1.2 clears the requirement by 0.38 dB, which is not a margin. 1.4 clears it by
1.5 dB, and the two lowest notes are identical at 1.4 and 1.6 — their natural
ratio is already under the ceiling, so it never engages there. **The ceiling
binds only above the low strings, which is exactly where the level was running
away and exactly where Growl was never needed.**

Peak at full Growl, before and after: 2.52 → 2.13 at 111 Hz, 4.52 → 1.80 at
220 Hz, 3.69 → 1.24 at 439 Hz. RMS rise is now 2.91 dB at every note measured,
against a +3 dB ceiling.

### A smaller thing found on the way

The old form was `rumble + pitch * (1 + growl * boost)`. The new one is
`shaped + pitch * growl * boost`. Those look equivalent and are not: a
Linkwitz-Riley crossover is **all-pass complementary**, so its two halves sum to
flat magnitude but not back to the input. The old form therefore ran the octave
through an all-pass even at Growl zero — same spectrum, 3.3 dB more crest
factor, no benefit. The comment claiming the halves "sum back to the octave
untouched" had been wrong since section 3n and nobody had checked it, because
nothing downstream of a magnitude measurement can see a phase shift.

Caught only because the growl-zero peak changed when it should not have, and the
number was worth chasing rather than waving at.

---


## 3s. A crash, open

pluginval segfaults in its **"Parameter thread safety"** test. Exit 139, no
message. It is real, it is rare, and it is not diagnosed.

| | |
|---|---|
| Rate, measured | 2 crashes in 16 local runs, plus 1 CI failure — about 12% |
| Where | always "Parameter thread safety", never anywhere else |
| Since | at least commit `7914d2f`; the code it touches predates the CI work |

What that test does is set every parameter 500 times from the message thread
while `processBlock` runs on another, through the VST3 wrapper.

### Ruled out, by checking rather than by argument

- **Allocation on the audio thread.** Audited every pedal's `process()`. None.
- **Chorus delay-line overrun**, the obvious candidate since it is the only
  modulated index into a buffer. The LFO is unipolar `[0, 1]` and depth is
  capped at the allocated headroom, so the read index cannot go negative or past
  the end.
- **The chain order.** `processBlock` takes one acquire load of a single atomic
  word and bounds-checks the nibble it decodes. A concurrent write can only
  swap one complete permutation for another.
- **Our processor on its own.** A test that hammers every parameter from a
  second thread while processing runs clean 12 times out of 12. The VST3
  wrapper is needed to provoke it, which is why that test is labelled as a smoke
  test and not as a reproduction.

### A bad inference, recorded

Disabling the two `ValueTree` listeners gave 8 clean runs out of 8, and that was
written up as having found the cause. It had not. Re-measuring the **unmodified**
code afterwards gave 0 crashes in 10. At a 12% rate, eight clean runs happens by
chance about a third of the time, so the original result carried no information
at all — the baseline was simply never measured.

The lesson is the same one as sections 3b, 3k and 3q, in a new costume: an
intermittent fault needs a control group before any change to it means anything.
Three earlier mistakes in this file were measurements of the wrong quantity.
This one was a measurement of the right quantity with no baseline, which is
worse, because it looks like evidence.

### Possibly the same thing as 3p's open item

Section 3p records a Windows-only intermittent where the chain stability test
produced a non-finite sample and never repeated. Same platform, same
"intermittent, does not reproduce, no explanation". They may well be one fault.
Recorded as a suspicion, not a finding — there is no evidence linking them
beyond both being rare and both being on Windows.

### What it would take

A stack trace, not more sampling. A 12% race cannot be bisected by running
things twice and looking at the answer, which is the mistake above. There is no
debugger on the machine this was built on, so the route is a small host harness
that loads the VST3 through `VST3PluginFormat` with JUCE's crash handler
installed to print a backtrace. Not built yet.

Until then this ships as a known defect, and CI will go red intermittently
because of it. That is the correct behaviour from CI and it should not be
papered over by lowering the strictness level or dropping the repeat count.

---


## 4. Fixing what went wrong in Wibeboard

| Wibeboard | BassRig |
|---|---|
| `CriticalSection` shared with the audio callback to swap effects | No locks on the audio thread. Atomics + `SmoothedValue` only. Pedal choice is compile-time via the registry. |
| Hard-coded stereo in/out for a mono instrument | Bus layouts: mono→mono, mono→stereo, stereo→stereo, all supported and tested. |
| Nonlinearities running at native rate — aliasing | ADAA shapers + 2× oversampling, latency reported. |
| `GenericAudioProcessorEditor`, no presets | Custom pedal face; APVTS state + factory presets from day one. |
| No tests, no plugin validation | Catch2 unit tests + pluginval in CI. |

## 5. Layout

```
BassRig/
├─ CMakeLists.txt            juce_add_plugin, CPM for deps
├─ Source/
│  ├─ PluginProcessor.*      thin: params, bypass, oversampling, hosts one Pedal
│  ├─ PluginEditor.*
│  ├─ dsp/
│  │  ├─ Pedal.h             prepare / process / reset / paramLayout / latency
│  │  ├─ PedalRegistry.*     ← placeholder for pedals 2..n
│  │  ├─ DrivePedal/         the one we actually build
│  │  └─ common/             DCBlocker, ADAA shapers, crossover, latency-matched delay
│  ├─ params/ParamIDs.h
│  └─ ui/                    LookAndFeel, Knob, Toggle, Footswitch, LED, PedalFace
├─ Assets/
└─ tests/                    null tests, latency, denormals, THD/alias sweep
```

`Pedal` is a plain abstract class, not a `juce::AudioProcessor`. One
`AudioProcessor` at the top; pedals underneath are pure DSP. That is what makes
adding pedal #2 cheap and keeps the plugin boundary in one place.

## 6. Licence: AGPLv3 (decided)

BassRig is open source under AGPLv3, matching JUCE's free licence and every
reference project above. Consequences:

- JUCE 8 free licence — no commercial licence needed, splash screen stays.
- `chowdsp_waveshapers` (GPLv3) ADAA clippers are usable directly. No need to
  reimplement the antiderivative maths.
- `chowdsp_wdf` is BSD-3, compatible either way.
- Source must be published if the plugin is distributed.

## 7. Build order

1. [x] CMake + plugin skeleton. VST3 and Standalone both build.
2. [x] Param layout + state + bus layouts (mono, mono->stereo, stereo) and
       latency reporting.
3. [x] Clean path, blend, latency-matched delay. Verified against a deliberately
       sabotaged build.
4. [x] ADAA shaper cascade from `chowdsp_waveshapers`. Aliasing measured against
       a naive build: 16-23 dB better at full drive.
5. [x] Voicing. Drive, bass and treble tapers rebuilt from measured curves;
       makeup and path levels normalised. 30 tests green. Not yet judged by ear.
6. [x] WDF Baxandall tone stack (`chowdsp_wdf`) plus two peaking mid bands.
       Flat at noon within +-0.25 dB, 40 Hz to 8 kHz. 21 tests green.
7. [x] Custom pedal face, drawn in code. Headless PNG snapshot tool for
       inspecting it; editor tests assert the controls are parameter-bound.
8. [x] Preset system: thirteen factory presets, user save/load, host program
       API, session round trip. 39 tests green.
9. [x] Pedal #2: a compressor with a sidechain-filtered key input, ahead of
       the drive. Parameters now flow through the registry. 48 tests green.
10. [x] Pedal #3: envelope filter, BassIQ-style, plus a tabbed rack so the
        enclosure stops growing with each pedal. 58 tests green.
11. [x] Chorus and phaser, both with crossovers. The phaser needed one that
        section 3j said it would not. 80 tests green.
12. [x] Reorderable chain: lock-free, drag to rearrange, carried by presets.
13. [x] Octaver (analogue-style divider) and noise gate. Seven pedals in the
        rig. 92 tests green.
14. [x] Octave audibility on the low strings: measured, diagnosed as physics
        rather than tracking, and given a Growl control. 93 tests green.
15. [x] Octave jumping up on decaying notes: squelch flicker resetting the
        divider. Three redundant guards. 94 tests green.
16. [x] CI on Linux, macOS and Windows, with pluginval at strictness 10 and
        `--repeat 10`. It found a state-restoration bug on its first run that
        no test here could see. AU added, since macOS runners make it free.
        95 tests green.

### Also outstanding

- The tone stack runs at base rate, so its treble response carries some bilinear
  frequency warping near 10 kHz. Upstream oversamples the Baxandall to avoid
  this. Unmeasured.
- The compressor is untested by ear too, and its knee (6 dB), detector (RMS)
  and gain computer (feed forward) are single fixed choices rather than the
  switchable modes chowdsp offers.
- The voicing has not been A/B'd against reference hardware or other plugins.
  It HAS been played, and two of the bugs above came from playing it rather than
  from any test, but every voicing value is still set so a control is measurably
  distinct and evenly spread, which is not the same as sounding good. The Grunt
  corners (5 / 100 / 250 Hz), the recovery low-pass at 5 kHz and the interstage
  gain of 1.8 are all still first guesses in that sense.
- The octaver generates its square by hard-switching at base rate, with no band
  limiting and no oversampling. Now measured rather than assumed: −32 to −44 dB
  of off-grid content, improving 6 dB per doubling of sample rate, so
  oversampling is a poor trade and sub-sample edge placement is the route if it
  is ever worth fixing. See section 3q. Whether it is audible is still unknown.
- The macOS and Linux builds are CI-verified but have never been run in a host
  by a person. AU passes pluginval; it has not been opened in Logic.
- Release binaries are unsigned on both Windows and macOS.
- **pluginval crashes intermittently**, about one run in eight, always in its
  parameter thread safety test. Real, measured, undiagnosed, and it will make CI
  red at random. See section 3s.
- **One unexplained failure, still open.** A Windows CI runner failed "every
  order produces finite audio" once, on the first rotation, and has not done it
  again. It does not reproduce: same commit, Visual Studio and Ninja generators
  both, full suite and filtered, repeatedly green, and Linux and macOS pass it
  every time. Three explanations were checked and none of them fit -- headroom
  is 0.07 to 0.27 against a limit of 16, so nothing was close to overflowing;
  the ADAA lookup tables cannot be read early, because destroying a future from
  std::async joins it; and every pedal reads its on/off parameter as a
  threshold, so the LosslessBool change cannot leak a fraction into the audio.
  It predates the CI work -- the test and the whole signal path are unchanged
  from the first commit -- so it is a pre-existing intermittent that having CI
  merely revealed. The test now reports the count, index, channel, block and
  class of the first non-finite sample, verified by injecting a NaN at a known
  location, so the next occurrence should arrive with enough to diagnose it.
  Recorded rather than closed, because it has not been explained.
- The drive makeup table is calibrated at one reference level, a 220 Hz tone at
  half scale. A much hotter or quieter input will not track it exactly.
- Attack is asymmetric in effect: +3.2 dB boost against -1.3 dB cut, because the
  clipper compresses the boosted case less than it expands the cut one.
- Mid-band coefficients update per block rather than per chunk, so fast
  automation of Lo-Mid or Hi-Mid can click. The Baxandall pots are smoothed and
  updated every 32 samples; the mids are not.
- Oversampling is fixed at 2x. Exposing 1x/4x/8x as a quality setting needs a
  re-prepare when it changes, since it moves the reported latency. The cascade's
  ADAA latency is one host sample only because two stages at 2x divides evenly;
  a `static_assert` in `prepare()` catches any change that breaks that, since
  the clean delay line is integer-only.
- The ADAA lookup tables are ~11 MB, shared process-wide via
  `SharedResourcePointer` rather than built per instance.
- Drive gain is capped at 20 and the shaper input clamped to +-25, because
  chowdsp's lookup tables CLAMP out-of-range inputs. Harmless for tanh, which is
  flat past |x|=10, but its antiderivatives keep growing, so an out-of-range
  input silently corrupts the ADAA maths.
- chowdsp_wdf is fetched with `SOURCE_SUBDIR` pointing at a nonexistent
  directory so its CMake never runs: v1.0.0 declares a pre-3.5 minimum that
  CMake 4 refuses, and the library is header only.
- JUCE 9.0.1 exists; pinned to 8.0.15 until chowdsp compatibility is confirmed.
