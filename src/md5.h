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

#ifndef _MD5_H_
#define _MD5_H_

#include <inttypes.h>

/* due to lack of explict information about TWRP, I'm forced to make due
with these declarations...

The MD5_CTX structure is almost certainly overly large, but that works
unlike being too small by 1 byte */

typedef struct {
	uint64_t blobs[16];
} MD5_CTX;

extern int MD5_Init(MD5_CTX *c);
extern int MD5_Update(MD5_CTX *c, const void *data, size_t len);
extern int MD5_Final(unsigned char *md, MD5_CTX *c);

#ifndef BUILDTIME_LINK_LIBS
#define WRAPSYM(sym) extern __typeof__(sym) *p##sym
#else
#define WRAPSYM(sym) extern __typeof__(sym) *const p##sym
#endif
WRAPSYM(MD5_Init);
WRAPSYM(MD5_Update);
WRAPSYM(MD5_Final);
#undef WRAPSYM

extern void md5_start(void);
extern void md5_stop(void);

#endif

