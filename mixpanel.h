/*
 *
 *  postfish
 *    
 *      Copyright (C) 2002-2005 Monty
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

#include "postfish.h"

extern void mixpanel_create_channel(postfish_mainpanel *mp,
				   GtkWidget **windowbutton,
				   GtkWidget **activebutton);
extern void attenpanel_create(postfish_mainpanel *mp,
			      GtkWidget **windowbutton,
			      GtkWidget **activebutton);
extern void mixpanel_feedback(int displayit);
extern void mixpanel_reset(void);

extern void mixpanel_state_to_config(int bank);
extern void mixpanel_state_from_config(int bank);
