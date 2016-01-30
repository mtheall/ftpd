ftpd
====

FTP Server for 3DS.

Features
--------
- Appears to work well with a variety of clients.
- Also compiles for Linux.
- Supports multiple simultaneous clients. The 3DS itself only appears to support enough sockets to perform 4-5 simultaneous data transfers, so it will help if you limit your FTP client to this many parallel requests.

Build and install
------------------

You must first install and set up [devkitARM and libctru](http://3dbrew.org/wiki/Setting_up_Development_Environment).
Clone this repository and cd in the resulting directory.

    $ git submodule update --init
    $ make

The result binaries (`ftpd.elf`, `ftpd.3dsx/ftpd.smdh`, `ftpd.3ds` and `ftpd.cia`) can be found in `output` folder, including a ready to distribute `ftpd.zip` file.
Copy them to your SD card and launch it.

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
- MDTM
- MKD
- MODE (no-op)
- NLST
- NOOP
- OPTS
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
- XCWD
- XMKD
- XPWD
- XRMD

Planned Commands
----------------

- STOU

Credits
-------

- Code by [mtheall](https://github.com/mtheall/ftpd).
- Buildtools by [Steveice10](https://github.com/Steveice10/buildtools).
- Folder icon (used in `banner.png`) made by [Sergio Calcara](https://thenounproject.com/term/folder/1249/), modified by [m45t3r](https://github.com/m45t3r).
