Red Hydra
=========

A Virtual Boy emulator for the Nintendo 3DS based on [r3Ddragon](https://github.com/mrdanielps/r3Ddragon) by mrdanielps,
which is itself based on Reality Boy / Red Dragon.

## Features
* All officially licensed games are playable at full speed, even on the original 3DS
* 3D support
* Game saves are supported
* Map either the A/B buttons or the right D-Pad to the face buttons, with the other being on the touch screen
* Configurable face button mapping
* Configurable color filter

Future additions:
* Support for PCM samples
* Savestates
* Homebrew support
* More versatile color filter
* A way to load a specific game from the home screen

## Usage

The latest release can be found [here](https://github.com/skyfloogle/r3Ddragon/releases),
provided as a 3dsx (can be loaded with Homebrew Launcher) and as a cia (can be installed to the home menu with FBI).
ROMs can be placed in any desired location on your SD card. The emulator will remember the location of the last ROM you loaded.

## FAQs

> Why would you make a Virtual Boy emulator? Nobody asked for it.

The 3DS is the perfect system to faithfully emulate the Virtual Boy. They have similar screen resolutions, the
 3D effect is better and it's actually portable.

> OK, but wasn't the Virtual Boy, like, the worst console ever?

There were many reasons why it was commercial failure. That doesn't mean the console is bad, or the games
aren't worth playing. It's definitely received way more hate than it deserved.

Plus, it has a nice homebrew scene with gems such as Hyper Fighting, Snatcher and many more.

> Do I need a new 3DS to run this?

Unfortunately, yes. The old 3DS is too slow to run it at a playable speed. That might change in the future,
but it's unlikely.

> Why did it take so long for an emulator with good compatibility to come out?

Making a fast emulator is no easy task. Simply porting an existing emulator wouldn't have worked, as none of the
existing emulators were designed to run on systems as slow as the 3DS.

> Where can I download it?

You can find the latest release [here](https://github.com/mrdanielps/r3Ddragon/releases).

## Building

Before building, fetch the dependencies:

```bash
> git submodule update
```

Once you have [ctrulib installed](http://3dbrew.org/wiki/Setting_up_Development_Environment), you can choose
between four different make targets:

* **`make release`** is the default, and adds `-O3` to CFLAGS.
* **`make testing`** adds `-O3` to CFLAGS. It will output basic debug info to a connected debugger.
* **`make debug`** adds `-g -O0` to CFLAGS. It builds without optimizations so it can be debugged more easily.
* **`make slowdebug`** adds `-g -O0` to CFLAGS. It will output a lot of debug information, which will slow emulation down but might be helpful to debug game-specific issues.

## License

Some of the code is distributed under the MIT License (check source files for that) but, since
this is a port of Reality Boy, here is (part of) the original readme:

```
This Reality Boy emulator is copyright (C) David Tucker 1997-2008, all rights
reserved.   You may use this code as long as you make no money from the use of
this code and you acknowledge the original author (Me).  I reserve the right to
dictate who can use this code and how (Just so you don't do something stupid
with it).
Most Importantly, this code is swap ware.  If you use It send along your new
program (with code) or some other interesting tidbits you wrote, that I might be
interested in.
This code is in beta, there are bugs!  I am not responsible for any damage
done to your computer, reputation, ego, dog, or family life due to the use of
this code.  All source is provided as is, I make no guaranties, and am not
responsible for anything you do with the code (legal or otherwise).
Virtual Boy is a trademark of Nintendo, and V810 is a trademark of NEC.  I am
in no way affiliated with either party and all information contained hear was
found freely through public domain sources.

Acknowledgments:
----------------

Frostgiant, Parasyte, and DogP (and the rest of people that have contributed
to the VB sceen in the last five years) - Their work on Red_Dragon has been a
real inspiration. Its amazing how far they have gone with so little to start
with.

Bob VanderClay - most of the original code is based off
of his VB disassembler.

Ben Haynor - Provided me with a much better understanding of
the VB internals.

Joseph LoCicero, Dave Shadoff - I stole the jump table ideas from their tg16
disassembler, thanks guys.

Neill Corlett - took many ideas (and some code)
from his Starscream CPU core

Kevin Banks - for donating a very nice pair of Frenzle 3D
viewers, and being an all around great guy.

Megan Tucker - For putting up with my tinkering all night, and resisting the
urge to toss all my video games out the window.

v810 is a trademark of NEC co.
Virtual Boy is a trade mark of Nintendo
Reality Boy is in no way affiliated with either of these parties
```

### Credits

* Everyone mentioned in the license. Without Reality Boy and Red Dragon it wouldn't have been possible.
* smealum and contributors - ctrulib.
* Vappy, Team Fail, HtheB, hippy dave and kane159 on GBAtemp - early testing.
* benhoyt - inih.
* Myria - libkhax
* thunderstruck - CIA banner sound (taken from Fishbone).
* nop90 - Reality Boy backports and fixes.
* danielps - Initial 3DS port and V810 dynarec.
* Floogle - 3DS hardware renderer; many optimizations, bugfixes, and other improvements.
