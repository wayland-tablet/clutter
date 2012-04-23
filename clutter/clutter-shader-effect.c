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
 * Author:
 *   Emmanuele Bassi <ebassi@linux.intel.com>
 *   Neil Roberts <neil@linux.intel.com>
 */

/**
 * SECTION:clutter-shader-effect
 * @short_description: Base class for shader effects
 * @See_Also: #ClutterEffect, #ClutterOffscreenEffect
 *
 * #ClutterShaderEffect is a class that implements all the plumbing for
 * creating #ClutterEffect<!-- -->s using GLSL shaders.
 *
 * #ClutterShaderEffect creates an offscreen buffer and then applies the
 * GLSL shader (after checking whether the compilation and linking were
 * successfull) to the buffer before painting it on screen.
 *
 * <refsect2 id="ClutterShaderEffect-implementing">
 *   <title>Implementing a ClutterShaderEffect</title>
 *   <para>Creating a sub-class of #ClutterShaderEffect requires the
 *   overriding of the #ClutterOffscreenEffectClass.paint_target() virtual
 *   function from the #ClutterOffscreenEffect class as well as the
 *   <function>get_static_shader_source()</function> virtual from the
 *   #ClutterShaderEffect class.</para>
 *   <para>The #ClutterShaderEffectClass.get_static_shader_source()
 *   function should return a copy of the shader source to use. This
 *   function is only called once per subclass of #ClutterShaderEffect
 *   regardless of how many instances of the effect are created. The
 *   source for the shader is typically stored in a static const
 *   string which is returned from this function via
 *   g_strdup().</para>
 *   <para>The <function>paint_target()</function> should set the
 *   shader's uniforms if any. This is done by calling
 *   clutter_shader_effect_set_uniform_value() or
 *   clutter_shader_effect_set_uniform(). The sub-class should then
 *   chain up to the #ClutterShaderEffect implementation.</para>
 *   <example id="ClutterShaderEffect-example-uniforms">
 *     <title>Setting uniforms on a ClutterShaderEffect</title>
 *     <para>The example below shows a typical implementation of the
 *     <function>get_static_shader_source()</function> and
 *     <function>paint_target()</function> phases of a
 *     #ClutterShaderEffect sub-class.</para>
 *     <programlisting>
 *  static gchar *
 *  my_effect_get_static_shader_source (ClutterShaderEffect *effect)
 *  {
 *    return g_strdup (shader_source);
 *  }
 *
 *  static gboolean
 *  my_effect_paint_target (ClutterOffscreenEffect *effect)
 *  {
 *    MyEffect *self = MY_EFFECT (effect);
 *    ClutterShaderEffect *shader = CLUTTER_SHADER_EFFECT (effect);
 *    ClutterEffectClass *parent_class;
 *    gfloat component_r, component_g, component_b;
 *
 *    /&ast; the "tex" uniform is declared in the shader as:
 *     &ast;
 *     &ast;   uniform int tex;
 *     &ast;
 *     &ast; and it is passed a constant value of 0
 *     &ast;/
 *    clutter_shader_effect_set_uniform (shader, "tex", G_TYPE_INT, 1, 0);
 *
 *    /&ast; the "component" uniform is declared in the shader as:
 *     &ast;
 *     &ast;   uniform vec3 component;
 *     &ast;
 *     &ast; and it's defined to contain the normalized components
 *     &ast; of a #ClutterColor
 *     &ast;/
 *    component_r = self->color.red   / 255.0f;
 *    component_g = self->color.green / 255.0f;
 *    component_b = self->color.blue  / 255.0f;
 *    clutter_shader_effect_set_uniform (shader, "component",
 *                                       G_TYPE_FLOAT, 3,
 *                                       component_r,
 *                                       component_g,
 *                                       component_b);
 *
 *    /&ast; chain up to the parent's implementation &ast;/
 *    parent_class = CLUTTER_OFFSCREEN_EFFECT_CLASS (my_effect_parent_class);
 *    return parent_class->paint_target (effect);
 *  }
 *     </programlisting>
 *   </example>
 * </refsect2>
 *
 * #ClutterShaderEffect is available since Clutter 1.4
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define CLUTTER_ENABLE_EXPERIMENTAL_API
/* XXX: This file depends on the cogl_program_ api with has been
 * removed for Cogl 2.0 so we undef COGL_ENABLE_EXPERIMENTAL_2_0_API
 * for this file for now */
#undef COGL_ENABLE_EXPERIMENTAL_2_0_API
#include "cogl/cogl.h"

#include "clutter-shader-effect.h"

#include "clutter-debug.h"
#include "clutter-enum-types.h"
#include "clutter-feature.h"
#include "clutter-private.h"
#include "clutter-shader-types.h"

struct _ClutterShaderEffectPrivate
{
  ClutterActor *actor;

  ClutterShaderType shader_type;

  /* The pipeline to use to render the texture. This will be created
   * either when get_target is first called, when the shader source is
   * set or when a uniform is first set. If it is created when a
   * uniform is set then it won't get a snippet attached until
   * something sets the source */
  CoglPipeline *pipeline;
  /* The snippet that was attached to the pipeline. This will be NULL
   * if no snippet has been attached to the pipeline yet. This will
   * happen if a uniform is set before setting the source */
  CoglSnippet *snippet;

#ifndef CLUTTER_COGL2
  /* A CoglShader containing the same source as the snippet. This is
   * created on demand only if clutter_shader_effect_get_program is
   * called. */
  CoglHandle shader;
  /* A CoglProgram containing the above shader. This is also only
   * created on demand */
  CoglHandle program;
#endif /* CLUTTER_COGL2 */
};

typedef struct _ClutterShaderEffectClassPrivate
{
  /* This is a per-class base pipeline with the snippet attached so
     that Cogl can attach the pre-compiled program to it. This is only
     used when the class implements get_static_shader_source without
     calling set_shader_source. It will be shared by all instances of
     this class */
  CoglPipeline *base_pipeline;
  /* When base_pipeline is not NULL, this will be a reference to the
   * shader snippet that was used to create the pipeline */
  CoglSnippet *base_snippet;
} ClutterShaderEffectClassPrivate;

enum
{
  PROP_0,

  PROP_SHADER_TYPE,

  PROP_LAST
};

static GParamSpec *obj_props[PROP_LAST];

G_DEFINE_TYPE_WITH_CODE (ClutterShaderEffect,
                         clutter_shader_effect,
                         CLUTTER_TYPE_OFFSCREEN_EFFECT,
                         g_type_add_class_private (g_define_type_id,
                                                   sizeof (ClutterShaderEffectClassPrivate)))

static void
clutter_shader_effect_set_actor (ClutterActorMeta *meta,
                                 ClutterActor     *actor)
{
  ClutterShaderEffect *self = CLUTTER_SHADER_EFFECT (meta);
  ClutterShaderEffectPrivate *priv = self->priv;
  ClutterActorMetaClass *parent;

  if (!clutter_feature_available (CLUTTER_FEATURE_SHADERS_GLSL))
    {
      /* if we don't have support for GLSL shaders then we
       * forcibly disable the ActorMeta
       */
      g_warning ("Unable to use the ShaderEffect: the graphics hardware "
                 "or the current GL driver does not implement support "
                 "for the GLSL shading language.");
      clutter_actor_meta_set_enabled (meta, FALSE);
      return;
    }

  parent = CLUTTER_ACTOR_META_CLASS (clutter_shader_effect_parent_class);
  parent->set_actor (meta, actor);

  /* we keep a back pointer here */
  priv->actor = clutter_actor_meta_get_actor (meta);
  if (priv->actor == NULL)
    return;

  CLUTTER_NOTE (SHADER, "Preparing shader effect of type '%s'",
                G_OBJECT_TYPE_NAME (meta));
}

static CoglSnippet *
clutter_shader_effect_create_snippet (ClutterShaderEffect *self,
                                      const char *source)
{
  ClutterShaderEffectPrivate *priv = self->priv;
  CoglSnippet *snippet;
  char *wrapped_source;

  switch (priv->shader_type)
    {
    case CLUTTER_FRAGMENT_SHADER:
      snippet = cogl_snippet_new (COGL_SNIPPET_HOOK_FRAGMENT,
                                  NULL /* declarations */,
                                  NULL /* post */);
      break;

    case CLUTTER_VERTEX_SHADER:
      snippet = cogl_snippet_new (COGL_SNIPPET_HOOK_VERTEX,
                                  NULL /* declarations */,
                                  NULL /* post */);
      break;

    default:
      g_assert_not_reached ();
    }

  /* The snippet can't define a 'main' function so we need to redefine
   * the application's main function to something else */
  wrapped_source = g_strconcat ("#define main clutter_shader_effect_main\n",
                                source,
                                "\n#undef main\n",
                                NULL);
  cogl_snippet_set_declarations (snippet, wrapped_source);
  g_free (wrapped_source);

  cogl_snippet_set_replace (snippet, "clutter_shader_effect_main ();\n");

  return snippet;
}

static void
clutter_shader_effect_try_static_source (ClutterShaderEffect *self)
{
  ClutterShaderEffectPrivate *priv = self->priv;
  ClutterShaderEffectClass *shader_effect_class =
    CLUTTER_SHADER_EFFECT_GET_CLASS (self);

  if (shader_effect_class->get_static_shader_source != NULL)
    {
      ClutterShaderEffectClassPrivate *class_priv;

      class_priv =
        G_TYPE_CLASS_GET_PRIVATE (shader_effect_class,
                                  CLUTTER_TYPE_SHADER_EFFECT,
                                  ClutterShaderEffectClassPrivate);

      if (class_priv->base_pipeline == NULL)
        {
          ClutterBackend *backend = clutter_get_default_backend ();
          CoglContext *cogl_context =
            clutter_backend_get_cogl_context (backend);
          gchar *source;

          source = shader_effect_class->get_static_shader_source (self);

          class_priv->base_snippet =
            clutter_shader_effect_create_snippet (self, source);

          g_free (source);

          class_priv->base_pipeline = cogl_pipeline_new (cogl_context);

          cogl_pipeline_add_snippet (class_priv->base_pipeline,
                                     class_priv->base_snippet);

          cogl_pipeline_set_layer_null_texture (class_priv->base_pipeline,
                                                0, /* layer_index */
                                                COGL_TEXTURE_TYPE_2D);
        }

      /* There might already be a pipeline if some uniforms were set
       * on the effect first. In that case we'll create a new pipeline
       * and share just the snippet. This should still end up using
       * the same GL program as other instances because Cogl will be
       * able to recognise that the same snippet is used */
      if (priv->pipeline)
        cogl_pipeline_add_snippet (priv->pipeline,
                                   class_priv->base_snippet);
      else
        priv->pipeline = cogl_pipeline_copy (class_priv->base_pipeline);

      priv->snippet = cogl_object_ref (class_priv->base_snippet);
    }
}

static CoglMaterial *
clutter_shader_effect_get_target (ClutterOffscreenEffect *effect)
{
  ClutterShaderEffect *self = CLUTTER_SHADER_EFFECT (effect);
  ClutterShaderEffectPrivate *priv = self->priv;

  /* If the source hasn't been set yet then we'll try to get it from
     the static source instead */
  if (priv->snippet == NULL)
    clutter_shader_effect_try_static_source (self);

  if (priv->snippet == NULL)
    {
      ClutterOffscreenEffectClass *parent;
      parent =
        CLUTTER_OFFSCREEN_EFFECT_CLASS (clutter_shader_effect_parent_class);
      return parent->get_target (effect);
    }
  else
    {
      CoglTexture *texture = clutter_offscreen_effect_get_texture (effect);
      cogl_pipeline_set_layer_texture (priv->pipeline, 0, texture);

      return (CoglMaterial *) priv->pipeline;
    }
}

static void
clutter_shader_effect_set_property (GObject      *gobject,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
  ClutterShaderEffectPrivate *priv = CLUTTER_SHADER_EFFECT (gobject)->priv;

  switch (prop_id)
    {
    case PROP_SHADER_TYPE:
      priv->shader_type = g_value_get_enum (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
    }
}

static void
clutter_shader_effect_finalize (GObject *gobject)
{
  ClutterShaderEffect *effect = CLUTTER_SHADER_EFFECT (gobject);
  ClutterShaderEffectPrivate *priv = effect->priv;

  if (priv->pipeline != NULL)
    {
      cogl_object_unref (priv->pipeline);

      priv->pipeline = NULL;
    }

  if (priv->snippet != NULL)
    {
      cogl_object_unref (priv->snippet);

      priv->snippet = NULL;
    }

#ifndef CLUTTER_COGL2
  if (priv->shader != COGL_INVALID_HANDLE)
    {
      cogl_handle_unref (priv->shader);

      priv->shader = COGL_INVALID_HANDLE;
    }

  if (priv->program != COGL_INVALID_HANDLE)
    {
      cogl_handle_unref (priv->program);

      priv->program = COGL_INVALID_HANDLE;
    }
#endif /* CLUTTER_COGL2 */

  G_OBJECT_CLASS (clutter_shader_effect_parent_class)->finalize (gobject);
}

static void
clutter_shader_effect_class_init (ClutterShaderEffectClass *klass)
{
  ClutterActorMetaClass *meta_class = CLUTTER_ACTOR_META_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  ClutterOffscreenEffectClass *offscreen_class;

  offscreen_class = CLUTTER_OFFSCREEN_EFFECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (ClutterShaderEffectPrivate));

  /**
   * ClutterShaderEffect:shader-type:
   *
   * The type of shader that is used by the effect. This property
   * should be set by the constructor of #ClutterShaderEffect
   * sub-classes.
   *
   * Since: 1.4
   */
  obj_props[PROP_SHADER_TYPE] =
    g_param_spec_enum ("shader-type",
                       P_("Shader Type"),
                       P_("The type of shader used"),
                       CLUTTER_TYPE_SHADER_TYPE,
                       CLUTTER_FRAGMENT_SHADER,
                       CLUTTER_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY);

  gobject_class->set_property = clutter_shader_effect_set_property;
  gobject_class->finalize = clutter_shader_effect_finalize;
  g_object_class_install_properties (gobject_class,
                                     PROP_LAST,
                                     obj_props);

  meta_class->set_actor = clutter_shader_effect_set_actor;

  offscreen_class->get_target = clutter_shader_effect_get_target;
}

static void
clutter_shader_effect_init (ClutterShaderEffect *effect)
{
  effect->priv = G_TYPE_INSTANCE_GET_PRIVATE (effect,
                                              CLUTTER_TYPE_SHADER_EFFECT,
                                              ClutterShaderEffectPrivate);
}

/**
 * clutter_shader_effect_new:
 * @shader_type: the type of the shader, either %CLUTTER_FRAGMENT_SHADER,
 *   or %CLUTTER_VERTEX_SHADER
 *
 * Creates a new #ClutterShaderEffect, to be applied to an actor using
 * clutter_actor_add_effect().
 *
 * The effect will be empty until clutter_shader_effect_set_shader_source()
 * is called.
 *
 * Return value: (transfer full): the newly created #ClutterShaderEffect.
 *   Use g_object_unref() when done.
 *
 * Since: 1.8
 */
ClutterEffect *
clutter_shader_effect_new (ClutterShaderType shader_type)
{
  return g_object_new (CLUTTER_TYPE_SHADER_EFFECT,
                       "shader-type", shader_type,
                       NULL);
}

#ifndef CLUTTER_COGL2

/**
 * clutter_shader_effect_get_shader:
 * @effect: a #ClutterShaderEffect
 *
 * Retrieves a pointer to the shader's handle
 *
 * Return value: (transfer none): a pointer to the shader's handle,
 *   or %COGL_INVALID_HANDLE
 *
 * Since: 1.4
 */
CoglHandle
clutter_shader_effect_get_shader (ClutterShaderEffect *effect)
{
  ClutterShaderEffectPrivate *priv;
  char *source;

  g_return_val_if_fail (CLUTTER_IS_SHADER_EFFECT (effect),
                        COGL_INVALID_HANDLE);

  priv = effect->priv;

  if (priv->shader != COGL_INVALID_HANDLE)
    return priv->shader;

  if (priv->snippet == NULL)
    /* Source not set yet */
    return COGL_INVALID_HANDLE;

  switch (priv->shader_type)
    {
    case CLUTTER_FRAGMENT_SHADER:
      priv->shader = cogl_create_shader (COGL_SHADER_TYPE_FRAGMENT);
      break;

    case CLUTTER_VERTEX_SHADER:
      priv->shader = cogl_create_shader (COGL_SHADER_TYPE_VERTEX);
      break;

    default:
      g_assert_not_reached ();
    }

  /* The source in the snippet is wrapped to replace the main
   * function. To convert it back to a full shader source we need to
   * add some extra code to call the wrapped main function */
  source = g_strconcat (cogl_snippet_get_declarations (priv->snippet),
                        "\n"
                        "void\n"
                        "main ()\n"
                        "{\n"
                        "  clutter_shader_effect_main ();\n"
                        "}\n",
                        NULL);

  cogl_shader_source (priv->shader, source);

  g_free (source);

  cogl_shader_compile (priv->shader);

  if (!cogl_shader_is_compiled (priv->shader))
    {
      gchar *log_buf = cogl_shader_get_info_log (priv->shader);
      g_warning (G_STRLOC ": Unable to compile the GLSL shader: %s", log_buf);
      g_free (log_buf);

      cogl_handle_unref (priv->shader);
      priv->shader = NULL;
    }

  return priv->shader;
}

/**
 * clutter_shader_effect_get_program:
 * @effect: a #ClutterShaderEffect
 *
 * Retrieves a pointer to the program's handle
 *
 * Return value: (transfer none): a pointer to the program's handle,
 *   or %COGL_INVALID_HANDLE
 *
 * Since: 1.4
 */
CoglHandle
clutter_shader_effect_get_program (ClutterShaderEffect *effect)
{
  ClutterShaderEffectPrivate *priv;
  CoglHandle shader;

  g_return_val_if_fail (CLUTTER_IS_SHADER_EFFECT (effect),
                        COGL_INVALID_HANDLE);

  priv = effect->priv;

  if (priv->program != COGL_INVALID_HANDLE)
    return priv->program;

  shader = clutter_shader_effect_get_shader (effect);

  if (shader == COGL_INVALID_HANDLE)
    return COGL_INVALID_HANDLE;

  priv->program = cogl_create_program ();
  cogl_program_attach_shader (priv->program, shader);
  cogl_program_link (priv->program);

  return priv->program;
}

#endif /* CLUTTER_COGL2 */

static void
clutter_shader_effect_ensure_pipeline (ClutterShaderEffect *effect)
{
  ClutterShaderEffectPrivate *priv = effect->priv;

  if (priv->pipeline == NULL)
    {
      CoglContext *context =
        clutter_backend_get_cogl_context (clutter_get_default_backend ());

      priv->pipeline = cogl_pipeline_new (context);
    }
}

static inline void
clutter_shader_effect_add_uniform (ClutterShaderEffect *effect,
                                   const gchar         *name,
                                   const GValue        *value)
{
  ClutterShaderEffectPrivate *priv = effect->priv;
  int uniform_location;
  gsize size;

  clutter_shader_effect_ensure_pipeline (effect);

  uniform_location = cogl_pipeline_get_uniform_location (priv->pipeline, name);

  if (CLUTTER_VALUE_HOLDS_SHADER_FLOAT (value))
    {
      const float *floats;

      floats = clutter_value_get_shader_float (value, &size);
      cogl_pipeline_set_uniform_float (priv->pipeline, uniform_location,
                                       size, 1,
                                       floats);
    }
  else if (CLUTTER_VALUE_HOLDS_SHADER_INT (value))
    {
      const int *ints;

      ints = clutter_value_get_shader_int (value, &size);
      cogl_pipeline_set_uniform_int (priv->pipeline, uniform_location,
                                     size, 1,
                                     ints);
    }
  else if (CLUTTER_VALUE_HOLDS_SHADER_MATRIX (value))
    {
      const float *matrix;

      matrix = clutter_value_get_shader_matrix (value, &size);
      cogl_pipeline_set_uniform_matrix (priv->pipeline, uniform_location,
                                        size, 1,
                                        FALSE,
                                        matrix);
    }
  else if (G_VALUE_HOLDS_FLOAT (value))
    {
      const float float_val = g_value_get_float (value);

      cogl_pipeline_set_uniform_float (priv->pipeline, uniform_location,
                                       1, 1,
                                       &float_val);
    }
  else if (G_VALUE_HOLDS_DOUBLE (value))
    {
      const float float_val =
        (float) g_value_get_double (value);

      cogl_pipeline_set_uniform_float (priv->pipeline, uniform_location,
                                       1, 1,
                                       &float_val);
    }
  else if (G_VALUE_HOLDS_INT (value))
    {
      const int int_val = g_value_get_int (value);

      cogl_pipeline_set_uniform_int (priv->pipeline, uniform_location,
                                     1, 1,
                                     &int_val);
    }
  else
    g_warning ("Invalid uniform of type '%s' for name '%s'",
               g_type_name (G_VALUE_TYPE (value)),
               name);

  if (priv->actor != NULL && !CLUTTER_ACTOR_IN_PAINT (priv->actor))
    clutter_effect_queue_repaint (CLUTTER_EFFECT (effect));
}

/**
 * clutter_shader_effect_set_uniform_value:
 * @effect: a #ClutterShaderEffect
 * @name: the name of the uniform to set
 * @value: a #GValue with the value of the uniform to set
 *
 * Sets @value as the payload for the uniform @name inside the shader
 * effect
 *
 * The #GType of the @value must be one of: %G_TYPE_INT, for a single
 * integer value; %G_TYPE_FLOAT, for a single floating point value;
 * %CLUTTER_TYPE_SHADER_INT, for an array of integer values;
 * %CLUTTER_TYPE_SHADER_FLOAT, for an array of floating point values;
 * and %CLUTTER_TYPE_SHADER_MATRIX, for a matrix of floating point
 * values. It also accepts %G_TYPE_DOUBLE for compatibility with other
 * languages than C.
 *
 * Since: 1.4
 */
void
clutter_shader_effect_set_uniform_value (ClutterShaderEffect *effect,
                                         const gchar         *name,
                                         const GValue        *value)
{
  g_return_if_fail (CLUTTER_IS_SHADER_EFFECT (effect));
  g_return_if_fail (name != NULL);
  g_return_if_fail (value != NULL);

  clutter_shader_effect_add_uniform (effect, name, value);
}

static void
clutter_shader_effect_set_uniform_valist (ClutterShaderEffect *effect,
                                          const gchar         *name,
                                          GType                value_type,
                                          gsize                n_values,
                                          va_list             *args)
{
  GValue value = G_VALUE_INIT;

  if (value_type == CLUTTER_TYPE_SHADER_INT)
    {
      gint *int_values = va_arg (*args, gint*);

      g_value_init (&value, CLUTTER_TYPE_SHADER_INT);
      clutter_value_set_shader_int (&value, n_values, int_values);

      goto add_uniform;
    }

  if (value_type == CLUTTER_TYPE_SHADER_FLOAT)
    {
      gfloat *float_values = va_arg (*args, gfloat*);

      g_value_init (&value, CLUTTER_TYPE_SHADER_FLOAT);
      clutter_value_set_shader_float (&value, n_values, float_values);

      goto add_uniform;
    }

  if (value_type == CLUTTER_TYPE_SHADER_MATRIX)
    {
      gfloat *float_values = va_arg (*args, gfloat*);

      g_value_init (&value, CLUTTER_TYPE_SHADER_MATRIX);
      clutter_value_set_shader_matrix (&value, n_values, float_values);

      goto add_uniform;
    }

  if (value_type == G_TYPE_INT)
    {
      g_return_if_fail (n_values <= 4);

      /* if we only have one value we can go through the fast path
       * of using G_TYPE_INT, otherwise we create a vector of integers
       * from the passed values
       */
      if (n_values == 1)
        {
          gint int_val = va_arg (*args, gint);

          g_value_init (&value, G_TYPE_INT);
          g_value_set_int (&value, int_val);
        }
      else
        {
          gint *int_values = g_new (gint, n_values);
          gint i;

          for (i = 0; i < n_values; i++)
            int_values[i] = va_arg (*args, gint);

          g_value_init (&value, CLUTTER_TYPE_SHADER_INT);
          clutter_value_set_shader_int (&value, n_values, int_values);

          g_free (int_values);
        }

      goto add_uniform;
    }

  if (value_type == G_TYPE_FLOAT)
    {
      g_return_if_fail (n_values <= 4);

      /* if we only have one value we can go through the fast path
       * of using G_TYPE_FLOAT, otherwise we create a vector of floats
       * from the passed values
       */
      if (n_values == 1)
        {
          gfloat float_val = (gfloat) va_arg (*args, gdouble);

          g_value_init (&value, G_TYPE_FLOAT);
          g_value_set_float (&value, float_val);
        }
      else
        {
          gfloat *float_values = g_new (gfloat, n_values);
          gint i;

          for (i = 0; i < n_values; i++)
            float_values[i] = (gfloat) va_arg (*args, double);

          g_value_init (&value, CLUTTER_TYPE_SHADER_FLOAT);
          clutter_value_set_shader_float (&value, n_values, float_values);

          g_free (float_values);
        }

      goto add_uniform;
    }

  g_warning ("Unrecognized type '%s' (values: %d) for uniform name '%s'",
             g_type_name (value_type),
             (int) n_values,
             name);
  return;

add_uniform:
  clutter_shader_effect_add_uniform (effect, name, &value);
  g_value_unset (&value);
}

/**
 * clutter_shader_effect_set_uniform:
 * @effect: a #ClutterShaderEffect
 * @name: the name of the uniform to set
 * @gtype: the type of the uniform to set
 * @n_values: the number of values
 * @...: a list of values
 *
 * Sets a list of values as the payload for the uniform @name inside
 * the shader effect
 *
 * The @gtype must be one of: %G_TYPE_INT, for 1 or more integer values;
 * %G_TYPE_FLOAT, for 1 or more floating point values;
 * %CLUTTER_TYPE_SHADER_INT, for a pointer to an array of integer values;
 * %CLUTTER_TYPE_SHADER_FLOAT, for a pointer to an array of floating point
 * values; and %CLUTTER_TYPE_SHADER_MATRIX, for a pointer to an array of
 * floating point values mapping a matrix
 *
 * The number of values interepreted is defined by the @n_value
 * argument, and by the @gtype argument. For instance, a uniform named
 * "sampler0" and containing a single integer value is set using:
 *
 * |[
 *   clutter_shader_effect_set_uniform (effect, "sampler0",
 *                                      G_TYPE_INT, 1,
 *                                      0);
 * ]|
 *
 * While a uniform named "components" and containing a 3-elements vector
 * of floating point values (a "vec3") can be set using:
 *
 * |[
 *   gfloat component_r, component_g, component_b;
 *
 *   clutter_shader_effect_set_uniform (effect, "components",
 *                                      G_TYPE_FLOAT, 3,
 *                                      component_r,
 *                                      component_g,
 *                                      component_b);
 * ]|
 *
 * or can be set using:
 *
 * |[
 *   gfloat component_vec[3];
 *
 *   clutter_shader_effect_set_uniform (effect, "components",
 *                                      CLUTTER_TYPE_SHADER_FLOAT, 3,
 *                                      component_vec);
 * ]|
 *
 * Finally, a uniform named "map" and containing a matrix can be set using:
 *
 * |[
 *   clutter_shader_effect_set_uniform (effect, "map",
 *                                      CLUTTER_TYPE_SHADER_MATRIX, 1,
 *                                      cogl_matrix_get_array (&matrix));
 * ]|
 *
 * Since: 1.4
 */
void
clutter_shader_effect_set_uniform (ClutterShaderEffect *effect,
                                   const gchar         *name,
                                   GType                gtype,
                                   gsize                n_values,
                                   ...)
{
  va_list args;

  g_return_if_fail (CLUTTER_IS_SHADER_EFFECT (effect));
  g_return_if_fail (name != NULL);
  g_return_if_fail (gtype != G_TYPE_INVALID);
  g_return_if_fail (n_values > 0);

  va_start (args, n_values);
  clutter_shader_effect_set_uniform_valist (effect, name,
                                            gtype,
                                            n_values,
                                            &args);
  va_end (args);
}

/**
 * clutter_shader_effect_set_shader_source:
 * @effect: a #ClutterShaderEffect
 * @source: the source of a GLSL shader
 *
 * Sets the source of the GLSL shader used by @effect
 *
 * This function should only be called by implementations of
 * the #ClutterShaderEffect class, and not by application code.
 *
 * This function can only be called once; subsequent calls will
 * yield no result.
 *
 * Return value: %TRUE if the source was set
 *
 * Since: 1.4
 */
gboolean
clutter_shader_effect_set_shader_source (ClutterShaderEffect *effect,
                                         const gchar         *source)
{
  ClutterShaderEffectPrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_SHADER_EFFECT (effect), FALSE);
  g_return_val_if_fail (source != NULL && *source != '\0', FALSE);

  priv = effect->priv;

  if (priv->snippet != NULL)
    return TRUE;

  clutter_shader_effect_ensure_pipeline (effect);

  priv->snippet = clutter_shader_effect_create_snippet (effect, source);

  cogl_pipeline_add_snippet (priv->pipeline, priv->snippet);

  return TRUE;
}
