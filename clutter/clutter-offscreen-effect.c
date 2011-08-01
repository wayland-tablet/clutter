/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2010  Intel Corporation.
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
 * Authors:
 *   Emmanuele Bassi <ebassi@linux.intel.com>
 *   Robert Bragg <robert@linux.intel.com>
 */

/**
 * SECTION:clutter-offscreen-effect
 * @short_description: Base class for effects using offscreen buffers
 * @see_also: #ClutterBlurEffect, #ClutterEffect
 *
 * #ClutterOffscreenEffect is an abstract class that can be used by
 * #ClutterEffect sub-classes requiring access to an offscreen buffer.
 *
 * Some effects, like the fragment shader based effects, can only use GL
 * textures, and in order to apply those effects to any kind of actor they
 * require that all drawing operations are applied to an offscreen framebuffer
 * that gets redirected to a texture.
 *
 * #ClutterOffscreenEffect provides all the heavy-lifting for creating the
 * offscreen framebuffer, the redirection and the final paint of the texture on
 * the desired stage.
 *
 * <refsect2 id="ClutterOffscreenEffect-implementing">
 *   <title>Implementing a ClutterOffscreenEffect</title>
 *   <para>Creating a sub-class of #ClutterOffscreenEffect requires, in case
 *   of overriding the #ClutterEffect virtual functions, to chain up to the
 *   #ClutterOffscreenEffect's implementation.</para>
 *   <para>On top of the #ClutterEffect's virtual functions,
 *   #ClutterOffscreenEffect also provides a <function>paint_target()</function>
 *   function, which encapsulates the effective painting of the texture that
 *   contains the result of the offscreen redirection.</para>
 *   <para>The size of the target material is defined to be as big as the
 *   transformed size of the #ClutterActor using the offscreen effect.
 *   Sub-classes of #ClutterOffscreenEffect can change the texture creation
 *   code to provide bigger textures by overriding the
 *   <function>create_texture()</function> virtual function; no chain up
 *   to the #ClutterOffscreenEffect implementation is required in this
 *   case.</para>
 * </refsect2>
 *
 * #ClutterOffscreenEffect is available since Clutter 1.4
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define CLUTTER_ENABLE_EXPERIMENTAL_API

#include "clutter-offscreen-effect.h"

#include "cogl/cogl.h"

#include "clutter-actor-private.h"
#include "clutter-debug.h"
#include "clutter-private.h"
#include "clutter-stage-private.h"

typedef struct _PerCameraState
{
  const ClutterCamera *camera;
  int valid_for_age;

  CoglHandle offscreen;

  /* aka "target" for legacy reasons */
  CoglPipeline *pipeline;
  CoglHandle texture;

  gfloat viewport_x_offset;
  gfloat viewport_y_offset;

  /* This is the calculated size of the fbo before being passed
   * through update_fbo() and create_texture(). This needs to be
   * tracked separately from the final fbo_width/height so that we can
   * detect when a different size is calculated and regenerate the
   * fbo.
   *
   * NB: We can't just compare the fbo_width/height because some
   * sub-classes may return a texture from create_texture() that has
   * a different size from the calculated request size.
   */
  int request_width;
  int request_height;

  /* The matrix that was current the last time the fbo was updated. We
     need to keep track of this to detect when we can reuse the
     contents of the fbo without redrawing the actor. We need the
     actual matrix rather than just detecting queued redraws on the
     actor because any change in the parent hierarchy (even just a
     translation) could cause the actor to look completely different
     and it won't cause a redraw to be queued on the parent's
     children. */
  CoglMatrix last_matrix_drawn;

} PerCameraState;

struct _ClutterOffscreenEffectPrivate
{
  ClutterActor *actor;

  PerCameraState *camera_state;
  int n_cameras;
  int cameras_age;

  gint old_opacity_override;
};

G_DEFINE_ABSTRACT_TYPE (ClutterOffscreenEffect,
                        clutter_offscreen_effect,
                        CLUTTER_TYPE_EFFECT);

static void
clutter_offscreen_effect_set_actor (ClutterActorMeta *meta,
                                    ClutterActor     *actor)
{
  ClutterOffscreenEffect *self = CLUTTER_OFFSCREEN_EFFECT (meta);
  ClutterOffscreenEffectPrivate *priv = self->priv;
  ClutterActorMetaClass *meta_class;
  int i;

  meta_class = CLUTTER_ACTOR_META_CLASS (clutter_offscreen_effect_parent_class);
  meta_class->set_actor (meta, actor);

  /* clear out the previous state */
  for (i = 0; i < priv->n_cameras; i++)
    {
      PerCameraState *camera_state = &priv->camera_state[i];
      if (camera_state->offscreen != NULL)
        {
          cogl_object_unref (camera_state->offscreen);
          camera_state->offscreen = NULL;
        }
    }

  /* we keep a back pointer here, to avoid going through the ActorMeta */
  priv->actor = clutter_actor_meta_get_actor (meta);
}

static CoglHandle
clutter_offscreen_effect_real_create_texture (ClutterOffscreenEffect *effect,
                                              gfloat                  width,
                                              gfloat                  height)
{
  return cogl_texture_new_with_size (MAX (width, 1), MAX (height, 1),
                                     COGL_TEXTURE_NO_SLICING,
                                     COGL_PIXEL_FORMAT_RGBA_8888_PRE);
}

static void
invalidate_per_camera_state (PerCameraState *camera_state)
{
  if (camera_state->pipeline)
    {
      cogl_object_unref (camera_state->pipeline);
      camera_state->pipeline = NULL;
    }

  if (camera_state->offscreen)
    {
      cogl_object_unref (camera_state->offscreen);
      camera_state->offscreen = NULL;
    }
}

static PerCameraState *
get_per_camera_state (ClutterOffscreenEffect *self, int camera_index)
{
  ClutterOffscreenEffectPrivate *priv = self->priv;
  ClutterStage *stage =
    CLUTTER_STAGE (_clutter_actor_get_stage_internal (priv->actor));
  int cameras_age = _clutter_stage_get_cameras_age (stage);
  PerCameraState *camera_state;

  /* Whenever there are additions or removals of cameras associated
   * with the stage then the stage's 'cameras_age' is bumped and we
   * throw away any cached state associated with the old cameras. */

  if (G_UNLIKELY (cameras_age != priv->cameras_age))
    {
      int i;
      int n_cameras;

      for (i = 0; i < priv->n_cameras; i++)
        invalidate_per_camera_state (&priv->camera_state[i]);

      if (priv->camera_state)
        g_slice_free1 (sizeof (PerCameraState) * priv->n_cameras,
                       priv->camera_state);

      /* NB: We always allocate for the total number of cameras since
       * we expect that each camera is likely going to be painted each
       * frame so we should save having to re-allocate later. */
      n_cameras = _clutter_stage_get_n_cameras (stage);
      priv->camera_state = g_slice_alloc (sizeof (PerCameraState) * n_cameras);

      for (i = 0; i < n_cameras; i++)
        {
          camera_state = &priv->camera_state[i];

          camera_state->camera = _clutter_stage_get_camera (stage, i);
          camera_state->pipeline = NULL;
          camera_state->texture = NULL;
          camera_state->offscreen = NULL;
        }

      priv->n_cameras = n_cameras;
      priv->cameras_age = cameras_age;
    }

  camera_state = &priv->camera_state[camera_index];
  if (camera_state->camera->age != camera_state->valid_for_age)
    {
      invalidate_per_camera_state (camera_state);
      camera_state->valid_for_age = camera_state->camera->age;
    }

  return camera_state;
}

static gboolean
update_fbo (ClutterEffect *effect,
            int            camera_index,
            int            request_width,
            int            request_height)
{
  ClutterOffscreenEffect *self = CLUTTER_OFFSCREEN_EFFECT (effect);
  PerCameraState *camera_state;

  camera_state = get_per_camera_state (self, camera_index);
  if (camera_state->request_width == request_width &&
      camera_state->request_height == request_height &&
      camera_state->offscreen != NULL)
    return TRUE;

  if (camera_state->pipeline == NULL)
    {
      CoglContext *ctx =
        clutter_backend_get_cogl_context (clutter_get_default_backend ());

      camera_state->pipeline = cogl_pipeline_new (ctx);

      /* We're always going to render the texture at a 1:1 texel:pixel
         ratio so we can use 'nearest' filtering to decrease the
         effects of rounding errors in the geometry calculation */
      cogl_pipeline_set_layer_filters (camera_state->pipeline,
                                       0, /* layer_index */
                                       COGL_PIPELINE_FILTER_NEAREST,
                                       COGL_PIPELINE_FILTER_NEAREST);
    }

  if (camera_state->texture != NULL)
    {
      cogl_object_unref (camera_state->texture);
      camera_state->texture = NULL;
    }

  camera_state->texture =
    clutter_offscreen_effect_create_texture (self,
                                             request_width,
                                             request_height);
  if (camera_state->texture == NULL)
    return FALSE;

  cogl_pipeline_set_layer_texture (camera_state->pipeline, 0, camera_state->texture);

  camera_state->request_width = request_width;
  camera_state->request_height = request_height;

  if (camera_state->offscreen != NULL)
    cogl_object_unref (camera_state->offscreen);

  camera_state->offscreen = cogl_offscreen_new_to_texture (camera_state->texture);
  if (camera_state->offscreen == NULL)
    {
      g_warning ("%s: Unable to create an Offscreen buffer", G_STRLOC);

      cogl_object_unref (camera_state->pipeline);
      camera_state->pipeline = NULL;

      camera_state->request_width = 0;
      camera_state->request_height = 0;

      return FALSE;
    }

  return TRUE;
}

static gboolean
clutter_offscreen_effect_pre_paint (ClutterEffect *effect)
{
  ClutterOffscreenEffect *self = CLUTTER_OFFSCREEN_EFFECT (effect);
  ClutterOffscreenEffectPrivate *priv = self->priv;
  ClutterActorBox box;
  CoglMatrix projection;
  CoglColor transparent;
  gfloat fbo_request_width, fbo_request_height;
  gfloat stage_viewport_x, stage_viewport_y;
  gfloat stage_viewport_width, stage_viewport_height;
  gfloat xexpand, yexpand;
  int texture_width, texture_height;
  const ClutterCamera *camera;
  PerCameraState *camera_state;
  ClutterStage *stage;

  if (!clutter_actor_meta_get_enabled (CLUTTER_ACTOR_META (effect)))
    return FALSE;

  if (priv->actor == NULL)
    return FALSE;

  stage = CLUTTER_STAGE (_clutter_actor_get_stage_internal (priv->actor));

  camera = _clutter_stage_get_current_camera (stage);
  camera_state = get_per_camera_state (self, camera->index);

  stage_viewport_x = camera->viewport[0];
  stage_viewport_y = camera->viewport[1];
  stage_viewport_width = camera->viewport[2];
  stage_viewport_height = camera->viewport[3];

  /* The paint box is the bounding box of the actor's paint volume in
   * screen coordinates (i.e. stage coordinates that have been
   * projected by the current camera and had the camera's viewport
   * transform applied). This will give us the size for the
   * framebuffer we need to redirect its rendering offscreen.
   *
   * The position will be used to setup an offset viewport so we need
   * an offset that is relative to the top-left of the stage's
   * viewport rectangle not relative to the screen.
   *
   * NB: We can't assume the stage viewport has an origin of (0,0) or
   * that the viewport size matches the geometry of the stage because
   * for example when we are running in stereoscopic rendering mode
   * the stage size might map to half the width or height in screen
   * coordinates if a horizonal or vertical split screen is being
   * used.
   */
  if (clutter_actor_get_paint_box (priv->actor, &box))
    {
      clutter_actor_box_get_size (&box,
                                  &fbo_request_width, &fbo_request_height);
      clutter_actor_box_get_origin (&box,
                                    &camera_state->viewport_x_offset,
                                    &camera_state->viewport_y_offset);
      camera_state->viewport_x_offset -= stage_viewport_x;
      camera_state->viewport_y_offset -= stage_viewport_y;
    }
  else
    {
      /* If we can't get a valid paint box then we fallback to
       * creating a full stage size fbo.
       *
       * Note: as mentioned above the stage's viewport might not
       * match the stage's geometry so if we want to know the stages
       * size in screen coordinates we should look at the viewport
       * geometry.
       *
       * Note: we may need to change how we determine the screen-space
       * size of the stage if we add support for sliced stages in the
       * future.
       */
      fbo_request_width = stage_viewport_width;
      fbo_request_height = stage_viewport_height;
      camera_state->viewport_x_offset = 0;
      camera_state->viewport_y_offset = 0;
    }

  /* First assert that the framebuffer is the right size... */
  if (!update_fbo (effect, camera->index,
                   fbo_request_width, fbo_request_height))
    return FALSE;

  texture_width = cogl_texture_get_width (camera_state->texture);
  texture_height = cogl_texture_get_height (camera_state->texture);

  /* get the current modelview matrix so that we can copy it to the
   * framebuffer. We also store the matrix that was last used when we
   * updated the FBO so that we can detect when we don't need to
   * update the FBO to paint a second time */
  cogl_get_modelview_matrix (&camera_state->last_matrix_drawn);

  /* let's draw offscreen */
  cogl_push_framebuffer (camera_state->offscreen);

  /* Copy the modelview that would have been used if rendering onscreen */
  cogl_set_modelview_matrix (&camera_state->last_matrix_drawn);

  /* Expand the viewport if the actor is partially off-stage,
   * otherwise the actor will end up clipped to the stage viewport
   */
  xexpand = 0.f;
  if (camera_state->viewport_x_offset < 0.f)
    xexpand = -camera_state->viewport_x_offset;
  if (camera_state->viewport_x_offset + texture_width > stage_viewport_width)
    xexpand = MAX (xexpand, (camera_state->viewport_x_offset +
                             texture_width) - stage_viewport_width);

  yexpand = 0.f;
  if (camera_state->viewport_y_offset < 0.f)
    yexpand = -camera_state->viewport_y_offset;
  if (camera_state->viewport_y_offset + texture_height > stage_viewport_height)
    yexpand = MAX (yexpand, (camera_state->viewport_y_offset +
                             texture_height) - stage_viewport_height);

  /* Set the viewport */
  cogl_set_viewport (-(camera_state->viewport_x_offset + xexpand),
                     -(camera_state->viewport_y_offset + yexpand),
                     stage_viewport_width + (2 * xexpand),
                     stage_viewport_height + (2 * yexpand));

  /* Copy the stage's projection matrix across to the framebuffer */
  _clutter_stage_get_projection_matrix (stage, &projection);

  /* If we've expanded the viewport, make sure to scale the projection
   * matrix accordingly (as it's been initialised to work with the
   * original viewport and not our expanded one).
   */
  if (xexpand > 0.f || yexpand > 0.f)
    {
      gfloat new_width, new_height;

      new_width = stage_viewport_width + (2 * xexpand);
      new_height = stage_viewport_height + (2 * yexpand);

      cogl_matrix_scale (&projection,
                         stage_viewport_width / new_width,
                         stage_viewport_height / new_height,
                         1);
    }

  cogl_set_projection_matrix (&projection);

  cogl_color_init_from_4ub (&transparent, 0, 0, 0, 0);
  cogl_clear (&transparent,
              COGL_BUFFER_BIT_COLOR |
              COGL_BUFFER_BIT_DEPTH);

  cogl_push_matrix ();

  /* Override the actor's opacity to fully opaque - we paint the offscreen
   * texture with the actor's paint opacity, so we need to do this to avoid
   * multiplying the opacity twice.
   */
  priv->old_opacity_override =
    _clutter_actor_get_opacity_override (priv->actor);
  _clutter_actor_set_opacity_override (priv->actor, 0xff);

  return TRUE;
}

static void
clutter_offscreen_effect_real_paint_target (ClutterOffscreenEffect *effect)
{
  ClutterOffscreenEffectPrivate *priv = effect->priv;
  guint8 paint_opacity;
  ClutterStage *stage =
    CLUTTER_STAGE (_clutter_actor_get_stage_internal (priv->actor));
  const ClutterCamera *camera = _clutter_stage_get_current_camera (stage);
  PerCameraState *camera_state = get_per_camera_state (effect, camera->index);
  int texture_width;
  int texture_height;

  paint_opacity = clutter_actor_get_paint_opacity (priv->actor);

  cogl_pipeline_set_color4ub (camera_state->pipeline,
                              paint_opacity,
                              paint_opacity,
                              paint_opacity,
                              paint_opacity);
  cogl_set_source (camera_state->pipeline);

  /* At this point we are in stage coordinates translated so if
   * we draw our texture using a textured quad the size of the paint
   * box then we will overlay where the actor would have drawn if it
   * hadn't been redirected offscreen.
   */
  texture_width = cogl_texture_get_width (camera_state->texture);
  texture_height = cogl_texture_get_height (camera_state->texture);
  cogl_rectangle_with_texture_coords (0, 0,
                                      texture_width,
                                      texture_height,
                                      0.0, 0.0,
                                      1.0, 1.0);
}

static void
clutter_offscreen_effect_paint_texture (ClutterOffscreenEffect *effect)
{
  ClutterOffscreenEffectPrivate *priv = effect->priv;
  CoglMatrix modelview;
  ClutterStage *stage =
    CLUTTER_STAGE (_clutter_actor_get_stage_internal (priv->actor));
  const ClutterCamera *camera = _clutter_stage_get_current_camera (stage);
  PerCameraState *camera_state = get_per_camera_state (effect, camera->index);
  CoglMatrix saved_projection;
  gfloat scale_x, scale_y;
  gfloat stage_x, stage_y;
  gfloat stage_width, stage_height;

  /* Now reset the modelview/projection to put us in orthographic
   * stage coordinates so we can draw the result of our offscreen render as
   * a textured quad...
   *
   * XXX: Note we don't use _apply_transform (stage, &modelview) to
   * put is in regular stage coordinates because that might include a
   * stereoscopic camera view transform and we don't want our
   * rectangle to be affected by that.
   *
   * XXX: since clutter-stage.c should be free to play tricks with the
   * viewport and projection matrix to support different forms of
   * stereoscopic rendering it might make sense at some point to add
   * some internal _clutter_stage api something like
   * _clutter_stage_push/pop_orthographic() that can handle the
   * details of giving us an orthographic projection without
   * clobbering any of the transforms in place for stereo rendering.
   *
   * XXX: For now we are assuming that clutter-stage.c only plays with
   * the viewport for vertical and horizontal split stereo rendering
   * but at some point if we start using the projection matrix instead
   * then this code will conflict with that!
   */

  cogl_get_projection_matrix (&saved_projection);
  clutter_actor_get_size (CLUTTER_ACTOR (stage), &stage_width, &stage_height);
  cogl_ortho (0, stage_width, /* left, right */
              stage_height, 0, /* bottom, top */
              -1, 100 /* z near, far */);

  cogl_push_matrix ();

  /* NB: camera_state->viewport_x/y_offset are in screen coordinates
   * relative to the stage's viewport rectangle but here we need a
   * position in stage coordinates.
   *
   * Also our texture size was measured in screen coordinates but we
   * want to paint the texture in actor coordinates.
   */
  scale_x = (stage_width / camera->viewport[2]);
  scale_y = (stage_height / camera->viewport[3]);

  stage_x = camera_state->viewport_x_offset * scale_x;
  stage_y = camera_state->viewport_y_offset * scale_y;

  cogl_matrix_init_identity (&modelview);
  cogl_matrix_translate (&modelview, stage_x, stage_y, 0.0f);
  cogl_matrix_scale (&modelview, scale_x, scale_y, 1);
  cogl_set_modelview_matrix (&modelview);

  /* paint the target material; this is virtualized for
   * sub-classes that require special hand-holding
   */
  clutter_offscreen_effect_paint_target (effect);

  cogl_pop_matrix ();
  cogl_set_projection_matrix (&saved_projection);
}

static void
clutter_offscreen_effect_post_paint (ClutterEffect *effect)
{
  ClutterOffscreenEffect *self = CLUTTER_OFFSCREEN_EFFECT (effect);
  ClutterOffscreenEffectPrivate *priv = self->priv;
  ClutterStage *stage =
    CLUTTER_STAGE (_clutter_actor_get_stage_internal (priv->actor));
  const ClutterCamera *camera = _clutter_stage_get_current_camera (stage);
  PerCameraState *camera_state = get_per_camera_state (self, camera->index);

  if (camera_state->offscreen == NULL ||
      camera_state->pipeline == NULL ||
      priv->actor == NULL)
    return;

  /* Restore the previous opacity override */
  _clutter_actor_set_opacity_override (priv->actor, priv->old_opacity_override);

  cogl_pop_matrix ();
  cogl_pop_framebuffer ();

  clutter_offscreen_effect_paint_texture (self);
}

static void
clutter_offscreen_effect_paint (ClutterEffect           *effect,
                                ClutterEffectPaintFlags  flags)
{
  ClutterOffscreenEffect *self = CLUTTER_OFFSCREEN_EFFECT (effect);
  ClutterOffscreenEffectPrivate *priv = self->priv;
  CoglMatrix matrix;
  ClutterStage *stage =
    CLUTTER_STAGE (_clutter_actor_get_stage_internal (priv->actor));
  const ClutterCamera *camera = _clutter_stage_get_current_camera (stage);
  PerCameraState *camera_state = get_per_camera_state (self, camera->index);

  cogl_get_modelview_matrix (&matrix);

  /* If we've already got a cached image for the same matrix and the
   * actor hasn't been redrawn then we can just use the cached image
   * in the fbo
   */
  if (camera_state->offscreen == NULL ||
      (flags & CLUTTER_EFFECT_PAINT_ACTOR_DIRTY) ||
      !cogl_matrix_equal (&matrix, &camera_state->last_matrix_drawn) ||
      camera_state->valid_for_age != camera_state->camera->age)
    {
      /* Chain up to the parent paint method which will call the pre and
         post paint functions to update the image */
      CLUTTER_EFFECT_CLASS (clutter_offscreen_effect_parent_class)->
        paint (effect, flags);
      camera_state->valid_for_age = camera_state->camera->age;
    }
  else
    clutter_offscreen_effect_paint_texture (self);
}

static void
clutter_offscreen_effect_finalize (GObject *gobject)
{
  ClutterOffscreenEffect *self = CLUTTER_OFFSCREEN_EFFECT (gobject);
  ClutterOffscreenEffectPrivate *priv = self->priv;
  int i;

  for (i = 0; i < priv->n_cameras; i++)
    {
      PerCameraState *camera_state = &priv->camera_state[i];

      if (camera_state->offscreen)
        cogl_object_unref (camera_state->offscreen);

      if (camera_state->pipeline)
        cogl_object_unref (camera_state->pipeline);

      if (camera_state->texture)
        cogl_object_unref (camera_state->texture);
    }

  if (priv->camera_state)
    {
      g_slice_free1 (sizeof (PerCameraState) * priv->n_cameras,
                     priv->camera_state);
      priv->camera_state = NULL;
      priv->n_cameras = 0;
    }

  G_OBJECT_CLASS (clutter_offscreen_effect_parent_class)->finalize (gobject);
}

static void
clutter_offscreen_effect_class_init (ClutterOffscreenEffectClass *klass)
{
  ClutterActorMetaClass *meta_class = CLUTTER_ACTOR_META_CLASS (klass);
  ClutterEffectClass *effect_class = CLUTTER_EFFECT_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (ClutterOffscreenEffectPrivate));

  klass->create_texture = clutter_offscreen_effect_real_create_texture;
  klass->paint_target = clutter_offscreen_effect_real_paint_target;

  meta_class->set_actor = clutter_offscreen_effect_set_actor;

  effect_class->pre_paint = clutter_offscreen_effect_pre_paint;
  effect_class->post_paint = clutter_offscreen_effect_post_paint;
  effect_class->paint = clutter_offscreen_effect_paint;

  gobject_class->finalize = clutter_offscreen_effect_finalize;
}

static void
clutter_offscreen_effect_init (ClutterOffscreenEffect *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                            CLUTTER_TYPE_OFFSCREEN_EFFECT,
                                            ClutterOffscreenEffectPrivate);

  self->priv->cameras_age = -1;
}

/**
 * clutter_offscreen_effect_get_texture:
 * @effect: a #ClutterOffscreenEffect
 *
 * Retrieves the texture used as a render target for the offscreen
 * buffer created by @effect
 *
 * You should only use the returned texture when painting. The texture
 * may change after ClutterEffect::pre_paint is called so the effect
 * implementation should update any references to the texture after
 * chaining-up to the parent's pre_paint implementation. This can be
 * used instead of clutter_offscreen_effect_get_target() when the
 * effect subclass wants to paint using its own material.
 *
 * Return value: (transfer none): a #CoglHandle or %COGL_INVALID_HANDLE. The
 *   returned texture is owned by Clutter and it should not be
 *   modified or freed
 *
 * Since: 1.10
 */
CoglHandle
clutter_offscreen_effect_get_texture (ClutterOffscreenEffect *effect)
{
  ClutterOffscreenEffect *self;
  ClutterOffscreenEffectPrivate *priv;
  ClutterStage *stage;
  const ClutterCamera *camera;
  PerCameraState *camera_state;

  g_return_val_if_fail (CLUTTER_IS_OFFSCREEN_EFFECT (effect),
                        NULL);

  self = CLUTTER_OFFSCREEN_EFFECT (effect);
  priv = self->priv;

  stage = CLUTTER_STAGE (_clutter_actor_get_stage_internal (priv->actor));
  camera = _clutter_stage_get_current_camera (stage);
  camera_state = get_per_camera_state (self, camera->index);

  return camera_state->texture;
}

/**
 * clutter_offscreen_effect_get_target:
 * @effect: a #ClutterOffscreenEffect
 *
 * Retrieves the material used as a render target for the offscreen
 * buffer created by @effect
 *
 * You should only use the returned #CoglMaterial when painting. The
 * returned material might change between different frames.
 *
 * Return value: (transfer none): a #CoglMaterial or %NULL. The
 *   returned material is owned by Clutter and it should not be
 *   modified or freed
 *
 * Since: 1.4
 */
CoglMaterial *
clutter_offscreen_effect_get_target (ClutterOffscreenEffect *effect)
{
  ClutterOffscreenEffect *self;
  ClutterOffscreenEffectPrivate *priv;
  ClutterStage *stage;
  const ClutterCamera *camera;
  PerCameraState *camera_state;

  g_return_val_if_fail (CLUTTER_IS_OFFSCREEN_EFFECT (effect),
                        NULL);

  self = CLUTTER_OFFSCREEN_EFFECT (effect);
  priv = self->priv;

  stage = CLUTTER_STAGE (_clutter_actor_get_stage_internal (priv->actor));
  camera = _clutter_stage_get_current_camera (stage);
  camera_state = get_per_camera_state (self, camera->index);

  return (CoglMaterial *)camera_state->pipeline;
}

/**
 * clutter_offscreen_effect_paint_target:
 * @effect: a #ClutterOffscreenEffect
 *
 * Calls the paint_target() virtual function of the @effect
 *
 * Since: 1.4
 */
void
clutter_offscreen_effect_paint_target (ClutterOffscreenEffect *effect)
{
  g_return_if_fail (CLUTTER_IS_OFFSCREEN_EFFECT (effect));

  CLUTTER_OFFSCREEN_EFFECT_GET_CLASS (effect)->paint_target (effect);
}

/**
 * clutter_offscreen_effect_create_texture:
 * @effect: a #ClutterOffscreenEffect
 * @width: the minimum width of the target texture
 * @height: the minimum height of the target texture
 *
 * Calls the create_texture() virtual function of the @effect
 *
 * Return value: (transfer full): a handle to a Cogl texture, or
 *   %COGL_INVALID_HANDLE. The returned handle has its reference
 *   count increased.
 *
 * Since: 1.4
 */
CoglHandle
clutter_offscreen_effect_create_texture (ClutterOffscreenEffect *effect,
                                         gfloat                  width,
                                         gfloat                  height)
{
  g_return_val_if_fail (CLUTTER_IS_OFFSCREEN_EFFECT (effect),
                        NULL);

  return CLUTTER_OFFSCREEN_EFFECT_GET_CLASS (effect)->create_texture (effect,
                                                                      width,
                                                                      height);
}

/**
 * clutter_offscreen_effect_get_target_size:
 * @effect: a #ClutterOffscreenEffect
 * @width: (out): return location for the target width, or %NULL
 * @height: (out): return location for the target height, or %NULL
 *
 * Retrieves the size of the offscreen buffer used by @effect to
 * paint the actor to which it has been applied.
 *
 * This function should only be called by #ClutterOffscreenEffect
 * implementations, from within the <function>paint_target()</function>
 * virtual function.
 *
 * <note>If stereoscopic rendering has been enabled then this function
 * returns the size according to the eye currently being
 * rendered.</note>
 *
 * Return value: %TRUE if the offscreen buffer has a valid size,
 *   and %FALSE otherwise
 *
 * Since: 1.8
 */
gboolean
clutter_offscreen_effect_get_target_size (ClutterOffscreenEffect *effect,
                                          gfloat                 *width,
                                          gfloat                 *height)
{
  ClutterOffscreenEffectPrivate *priv;
  ClutterStage *stage;
  const ClutterCamera *camera;
  PerCameraState *camera_state;

  g_return_val_if_fail (CLUTTER_IS_OFFSCREEN_EFFECT (effect), FALSE);

  priv = effect->priv;

  stage = CLUTTER_STAGE (_clutter_actor_get_stage_internal (priv->actor));
  camera = _clutter_stage_get_current_camera (stage);
  camera_state = get_per_camera_state (effect, camera->index);

  if (camera_state->texture == NULL)
    return FALSE;

  if (width)
    *width = cogl_texture_get_width (camera_state->texture);

  if (height)
    *height = cogl_texture_get_height (camera_state->texture);

  return TRUE;
}
