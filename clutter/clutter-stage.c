/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Authored By Matthew Allum  <mallum@openedhand.com>
 *
 * Copyright (C) 2006 OpenedHand
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
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:clutter-stage
 * @short_description: Top level visual element to which actors are placed.
 * 
 * #ClutterStage is a top level 'window' on which child actors are placed
 * and manipulated.
 */

#include "config.h"

#include "clutter-backend.h"
#include "clutter-stage.h"
#include "clutter-main.h"
#include "clutter-color.h"
#include "clutter-util.h"
#include "clutter-marshal.h"
#include "clutter-enum-types.h"
#include "clutter-private.h"
#include "clutter-debug.h"
#include "clutter-version.h" 	/* For flavour */

#include "cogl.h"

#include <gdk-pixbuf-xlib/gdk-pixbuf-xlib.h>

#define FIXED_PERSPECTIVE
#undef  USING_GLES

G_DEFINE_ABSTRACT_TYPE (ClutterStage, clutter_stage, CLUTTER_TYPE_GROUP);

#define CLUTTER_STAGE_GET_PRIVATE(obj) \
(G_TYPE_INSTANCE_GET_PRIVATE ((obj), CLUTTER_TYPE_STAGE, ClutterStagePrivate))

struct _ClutterStagePrivate
{
  ClutterColor color;

  guint is_fullscreen     : 1;
  guint is_offscreen      : 1;
  guint is_cursor_visible : 1;
};

enum
{
  PROP_0,
  
  PROP_COLOR,
  PROP_FULLSCREEN,
  PROP_OFFSCREEN,
  PROP_CURSOR_VISIBLE
};

enum
{
  EVENT,
  EVENT_AFTER,
  BUTTON_PRESS_EVENT,
  BUTTON_RELEASE_EVENT,
  SCROLL_EVENT,
  KEY_PRESS_EVENT,
  KEY_RELEASE_EVENT,
  MOTION_EVENT,
  STAGE_STATE_EVENT,
  
  LAST_SIGNAL
};

static guint stage_signals[LAST_SIGNAL] = { 0, };

static void
clutter_stage_paint (ClutterActor *actor)
{
  /* chain up */
  CLUTTER_NOTE (PAINT, "Chaining up to parent class paint");

  if (CLUTTER_ACTOR_CLASS (clutter_stage_parent_class)->paint)
    CLUTTER_ACTOR_CLASS (clutter_stage_parent_class)->paint (actor);
}

static void
clutter_stage_set_property (GObject      *object, 
			    guint         prop_id,
			    const GValue *value, 
			    GParamSpec   *pspec)
{
  ClutterStage *stage;
  ClutterStagePrivate *priv;
  ClutterActor *actor;

  stage = CLUTTER_STAGE (object);
  actor = CLUTTER_ACTOR (stage);
  priv = stage->priv;

  switch (prop_id) 
    {
    case PROP_COLOR:
      clutter_stage_set_color (stage, g_value_get_boxed (value));
      break;
    case PROP_OFFSCREEN:
      if (CLUTTER_ACTOR_IS_REALIZED (actor))
        {
          clutter_actor_unrealize (actor);
          priv->is_offscreen = g_value_get_boolean (value);
          clutter_actor_realize (actor);
        }
      else
        priv->is_offscreen = g_value_get_boolean (value);
      break;
    case PROP_FULLSCREEN:
      if (g_value_get_boolean (value))
        clutter_stage_fullscreen (stage);
      else
        clutter_stage_unfullscreen (stage);
      break;
    case PROP_CURSOR_VISIBLE:
      if (g_value_get_boolean (value))
        clutter_stage_show_cursor (stage);
      else
        clutter_stage_hide_cursor (stage);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
clutter_stage_get_property (GObject    *object, 
			      guint       prop_id,
			      GValue     *value, 
			      GParamSpec *pspec)
{
  ClutterStage        *stage;
  ClutterStagePrivate *priv;
  ClutterColor         color;

  stage = CLUTTER_STAGE(object);
  priv = stage->priv;

  switch (prop_id) 
    {
    case PROP_COLOR:
      clutter_stage_get_color (stage, &color);
      g_value_set_boxed (value, &color);
      break;
    case PROP_OFFSCREEN:
      g_value_set_boolean (value, priv->is_offscreen);
      break;
    case PROP_FULLSCREEN:
      g_value_set_boolean (value, priv->is_fullscreen);
      break;
    case PROP_CURSOR_VISIBLE:
      g_value_set_boolean (value, priv->is_cursor_visible);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    } 
}

static void
clutter_stage_class_init (ClutterStageClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  ClutterActorClass *actor_class = CLUTTER_ACTOR_CLASS (klass);

  gobject_class->set_property = clutter_stage_set_property;
  gobject_class->get_property = clutter_stage_get_property;

  actor_class->paint = clutter_stage_paint;

  /**
   * ClutterStage:fullscreen
   *
   * Whether the stage should be fullscreen or not.
   */
  g_object_class_install_property
    (gobject_class, PROP_FULLSCREEN,
     g_param_spec_boolean ("fullscreen",
			   "Fullscreen",
			   "Whether the main stage is fullscreen",
			   FALSE,
			   G_PARAM_CONSTRUCT | CLUTTER_PARAM_READWRITE));

  g_object_class_install_property
    (gobject_class, PROP_OFFSCREEN,
     g_param_spec_boolean ("offscreen",
			   "Offscreen",
			   "Whether the main stage is renderer offscreen",
			   FALSE,
			   G_PARAM_CONSTRUCT | CLUTTER_PARAM_READWRITE));


  g_object_class_install_property
    (gobject_class, PROP_CURSOR_VISIBLE,
     g_param_spec_boolean ("cursor-visible",
			   "Cursor Visible",
			   "Whether the mouse pointer is visible on the main stage ",
			   FALSE,
			   G_PARAM_CONSTRUCT | CLUTTER_PARAM_READWRITE));

  g_object_class_install_property
    (gobject_class, PROP_COLOR,
     g_param_spec_boxed ("color",
			 "Color",
			 "The color of the main stage",
			 CLUTTER_TYPE_COLOR,
			 CLUTTER_PARAM_READWRITE));
  /**
   * ClutterStage::event:
   * @stage: the actor which received the event
   * @event: a #ClutterEvent
   *
   * The ::event signal is emitted each time and event is received
   * by the @stage.
   */
  stage_signals[EVENT] =
    g_signal_new ("event",
		  G_TYPE_FROM_CLASS (gobject_class),
		  G_SIGNAL_RUN_LAST,
		  G_STRUCT_OFFSET (ClutterStageClass, event),
		  NULL, NULL,
		  clutter_marshal_VOID__BOXED,
		  G_TYPE_NONE, 1,
		  CLUTTER_TYPE_EVENT | G_SIGNAL_TYPE_STATIC_SCOPE);
  /**
   * ClutterStage::event-after:
   * @stage: the actor which received the event
   * @event: a #ClutterEvent
   *
   * The ::event-after signal is emitted after each event, except for
   * the "delete-event" is received by @stage.
   */
  stage_signals[EVENT_AFTER] =
    g_signal_new ("event-after",
                  G_TYPE_FROM_CLASS (gobject_class),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (ClutterStageClass, event_after),
                  NULL, NULL,
                  clutter_marshal_VOID__BOXED,
                  G_TYPE_NONE, 1,
                  CLUTTER_TYPE_EVENT | G_SIGNAL_TYPE_STATIC_SCOPE);
  /**
   * ClutterStage::button-press-event:
   * @stage: the actor which received the event
   * @event: a #ClutterButtonEvent
   *
   * The ::button-press-event signal is emitted each time a mouse button
   * is pressed on @stage.
   */
  stage_signals[BUTTON_PRESS_EVENT] =
    g_signal_new ("button-press-event",
		  G_TYPE_FROM_CLASS (gobject_class),
		  G_SIGNAL_RUN_LAST,
		  G_STRUCT_OFFSET (ClutterStageClass, button_press_event),
		  NULL, NULL,
		  clutter_marshal_VOID__BOXED,
		  G_TYPE_NONE, 1,
		  CLUTTER_TYPE_EVENT | G_SIGNAL_TYPE_STATIC_SCOPE);
  /**
   * ClutterStage::button-release-event:
   * @stage: the actor which received the event
   * @event: a #ClutterButtonEvent
   *
   * The ::button-release-event signal is emitted each time a mouse button
   * is released on @stage.
   */
  stage_signals[BUTTON_RELEASE_EVENT] =
    g_signal_new ("button-release-event",
		  G_TYPE_FROM_CLASS (gobject_class),
		  G_SIGNAL_RUN_LAST,
		  G_STRUCT_OFFSET (ClutterStageClass, button_release_event),
		  NULL, NULL,
		  clutter_marshal_VOID__BOXED,
		  G_TYPE_NONE, 1,
		  CLUTTER_TYPE_EVENT | G_SIGNAL_TYPE_STATIC_SCOPE);
  /**
   * ClutterStage::scroll-event:
   * @stage: the actor which received the event
   * @event: a #ClutterScrollEvent
   *
   * The ::scroll-event signal is emitted each time a the mouse is
   * scrolled on @stage
   *
   * Since: 0.4
   */
  stage_signals[SCROLL_EVENT] =
    g_signal_new ("scroll-event",
		  G_TYPE_FROM_CLASS (gobject_class),
		  G_SIGNAL_RUN_LAST,
		  G_STRUCT_OFFSET (ClutterStageClass, scroll_event),
		  NULL, NULL,
		  clutter_marshal_VOID__BOXED,
		  G_TYPE_NONE, 1,
		  CLUTTER_TYPE_EVENT | G_SIGNAL_TYPE_STATIC_SCOPE);
  /**
   * ClutterStage::key-press-event:
   * @stage: the actor which received the event
   * @event: a #ClutterKeyEvent
   *
   * The ::key-press-event signal is emitted each time a keyboard button
   * is pressed on @stage.
   */
  stage_signals[KEY_PRESS_EVENT] =
    g_signal_new ("key-press-event",
		  G_TYPE_FROM_CLASS (gobject_class),
		  G_SIGNAL_RUN_LAST,
		  G_STRUCT_OFFSET (ClutterStageClass, key_press_event),
		  NULL, NULL,
		  clutter_marshal_VOID__BOXED,
		  G_TYPE_NONE, 1,
		  CLUTTER_TYPE_EVENT | G_SIGNAL_TYPE_STATIC_SCOPE);
  /**
   * ClutterStage::key-release-event:
   * @stage: the actor which received the event
   * @event: a #ClutterKeyEvent
   *
   * The ::key-release-event signal is emitted each time a keyboard button
   * is released on @stage.
   */
  stage_signals[KEY_RELEASE_EVENT] =
    g_signal_new ("key-release-event",
		  G_TYPE_FROM_CLASS (gobject_class),
		  G_SIGNAL_RUN_LAST,
		  G_STRUCT_OFFSET (ClutterStageClass, key_release_event),
		  NULL, NULL,
		  clutter_marshal_VOID__BOXED,
		  G_TYPE_NONE, 1,
		  CLUTTER_TYPE_EVENT | G_SIGNAL_TYPE_STATIC_SCOPE);
  /**
   * ClutterStage::motion-event:
   * @stage: the actor which received the event
   * @event: a #ClutterMotionEvent
   *
   * The ::motion-event signal is emitted each time the mouse pointer is
   * moved on @stage.
   */
  stage_signals[MOTION_EVENT] =
    g_signal_new ("motion-event",
		  G_TYPE_FROM_CLASS (gobject_class),
		  G_SIGNAL_RUN_LAST,
		  G_STRUCT_OFFSET (ClutterStageClass, motion_event),
		  NULL, NULL,
		  clutter_marshal_VOID__BOXED,
		  G_TYPE_NONE, 1,
		  CLUTTER_TYPE_EVENT | G_SIGNAL_TYPE_STATIC_SCOPE);
  
  g_type_class_add_private (gobject_class, sizeof (ClutterStagePrivate));
}

static void
clutter_stage_init (ClutterStage *self)
{
  ClutterStagePrivate *priv;

  /* a stage is a top-level object */
  CLUTTER_SET_PRIVATE_FLAGS (self, CLUTTER_ACTOR_IS_TOPLEVEL);
  
  self->priv = priv = CLUTTER_STAGE_GET_PRIVATE (self);

  priv->is_offscreen = FALSE;
  priv->is_fullscreen = FALSE;
  priv->is_cursor_visible = FALSE;

  priv->color.red   = 0xff;
  priv->color.green = 0xff;
  priv->color.blue  = 0xff;
  priv->color.alpha = 0xff;
  
  clutter_actor_set_size (CLUTTER_ACTOR (self), 640, 480);
}

/**
 * clutter_stage_get_default:
 *
 * Returns the main stage.  #ClutterStage is a singleton, so
 * the stage will be created the first time this function is
 * called (typically, inside clutter_init()); all the subsequent
 * calls to clutter_stage_get_default() will return the same
 * instance.
 *
 * Return value: the main #ClutterStage.  You should never
 *   destroy or unref the returned actor.
 */
ClutterActor *
clutter_stage_get_default (void)
{
  ClutterMainContext *context;

  context = clutter_context_get_default ();
  g_assert (context != NULL);

  return _clutter_backend_get_stage (context->backend);
}

/**
 * clutter_stage_set_color
 * @stage: A #ClutterStage
 * @color: A #ClutterColor
 * 
 * Set the stage color.
 **/
void
clutter_stage_set_color (ClutterStage       *stage,
			 const ClutterColor *color)
{
  ClutterStagePrivate *priv;

  g_return_if_fail (CLUTTER_IS_STAGE (stage));
  g_return_if_fail (color != NULL);
  
  priv = stage->priv;
  priv->color.red = color->red;
  priv->color.green = color->green;
  priv->color.blue = color->blue;
  priv->color.alpha = color->alpha;

  if (CLUTTER_ACTOR_IS_VISIBLE (CLUTTER_ACTOR (stage)))
    clutter_actor_queue_redraw (CLUTTER_ACTOR (stage));
  
  g_object_notify (G_OBJECT (stage), "color");
}

/**
 * clutter_stage_get_color
 * @stage: A #ClutterStage
 * @color: return location for a #ClutterColor
 * 
 * Retrieves the stage color.
 */
void
clutter_stage_get_color (ClutterStage *stage,
			 ClutterColor *color)
{
  ClutterStagePrivate *priv;
  
  g_return_if_fail (CLUTTER_IS_STAGE (stage));
  g_return_if_fail (color != NULL);

  priv = stage->priv;
  
  color->red = priv->color.red;
  color->green = priv->color.green;
  color->blue = priv->color.blue;
  color->alpha = priv->color.alpha;
}

/**
 * clutter_stage_fullscreen:
 * @stage: a #ClutterStage
 *
 * Asks to place the stage window in the fullscreen state. Note that you
 * shouldn't assume the window is definitely full screen afterward, because
 * other entities (e.g. the user or window manager) could unfullscreen it
 * again, and not all window managers honor requests to fullscreen windows.
 */
void
clutter_stage_fullscreen (ClutterStage *stage)
{
  ClutterStagePrivate *priv;

  g_return_if_fail (CLUTTER_IS_STAGE (stage));

  priv = stage->priv;
  if (!priv->is_fullscreen)
    {
      priv->is_fullscreen = TRUE;

      if (CLUTTER_STAGE_GET_CLASS (stage)->set_fullscreen)
        CLUTTER_STAGE_GET_CLASS (stage)->set_fullscreen (stage, TRUE);

      g_object_notify (G_OBJECT (stage), "fullscreen");
    }
}

/**
 * clutter_stage_unfullscreen:
 * @stage: a #ClutterStage
 *
 * Asks to toggle off the fullscreen state for the stage window. Note that
 * you shouldn't assume the window is definitely not full screen afterward,
 * because other entities (e.g. the user or window manager) could fullscreen
 * it again, and not all window managers honor requests to unfullscreen
 * windows.
 */
void
clutter_stage_unfullscreen (ClutterStage *stage)
{
  ClutterStagePrivate *priv;

  g_return_if_fail (CLUTTER_IS_STAGE (stage));

  priv = stage->priv;
  if (priv->is_fullscreen)
    {
      priv->is_fullscreen = FALSE;

      if (CLUTTER_STAGE_GET_CLASS (stage)->set_fullscreen)
        CLUTTER_STAGE_GET_CLASS (stage)->set_fullscreen (stage, FALSE);

      g_object_notify (G_OBJECT (stage), "fullscreen");
    }
}

/**
 * clutter_stage_show_cursor:
 * @stage: a #ClutterStage
 *
 * Shows the cursor on the stage window
 */
void
clutter_stage_show_cursor (ClutterStage *stage)
{
  ClutterStagePrivate *priv;

  g_return_if_fail (CLUTTER_IS_STAGE (stage));

  priv = stage->priv;
  if (!priv->is_cursor_visible)
    {
      priv->is_cursor_visible = TRUE;

      if (CLUTTER_STAGE_GET_CLASS (stage)->set_cursor_visible)
        CLUTTER_STAGE_GET_CLASS (stage)->set_cursor_visible (stage, TRUE);

      g_object_notify (G_OBJECT (stage), "cursor-visible");
    }
}

/**
 * clutter_stage_hide_cursor:
 * @stage:
 *
 * Hides the cursor as invisible on the stage window
 */
void
clutter_stage_hide_cursor (ClutterStage *stage)
{
  ClutterStagePrivate *priv;

  g_return_if_fail (CLUTTER_IS_STAGE (stage));

  priv = stage->priv;
  if (priv->is_cursor_visible)
    {
      priv->is_cursor_visible = FALSE;

      if (CLUTTER_STAGE_GET_CLASS (stage)->set_cursor_visible)
        CLUTTER_STAGE_GET_CLASS (stage)->set_cursor_visible (stage, FALSE);

      g_object_notify (G_OBJECT (stage), "cursor-visible");
    }
}

/**
 * clutter_stage_snapshot
 * @stage: A #ClutterStage
 * @x: x coordinate of the first pixel that is read from stage
 * @y: y coordinate of the first pixel that is read from stage
 * @width: Width dimention of pixels to be read, or -1 for the
 *   entire stage width
 * @height: Height dimention of pixels to be read, or -1 for the
 *   entire stage height
 *
 * Gets a pixel based representation of the current rendered stage.
 *
 * Return value: pixel representation as a  #GdkPixbuf
 **/
GdkPixbuf *
clutter_stage_snapshot (ClutterStage *stage,
			gint          x,
			gint          y,
			gint          width,
			gint          height)
{
  ClutterStageClass *klass;

  g_return_val_if_fail (CLUTTER_IS_STAGE (stage), NULL);
  g_return_val_if_fail (x >= 0 && y >= 0, NULL);

  klass = CLUTTER_STAGE_GET_CLASS (stage);
  if (klass->draw_to_pixbuf)
    {
      GdkPixbuf *retval = NULL;

      klass->draw_to_pixbuf (stage, retval, x, y, width, height);
      
      return retval;
    }

  return NULL;
}

void
_clutter_stage_sync_viewport (ClutterStage *stage)
{
  /* FIXME: 
   *   Something needs to be done with this func, apps may need to
   *   overide it and its need better integration into the backend.
  */
  ClutterActor *actor;
  gint width, height;

  g_return_if_fail (CLUTTER_IS_STAGE (stage));

  actor = CLUTTER_ACTOR (stage);

  width = clutter_actor_get_width (actor);
  height = clutter_actor_get_height (actor);

  cogl_setup_viewport (width,
		       height,
		       171, /* 60 degrees */
		       CFX_ONE,
		       CLUTTER_FLOAT_TO_FIXED (0.1),
		       CLUTTER_FLOAT_TO_FIXED (100.0));
}

/**
 * clutter_stage_get_actor_at_pos:
 * @stage:
 * @x:
 * @y:
 *
 * FIXME
 *
 * Return value: the actor at the specified coordinates, if any
 */
ClutterActor *
clutter_stage_get_actor_at_pos (ClutterStage *stage,
                                gint          x,
                                gint          y)
{
#if HAVE_COGL_GL

  /* FIXME: Add a clutter feature flag for this or figure out  
   *        for gles.
  */

  ClutterActor *found = NULL;
  GLuint buff[64] = { 0 };
  GLint hits;
  GLint view[4];
  
  g_return_val_if_fail (CLUTTER_IS_STAGE (stage), NULL);
 
  glSelectBuffer (sizeof (buff), buff);
  glGetIntegerv (GL_VIEWPORT, view);
  glRenderMode (GL_SELECT);

  glInitNames ();

  glPushName (0);
 
  glMatrixMode (GL_PROJECTION);
  glPushMatrix ();
  glLoadIdentity ();

  /* This is gluPickMatrix(x, y, 1.0, 1.0, view); */
  glTranslatef ((view[2] - 2 * (x - view[0])),
	        (view[3] - 2 * (y - view[1])), 0);
  glScalef (view[2], -view[3], 1.0);

  cogl_perspective (171, /* 60 degrees */
		    CFX_ONE,
		    CLUTTER_FLOAT_TO_FIXED (0.1),
		    CLUTTER_FLOAT_TO_FIXED (100.0));

  glMatrixMode (GL_MODELVIEW);

  clutter_actor_paint (CLUTTER_ACTOR (stage));

  glMatrixMode (GL_PROJECTION);
  glPopMatrix ();

  hits = glRenderMode (GL_RENDER);

  if (hits != 0)
    {
#if 0
      gint i
      for (i = 0; i < hits; i++)
	g_print ("Hit at %i\n", buff[i * 4 + 3]);
#endif
  
      found = clutter_group_find_child_by_id (CLUTTER_GROUP (stage), 
					      buff[(hits-1) * 4 + 3]);
    }

  _clutter_stage_sync_viewport (stage);

  return found;
#else
  /* GL/ES cannot do this yet.. */
  return NULL;
#endif
}

/**
 * clutter_stage_event:
 * @stage: a #ClutterStage
 * @event: a #ClutterEvent
 *
 * This function is used to emit an event on the main stage.
 * You should rarely need to use this function, except for
 * synthetising events.
 *
 * Return value: the return value from the signal emission
 *
 * Since: 0.4
 */
gboolean
clutter_stage_event (ClutterStage *stage,
                     ClutterEvent *event)
{
  gboolean res = TRUE;
  gint signal_num = -1;

  g_return_val_if_fail (CLUTTER_IS_STAGE (stage), FALSE);
  g_return_val_if_fail (event != NULL, FALSE);

  g_object_ref (stage);

  g_signal_emit (stage, stage_signals[EVENT], 0, event);
  
  switch (event->type)
    {
    case CLUTTER_NOTHING:
      break;
    case CLUTTER_BUTTON_PRESS:
    case CLUTTER_2BUTTON_PRESS:
    case CLUTTER_3BUTTON_PRESS:
      signal_num = BUTTON_PRESS_EVENT;
      break;
    case CLUTTER_BUTTON_RELEASE:
      signal_num = BUTTON_RELEASE_EVENT;
      break;
    case CLUTTER_SCROLL:
      signal_num = SCROLL_EVENT;
      break;
    case CLUTTER_KEY_PRESS:
      signal_num = KEY_PRESS_EVENT;
      break;
    case CLUTTER_KEY_RELEASE:
      signal_num = KEY_RELEASE_EVENT;
      break;
    case CLUTTER_MOTION:
      signal_num = MOTION_EVENT;
      break;
    case CLUTTER_DELETE:
      signal_num = -1;
      break;
    case CLUTTER_STAGE_STATE:
      signal_num = -1;
      break;
    case CLUTTER_DESTROY_NOTIFY:
      signal_num = -1;
      break;
    case CLUTTER_CLIENT_MESSAGE:
      signal_num = -1;
      break;
    }

  if (signal_num != -1)
    {
      g_signal_emit (stage, stage_signals[signal_num], 0, event);
      g_signal_emit (stage, stage_signals[EVENT_AFTER], 0, event);
      res = TRUE;
    }

  g_object_unref (stage);

  return res;
}

/*** Perspective boxed type ******/

/**
 * clutter_perspective_copy:
 * @perspective: a #ClutterPerspective
 *
 * Makes a copy of the perspective structure.  The result must be
 * freed using clutter_perspective_free().
 *
 * Return value: an allocated copy of @perspective.
 *
 * Since: 0.2
 */
ClutterPerspective *
clutter_perspective_copy (const ClutterPerspective *perspective)
{
  ClutterPerspective *result;
  
  g_return_val_if_fail (perspective != NULL, NULL);

  result = g_slice_new (ClutterPerspective);
  *result = *perspective;

  return result;
}

/**
 * clutter_perspective_free:
 * @perspective: a #ClutterPerspective
 *
 * Frees a perspective structure created with clutter_perspective_copy().
 *
 * Since: 0.4
 */
void
clutter_perspective_free (ClutterPerspective *perspective)
{
  g_return_if_fail (perspective != NULL);

  g_slice_free (ClutterPerspective, perspective);
}

GType
clutter_perspective_get_type (void)
{
  static GType our_type = 0;
  
  if (!our_type)
    our_type = g_boxed_type_register_static 
                       ("ClutterPerspective",
			(GBoxedCopyFunc) clutter_perspective_copy,
			(GBoxedFreeFunc) clutter_perspective_free);
  return our_type;
}
