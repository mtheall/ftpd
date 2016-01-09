#include "fsfile.h"
#ifdef _3DS
#include <string.h>

int fsfile_inited = 0;
FS_Archive sdmcArchive = {0x00000009, (FS_Path){PATH_EMPTY, 1, (u8*)""}};

void FSFILE_Init() {
    FSUSER_OpenArchive(&sdmcArchive);
    fsfile_inited = 1;
}

void FSFILE_Exit() {
    fsfile_inited = 0;
    FSUSER_CloseArchive(&sdmcArchive);
}

FSFILE* FSFILE_Fopen(const char *path, const char *mode) {
    FSFILE *f = NULL;
    u32 openFlags = 0;
    if(fsfile_inited == 0) {
        return f;
    }
    //mode just support r and w
    if(strlen(mode) <= 0) {
        return f;
    }

    if(mode[0] == 'r') {
        openFlags = FS_OPEN_READ;
    } else if(mode[0] == 'w') {
        openFlags = FS_OPEN_WRITE|FS_OPEN_CREATE; //no trunc flag?
    } else {
        return f;
    }
    //open file only, not support dir
    f = (FSFILE *)malloc(sizeof(FSFILE));
    if(f == NULL) {
        return f;
    }
    int ret = FSUSER_OpenFile(&(f->fileHandle), sdmcArchive, fsMakePath(PATH_ASCII, path), openFlags, 0);
    if(ret != 0) {
        free(f);
        return NULL;
    }

    f->offset = 0;
    f->fsErrno = 0;
    return f;

}

int FSFILE_Fread(FSFILE *f, void *buf, size_t count) {
    u32 readSize = 0;
    if(f == NULL) return -1;
    int ret = FSFILE_Read(f->fileHandle, (u32*)&readSize, f->offset, (u32*)buf, count);
    if(ret != 0 || readSize == 0) {
        //should set errno?
        return -1;
    }

    f->offset += readSize;
    return readSize;
}

int FSFILE_Fwrite(FSFILE *f, void *buf, size_t count) {
    u32 writeSize = 0;
    if(f == NULL) return -1;

    //flags: FS_WRITE_FLUSH, FS_WRITE_UPDATE_TIME
    //       strange flag 0x10001 in ftpony?
    int ret = FSFILE_Write(f->fileHandle, (u32*)&writeSize, f->offset, (u32*)buf, count, 0);
    if(ret != 0 || writeSize == 0) {
        return -1;
    }
    f->offset += writeSize;
    return writeSize;
}

int FSFILE_Fflush(FSFILE *f) {
    if(f == NULL) return -1;
    FSFILE_Flush(f->fileHandle);
    return 0;
}

int FSFILE_Fclose(FSFILE *f) {
    if(f == NULL) return -1;
    FSFILE_Flush(f->fileHandle);
    FSFILE_Close(f->fileHandle);
    free(f);
    return 0;
}

s64 FSFILE_Fsize(FSFILE *f) {
    u64 size = 0;
    if(f == NULL) return -1;
    if(FSFILE_GetSize(f->fileHandle, &size) != 0) {
        return -1;
    }
    return size;
}

#else
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>


void FSFILE_Init() {}
void FSFILE_Exit() {}

FSFILE* FSFILE_Fopen(const char *path, const char *mode) {
    FSFILE *f = NULL;
    int openFlags = 0;
    //mode just support r and w
    if(strlen(mode) <= 0) {
        return f;
    }

    if(mode[0] == 'r') {
        openFlags = O_RDONLY;
    } else if(mode[0] == 'w') {
        openFlags = O_WRONLY|O_CREAT|O_TRUNC;
    } else {
        return f;
    }
    f = (FSFILE *)malloc(sizeof(FSFILE));
    if(f == NULL) {
        return f;
    }
    f->fd = open(path, openFlags, 0644);
    if(f->fd < 0) {
        free(f);
        return NULL;
    }
    return f;
}

int FSFILE_Fread(FSFILE *f, void *buf, size_t count) {
    if(f == NULL) return -1;
    return read(f->fd, buf, count);
}

int FSFILE_Fwrite(FSFILE *f, void *buf, size_t count) {
    if(f == NULL) return -1;
    return write(f->fd, buf, count);
}

int FSFILE_Fflush(FSFILE *f) {
    sync();
    return 0; 
}

int FSFILE_Fclose(FSFILE *f){
    if(f == NULL) return -1;
    int ret = close(f->fd);
    free(f);
    return ret;
}

s64 FSFILE_Fsize(FSFILE *f) {
    int rc;
    struct stat st;
    if(f == NULL) return -1;
    rc = fstat(f->fd, &st);
    if(rc != 0) {
        return -1;
    }
    return st.st_size;
}

#endif