#define COGL_ENABLE_EXPERIMENTAL_2_0_API
#include <clutter/clutter.h>
#include <clutter/wayland/clutter-wayland-compositor.h>
#include <clutter/wayland/clutter-wayland-surface.h>

#include <glib.h>
#include <sys/time.h>
#include <string.h>

#include <wayland-server.h>

typedef struct _TWSCompositor TWSCompositor;

typedef struct
{
  struct wl_buffer *wayland_buffer;
  GList *surfaces_attached_to;
} TWSBuffer;

typedef struct
{
  TWSCompositor *compositor;
  struct wl_surface wayland_surface;
  int x;
  int y;
  TWSBuffer *buffer;
  ClutterActor *actor;
} TWSSurface;

typedef struct
{
  struct wl_object wayland_output;
  int x;
  int y;
  int width;
  int height;
  /* XXX: with sliced stages we'd reference a CoglFramebuffer here. */
} TWSOutput;

typedef struct
{
  GSource source;
  GPollFD pfd;
  struct wl_event_loop *loop;
} WaylandEventSource;

struct _TWSCompositor
{
  struct wl_display *wayland_display;
  struct wl_compositor wayland_compositor;
  struct wl_shm *wayland_shm;
  struct wl_event_loop *wayland_loop;
  ClutterActor *stage;
  GList *outputs;
  GSource *wayland_event_source;
  GList *surfaces;
};

static guint32
get_time (void)
{
  struct timeval tv;
  gettimeofday (&tv, NULL);
  return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

static gboolean
wayland_event_source_prepare (GSource *base, int *timeout)
{
  *timeout = -1;
  return FALSE;
}

static gboolean
wayland_event_source_check (GSource *base)
{
  WaylandEventSource *source = (WaylandEventSource *)base;
  return source->pfd.revents;
}

static gboolean
wayland_event_source_dispatch (GSource *base,
                               GSourceFunc callback,
                               void *data)
{
  WaylandEventSource *source = (WaylandEventSource *)base;
  wl_event_loop_dispatch (source->loop, 0);
  return TRUE;
}

static GSourceFuncs wayland_event_source_funcs =
{
  wayland_event_source_prepare,
  wayland_event_source_check,
  wayland_event_source_dispatch,
  NULL
};

GSource *
wayland_event_source_new (struct wl_event_loop *loop)
{
  WaylandEventSource *source;

  source = (WaylandEventSource *) g_source_new (&wayland_event_source_funcs,
                                                sizeof (WaylandEventSource));
  source->loop = loop;
  source->pfd.fd = wl_event_loop_get_fd (loop);
  source->pfd.events = G_IO_IN | G_IO_ERR;
  g_source_add_poll (&source->source, &source->pfd);

  return &source->source;
}

static TWSBuffer *
tws_buffer_new (struct wl_buffer *wayland_buffer)
{
  TWSBuffer *buffer = g_slice_new (TWSBuffer);

  buffer->wayland_buffer = wayland_buffer;
  buffer->surfaces_attached_to = NULL;

  return buffer;
}

static void
tws_buffer_free (TWSBuffer *buffer)
{
  GList *l;

  buffer->wayland_buffer->user_data = NULL;

  for (l = buffer->surfaces_attached_to; l; l = l->next)
    {
      TWSSurface *surface = l->data;
      surface->buffer = NULL;
    }

  g_list_free (buffer->surfaces_attached_to);
  g_slice_free (TWSBuffer, buffer);
}

static void
shm_buffer_created (struct wl_buffer *wayland_buffer)
{
  wayland_buffer->user_data = tws_buffer_new (wayland_buffer);
}

static void
shm_buffer_damaged (struct wl_buffer *wayland_buffer,
		    gint32 x,
                    gint32 y,
                    gint32 width,
                    gint32 height)
{
  TWSBuffer *buffer = wayland_buffer->user_data;
  GList *l;

  for (l = buffer->surfaces_attached_to; l; l = l->next)
    {
      TWSSurface *surface = l->data;
      ClutterWaylandSurface *surface_actor =
        CLUTTER_WAYLAND_SURFACE (surface->actor);
      clutter_wayland_surface_damage_buffer (surface_actor,
                                             wayland_buffer,
                                             x, y, width, height);
    }
}

static void
shm_buffer_destroyed (struct wl_buffer *wayland_buffer)
{
  if (wayland_buffer->user_data)
    tws_buffer_free ((TWSBuffer *)wayland_buffer->user_data);
}

const static struct wl_shm_callbacks shm_callbacks = {
  shm_buffer_created,
  shm_buffer_damaged,
  shm_buffer_destroyed
};

static void
tws_surface_destroy (struct wl_client *wayland_client,
                     struct wl_surface *wayland_surface)
{
  wl_resource_destroy (&wayland_surface->resource, wayland_client, get_time ());
}

static void
tws_surface_detach_buffer (TWSSurface *surface)
{
  TWSBuffer *buffer = surface->buffer;

  if (buffer)
    {
      buffer->surfaces_attached_to =
        g_list_remove (buffer->surfaces_attached_to, surface);
      if (buffer->surfaces_attached_to == NULL)
        tws_buffer_free (buffer);
      surface->buffer = NULL;
    }
}

static void
tws_surface_attach_buffer (struct wl_client *wayland_client,
                           struct wl_surface *wayland_surface,
                           struct wl_buffer *wayland_buffer,
                           gint32 dx, gint32 dy)
{
  TWSBuffer *buffer = wayland_buffer->user_data;
  TWSSurface *surface =
    container_of (wayland_surface, TWSSurface, wayland_surface);
  TWSCompositor *compositor = surface->compositor;
  ClutterWaylandSurface *surface_actor;

  tws_surface_detach_buffer (surface);

  /* XXX: we will have been notified of shm buffers already via the
   * callbacks, but this will be the first we know of drm buffers */
  if (!buffer)
    {
      buffer = tws_buffer_new (wayland_buffer);
      wayland_buffer->user_data = buffer;
    }

  /* wayland-drm.c: drm_create_buffer doesn't fill this in for us...*/
  if (!wayland_buffer->compositor)
    wayland_buffer->compositor = &compositor->wayland_compositor;

  g_return_if_fail (g_list_find (buffer->surfaces_attached_to, surface) == NULL);

  buffer->surfaces_attached_to = g_list_prepend (buffer->surfaces_attached_to,
                                                 surface);

  if (!surface->actor)
    {
      surface->actor = clutter_wayland_surface_new (wayland_surface);
      clutter_container_add_actor (CLUTTER_CONTAINER (compositor->stage),
                                   surface->actor);
    }

  surface_actor = CLUTTER_WAYLAND_SURFACE (surface->actor);
  if (!clutter_wayland_surface_attach_buffer (surface_actor, wayland_buffer,
                                              NULL))
    g_warning ("Failed to attach buffer to ClutterWaylandSurface");

  surface->buffer = buffer;
}

static void
tws_surface_map_toplevel (struct wl_client *client,
                          struct wl_surface *surface)
{
}

static void
tws_surface_map_transient (struct wl_client *client,
                           struct wl_surface *surface,
                           struct wl_surface *parent,
                           gint32 dx,
                           gint32 dy,
                           guint32 flags)
{
}

static void
tws_surface_map_fullscreen (struct wl_client *client,
                            struct wl_surface *surface)
{
}

static void
tws_surface_damage (struct wl_client *client,
                    struct wl_surface *surface,
                    gint32 x,
                    gint32 y,
                    gint32 width,
                    gint32 height)
{
}

const struct wl_surface_interface tws_surface_interface = {
  tws_surface_destroy,
  tws_surface_attach_buffer,
  tws_surface_map_toplevel,
  tws_surface_map_transient,
  tws_surface_map_fullscreen,
  tws_surface_damage
};

static void
tws_surface_free (TWSSurface *surface)
{
  TWSCompositor *compositor = surface->compositor;
  compositor->surfaces = g_list_remove (compositor->surfaces, surface);
  tws_surface_detach_buffer (surface);

  clutter_actor_destroy (surface->actor);

  g_slice_free (TWSSurface, surface);
}

static void
tws_surface_resource_destroy_cb (struct wl_resource *wayland_resource,
                                 struct wl_client *wayland_client)
{
  TWSSurface *surface =
    container_of (wayland_resource, TWSSurface, wayland_surface.resource);
  tws_surface_free (surface);
}

static void
tws_compositor_create_surface (struct wl_client *wayland_client,
                               struct wl_compositor *wayland_compositor,
                               guint32 wayland_id)
{
  TWSCompositor *compositor =
    container_of (wayland_compositor, TWSCompositor, wayland_compositor);
  TWSSurface *surface = g_slice_new0 (TWSSurface);
  surface->compositor = compositor;

  surface->wayland_surface.resource.destroy =
    tws_surface_resource_destroy_cb;

  surface->wayland_surface.resource.object.id = wayland_id;
  surface->wayland_surface.resource.object.interface = &wl_surface_interface;
  surface->wayland_surface.resource.object.implementation =
          (void (**)(void)) &tws_surface_interface;
  surface->wayland_surface.client = wayland_client;

  wl_client_add_resource (wayland_client, &surface->wayland_surface.resource);

  compositor->surfaces = g_list_prepend (compositor->surfaces, surface);
}

const static struct wl_compositor_interface tws_compositor_interface = {
  tws_compositor_create_surface,
};

static void
tws_output_post_geometry (struct wl_client *wayland_client,
                          struct wl_object *wayland_output,
                          guint32 version)
{
  TWSOutput *output =
    container_of (wayland_output, TWSOutput, wayland_output);

  wl_client_post_event (wayland_client,
                        wayland_output,
                        WL_OUTPUT_GEOMETRY,
                        output->x, output->y,
                        output->width, output->height);
}

static void
paint_finished_cb (ClutterActor *self, void *user_data)
{
  TWSCompositor *compositor = user_data;
  GList *l;

  for (l = compositor->surfaces; l; l = l->next)
    {
      TWSSurface *surface = l->data;
      wl_display_post_frame (compositor->wayland_display,
                             &surface->wayland_surface, get_time ());
    }
}

static void
tws_compositor_create_output (TWSCompositor *compositor,
                              int x,
                              int y,
                              int width,
                              int height)
{
  TWSOutput *output = g_slice_new0 (TWSOutput);

  output->wayland_output.interface = &wl_output_interface;

  wl_display_add_object (compositor->wayland_display, &output->wayland_output);
  wl_display_add_global (compositor->wayland_display, &output->wayland_output,
                         tws_output_post_geometry);

  output->x = x;
  output->y = y;
  output->width = width;
  output->height = height;

  /* XXX: eventually we will support sliced stages and an output should
   * correspond to a slice/CoglFramebuffer, but for now we only support
   * one output so we make sure it always matches the size of the stage
   */
  clutter_actor_set_size (compositor->stage, width, height);

  compositor->outputs = g_list_prepend (compositor->outputs, output);
}

G_MODULE_EXPORT int
test_wayland_surface_main (int argc, char **argv)
{
  TWSCompositor compositor;
  GMainLoop *loop;

  memset (&compositor, 0, sizeof (compositor));

  compositor.wayland_display = wl_display_create ();
  if (compositor.wayland_display == NULL)
    g_error ("failed to create wayland display");

  if (wl_compositor_init (&compositor.wayland_compositor,
                          &tws_compositor_interface,
                          compositor.wayland_display) < 0)
    g_error ("Failed to init wayland compositor");

  compositor.wayland_shm = wl_shm_init (compositor.wayland_display,
                                        &shm_callbacks);
  if (!compositor.wayland_shm)
    g_error ("Failed to allocate setup wayland shm callbacks");

  loop = g_main_loop_new (NULL, FALSE);
  compositor.wayland_loop =
    wl_display_get_event_loop (compositor.wayland_display);
  compositor.wayland_event_source =
    wayland_event_source_new (compositor.wayland_loop);
  g_source_attach (compositor.wayland_event_source, NULL);

  clutter_wayland_set_compositor_display (compositor.wayland_display);

  if (clutter_init (&argc, &argv) != CLUTTER_INIT_SUCCESS)
    return 1;

  compositor.stage = clutter_stage_get_default ();
  clutter_stage_set_user_resizable (CLUTTER_STAGE (compositor.stage), FALSE);
  g_signal_connect_after (compositor.stage, "paint",
                          G_CALLBACK (paint_finished_cb), &compositor);

  tws_compositor_create_output (&compositor, 0, 0, 800, 600);

  clutter_actor_show (compositor.stage);

  if (wl_display_add_socket (compositor.wayland_display, "wayland-0"))
    g_error ("Failed to create socket");

  g_main_loop_run (loop);

  return 0;
}
