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

#ifndef __CLUTTER_TOUCHPAD_GESTURE_PRIVATE_H__
#define __CLUTTER_TOUCHPAD_GESTURE_PRIVATE_H__

#include <clutter/clutter-types.h>

G_BEGIN_DECLS

#define CLUTTER_TYPE_TOUCHPAD_GESTURE           (clutter_touchpad_gesture_get_type ())
#define CLUTTER_TOUCHPAD_GESTURE(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), CLUTTER_TYPE_TOUCHPAD_GESTURE, ClutterTouchpadGesture))
#define CLUTTER_IS_TOUCHPAD_GESTURE(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CLUTTER_TYPE_TOUCHPAD_GESTURE))
#define CLUTTER_TOUCHPAD_GESTURE_GET_IFACE(obj) (G_TYPE_INSTANCE_GET_INTERFACE ((obj), CLUTTER_TYPE_TOUCHPAD_GESTURE, ClutterTouchpadGestureIface))

typedef struct _ClutterTouchpadGesture ClutterTouchpadGesture;
typedef struct _ClutterTouchpadGestureIface ClutterTouchpadGestureIface;

struct _ClutterTouchpadGestureIface
{
  /*< private >*/
  GTypeInterface g_iface;

  gboolean (* handle_event) (ClutterTouchpadGesture *gesture,
                             const ClutterEvent     *event);

  gboolean (* over_threshold) (ClutterTouchpadGesture *gesture);

  gboolean (* begin)  (ClutterTouchpadGesture *gesture);
  gboolean (* update) (ClutterTouchpadGesture *gesture);
  void     (* end)    (ClutterTouchpadGesture *gesture);
};

GType    clutter_touchpad_gesture_get_type       (void) G_GNUC_CONST;

gboolean clutter_touchpad_gesture_handle_event   (ClutterTouchpadGesture *gesture,
                                                  const ClutterEvent     *event);
gboolean clutter_touchpad_gesture_over_threshold (ClutterTouchpadGesture *gesture);
gboolean clutter_touchpad_gesture_begin          (ClutterTouchpadGesture *gesture);
gboolean clutter_touchpad_gesture_update         (ClutterTouchpadGesture *gesture);
void     clutter_touchpad_gesture_end            (ClutterTouchpadGesture *gesture);

G_END_DECLS

#endif /* __CLUTTER_TOUCHPAD_GESTURE_PRIVATE_H__ */
