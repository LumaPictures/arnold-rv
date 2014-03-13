Arnold RV Driver
================

This is a driver for rendering Arnold AOV tiles to RV.

It uses boost asio to communicate with RV over sockets.


RV Version Warning
------------------

RV versions prior to 4.0.10 suffer from a crippling memory leak related to the
use of the PIXELTILE socket command for sending pixel data. While the driver
will still work with these older versions, it is not particularly practical, as
RV will allocate multiple GB of RAM for a single HD image.
