/*
 *
 *  postfish
 *    
 *      Copyright (C) 2018 Xiph.Org
 *
 *  Postfish is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *   
 *  Postfish is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *   
 *  You should have received a copy of the GNU General Public License
 *  along with Postfish; see the file COPYING.  If not, write to the
 *  Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * 
 */

/* Work around M_PIl only being available in libc */
#ifndef M_PIl
# define M_PIl      3.141592653589793238462643383279502884L
#endif

/* Work-around lack of PATH_MAX */
#ifndef PATH_MAX
# define PATH_MAX 1024
#endif
