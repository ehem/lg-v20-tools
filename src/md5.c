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
*$Id$		*
************************************************************************/


#include <dlfcn.h>
#include <stdlib.h>
#include <stdio.h>

#include "md5.h"

static void *libcrypto=NULL;

int (*pMD5_Init)(MD5_CTX *c)=NULL;
int (*pMD5_Update)(MD5_CTX *c, const void *data, size_t len)=NULL;
int (*pMD5_Final)(unsigned char *md, MD5_CTX *c)=NULL;


void md5_start(void)
{
	int i;
	struct {
		void **psym;
		const char name[16];
	} syms[]={
		{(void **)&pMD5_Final,	"MD5_Final"},
		{(void **)&pMD5_Update,	"MD5_Update"},
		{(void **)&pMD5_Init,	"MD5_Init"},
	};

	if(libcrypto) return;

	if(!(libcrypto=dlopen("libcrypto.so", RTLD_GLOBAL))) {
		fprintf(stderr, "Failed to dlopen() libcrypto.so: %s\n",
dlerror());
		exit(-1);
	}

	for(i=0; i<sizeof(syms)/sizeof(syms[0]); ++i) {
		if(!(*(syms[i].psym)=dlsym(libcrypto, syms[i].name))) {
			fprintf(stderr, "Failed to resolve \"%s\": %s\n",
syms[i].name, dlerror());
			exit(-1);
		}
	}
}

void md5_stop(void)
{
	if(!libcrypto) return;

	dlclose(libcrypto);
	libcrypto=NULL;
	pMD5_Init=NULL;
	pMD5_Update=NULL;
	pMD5_Final=NULL;
}

