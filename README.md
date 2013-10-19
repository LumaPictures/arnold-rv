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


