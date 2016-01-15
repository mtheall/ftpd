ftbrony
=======

FTP Server for 3DS.

Features
--------
- Appears to work well with a variety of clients.
- Also compiles for Linux.
- Supports multiple simultaneous clients.
- Cutting-edge graphics.

Build and install
------------------

You must first install and set up [devkitARM and libctru](http://3dbrew.org/wiki/Setting_up_Development_Environment).
Clone this repository and cd in the resulting directory.

    make

Create a **ftbrony** (double check that it is spelt **exactly** like this) directory inside the 3ds directory on the root of your SD card and copy the following files in it:
- ftbrony.3dsx
- ftbrony.smdh


Supported Commands
------------------

- APPE
- CDUP
- CWD
- DELE
- FEAT (no-op)
- LIST
- MKD
- MODE (no-op)
- NLST
- NOOP
- PASS (no-op)
- PASV
- PORT
- PWD
- QUIT
- RETR
- RMD
- RNFR
- RNTO (rename syscall is broken?)
- STOR
- STRU (no-op)
- SYST
- TYPE (no-op)
- USER (no-op)
- XCUP
- XMKD
- XPWD
- XRMD

Planned Commands
----------------

- ALLO
- REST
- STOU
