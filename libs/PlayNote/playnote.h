/* *****************************************************************************
*  PlayNote: a minimal Vircon32 note-playing library.       File version: 1.0  *
*  --------------------------------------------------------------------------- *
*  Plays arbitrary frequencies from a single-cycle waveform sample using the   *
*  SPU's native per-channel playback-speed control (set_channel_speed) -      *
*  entirely in hardware once a note is started, with no per-frame envelope/   *
*  LFO/arpeggiator processing the way a full synth engine needs. The only     *
*  per-frame work this library does is a short linear volume ramp at the      *
*  start and end of each note, just to avoid the audible click a hard,        *
*  instant stop mid-waveform can cause - not a real ADSR envelope.            *
*                                                                              *
*  This trades away everything a full synth gives you (vibrato, tremolo,      *
*  arpeggios, per-instrument envelopes, a sequencer) for a much smaller       *
*  per-frame cost: playing/stopping N simultaneous notes here costs a         *
*  handful of hardware register writes per note-start/note-stop and, while a *
*  fade is in progress, one more per fading voice per frame - nothing at all *
*  for a note that's just sustaining. Good fit for short, percussive,        *
*  chiptune-style blips; not a replacement for an actual instrument/music    *
*  synth with real envelopes, effects, or a sequencer.                       *
*                                                                              *
*  Requires one embedded sound asset: a single-cycle waveform (any shape -    *
*  sine, saw, square, etc.) of exactly WavePeriodSamples samples, at          *
*  44100Hz, mono-or-stereo 16-bit PCM (see sounds/wt_saw.wav, vendored right  *
*  alongside this header, for a ready-made one - or bring your own; see this *
*  library's README.md for how). playnote_init() configures that asset to    *
*  loop over its full length, so any note started against it plays          *
*  continuously until stopped.                                               *
*                                                                              *
*  Usage:                                                                     *
*    playnote_init( waveSoundId, 2048 );  // sound_id, samples per cycle      *
*    ...                                                                      *
*    int voice = playnote_start( 440.0, 0.25 );  // A440 at 25% volume        *
*    ...                                                                      *
*    playnote_stop( voice );   // begins fading out; actually stops once      *
*                               // the fade finishes, in a later              *
*                               // playnote_update() call                     *
*    ...                                                                      *
*    playnote_update();  // call exactly once per frame, always              *
*                                                                              *
*  See README.md (in this same folder) for the full API reference.           *
***************************************************************************** */

// *****************************************************************************
    // start include guard
    #ifndef PLAYNOTE_H
    #define PLAYNOTE_H

    #include "audio.h"
// *****************************************************************************


// How many frames a note's volume takes to ramp in (on start) or out (on
// stop) - the only thing standing in for an envelope here, so both are
// kept short (this library is meant for snappy blips, not sustained
// musical notes) but not zero: a same-frame jump straight to/from
// silence is exactly the kind of waveform discontinuity that causes an
// audible click. Both set to the minimum (1 frame) for the snappiest
// possible attack and release. Worth knowing if a click ever becomes
// audible on note-stop specifically: fade-out is the harder of the two
// to keep click-free, since a stop can land at any point in the
// waveform's cycle (including near a peak), whereas a start always
// begins at sample 0 (a known, consistent point - typically near zero
// amplitude for a well-formed loop) - so PLAYNOTE_FADE_OUT_FRAMES is the
// one to raise first if that turns up, back toward 2 or higher.
#define PLAYNOTE_FADE_IN_FRAMES 0
#define PLAYNOTE_FADE_OUT_FRAMES 0

// one per hardware SPU channel (see sound_channels in audio.h)
#define PLAYNOTE_MAX_VOICES 16

struct PlayNoteVoice {
  bool active;
  bool fadingOut;
  int fadeFramesLeft;
  float currentVolume;
  float fadeStep;
};

PlayNoteVoice[PLAYNOTE_MAX_VOICES] playnote_voices;
int playnote_sound_id;
int playnote_wave_period;

// Mirror PLAYNOTE_FADE_IN_FRAMES/PLAYNOTE_FADE_OUT_FRAMES into plain
// variables, set once in playnote_init() below. This isn't just style:
// this compiler statically rejects any expression that divides by a
// literal-zero constant (a #define included), even one guarded by a
// runtime `if` that would prevent it from ever actually executing - so
// `volume / PLAYNOTE_FADE_IN_FRAMES` directly would fail to *compile* at
// all if that macro were set to 0, regardless of the surrounding
// if/else below. Dividing by these variables instead sidesteps that
// compile-time check while behaving identically at runtime for any
// value - the two macros stay the actual tunable constants (edit those,
// not these), this is purely a compile-time workaround.
int playnote_fade_in_frames;
int playnote_fade_out_frames;

// Call once at startup. sound_id is the cartridge sound asset holding the
// single-cycle waveform; wave_period_samples is its length in samples
// (2048 for sounds/wt_saw.wav, the wavetable vendored alongside this
// header - see this library's README.md if you swap in a different one).
// Configures that sound asset to loop over its full length, so any note
// played against it sustains until explicitly stopped rather than
// playing through once and falling silent.
void playnote_init(int sound_id, int wave_period_samples) {
  playnote_sound_id = sound_id;
  playnote_wave_period = wave_period_samples;
  playnote_fade_in_frames = PLAYNOTE_FADE_IN_FRAMES;
  playnote_fade_out_frames = PLAYNOTE_FADE_OUT_FRAMES;

  select_sound(sound_id);
  set_sound_loop(true);
  set_sound_loop_start(0);
  set_sound_loop_end(wave_period_samples);

  for (int i = 0; i < PLAYNOTE_MAX_VOICES; i++) {
    playnote_voices[i].active = false;
  }
}

// Starts playing freq_hz (at the given 0..1 volume) on the first free SPU
// channel, fading in over PLAYNOTE_FADE_IN_FRAMES frames (or jumping
// straight to full volume with no ramp at all, if that's set to 0).
// Returns the channel index used (pass this to playnote_stop() later),
// or -1 if every channel is already busy.
int playnote_start(float freq_hz, float volume) {
  int channel = play_sound(playnote_sound_id);
  if (channel < 0) {
    return -1;
  }

  // A wave_period-sample cycle played at native speed (1.0) takes
  // wave_period/44100 seconds, i.e. sounds at 44100/wave_period Hz - so
  // reaching freq_hz needs speed = freq_hz / (44100/wave_period) =
  // freq_hz * wave_period / 44100.
  float speed = freq_hz * playnote_wave_period / 44100.0;
  select_channel(channel);
  set_channel_speed(speed);

  playnote_voices[channel].active = true;
  playnote_voices[channel].fadingOut = false;

  if (playnote_fade_in_frames > 0) {
    set_channel_volume(0);
    playnote_voices[channel].fadeFramesLeft = playnote_fade_in_frames;
    playnote_voices[channel].currentVolume = 0;
    playnote_voices[channel].fadeStep = volume / playnote_fade_in_frames;
  } else {
    // No fade-in configured - jump straight to full volume. Vircon32
    // hard-crashes on a division by zero (confirmed elsewhere in this
    // project), so this path exists specifically to make
    // PLAYNOTE_FADE_IN_FRAMES=0 a supported "as instant as physically
    // possible" setting rather than a crash - accepting the higher click
    // risk of a same-frame jump from silence in exchange for it.
    set_channel_volume(volume);
    playnote_voices[channel].fadeFramesLeft = 0;
    playnote_voices[channel].currentVolume = volume;
    playnote_voices[channel].fadeStep = 0;
  }

  return channel;
}

// Begins fading out the given voice. It keeps sounding (fading) for up
// to PLAYNOTE_FADE_OUT_FRAMES more playnote_update() calls, then actually
// stops (or stops immediately, with no fade at all, if that's set to 0).
// Safe to call on an already-inactive or out-of-range channel (does
// nothing).
void playnote_stop(int channel) {
  if (channel < 0 || channel >= PLAYNOTE_MAX_VOICES) {
    return;
  }
  if (!playnote_voices[channel].active || playnote_voices[channel].fadingOut) {
    return;
  }

  playnote_voices[channel].fadingOut = true;

  if (playnote_fade_out_frames > 0) {
    playnote_voices[channel].fadeFramesLeft = playnote_fade_out_frames;
    playnote_voices[channel].fadeStep = playnote_voices[channel].currentVolume / playnote_fade_out_frames;
  } else {
    // No fade-out configured - same reasoning as playnote_start() above:
    // stop immediately rather than dividing by zero, making this a
    // supported setting instead of a crash.
    select_channel(channel);
    stop_channel(channel);
    playnote_voices[channel].active = false;
    playnote_voices[channel].fadeFramesLeft = 0;
  }
}

// Immediately silences and stops every voice, with no fade - use this
// for a hard mute (e.g. the player turning sound off entirely), not for
// ending individual notes during normal play (use playnote_stop() for
// that, to avoid a click).
void playnote_stop_all() {
  for (int i = 0; i < PLAYNOTE_MAX_VOICES; i++) {
    if (playnote_voices[i].active) {
      stop_channel(i);
      playnote_voices[i].active = false;
    }
  }
}

// Call exactly once per frame, regardless of how many notes are
// currently playing. Advances any in-progress fade in/out; a voice
// that isn't fading (the common case: sustaining at a fixed volume, or
// simply silent) costs nothing here beyond the loop check itself.
void playnote_update() {
  for (int i = 0; i < PLAYNOTE_MAX_VOICES; i++) {
    if (!playnote_voices[i].active || playnote_voices[i].fadeFramesLeft <= 0) {
      continue;
    }

    if (playnote_voices[i].fadingOut) {
      playnote_voices[i].currentVolume -= playnote_voices[i].fadeStep;
    } else {
      playnote_voices[i].currentVolume += playnote_voices[i].fadeStep;
    }
    playnote_voices[i].fadeFramesLeft--;

    select_channel(i);
    set_channel_volume(playnote_voices[i].currentVolume);

    if (playnote_voices[i].fadingOut && playnote_voices[i].fadeFramesLeft == 0) {
      stop_channel(i);
      playnote_voices[i].active = false;
    }
  }
}


// *****************************************************************************
    // end include guard
    #endif
// *****************************************************************************
