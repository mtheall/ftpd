[ftp] - Graphic ModifierX Edition
=======

ftbrony is originally created by mtheall. This fork is soley for aesthetic modifications and CFW/Flashcart builds.

Custom Graphics
---------------
Modify the .png files in the `gfx` to add your own graphics.
**app_banner:** this image will appear on the top screen before you run the application (.3ds and .cia)
**app_bottom:** this is the static in-app image on the bottom screen
**app_icon:** this is the icon for the .cia, .3ds, and .3dsx

*the default folder is the original [ftp] theme; feel free to delete it*

Features
--------
- Appears to work well with a variety of clients.
- Also compiles for Linux.
- Supports multiple simultaneous clients. The 3DS itself only appears to support enough sockets to perform 4-5 simultaneous data transfers, so it will help if you limit your FTP client to this many parallel requests.
- Cutting-edge graphics.

Before building
---------------

You must first install and set up [devkitARM and libctru](http://3dbrew.org/wiki/Setting_up_Development_Environment).
You must also install sf2dlib (https://gbatemp.net/threads/release-beta-sf2dlib-simple-and-fast-2d-library-using-the-gpu.384796/)

How to build
------------
1) Download the .zip for this repo
2) extract
3) while holding *left shift* on your keyboard, right click FTP-GMX-master and hit `Open command window here`
4) run the following command

    make
5) You will have new files created in the folder

**Homebrew:**
Copy the `FTP-GMX-2.2.3dsx` and `FTP-GMX-2.2.smdh` to a folder named `FTP-GMX-2.2`. Copy this folder to the `3ds` folder on your SD card and launch it via homebrew.

**CFW:**
Copy `FTP-GMX-2.2.cia` to your SD card and install it with a CIA installer.

**Flashcarts:**
Copy `FTP-GMX-2.2.3ds` to your SD card.


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
