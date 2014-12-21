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

You must first install and set up [devkitARM](http://devkitpro.org/).
Clone this repository and cd in the resulting directory.

    make 3ds

Create a **ftbrony** (double check that it is splelt **exactly** like this) directory inside the 3ds directory on your SD card and copy the following files in it:
- ftbrony.3ds
- ftbrony.png
- ftbrony.smdh

Supported Commands
------------------

- CDUP
- CWD
- DELE
- FEAT (no-op)
- LIST
- MKD
- MODE (no-op)
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
- APPE
- NLST
- REST
- STOU
