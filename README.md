# ftpd

FTP Server for 3DS/Switch/Linux.

## Features

- Appears to work well with a variety of clients.
- Supports multiple simultaneous clients. The 3DS itself only appears to support enough sockets to perform 4-5 simultaneous data transfers, so it will help if you limit your FTP client to this many parallel requests.
- Cutting-edge graphics.

## Latest Builds

CIA: https://mtheall.com/~mtheall/ftpd-3ds.cia

3DSX: https://mtheall.com/~mtheall/ftpd-3ds.3dsx

NRO: https://mtheall.com/~mtheall/ftpd-nx.nro

CIA QR Code

![ftpd-3ds.cia](https://github.com/mtheall/ftpd/raw/master/ftpd_qr.png)

## Build and install

You must set up the [development environment](https://devkitpro.org/wiki/Getting_Started).

### 3DSX

The following pacman packages are required to build `ftpd-3ds.3dsx`:

    3dstools
    devkitARM
    libctru

They are available as part of the `3ds-dev` meta-package.

Build `ftpd-3ds.3dsx`:

    make 3dsx

### NRO

The following pacman packages are required to build `ftpd-nx.nro`:

    devkitA64
    libnx
    switch-tools

They are available as part of the `switch-dev` meta-package.

Build `ftpd-nx.nro`:

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
