Initial Prompt / plan:
Create a plan to research and set this up in a new md file that we can follow and tick off subtasks.
Bias toward small, compartmentalized specs.
Make me verify key decisions explicitly so nothing is missed.

I want to make a cpp libDaisy version of Plaits by MI for the Synthux Simple Touch
There have been previous versions of Plaits running on The Simple Touch which is a Daisy Seed on a PCB with pots and the mpr121 touch enabling breakout.

It's a platform easily allowing to install new firmwares.
Hence I want to make a Plaits version.

In the Hardware comparison file you can find the possible combinations of inputs.
Our Simple Touch has no additional CV control, so everything happens in the box, and optionally via usb midi (even TRS midi for anyone that has modded their Touch)

In the core we'll start with as close to a 1 to 1 clone, mapping the 4 main knobs and using the Touchpads as note inputs.
Additioanlly we can implement multiple scales onto the 7 pads P3 - P9 seven notes and an option to switch octaves.
Pads P10 and P11 as well as P0 and P2 can be used as settings;
We can use two toggle switches for 6 additional settings.

difficulties and remarks we need to figure out: 
- what hardware to map where
- which method will we use to go through the 16 + 8 new models 
- 4 main knobs would seem obvious to link to the top row of knobs (s31 - s34),
- 2 knobs and two faders can allow additional controls fro e.g. envelope, scale selection, model selection, etc.  
- there's only one LED, making it hard to do complex info, we can use a few modes of blinks to confirm.
- e.g. how I've used it in the past: using pad combo's that would make a pad engage modulator mode, pressing P0 and then I used  P8 and P9 as a down and up value changer (e.g. allowing 6 values of noise mix), the modulator patch disabled the note playing, when a value change was done the LED blinks, when the min and max values are reached it blinks a longer sequence

We also have an audio input  that is unused we could just route it through, or come up with an additional plan t make it interact with the models, making our Plaits version have a unique twist.

We can learn from a few resources:
M.I.
- There's the actual mutable instruments code: https://github.com/pichenettes/eurorack/tree/master/plaits
- main docs: https://pichenettes.github.io/mutable-instruments-documentation/modules/plaits/
- docs > manual https://pichenettes.github.io/mutable-instruments-documentation/modules/plaits/manual/

Synthux:
TouchBass: example repository using libDaisy, where the Simple Touch is already nicely setup: 
!This is our base template example
https://github.com/Synthux-Academy/TouchBass

Mosc
Someone recently was making Plaits clone onto another Daisy device from Synthux, the Spotykach: they used the mosc engine to put plaits onto that hardware,  it also uses similar hardware, albeit with a lot more tools like leds etc, so it's not suited for direct copying. But maybe we can learn from the DSP method? https://github.com/shakfu/sk-engines/tree/main/src/engine/mosc
https://github.com/shakfu/sk-engines/blob/0.5.1/docs/engines/mosc.md
