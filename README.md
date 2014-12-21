=======================
Arnold RV Driver
=======================

This is a driver for sending Arnold AOVs to RV.

It uses boost asio for communicating with RV over sockets.

-----------------------
Disclaimer
-----------------------

At the time that I was writing this code (a year ago?) we discovered a crippling memory leak in RV
pertaining to its protocol for sending buffers over sockets.  We alerted Tweak, but it seemed there
was no progress on the issue. Hence, the unfinished state of the code. Perhaps it is fixed now?

Let me know if it works for you without issue, because I would love to actually use this in production.

All I ask is that if you do improve it, please fork and commit your changes back to github.


Chad Dombrova

-----------------------
Updates
-----------------------

First, I'd like to thank Chad and LumaPictures for sharing the source code of their original code.

I'm glad to say that the leak Chad was talking about was fixed in late 2013 after I took some time to talk with tweaks software support and provide them with data to re-produce the issues. Several other issues related to the improvements I have made to the driver were also fixed on the go.

Since then, we've been using it in production.

Here are the modifications I've brought to the orignal implementation:
- removed runtime dependency to boost libraries. (That said, I have left the code using boost)
- added my own utility libraries gnet, gcore as submodules in replacement of boost.
- build using SCons with the help of excons (pulled as a submodule).
  The new driver target is 'rvdriver' (default). The old boost based driver target is 'driver_rv'.
- support compiling with arnold 4.1.
- when using a fixed media name, stack consecutive renders as frames in the media.
- added option to suffix timestamp to media name in case one may not want renders to be stacked as frames.
- re-order AOVs so that RGBA always comes first.
- added a few color correction parameter:
color_correction: one of None, sRGB, Rec709, Gamma 2.2, Gamma 2.4, Custom Gamma, LUT or OCIO
gamma: Used when mode is Custom Gamma. It can also be controlled by setting the ARNOLD_RV_DRIVER_GAMMA environment variable.
lut: Used when mode is LUT. It can also be controlled by setting the ARNOLD_RV_DRIVER_LUT environment variable.
ocio_profile: Use when mode is OCIO. It can also be controlled by the OCIO environment variable.

Note: To switch between OCIO and any other color correction mode, RV has to be re-started 
(the driver will start rv with the approriate command line when necessary)
- respond to RV ping signal
- new RV package to start maya render/ipr (including region) from RV
Note: Works using the maya command port, so you need have one open. I also added a simple script to open one if necessary. 
Also Maya RenderView will popup but I couldn't find a way to avoid that (it actually goes as far as to make maya crash in some cases).

Gaetan Guidet
