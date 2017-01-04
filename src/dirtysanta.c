/* **********************************************************************
* Copyright (C) 2016 "me2151"						*
*									*
*	This program is free software: you can redistribute it and/or	*
*	modify it under the terms of the GNU General Public License as	*
*	published by the Free Software Foundation, either version 3 of	*
*	the License, or (at your option) any later version.		*
*									*
*	This program is distributed in the hope that it will be useful,	*
*	but WITHOUT ANY WARRANTY; without even the implied warranty of	*
*	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the	*
*	GNU General Public License for more details.			*
*									*
*	You should have received a copy of the GNU General Public	*
*	License along with this program.  If not, see			*
*	<http://www.gnu.org/licenses/>.					*
************************************************************************/

/* **********************************************************************
* full original source not publically accessible, only author contact	*
* is via XDA Developers:						*
* https://forum.xda-developers.com/v20/development/ls997vs995h910-dirtysanta-bootloader-t3519410 *
************************************************************************/


#include <android/log.h>
#include <stdio.h>
#include <string.h>

#define LOGV(...) { __android_log_print(ANDROID_LOG_INFO, "dirtysanta", __VA_ARGS__); }

int main ()
{
    char command[100];
    char cmd2[100];
    char cmd3[100];

    LOGV("Starting Backup");
    strcpy( cmd3, "dd if=/dev/block/sde1 of=/storage/emulated/0/bootbackup.img");
    system(cmd3);

    strcpy( cmd2, "dd if=/dev/block/sde6 of=/storage/emulated/0/abootbackup.img");
    system(cmd2);

    LOGV("Backup Complete.");
    sleep(5);

    LOGV("Starting flash of Aboot!");
    strcpy( command, "dd if=/storage/emulated/0/aboot.img of=/dev/block/sde6" );
    system(command);

    LOGV("Finished. Please run Step 2 now.");
    sleep(999999);
    return(0);
}
