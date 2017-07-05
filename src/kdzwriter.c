/* **********************************************************************
* Copyright (C) 2017 Elliott Mitchell					*
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
*************************************************************************
*$Id$			*
************************************************************************/


#include <stdio.h>
#include <getopt.h>

#include "kdz.h"
#include "md5.h"


int verbose=0;


int main(int argc, char **argv)
{
	struct kdz_file *kdz;
	int ret=0;
	int opt;
	enum {
		READ	=0x40,
		WRITE	=0x80,
		RW_MASK	=0xF0,
		REPORT	=READ|0x1,
		TEST	=READ|0x2,
		SYSTEM	=WRITE|0x1,
		MODEM	=WRITE|0x2,
		KERNEL	=WRITE|0x4,
		OP	=WRITE|0x8,
		EXCL_WRITE=WRITE|0x20,
		BOOTLOADER=EXCL_WRITE|0x1,
		RESTORE	=EXCL_WRITE|0x2,
		MODE_MASK=0x0F,
	} mode=0;

	while((opt=getopt(argc, argv, "trsmkOabvqBhH?"))>=0) {
		switch(opt) {
		case 'r':
			if(mode) goto badmode;
			mode=REPORT;
			break;
		case 't':
			if(mode) goto badmode;
			mode=TEST;
			break;

		case 's':
			if((mode&READ)||(mode&EXCL_WRITE)==EXCL_WRITE) goto badmode;
			mode|=SYSTEM;
			break;
		case 'm':
			if((mode&READ)||(mode&EXCL_WRITE)==EXCL_WRITE) goto badmode;
			mode|=MODEM;
			break;
		case 'k':
			if((mode&READ)||(mode&EXCL_WRITE)==EXCL_WRITE) goto badmode;
			mode|=KERNEL;
			break;
		case 'O':
			if((mode&READ)||(mode&EXCL_WRITE)==EXCL_WRITE) goto badmode;
			mode|=OP;
			break;

		case 'a':
			if(mode) goto badmode;
			mode=SYSTEM|MODEM;
			break;

		case 'b':
			if(mode) goto badmode;
			mode=BOOTLOADER;
			break;

		badmode:
			fprintf(stderr, "Multiple incompatible modes have been selected, cannot continue!\n");
			return 1;

		case 'B':
			/* set blocksize (ever needed?) */
			break;
		case 'v':
			if(verbose!=((int)-1>>1)) ++verbose;
			break;

		case 'q':
			if(verbose!=~((int)-1>>1)) --verbose;
			break;

		default:
			ret=1;
		case 'h':
		case 'H':
		case '?':
			goto usage;
		}
	}

	if(argc-optind!=1) {
		ret=1;
	usage:
		fprintf(stderr, "Usage: %s [-trsmOabvqB] <KDZ file>\n"
"  -h  Help, this message\n" "  -v  Verbose, increase verbosity\n"
"  -q  Quiet, decrease verbosity\n"
"  -t  Test, does the KDZ file appear applicable to this device\n"
"  -r  Report, list status of KDZ chunks\n"
"  -a  Apply all, write all areas safe to write from KDZ\n"
"  -s  System, write system area from KDZ\n"
"  -m  Modem, write modem area from KDZ\n"
"  -O  OP, write OP area from KDZ\n"
"  -k  Kernel, write kernel/boot area from KDZ\n"
"  -b  Bootloader, write bootloader from KDZ; USED FOR RETURNING TO STOCK!\n"
"Only one of -t, -r, -a, or -b is allowed.  -s, -m, -k, and -O may be used \n"
"together, but they exclude the prior options.\n", argv[0]);
		return ret;
	}

	md5_start();

	if(!(kdz=open_kdzfile(argv[optind]))) {
		fprintf(stderr, "Failed to open KDZ file \"%s\", aborting\n", argv[optind]);
		ret=1;
		goto abort;
	}

	switch(mode) {
		bool okay;
	case REPORT:
		printf("Invoke report() (to be implemented)\n");
		break;
	case TEST:
		printf("Invoke testmode() (to be implemented)\n");
		break;
	case RESTORE:
		printf("Write everything (to be implemented)\n");
		break;
	case BOOTLOADER:
		printf("Write bootloader (to be implemented)\n");
		break;
	default:
		okay=0;
		if((mode&SYSTEM)==SYSTEM) {
			printf("Write system (to be implemented)\n");
			okay=1;
		}
		if((mode&MODEM)==MODEM) {
			printf("Write modem (to be implemented)\n");
			okay=1;
		}
		if((mode&OP)==OP) {
			printf("Write OP (to be implemented)\n");
			okay=1;
		}
		if((mode&KERNEL)==KERNEL) {
			printf("Write kernel/boot (to be implemented)\n");
			okay=1;
		}
		if(!okay) {
			ret=1;
			fprintf(stderr, "%s: no action specified\n", argv[0]);
		}
	}

abort:
	if(kdz) close_kdzfile(kdz);

	md5_stop();

	return ret;
}

