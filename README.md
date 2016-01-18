ftbrony
=======

FTP Server for 3DS.

Features
--------
- Appears to work well with a variety of clients.
- Also compiles for Linux.
- Supports multiple simultaneous clients. The 3DS itself only appears to support enough sockets to perform 4-5 simultaneous data transfers, so it will help if you limit your FTP client to this many parallel requests.
- Cutting-edge graphics.

Build and install
------------------

You must first install and set up [devkitARM and libctru](http://3dbrew.org/wiki/Setting_up_Development_Environment).
Clone this repository and cd in the resulting directory.

    make

Copy the `ftbrony.3dsx` file to your SD card and launch it.

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
- REST
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
- STOU
