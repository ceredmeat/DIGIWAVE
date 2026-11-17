# DIGIWAVE 0.4 (Digitakt MKI, OS 1.54)

A wavetable machine in the PPG manner. Pick it with **FUNC + SRC** (machine menu) on an audio track.
The Digitakt's own filter, amp envelope, LFO and FX work on it as on a sample.

Each track gets two wavetable oscillators and a sub. A table is 16 waves of 128 8-bit samples (the
PPG's own size and depth). 26 tables: 22 made fresh in the PPG manner, and 4 (JX1-JX4) from the 64
shapes in the Jomox Alphabase's wave file.

| Table | | Table | |
|---|---|---|---|
| PWM | saw into narrowing pulse | FOLD | sine folded harder |
| BILD | sine, harmonics added one by one | PD | phase-distortion resonance |
| ODD | square built up | COMB | metallic comb |
| RSAW / RSQR | resonance sweeping a saw / square | GRIT | morphing random spectra |
| VOX | vowels A E I O U | STAI | sine with fewer and fewer steps |
| SYNC | hard-sync sweep | TSS | triangle, saw, square |
| PART | a single partial walking up | HOLW | saw losing its even harmonics |
| ORGN | drawbars pulled out | BRIT | dark to bright |
| FM1 / FM2 / FM3 | FM index sweeps, ratio 1 / 2 / 3 | RECT | sine, half- and full-wave rectified |
| BELL | bell partials growing | JX1-JX4 | Jomox Alphabase shapes |

## Page 1: WAVE

| Knob | |
|---|---|
| TUNE | pitch, as on a sample |
| TABL | the wavetable. Its picture is the table in 3D |
| WAVE | the place in the table (wave 1-16). Its picture is the wave it plays |
| ENV | how far a decay envelope sweeps the place at each trig, -64 to +63 |
| DEC | the envelope's decay, 5 ms to 10 s |
| STEP | 0 sweeps smoothly between waves, 127 jumps in hard PPG steps |
| BITS | OFF, or crush to 12 down to 3 bits |
| LEV | level |

## Page 2: OSC2 (press SRC again)

| Knob | |
|---|---|
| TBL2 | osc 2's table: SAME as osc 1, or its own |
| OFS | osc 2's place, offset from osc 1's |
| TUN2 | osc 2's tune, -24 to +24 semitones |
| DET | osc 2's detune, 0-50 cents |
| MIX | osc 1 to osc 2 |
| SUB | a square one octave below osc 1 |
| WAVE | the same WAVE as on page 1, here so you can sweep the table while you watch it |
| VIEW | a 3D picture of osc 1's (OSC1) or osc 2's (OSC2) table, filling the WAVE and VIEW spots, with the wave it plays in front and both oscillators' places marked along the bottom |

All knobs lock, take the LFO, and save with the sound.

## How much it can play

Like the PPG, each oscillator steps through its 128 points with no smoothing in between, and only
crossfades two neighbouring waves. A track costs nothing once its AMP envelope has faded out, an
oscillator MIX leaves out is not worked out, and SUB at 0 costs nothing. On a unit with all the other
flexmods loaded, six DIGIWAVE tracks playing at once run smoothly; seven start to lag.

## Install

Load `digiwave-0.4.elemod` in Elekloader with **core** and **machine-pages** (1.5 or newer), build
your OS and flash it. No `.syx` is shared here: it would contain Elektron's firmware.

## Notes

Tables are made by `tools/gen_digiwave.py` (it needs the Alphabase's `ABWV0300.wav` for JX1-JX4, not
included). Machine id 33. Page 2's values use sound slots
46-52, like every machine-pages machine with a second page, so on a DIGIWAVE track don't use FX
Locks or Deja Vu's RND destinations.

Licence: GPL-2.0-or-later (see LICENSE). By Cere, made with Claude.
