#include "disk.h"

#include <ctype.h>
#include <sys/statvfs.h>

static void strbufAppendMountPoint(FFstrbuf* mountpoint, const char* source)
{
    while(*source != '\0' && !isspace(*source))
    {
        //Backslash is encoded as \134
        if(strncmp(source, "\\134", 4) == 0)
        {
            ffStrbufAppendC(mountpoint, '\\');
            source += 4;
            continue;
        }

        //Space is encoded as \040
        if(strncmp(source, "\\040", 4) == 0)
        {
            ffStrbufAppendC(mountpoint, ' ');
            source += 4;
            continue;
        }

        //Tab is encoded as \011
        if(strncmp(source, "\\011", 4) == 0)
        {
            ffStrbufAppendC(mountpoint, '\t');
            source += 4;
            continue;
        }

        ffStrbufAppendC(mountpoint, *source);
        ++source;
    }
}

void ffDetectDisksImpl(FFDiskResult* disks)
{
    FILE* mountsFile = fopen("/proc/mounts", "r");
    if(mountsFile == NULL)
    {
        ffStrbufAppendS(&disks->error, "fopen(\"/proc/mounts\", \"r\") == NULL");
        return;
    }

    char* line = NULL;
    size_t len = 0;

    while(getline(&line, &len, mountsFile) != EOF)
    {
        //Format of the file: "<device> <mountpoint> <filesystem> <options> ..." (Same as fstab)
        char* currentPos = line;

        //Non pseudo filesystems have their device in /dev/
        //DrvFs is a filesystem plugin to WSL that was designed to support interop between WSL and the Windows filesystem.
        if(strncmp(currentPos, "/dev/", 5) != 0 && strncmp(currentPos, "drvfs", 5) != 0)
            continue;

        //Skip /dev/ or drvfs
        currentPos += 5;

        //Don't show loop file systems
        if(strncasecmp(currentPos, "loop", 4) == 0)
            continue;

        FFDisk* disk = ffListAdd(&disks->disks);

        //Go to mountpoint
        while(!isspace(*currentPos) && *currentPos != '\0')
            ++currentPos;
        while(isspace(*currentPos))
            ++currentPos;

        ffStrbufInitA(&disk->mountpoint, 16);
        strbufAppendMountPoint(&disk->mountpoint, currentPos);

        //Go to filesystem
        currentPos += disk->mountpoint.length;
        while(isspace(*currentPos))
            ++currentPos;

        ffStrbufInitA(&disk->filesystem, 16);
        ffStrbufAppendSUntilC(&disk->filesystem, currentPos, ' ');

        //Go to options, detect type
        currentPos += disk->filesystem.length;
        while(isspace(*currentPos))
            ++currentPos;

        #ifdef __ANDROID__
        if(ffStrbufEqualS(&disk->mountpoint, "/") || ffStrbufEqualS(&disk->mountpoint, "/storage/emulated"))
            disk->type = FF_DISK_TYPE_REGULAR;
        else if(ffStrbufStartsWithS(&disk->mountpoint, "/mnt/media_rw/"))
            disk->type = FF_DISK_TYPE_EXTERNAL;
        else
            disk->type = FF_DISK_TYPE_HIDDEN;
        #else
        if(strstr(currentPos, "nosuid") != NULL || strstr(currentPos, "nodev") != NULL)
            disk->type = FF_DISK_TYPE_EXTERNAL;
        else if(ffStrbufStartsWithS(&disk->mountpoint, "/boot") || ffStrbufStartsWithS(&disk->mountpoint, "/efi"))
            disk->type = FF_DISK_TYPE_HIDDEN;
        else
            disk->type = FF_DISK_TYPE_REGULAR;
        #endif

        //Detects stats
        #ifdef __USE_LARGEFILE64
            struct statvfs64 fs;
            if(statvfs64(disk->mountpoint.chars, &fs) != 0)
                memset(&fs, 0, sizeof(struct statvfs64)); //Set all values to 0, so our values get initialized to 0 too
        #else
            struct statvfs fs;
            if(statvfs(disk->mountpoint.chars, &fs) != 0)
                memset(&fs, 0, sizeof(struct statvfs)); //Set all values to 0, so our values get initialized to 0 too
        #endif

        disk->bytesTotal = fs.f_blocks * fs.f_frsize;
        disk->bytesUsed = disk->bytesTotal - (fs.f_bavail * fs.f_frsize);

        disk->filesTotal = (uint32_t) fs.f_files;
        disk->filesUsed = (uint32_t) (disk->filesTotal - fs.f_ffree);

        ffStrbufInit(&disk->name); //TODO: implement this
    }

    if(line != NULL)
        free(line);

    fclose(mountsFile);
}
