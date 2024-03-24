Red Viper
=========

A Virtual Boy emulator for the Nintendo 3DS continuing mrdanielps's work on [r3Ddragon](https://github.com/mrdanielps/r3Ddragon),
which is itself based on Reality Boy / Red Dragon. It uses a dynamic recompiler with busywait detection and a
hardware-accelerated renderer to achieve high performance on the 3DS's limited hardware.

## Features
* All officially licensed games are playable at full speed, even on the original 3DS
* 3D support
* Game saves are supported
* Map either the A/B buttons or the right D-Pad to the face buttons, with the other being on the touch screen
* New 3DS C-Stick is also supported
* Configurable face button mapping
* Configurable color filter

Future additions:
* Savestates
* Homebrew support
* Circle Pad Pro support
* More versatile color filter
* A forwarder to allow loading a specific game from the home menu

## Usage

[A modded 3DS](https://3ds.hacks.guide/) is needed. Once that's sorted, you can install it using [Universal Updater](https://universal-team.net/projects/universal-updater.html).

<details>
  <summary>You can also scan this QR code with FBI.</summary>

![image](https://github.com/skyfloogle/red-viper/assets/18466542/31fc852b-c701-4710-b849-fbf1d7dc29b8)
</details>

Alternatively, the latest release can be manually downloaded [here](https://github.com/skyfloogle/red-viper/releases),
provided as a 3DSX (can be loaded with Homebrew Launcher) and as a CIA (can be installed to the home menu with FBI).
ROMs can be placed in any desired location on your SD card. The emulator will remember the location of the last ROM you loaded.

## FAQ

> It doesn't work! / What's this DSP firmware error?

Try updating your Luma3DS installation (I tested with v13.0.2). Once it's updated, you may need to [dump your DSP firmware](https://3ds.hacks.guide/finalizing-setup#section-iii---rtc-and-dsp-setup).

> The depth slider is weird.

The Virtual Boy wasn't designed with a depth slider, so games can't have their depth scaled in the way 3DS games can. The alternative is to
shift the entire image backwards or forwards, similar to how the Virtual Boy's IPD slider works.
The default setting (3DS mode) starts at a "neutral" level and pushes the image further back as you move the slider up, keeping the metaphor fairly similar
to how most 3DS games work. Alternatively, "Virtual Boy IPD" mode can be enabled, which unlocks the full range, from closest to furthest.

To use the "default" level, use 3DS mode and keep the depth slider on, but near the bottom.

> What do the numbers in the performance info mean?

This displays the time taken for **A**ll processing in one frame, **D**RC processing (CPU emulation),
**C**itro3D processing (CPU graphics), and **P**ICA200 processing (GPU graphics). It also displays the
**M**emory usage of the DRC cache (increases over time then resets).

As this is a developer option at heart, frametimes are displayed in milliseconds rather than FPS, as the former is much easier to reason about in this context.
The target is usually 20ms, though some games only draw every other frame, so rendering has more leeway there.

## Building

After setting up [devkitPro](http://3dbrew.org/wiki/Setting_up_Development_Environment), install the
additional dependencies:
```bash
> pacman -S 3ds-zlib
```

After cloning the repository, fetch the last dependencies:
```bash
> git submodule update
```

Once that's all sorted, you can choose between four different make targets:

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
* djedditt - Enhanced 3D depth slider support.
