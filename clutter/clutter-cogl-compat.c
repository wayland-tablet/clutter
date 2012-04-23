/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2012  Intel Corporation.
 *
 * Authored By: Neil Roberts <neil@linux.intel.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "clutter-cogl-compat.h"

GType
cogl_gtype_handle_get_type (void)
{
   static volatile gsize type_volatile = 0;
   if (g_once_init_enter (&type_volatile))
     {
       GType type =
         g_boxed_type_register_static (g_intern_static_string ("CoglHandle"),
                                       (GBoxedCopyFunc) cogl_object_ref,
                                       (GBoxedFreeFunc) cogl_object_unref);
       g_once_init_leave (&type_volatile, type);
     }
   return type_volatile;
}
