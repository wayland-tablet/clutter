/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2015 Red Hat.
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
 *
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "clutter-touchpad-gesture-private.h"
#include "clutter-gesture-action.h"
#include "clutter-marshal.h"
#include "clutter-private.h"

typedef ClutterTouchpadGestureIface ClutterTouchpadGestureInterface;

G_DEFINE_INTERFACE (ClutterTouchpadGesture, clutter_touchpad_gesture,
                    CLUTTER_TYPE_GESTURE_ACTION)

static void
clutter_touchpad_gesture_default_init (ClutterTouchpadGestureIface *iface)
{
}

gboolean
clutter_touchpad_gesture_handle_event (ClutterTouchpadGesture *gesture,
                                       const ClutterEvent     *event)
{
  g_return_val_if_fail (CLUTTER_IS_TOUCHPAD_GESTURE (gesture), CLUTTER_EVENT_STOP);
  g_return_val_if_fail (event != NULL, CLUTTER_EVENT_STOP);

  return CLUTTER_TOUCHPAD_GESTURE_GET_IFACE (gesture)->handle_event (gesture, event);
}

gboolean
clutter_touchpad_gesture_over_threshold (ClutterTouchpadGesture *gesture)
{
  ClutterTouchpadGestureIface *iface;

  g_return_val_if_fail (CLUTTER_IS_TOUCHPAD_GESTURE (gesture), FALSE);

  iface = CLUTTER_TOUCHPAD_GESTURE_GET_IFACE (gesture);

  if (!iface->over_threshold)
    return TRUE;

  return iface->over_threshold (gesture);
}

gboolean
clutter_touchpad_gesture_begin (ClutterTouchpadGesture *gesture)
{
  ClutterTouchpadGestureIface *iface;

  g_return_val_if_fail (CLUTTER_IS_TOUCHPAD_GESTURE (gesture), FALSE);

  iface = CLUTTER_TOUCHPAD_GESTURE_GET_IFACE (gesture);

  if (iface->begin)
    return iface->begin (gesture);
  else
    return TRUE;
}

gboolean
clutter_touchpad_gesture_update (ClutterTouchpadGesture *gesture)
{
  ClutterTouchpadGestureIface *iface;

  g_return_val_if_fail (CLUTTER_IS_TOUCHPAD_GESTURE (gesture), FALSE);

  iface = CLUTTER_TOUCHPAD_GESTURE_GET_IFACE (gesture);

  if (iface->update)
    return iface->update (gesture);
  else
    return TRUE;
}

void
clutter_touchpad_gesture_end (ClutterTouchpadGesture *gesture)
{
  ClutterTouchpadGestureIface *iface;

  g_return_val_if_fail (CLUTTER_IS_TOUCHPAD_GESTURE (gesture), FALSE);

  iface = CLUTTER_TOUCHPAD_GESTURE_GET_IFACE (gesture);

  if (iface->end)
    iface->end (gesture);
}
