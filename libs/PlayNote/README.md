# PlayNote

A minimal note-playing library for the [Vircon32](https://www.vircon32.com/)
fantasy console.

Plays arbitrary frequencies from a single-cycle waveform sample using the
SPU's native per-channel playback-speed control (`set_channel_speed`) -
entirely in hardware once a note is started, with no per-frame envelope, LFO,
or arpeggiator processing the way a full synth engine needs. The only
per-frame work this library does is a short linear volume ramp at the start
and end of each note, just to avoid the audible click a hard, instant stop
mid-waveform can cause - not a real ADSR envelope.

This trades away everything a full synth engine gives you (vibrato, tremolo,
arpeggios, per-instrument envelopes, a step sequencer) for a much smaller
per-frame cost: playing or stopping *N* simultaneous notes costs a handful of
hardware register writes at note-start/note-stop, and one more per *fading*
voice per frame while a fade is in progress - nothing at all for a note
that's just sustaining. It's a good fit for short, percussive, chiptune-style
blips and simple melodies; it is not a replacement for an actual
instrument/music synth with real envelopes, effects, or a sequencer.

## Requirements

One embedded sound asset: a single-cycle waveform (any shape - sine, saw,
square, etc.) at 44100Hz, mono or stereo, 16-bit PCM. `sounds/wt_saw.wav` is
included as a ready-made one (a single cycle of a sawtooth wave, 2048
samples), sourced from the wavetable set in VirconSynthesizer, a separate
Vircon32 synth library this port doesn't otherwise depend on or include
any code from - only this one wav file was used. To use a
different waveform, generate or record your own
single-cycle wave, convert it to a Vircon32 sound asset with `wav2vircon`,
and pass its own sample count to `playnote_init()`.

## Setup

Add the wavetable as a cartridge sound asset (see your `rom.xml`), then:

```c
#include "libs/PlayNote/playnote.h"

void main() {
    // sound_id = the cartridge sound id of your wavetable asset,
    // wave_period_samples = its length in samples (2048 for
    // sounds/wt_saw.wav)
    playnote_init( 0, 2048 );

    while( true ) {
        // ... your game's normal per-frame logic ...

        playnote_update();  // call exactly once per frame, always
        end_frame();
    }
}
```

## Playing notes

```c
int voice = playnote_start( 440.0, 0.25 );  // A440 (concert A), 25% volume
...
playnote_stop( voice );   // begins fading out over PLAYNOTE_FADE_OUT_FRAMES
                           // frames; the voice actually stops once that
                           // fade finishes, in a later playnote_update()
```

`playnote_start()` returns which SPU channel (`0..15`) it used - hold onto
that value if you'll want to stop this specific note later. If every channel
is already busy, it returns `-1` and plays nothing.

To hard-mute everything at once (e.g. the player toggling sound off
entirely), use `playnote_stop_all()` instead of calling `playnote_stop()` on
every active voice - it skips the fade and stops immediately.

## API reference

### `void playnote_init( int sound_id, int wave_period_samples )`
Call once at startup, before playing any notes. `sound_id` is the cartridge
sound asset holding your single-cycle waveform; `wave_period_samples` is its
length in samples. Configures that sound asset to loop over its full length,
so a note started against it sustains until explicitly stopped rather than
playing through once and falling silent.

### `int playnote_start( float freq_hz, float volume )`
Starts playing `freq_hz` at `volume` (`0..1`) on the first free SPU channel,
fading in over `PLAYNOTE_FADE_IN_FRAMES` frames. Returns the channel used
(pass it to `playnote_stop()` later), or `-1` if every channel is already
busy.

### `void playnote_stop( int channel )`
Begins fading out the given voice. It keeps sounding, fading, for up to
`PLAYNOTE_FADE_OUT_FRAMES` more `playnote_update()` calls, then actually
stops. Safe to call on an already-inactive or out-of-range channel (does
nothing).

### `void playnote_stop_all()`
Immediately silences and stops every voice, with no fade. Use this for a
hard mute; use `playnote_stop()` for ending individual notes during normal
play, so they fade out cleanly instead of clicking.

### `void playnote_update()`
Call exactly once per frame, regardless of how many notes are currently
playing. Advances any in-progress fade in/out. A voice that isn't fading
(the common case - sustaining at a fixed volume, or simply silent) costs
nothing here beyond the loop check itself.

## Constants

- `PLAYNOTE_FADE_IN_FRAMES` / `PLAYNOTE_FADE_OUT_FRAMES` (both default `0`)
  - how many frames a note's volume takes to ramp in (on start) or out (on
  stop). This is the only thing standing in for an envelope here, so both
  default to the minimum - this library is meant for snappy blips, not
  sustained musical notes - but neither is zero: a same-frame jump
  straight to/from silence is exactly the kind of waveform discontinuity
  that causes an audible click. Fade-out is the harder of the two to keep
  click-free: a stop can land at any arbitrary point in the waveform's
  cycle, including near a peak, while a start always begins at sample 0
  (a known, consistent point - typically near zero amplitude for a
  well-formed loop). If a click ever becomes audible specifically on
  note-stop, `PLAYNOTE_FADE_OUT_FRAMES` is the one to raise first.
  Either can also be set to `0` for a same-frame jump straight to/from
  full volume - the highest possible click risk, but as instant as this
  library can make it. (Vircon32 hard-crashes at compile time on any
  expression that divides by a literal-zero constant, even one guarded
  by a runtime check that would prevent it from ever executing - setting
  either of these to `0` is handled safely regardless, so this is a
  genuinely supported value, not just something that happens not to
  crash.)
- `PLAYNOTE_MAX_VOICES` (default `16`) - matches Vircon32's SPU channel
  count (`sound_channels` in `audio.h`). Shouldn't normally need changing.

## How the pitch math works

A `wave_period_samples`-long cycle played at native speed (`1.0`) takes
`wave_period_samples / 44100` seconds, i.e. it sounds at
`44100 / wave_period_samples` Hz. To reach a target `freq_hz`:

```
speed = freq_hz * wave_period_samples / 44100
```

`playnote_start()` computes this automatically from the `wave_period_samples`
you passed to `playnote_init()`.

## License

MIT License

Copyright (c) 2026

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to
deal in the Software without restriction, including without limitation the
rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
sell copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
IN THE SOFTWARE.
