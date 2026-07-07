# A little info about the files in this archive folder

## Background

This whole repo was my first go at creating a firmware for the Simple Touch using C++. For the last few years I've mainly used Plugdata with HVCC, every now and then taking a look at some other firmwares made with C++, tweaking a small detail perhaps, like inserting a new scale.

I do not have the knowledge to write the full C++ code myself, but after a few years of building firmwares for Simple Touch I do feel like I've grown accustomed to thinking about user interface and user experience (UI, UX).

## The goal

Hence I set out to create a firmware based on open source Plaits. I knew it contains both melodic as well as drum sounds and wanted to build something on top of just being able to play the models with the original input from the MI module.

## Tools

To start the project I wrote a short prompt describing what sources I wanted to use and what the goal was.

I used VSCode together with the Claude extension (I'm paying for the basic Pro plan, which for my rather slow work tempo is sufficient).

## References

I prepared myself by looking at an overview of the hardware controls and a few examples: [TouchBass](https://github.com/Synthux-Academy/TouchBass) by Synthux (Vlad / aka Bleeptools) and the way [sk-engines](https://github.com/shakfu/sk-engines) (shakfu) used Plaits to build a firmware called [**mosc**](https://github.com/shakfu/sk-engines/blob/0.5.1/docs/engines/mosc.md).

## Process

I then fed this [initial prompt plan](Initial-Prompt-plan.md) together with the brief [hardware-compatibility.md file](hardware-compatibility.md) into Claude.

It returned with a thorough [plan](plan_archive.md). The plan in turn put forth a bunch of design choices, which I started answering, and then I set Claude to work building the environment.

From here on it became a back and forth of testing, feedback, rewriting, etc. I started using a separate notes.md file to keep track of thoughts and ideas as I went along.

When I had a fairly well functioning firmware, I archived the plan and created a roadmap + notes approach.

## Timeline — from the first prompt to v1 stable

A condensed view of how the project actually unfolded, tied to the git history:

1. **Research & planning** (`e095ef5` initial commit → `28c6693` "research phase" → `d43036a` "Complete planning phase"). The [initial prompt](Initial-Prompt-plan.md) plus [hardware-compatibility.md](hardware-compatibility.md) went in; the [plan](plan_archive.md) came out — phased tasks with explicit **VERIFY decision gates** (hardware mapping, block-size mismatch, model navigation, the playmode structure) that I answered before code was written.
2. **First working firmware** (`664f5ca`, with libDaisy pointed at the Synthux fork in `82a5d11`): the Plaits voice port, the touch interface, and a first drum sequencer running on the Simple Touch.
3. **Docs restructure** (`f59b385`): the plan was archived into this folder and the **roadmap + notes** split was created — ROADMAP.md for what's next, notes.md as the running design record.
4. **The numbered-steps era** (early July 2026, `1b304f6` + `8207a09` and daily iteration): Steps 1–15, now archived verbatim in [roadmap_v1_archive.md](roadmap_v1_archive.md). Highlights in order: SW2 mode restructure and the three playmodes, per-slot params + the recording redesign (hold/confirm/cancel/copy), soft-clip output, seq knob remap, staged P0+P2 randomize with per-mode memory, the background drum seq + P2+P11 transport, the knob-pickup system, voice expansion 4 → 6 (voice sleep + load-shed guard, budget analysis in notes.md), and MIDI — phase 1 notes/CC on TRS + USB, then clock + transport. Alongside the steps grew the drum pattern system: genre pattern folders, `gen_patterns.py` codegen, and the browser pattern editor with live preview.
5. **v1 stable** (2026-07-08): the completed steps moved here, ROADMAP.md became forward-looking only (single owner of future work), and the first post-v1 item — reverb/delay as a per-slot FX send — was analyzed and parked with a full resource budget in notes.md.

The working method stayed the same throughout: prompt → plan → verify the design decisions myself → let Claude implement → test on hardware → feed the findings back as the next round of notes.

## What's in this folder

In this folder you'll find the original prompt, the plan, and the completed v1 roadmap steps ([roadmap_v1_archive.md](roadmap_v1_archive.md)). The full extent of iteration — me reading through the code, suggesting, learning, adjusting, looking things up, etc. — is much more than what's here, but if anyone does land here, it might give you an idea of how I was able to make this with a mainly prompt-driven LLM approach.

[drumpatterns.h](drumpatterns/patternsdrums.h) is a separate research attempt at finding ways to build patterns into the auto sequencer; its ideas were integrated into [synth/sequencer.h](../synth/sequencer.h). The question it was exploring — how to author patterns by hand — has since been solved properly: patterns now live as one file per pattern in genre folders under [synth/patterns/](../synth/patterns/), a Python codegen ([tools/gen_patterns.py](../tools/gen_patterns.py), run on every `make`) embeds them into the firmware, and the browser-based [pattern editor](../tools/pattern_editor.html) (with live audio preview) is where new patterns get made — including the Anthony Rother-style electro bank this file was originally chasing.