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

I prepared myself by looking at an overview of the hardware controls and a few examples: TouchBass by Synthux (Vlad / aka Bleeptools) and the way sk-engines used Plaits to build a firmware called **mosc**.

## Process

I then fed this [initial prompt plan](Initial-Prompt-plan.md) together with the brief [hardware-compatibility.md file](hardware-compatibility.md) into Claude.

It returned with a thorough [plan](plan_archive.md). The plan in turn put forth a bunch of design choices, which I started answering, and then I set Claude to work building the environment.

From here on it became a back and forth of testing, feedback, rewriting, etc. I started using a separate notes.md file to keep track of thoughts and ideas as I went along.

When I had a fairly well functioning firmware, I archived the plan and created a roadmap + notes approach.

## What's in this folder

In this folder you'll find the original prompt and the plan. The full extent of iteration — me reading through the code, suggesting, learning, adjusting, looking things up, etc. — is much more than what's here, but if anyone does land here, it might give you an idea of how I was able to make this with a mainly prompt-driven LLM approach.

[drumpatterns.h](drumpatterns/patternsdrums.h) is a seperate research attempt at finding ways to build patterns into the auto sequencer, my next step will be to first build a few manual patterns, as the electro which I wanted to base on Anthony Rother type of style is still not fully to my liking. It has been integrated into [synth/sequencer.h](../synth/sequencer.h)