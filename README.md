ftpd
====

FTP Server for 3DS.

Features
--------

- Appears to work well with a variety of clients.
- Also compiles for Linux.
- Supports multiple simultaneous clients. The 3DS itself only appears to support enough sockets to perform 4-5 simultaneous data transfers, so it will help if you limit your FTP client to this many parallel requests.
- Cutting-edge graphics.

Latest Builds
-------------

CIA: https://mtheall.com/~mtheall/ftpd.cia

3DSX: https://mtheall.com/~mtheall/ftpd.3dsx

CIA QR Code

![ftpd.cia](https://github.com/mtheall/ftpd/raw/master/ftpd_qr.png)

Build and install
------------------

You must first install and set up [devkitARM and libctru](http://3dbrew.org/wiki/Setting_up_Development_Environment). You also need bannertool.exe in the root if you want to compile de cia version.
Clone this repository and cd in the resulting directory.

    make

Copy the `ftpd.3dsx` file to your SD card and launch it.

Run make cia if you want a cia compiled version.

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
- SIZE
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
