/* **********************************************************************
* Copyright (C) 2016 Elliott Mitchell <ehem+android@m5p.com>		*
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


#ifndef _UUID_H_
#define _UUID_H_

typedef char uuid_t[16];

static inline int uuid_is_null(const uuid_t uu)
{ int i; for(i=0; i<sizeof(uuid_t); ++i) if(uu[i]) return 0; return 1; }

static inline void uuid_copy(uuid_t dst, const uuid_t src)
{ memcpy(dst, src, sizeof(uuid_t)); }

static inline int uuid_compare(const uuid_t uu0, const uuid_t uu1)
{ return memcmp(uu0, uu1, sizeof(uuid_t)); }

static inline void uuid_unparse(const uuid_t uu, char *out)
{ sprintf(out, "%02hhx%02hhx%02hhx%02hhx-%02hhx%02hhx-%02hhx%02hhx-%02hhx%02hhx-%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx",
uu[0], uu[1], uu[2], uu[3], uu[4], uu[5], uu[6], uu[7], uu[8], uu[9], uu[10],
uu[11], uu[12], uu[13], uu[14], uu[15]);
}

#endif

