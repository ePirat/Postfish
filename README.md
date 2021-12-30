# The Postfish

This README file covers the 2005-01-01 pre-release of the Postfish by
Xiph.Org.

## What is the Postfish?

The Postfish is a digital audio post-processing, restoration,
filtering and mixdown tool.  It works as a linear audio filter, much
like a rack of analog effects.  The first stage of the filter
pipeline provides a bank of configurable per-channel processing
filters for up to 32 input channels.  The second stage provides
mixdown of the processed input audio into a group of up to eight
output channels.  The third stage applies processing filters to the
output group post-mixdown.

The Postfish is a stream filter; feed it audio from a list of files
or input stream, and it renders audio to standard out, as well as
optionally providing a configurable audio playback monitor via a
sound device.  If the input audio is being taken from files,
Postfish also provides simple forward/back/cue seeking and A-B
looping control.  The next major update of Postfish will also
include automation to allow mixdown settings to be 'recorded' and
applied automatically during rendering.


## What is the Postfish for?

The Postfish intends to include exactly and only the most useful
basic filters needed to produce a good mixdown from audio recorded
'in the field'.  The filter set also comprises the fundamentals
needed for master mixdown in a small studio.  It is not an editor;
for that reason, it is intended to be used with an audio editor such
as Audacity.

If, for example, you've just multi-tracked a rehearsal of your
troupe's current rock opera and the Director then appears out of
nowhere (they always do) and says "Have a mix ready for my review by
morning", the Postfish is all you need.

Or, as another example, you've recorded for a band who'd like to put
out a CD of the live performance...  All the band FX are already in
the multi-track, so the Postfish plus Audacity is all you need.

In a studio, tracks usually get recorded dry, so there's generally
multiple mixdown stages of adding effects.  Postfish (obviously)
does not give you a large array of instrument or situation-specific
effects and it never will.  What it does give you is the effects
necessary to take the tracks from earlier mixing and produce preview
mixes and final masters.  Of course, if you already have $100k of
analog rack... you likely won't be using Postfish.  If you don't
have that rack already, Postfish means you won't need it.


## What effects does the Postfish include?

### Declipper

The Postfish declipper is a 'build audio from scratch'
reconstruction filter. Any section of audio exceeding a configured
amplitude threshold is marked 'lost' and the filter builds new
audio to fill the gap. In this way it can be used to repair both
digital clipping that occurred during sampling, as well as analog
clipping that may have happened at an earlier stage.

### Single-band compander

A single-band compander is used to compress, limit, expand, or gate
an input signal, thus providing basic dynamic range manipulation
abilities.  

The Postfish single-band compander provides independent over and
under threshold controls for each channel, each providing expansion,
compression, attack, release, lookahead and soft-knee configuration
for three independent over, mid and under tracking filters.  Each
filter may also be configured to track by peak, or track by RMS
energy.

### Multi band compander

The Postfish multi-band compander is similar to the single-band
compander above, and provides all of the same controls with an
addition: each over/under threshold is configurable by full-octave,
half-octave or third-octave bands, for up to 30 bands of independent
companding for each of 32 input channels and the group of eight
outputs.

The multi-band compander includes a global 'mid' compand slider,
like the single-band compander.  This slider acquires a new use in
multi-band mode however, where it can flatten or expand the dynamic
range of an entire channel (or the entire recording) without
any artifacts.

### Equalization

30 band -60/+30dB 1/3-octave beat-less EQ per input channel and full
output group.

### Deverberator

Live recordings have a tendency to end up with too much reverb,
especially when one is forced to use ambient miking.  The
deverberator dries out overly 'wet' live signals.  Also good for
taking unwanted room echo out of speech recordings.

### Reverberator

...for adding reverb to signals that are too 'dry', especially to
even out apparent stage depth when mixing close miked signals (like
vocals) to an ambient-miked signal.

The Postfish provides a stereo reverb per input channel and a mono
reverb per output post-mix.

### Limiting

Simple, old-fashioned causal output hard-limiter to avoid unexpected
digital clipping on the output.  Configurable by threshold, knee
depth and release speed.

### Mixdown

Postfish provides both a master attenuation and delay panel (which
places these basic sliders for all channels on one window) as well
as per-input mixdown that allows each input channel to be multiply
routed to one or all of eight output through an additional cascade
of four additional independent attenuation/delay/invert units per
input, as well also allowing each input to be mixed through a
'crossplacer'.

The crossplacer is used to place any input into a stereo [or
greater] image by altering not only the relative attenuation across
channels (the 'cross attenuate' control), but also by adjusting
phase and delay across channels (the 'cross delay' control).  The
A-B slider then controls how far the input apparently images toward
the A output bank or the B output bank.  

The direct mixdown blocks allow additional manual control over the
input->output matrix; the direct mixdown blocks run in parallel with
the crossplacer and all may be active simultaneously.

Of course, with eight output channels, one can begin
imaging/mastering for more than just stereo...

## Don't we already have several free apps that do this sort of thing?

The short/wrong answer is maybe. The complicated answer is no.

- I needed a specific set of filters
- I needed them in one place working together 'out of the box' 
- I needed to instantly hear changes I made to settings as I made them
- I needed to be able to absolutely trust the filters 
- I needed it all to be convenient to use

Given my specific requirements, nothing else came close to filling
the niche and I didn't want to cobble together a 90% solution out of
multiple other apps when this functionality was the very core of
what I needed for mixdown.

Both the Postfish UI and the filter functionality are intended to be
the most usable day-to-day set, rather than sporting the maximum
number of buttons in the smallest space, pack in more features than
the next app, or sport the slickest 'skin'.

Postfish is the way it is because I need it, and I use it for the
core basics of mixing that I absolutely cannot afford to screw up.
Some filters (like the declipper and deverberator) are unique.  Even
among those that aren't, Postfish deliberately sets speed/quality
tradeoff much higher than most existing apps.

The multiband compander is a case in point; other free apps
implement this effect. To my knowledge, all use the simplest/fastest
method, operating directly on the FFT of a windowed block.  An
FFT-based multicompander is fast, but the aliasing and frequency
multiplication artifacts (you eventually end up multiplying the
input by the transfer function of the window shape; most noticeably,
they tend to cause odd pitch changes in raw vocals) render them
unsuitable for professional-quality work.

## What does the Postfish require?

- Linux 2.x (ports come later) with OSS or ALSA OSS emulation
- Libraries: FFTW3, pthreads, Gtk2, libao-dev
- Gcc and gmake
- A sound card or external USB/Firewire A/D/A
- A video card, preferrably one with fast AGP
- Alot of CPU.  Really.  As much as you can throw at it.  Dual Xeon 3GHZ?  
  Yup, you can use all of it.

Seriously, this is a very CPU hungry app because of the
aforementioned speed/quality tradeoff.  I can do simple mixdown of 8
channels to stereo with a few effects in realtime on my G3-400, but
the machine is crying.  My dual Athlon 2600 keeps up much better,
but it's still possible to overwhelm it by lighting up all the
effects and feeding it enough input channels.  The declipper,
especially, eats CPU on heavily damaged audio.  The multiband
compander is runner up in 'absurd levels of CPU usage'.  If you
don't have fast AGP video, the EQ panel alone will probably kill
your machine.

Postfish, BTW, is designed to scale to multiple CPUs.  A dual
Athlon/Pentium/UltraSparc/PowerPC runs Postfish much better than a
single processor.

## Why is this a pre-release?

Because it's not finished.  A few things are more obvious than others:

1. A number of panels are still unimplemented.  These are the only
   inactive features on the UI, but they're right on the main panel.

2. Postfish *should* be a JACK-able app, but isn't.  That too should
   be done for a real release.

3. Although the whole thing is designed as a rendering engine
   wrapped up in a neat async-safe library that's then used by an
   asynchronous UI, the source isn't entirely arranged that way.  It
   should be.  It will be for final release.

4. This code is just now seeing light of day.  It is probably *full*
   of simple bugs.  I'm confident in the audio pipeline itself (I've
   hammered on it mercilessly) but there's certainly many UI
   interconnection bugs left to find.

5. The automation robot isn't there yet.  Right now a user has to
   frob knobs in realtime to make settings adjustments during a
   render.  That's not really acceptible.  All teh setttings changes
   must be preprogrammable; they will be.

6. Everyone knows a release requires documentation.  There is no
   documentation.

## There's no documentation!?

Not yet; good documentation requires effort and time.  

That said, if you read the list of effects, knew what they were, and
knew basically how to use them, you should be able to pick up the
Postfish and do useful work in a few minutes of playing around.

I took care to establish and follow conventions in the UI: If you're
using the shipped postfish-gtkrc theme, square blue buttons turn
things on.  Triangular blue buttons pop configuration windows.  

(If you're running a nonstandard system-wide Gtk2 'theme' that skins
a set of custom widget renderers into Gtk, well, I'll let you deal
with figuring the convention out as I use a number of custom widgets
subclassed from 'stock' Gtk and so you'll see a mixmash of classic
Gtk and your skin.  Sorry, no way around it other than to purpousely
defeat your 'theme' which you probably won't like either.)

The grid of buttons on the right in the 'channel' frame are the
effects for the input channels.  The square blue buttons turn a
specific effect on for a specific channel.  A triangle pops the
configuration for that effect.  Mixing controls are labelled
"Atten/Mix" in the lowest row in the "channel" panel.

Further right is the "master" panel; these controls work the same
way for effects on the output channels after mixdown.

Finally, postfish -h will tell you how to get audio in and reroute
audio out.

Only two things are probably impossible to figure out just from
an afternoon of playing around:

1. When Postfish starts for the first time, all input channels are
   mixed into the center of a two channel output.  If, for example,
   the input is already two stereo channels and the output should be
   the same, the proper mixdown configuration is to go to the
   mixdown panels for channel 1 and channel 2 and then set input
   channel 1 to mix only into output channel 1 and input channel 2
   to only output channel 2.

2. Turning "Atten/Mix" off for any channel will mute it completely
   in all input effects, not just in the mixdown (this obviously
   doesn't affect output, but it also silences all the VU meters for
   that input channel, and that can be disconcerting if you don't
   expect it).

## How do I get, build and install it?

Postfish is in Xiph.Org's Subversion repository.  Get the source using:

    $ git clone https://gitlab.xiph.org/xiph/postfish.git

Edit the Makefile to select the proper 'ADD_DEF' line.  LinuxPPC
users want the first, almost everyone else wants the second.  It
should be self explanatory given text in the Makefile.

    $ cd postfish
    $ make

and as root,

    # make install

Happy hacking (and mixing),

Monty
TD, Xiph.Org
