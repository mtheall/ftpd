FTP - Graphic ModifierX Edition
=======

ftpd is originally created by mtheall. This fork is soley for aesthetic modifications and CFW/Flashcart builds.

Custom Graphics
---------------
Modify the .png files in the `gfx`folder to add your own graphics.

**app_banner:** 
this image will appear on the top screen before you run the application (.3ds and .cia)

**app_bottom:** 
this is the static in-app image on the bottom screen

**app_icon:** 
this is the icon for the .cia, .3ds, and .3dsx

Features
--------
- Appears to work well with a variety of clients.
- Also compiles for Linux.
- Supports multiple simultaneous clients. The 3DS itself only appears to support enough sockets to perform 4-5 simultaneous data transfers, so it will help if you limit your FTP client to this many parallel requests.
- *Your own* cutting-edge graphics.

Before building
---------------
**Update: 1-27-16**

1) install and set up [devkitARM and libctru](http://3dbrew.org/wiki/Setting_up_Development_Environment)

2) install the latest [ctrulib](https://github.com/smealum/ctrulib/tree/master/libctru)
*Note: devKitPro updater may not have the necessary files*

3) install [sf2dlib](https://github.com/xerpi/sf2dlib)

4) install [sfillib](https://github.com/xerpi/sfillib)

5) install [portlibs](https://github.com/devkitPro/3ds_portlibs)

**pre-compiled portlibs:** [download here](http://filebin.ca/2UjEzj4BslHV/portlibs.zip) *and put the* `portlibs` *folder in your* `devKitPro` *folder*

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

Troubleshooting
---------------

    error: 'NI_MAXHOST'
    error: 'NI_MAXSERV'
    error: 'sdmc_dir_t'
You do not have an updated ctrulib

    ../arm-none-eabi/bin/ld.exe: cannot find -lsfil
    ...
    collect2.exe: error: ld returned 1 exit status
    
You do not have portlibs installed

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

Planned Commands (ftpd)
-----------------------

- STOU
