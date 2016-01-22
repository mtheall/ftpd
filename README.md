ftp -Generic McBrandX edition
=======

ftbrony is originally created by mtheall. This fork is soley for aesthetic modifications and CFW/Flashcart builds.

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

Homebrew:
Copy the `ftbrony.3dsx` and `ftbrony.smdh` to a folder named `ftbrony`. Copy this folder to the `3ds` folder on your SD card and launch it via homebrew.

CFW:
Copy `ftbrony.cia` to your SD card and install it with a CIA installer.

Flashcarts:
Copy `ftbrony.3ds` to your SD card.


Supported Commands
------------------

- ABOR
- ALLO (no-op)
- APPE
- CDUP
- CWD
- DELE
- FEAT
- HELP
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
- RNTO
- STAT
- STOR
- STRU (no-op)
- SYST
- TYPE (no-op)
- USER (no-op)
- XCUP
- XMKD
- XPWD
- XRMD
