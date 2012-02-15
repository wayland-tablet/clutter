/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2010, 2012  Intel Corporation.
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
 * Author:
 *   Emmanuele Bassi <ebassi@linux.intel.com>
 *   Neil Roberts <neil@linux.intel.com>
 */

/**
 * SECTION:clutter-blur-effect
 * @short_description: A blur effect
 * @see_also: #ClutterEffect, #ClutterOffscreenEffect
 *
 * #ClutterBlurEffect is a sub-class of #ClutterEffect that allows blurring a
 * actor and its contents.
 *
 * #ClutterBlurEffect is available since Clutter 1.4
 */

#define CLUTTER_BLUR_EFFECT_CLASS(klass)        (G_TYPE_CHECK_CLASS_CAST ((klass), CLUTTER_TYPE_BLUR_EFFECT, ClutterBlurEffectClass))
#define CLUTTER_IS_BLUR_EFFECT_CLASS(klass)     (G_TYPE_CHECK_CLASS_TYPE ((klass), CLUTTER_TYPE_BLUR_EFFECT))
#define CLUTTER_BLUR_EFFECT_GET_CLASS(obj)      (G_TYPE_INSTANCE_GET_CLASS ((obj), CLUTTER_TYPE_BLUR_EFFECT, ClutterBlurEffectClass))

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define CLUTTER_ENABLE_EXPERIMENTAL_API

#include <math.h>

#include "clutter-blur-effect.h"

#include "cogl/cogl.h"

#include "clutter-debug.h"
#include "clutter-offscreen-effect.h"
#include "clutter-private.h"

#define BLUR_PADDING    2

struct _ClutterBlurEffect
{
  ClutterOffscreenEffect parent_instance;

  /* a back pointer to our actor, so that we can query it */
  ClutterActor *actor;

  gint radius;
  gfloat sigma;

  gint tex_width;
  gint tex_height;

  gboolean vertical_texture_dirty;

  CoglHandle horizontal_texture;
  CoglHandle vertical_texture;
  CoglHandle vertical_fbo;

  CoglPipeline *horizontal_pipeline;
  gint horizontal_pixel_step_uniform;
  gint horizontal_factors_uniform;
  CoglPipeline *vertical_pipeline;
  gint vertical_pixel_step_uniform;
  gint vertical_factors_uniform;
};

struct _ClutterBlurEffectClass
{
  ClutterOffscreenEffectClass parent_class;

  GHashTable *pipeline_cache;
};

enum
{
  PROP_0,

  PROP_SIGMA,

  PROP_LAST
};

static GParamSpec *obj_props[PROP_LAST];

G_DEFINE_TYPE (ClutterBlurEffect,
               clutter_blur_effect,
               CLUTTER_TYPE_OFFSCREEN_EFFECT);

static gboolean
clutter_blur_effect_pre_paint (ClutterEffect *effect)
{
  ClutterBlurEffect *self = CLUTTER_BLUR_EFFECT (effect);
  ClutterEffectClass *parent_class;

  if (!clutter_actor_meta_get_enabled (CLUTTER_ACTOR_META (effect)))
    return FALSE;

  self->actor = clutter_actor_meta_get_actor (CLUTTER_ACTOR_META (effect));
  if (self->actor == NULL)
    return FALSE;

  if (!clutter_feature_available (CLUTTER_FEATURE_SHADERS_GLSL))
    {
      /* if we don't have support for GLSL shaders then we
       * forcibly disable the ActorMeta
       */
      g_warning ("Unable to use the ShaderEffect: the graphics hardware "
                 "or the current GL driver does not implement support "
                 "for the GLSL shading language.");
      clutter_actor_meta_set_enabled (CLUTTER_ACTOR_META (effect), FALSE);
      return FALSE;
    }

  parent_class = CLUTTER_EFFECT_CLASS (clutter_blur_effect_parent_class);
  return parent_class->pre_paint (effect);
}

static CoglPipeline *
get_blur_pipeline (ClutterBlurEffectClass *klass,
                   int                     radius)
{
  CoglPipeline *pipeline;

  /* This generates a pipeline using a snippet to sample a line of
     samples with the given radius. The same snippet is used for both
     the horizontal and vertical phases. The snippet is made vertical
     by changing the direction in the 2-component ‘pixel_step’
     uniform. The samples are multiplied by some factors stored in a
     uniform array before being added together so that if the radius
     doesn't change then the effect can reuse the same snippets. */

  pipeline = g_hash_table_lookup (klass->pipeline_cache,
                                  GINT_TO_POINTER (radius));

  if (pipeline == NULL)
    {
      CoglSnippet *snippet;
      GString *source = g_string_new (NULL);
      CoglContext *ctx =
        clutter_backend_get_cogl_context (clutter_get_default_backend ());
      int i;

      g_string_append_printf (source,
                              "uniform vec2 pixel_step;\n"
                              "uniform float factors[%i];\n",
                              radius * 2 + 1);

      snippet = cogl_snippet_new (COGL_SNIPPET_HOOK_TEXTURE_LOOKUP,
                                  source->str,
                                  NULL /* post */);

      g_string_set_size (source, 0);

      pipeline = cogl_pipeline_new (ctx);
      cogl_pipeline_set_layer_null_texture (pipeline,
                                            0, /* layer_num */
                                            COGL_TEXTURE_TYPE_2D);
      cogl_pipeline_set_layer_wrap_mode (pipeline,
                                         0, /* layer_num */
                                         COGL_PIPELINE_WRAP_MODE_CLAMP_TO_EDGE);
      cogl_pipeline_set_layer_filters (pipeline,
                                       0, /* layer_num */
                                       COGL_PIPELINE_FILTER_NEAREST,
                                       COGL_PIPELINE_FILTER_NEAREST);

      for (i = 0; i < radius * 2 + 1; i++)
        {
          g_string_append (source, "cogl_texel ");

          if (i == 0)
            g_string_append (source, "=");
          else
            g_string_append (source, "+=");

          g_string_append_printf (source,
                                  " texture2D (cogl_sampler, "
                                  "cogl_tex_coord.st");
          if (i != radius)
            g_string_append_printf (source,
                                    " + pixel_step * %f",
                                    (float) (i - radius));
          g_string_append_printf (source,
                                  ") * factors[%i];\n",
                                  i);
        }

      cogl_snippet_set_replace (snippet, source->str);

      g_string_free (source, TRUE);

      cogl_pipeline_add_layer_snippet (pipeline, 0, snippet);

      cogl_object_unref (snippet);

      g_hash_table_insert (klass->pipeline_cache,
                           GINT_TO_POINTER (radius),
                           pipeline);
    }

  return pipeline;
}

static void
update_horizontal_pipeline_texture (ClutterBlurEffect *self)
{
  float pixel_step[2];

  cogl_pipeline_set_layer_texture (self->horizontal_pipeline,
                                   0, /* layer_num */
                                   self->horizontal_texture);
  pixel_step[0] = 1.0f / self->tex_width;
  pixel_step[1] = 0.0f;
  cogl_pipeline_set_uniform_float (self->horizontal_pipeline,
                                   self->horizontal_pixel_step_uniform,
                                   2, /* n_components */
                                   1, /* count */
                                   pixel_step);
}

static void
update_vertical_pipeline_texture (ClutterBlurEffect *self)
{
  float pixel_step[2];

  cogl_pipeline_set_layer_texture (self->vertical_pipeline,
                                   0, /* layer_num */
                                   self->vertical_texture);
  pixel_step[0] = 0.0f;
  pixel_step[1] = 1.0f / self->tex_height;
  cogl_pipeline_set_uniform_float (self->vertical_pipeline,
                                   self->vertical_pixel_step_uniform,
                                   2, /* n_components */
                                   1, /* count */
                                   pixel_step);
}

static void
clutter_blur_effect_post_paint (ClutterEffect *effect)
{
  ClutterBlurEffect *self = CLUTTER_BLUR_EFFECT (effect);
  ClutterEffectClass *parent_class;
  CoglHandle horizontal_texture;

  horizontal_texture =
    clutter_offscreen_effect_get_texture (CLUTTER_OFFSCREEN_EFFECT (effect));

  if (horizontal_texture != self->horizontal_texture)
    {
      if (self->horizontal_texture)
        cogl_object_unref (self->horizontal_texture);
      self->horizontal_texture = cogl_object_ref (horizontal_texture);

      self->tex_width = cogl_texture_get_width (horizontal_texture);
      self->tex_height = cogl_texture_get_height (horizontal_texture);

      update_horizontal_pipeline_texture (self);

      if (self->vertical_texture == NULL ||
          self->tex_width != cogl_texture_get_width (self->vertical_texture) ||
          self->tex_height != cogl_texture_get_height (self->vertical_texture))
        {
          if (self->vertical_texture)
            {
              cogl_object_unref (self->vertical_texture);
              cogl_object_unref (self->vertical_fbo);
            }

          self->vertical_texture =
            cogl_texture_new_with_size (self->tex_width, self->tex_height,
                                        COGL_TEXTURE_NO_SLICING,
                                        COGL_PIXEL_FORMAT_RGBA_8888_PRE);
          self->vertical_fbo =
            cogl_offscreen_new_to_texture (self->vertical_texture);

          update_vertical_pipeline_texture (self);
        }
    }

  self->vertical_texture_dirty = TRUE;

  parent_class = CLUTTER_EFFECT_CLASS (clutter_blur_effect_parent_class);
  parent_class->post_paint (effect);
}

static void
clutter_blur_effect_paint_target (ClutterOffscreenEffect *effect)
{
  ClutterBlurEffect *self = CLUTTER_BLUR_EFFECT (effect);
  guint8 paint_opacity;

  if (self->vertical_texture_dirty)
    {
      cogl_framebuffer_draw_rectangle (self->vertical_fbo,
                                       self->horizontal_pipeline,
                                       -1.0f, 1.0f, 1.0f, -1.0f);

      self->vertical_texture_dirty = FALSE;
    }

  paint_opacity = clutter_actor_get_paint_opacity (self->actor);

  cogl_pipeline_set_color4ub (self->vertical_pipeline,
                              paint_opacity,
                              paint_opacity,
                              paint_opacity,
                              paint_opacity);

  cogl_framebuffer_draw_rectangle (cogl_get_draw_framebuffer (),
                                   self->vertical_pipeline,
                                   0, 0,
                                   self->tex_width, self->tex_height);
}

static gboolean
clutter_blur_effect_get_paint_volume (ClutterEffect      *effect,
                                      ClutterPaintVolume *volume)
{
  gfloat cur_width, cur_height;
  ClutterVertex origin;

  clutter_paint_volume_get_origin (volume, &origin);
  cur_width = clutter_paint_volume_get_width (volume);
  cur_height = clutter_paint_volume_get_height (volume);

  origin.x -= BLUR_PADDING;
  origin.y -= BLUR_PADDING;
  cur_width += 2 * BLUR_PADDING;
  cur_height += 2 * BLUR_PADDING;
  clutter_paint_volume_set_origin (volume, &origin);
  clutter_paint_volume_set_width (volume, cur_width);
  clutter_paint_volume_set_height (volume, cur_height);

  return TRUE;
}

static void
clutter_blur_effect_dispose (GObject *gobject)
{
  ClutterBlurEffect *self = CLUTTER_BLUR_EFFECT (gobject);

  if (self->horizontal_pipeline != NULL)
    {
      cogl_object_unref (self->horizontal_pipeline);
      self->horizontal_pipeline = NULL;
    }

  if (self->vertical_pipeline != NULL)
    {
      cogl_object_unref (self->vertical_pipeline);
      self->vertical_pipeline = NULL;
    }

  if (self->horizontal_texture)
    {
      cogl_object_unref (self->horizontal_texture);
      self->horizontal_texture = NULL;
    }

  if (self->vertical_texture)
    {
      cogl_object_unref (self->vertical_texture);
      self->vertical_texture = NULL;
    }

  if (self->vertical_fbo)
    {
      cogl_object_unref (self->vertical_fbo);
      self->vertical_fbo = NULL;
    }

  G_OBJECT_CLASS (clutter_blur_effect_parent_class)->dispose (gobject);
}

static void
clutter_blur_effect_set_property (GObject      *gobject,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  ClutterBlurEffect *effect = CLUTTER_BLUR_EFFECT (gobject);

  switch (prop_id)
    {
    case PROP_SIGMA:
      clutter_blur_effect_set_sigma (effect,
                                     g_value_get_float (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
    }
}

static void
clutter_blur_effect_get_property (GObject      *gobject,
                                  guint         prop_id,
                                  GValue       *value,
                                  GParamSpec   *pspec)
{
  ClutterBlurEffect *effect = CLUTTER_BLUR_EFFECT (gobject);

  switch (prop_id)
    {
    case PROP_SIGMA:
      g_value_set_float (value,
                         clutter_blur_effect_get_sigma (effect));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
    }
}

static void
clutter_blur_effect_class_init (ClutterBlurEffectClass *klass)
{
  ClutterEffectClass *effect_class = CLUTTER_EFFECT_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  ClutterOffscreenEffectClass *offscreen_class;

  gobject_class->dispose = clutter_blur_effect_dispose;
  gobject_class->set_property = clutter_blur_effect_set_property;
  gobject_class->get_property = clutter_blur_effect_get_property;

  effect_class->pre_paint = clutter_blur_effect_pre_paint;
  effect_class->post_paint = clutter_blur_effect_post_paint;
  effect_class->get_paint_volume = clutter_blur_effect_get_paint_volume;

  offscreen_class = CLUTTER_OFFSCREEN_EFFECT_CLASS (klass);
  offscreen_class->paint_target = clutter_blur_effect_paint_target;


  obj_props[PROP_SIGMA] =
    g_param_spec_float ("sigma",
                        P_("Sigma"),
                        P_("The sigma value for the gaussian function"),
                        0.0, 10.0,
                        1.0,
                        CLUTTER_PARAM_READWRITE);
  g_object_class_install_properties (gobject_class,
                                     PROP_LAST,
                                     obj_props);

  klass->pipeline_cache =
    g_hash_table_new_full (g_direct_hash,
                           g_direct_equal,
                           NULL, /* key destroy notify */
                           (GDestroyNotify) cogl_object_unref);
}

static void
clutter_blur_effect_set_sigma_real (ClutterBlurEffect *self,
                                    gfloat             sigma)
{
  int radius;
  float *factors;
  float sum = 0.0f;
  int i;

  if (self->sigma == sigma)
    return;

  /* According to wikipedia, using a matrix with dimensions
     ⌈6σ⌉×⌈6σ⌉ gives good enough results in practice */
  radius = floorf (ceilf (6 * sigma) / 2.0f);

  if (self->horizontal_pipeline && radius != self->radius)
    {
      cogl_object_unref (self->horizontal_pipeline);
      self->horizontal_pipeline = NULL;
      cogl_object_unref (self->vertical_pipeline);
      self->vertical_pipeline = NULL;
    }

  if (self->horizontal_pipeline == NULL)
    {
      CoglPipeline *base_pipeline =
        get_blur_pipeline (CLUTTER_BLUR_EFFECT_GET_CLASS (self),
                           radius);

      self->horizontal_pipeline = cogl_pipeline_copy (base_pipeline);
      self->horizontal_pixel_step_uniform =
        cogl_pipeline_get_uniform_location (self->horizontal_pipeline,
                                            "pixel_step");
      self->horizontal_factors_uniform =
        cogl_pipeline_get_uniform_location (self->horizontal_pipeline,
                                            "factors");
      update_horizontal_pipeline_texture (self);

      self->vertical_pipeline = cogl_pipeline_copy (base_pipeline);
      self->vertical_pixel_step_uniform =
        cogl_pipeline_get_uniform_location (self->vertical_pipeline,
                                            "pixel_step");
      self->vertical_factors_uniform =
        cogl_pipeline_get_uniform_location (self->vertical_pipeline,
                                            "factors");
      update_vertical_pipeline_texture (self);

      /* To avoid needing to clear the vertical texture we going to
         disable blending in the horizontal pipeline and just fill it
         with the horizontal texture */
      cogl_pipeline_set_blend (self->horizontal_pipeline,
                               "RGBA = ADD (SRC_COLOR, 0)",
                               NULL);
    }

  factors = g_alloca (sizeof (float) * (radius * 2 + 1));
  /* Special case when the radius is zero to make it just draw the
     image normally */
  if (radius == 0)
    factors[0] = 1.0f;
  else
    {
      for (i = -radius; i <= radius; i++)
        factors[i + radius] = (powf (G_E, -(i * i) / (2.0f * sigma * sigma))
                               / sqrtf (2.0f * G_PI * sigma * sigma));
      /* Normalize all of the factors */
      for (i = -radius; i <= radius; i++)
        sum += factors[i + radius];
      for (i = -radius; i <= radius; i++)
        factors[i + radius] /= sum;
    }

  cogl_pipeline_set_uniform_float (self->horizontal_pipeline,
                                   self->horizontal_factors_uniform,
                                   1, /* n_components */
                                   radius * 2 + 1,
                                   factors);
  cogl_pipeline_set_uniform_float (self->vertical_pipeline,
                                   self->vertical_factors_uniform,
                                   1, /* n_components */
                                   radius * 2 + 1,
                                   factors);

  self->sigma = sigma;
  self->radius = radius;

  self->vertical_texture_dirty = TRUE;
}

void
clutter_blur_effect_set_sigma (ClutterBlurEffect *self,
                               gfloat             sigma)
{
  g_return_if_fail (CLUTTER_IS_BLUR_EFFECT (self));

  clutter_blur_effect_set_sigma_real (self, sigma);
  g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_SIGMA]);
  clutter_effect_queue_repaint (CLUTTER_EFFECT (self));
}

gfloat
clutter_blur_effect_get_sigma (ClutterBlurEffect *self)
{
  g_return_val_if_fail (CLUTTER_IS_BLUR_EFFECT (self), 0.0f);

  return self->sigma;
}

static void
clutter_blur_effect_init (ClutterBlurEffect *self)
{
  clutter_blur_effect_set_sigma_real (self, 0.84089642f);
}

/**
 * clutter_blur_effect_new:
 *
 * Creates a new #ClutterBlurEffect to be used with
 * clutter_actor_add_effect()
 *
 * Return value: the newly created #ClutterBlurEffect or %NULL
 *
 * Since: 1.4
 */
ClutterEffect *
clutter_blur_effect_new (void)
{
  return g_object_new (CLUTTER_TYPE_BLUR_EFFECT, NULL);
}
