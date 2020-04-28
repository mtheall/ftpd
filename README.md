# ftpd

FTP Server for 3DS/Switch/Linux.

## Features

- Appears to work well with a variety of clients.
- Supports multiple simultaneous clients. The 3DS itself only appears to support enough sockets to perform 4-5 simultaneous data transfers, so it will help if you limit your FTP client to this many parallel requests.
- Cutting-edge graphics.

- Toggle backlight on 3DS with SELECT button
- Toggle backlight on Switch with MINUS button

## Latest Builds
NDS: https://mtheall.com/~mtheall/ftpd.nds

CIA: https://mtheall.com/~mtheall/ftpd.cia

3DSX: https://mtheall.com/~mtheall/ftpd.3dsx

NRO: https://mtheall.com/~mtheall/ftpd.nro

CIA QR Code

![ftpd.cia](https://github.com/mtheall/ftpd/raw/feature/v3.0.0/ftpd-qr.png)

## Classic Builds

CIA: https://mtheall.com/~mtheall/ftpd-classic.cia

3DSX: https://mtheall.com/~mtheall/ftpd-classic.3dsx

NRO: https://mtheall.com/~mtheall/ftpd-classic.nro

CIA QR Code

![ftpd-classic.cia](https://github.com/mtheall/ftpd/raw/feature/v3.0.0/ftpd-classic-qr.png)

## Build and install

You must set up the [development environment](https://devkitpro.org/wiki/Getting_Started).

### NDS

The following pacman packages are required to build `nds/ftpd.nds`:

	devkitARM
	dswifi
	libfat-nds
	libnds

They are available as part of the `nds-dev` meta-package.

### 3DSX

The following pacman packages are required to build `3ds/ftpd.3dsx`:

    3dstools
    devkitARM
    libctru

They are available as part of the `3ds-dev` meta-package.

Build `3ds/ftpd.3dsx`:

    make 3dsx

### NRO

The following pacman packages are required to build `switch/ftpd.nro`:

    devkitA64
    libnx
    switch-tools

They are available as part of the `switch-dev` meta-package.

Build `switch/ftpd.nro`:

    make nro

## Supported Commands

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
- MLSD
- MLST
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
- SITE
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

## Planned Commands

- STOU

## SITE commands

- Show help:    SITE HELP
- Set username: SITE USER <NAME>
- Set password: SITE PASS <PASS>
- Set port:     SITE PORT <PORT>
- Set getMTime: SITE MTIME [0|1]
- Save config:  SITE SAVE
