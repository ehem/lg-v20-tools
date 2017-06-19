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


#include "kdz.h"


const char kdz_file_magic[KDZ_MAGIC_LEN]="\x28\x05\x00\x00" "\x24\x38\x22\x25";

const char dz_file_magic[DZ_MAGIC_LEN]="\x32\x96\x18\x74";

const char dz_chunk_magic[DZ_MAGIC_LEN]="\x30\x12\x95\x78";


struct kdz_file *open_kdzfile(const char *filename)
{
	return NULL;
}

