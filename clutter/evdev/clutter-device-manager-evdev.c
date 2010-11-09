/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2010  Intel Corp.
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
 * Author: Damien Lespiau <damien.lespiau@intel.com>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <linux/input.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include <glib.h>
#include <gudev/gudev.h>

#include "clutter-backend.h"
#include "clutter-debug.h"
#include "clutter-device-manager.h"
#include "clutter-device-manager-private.h"
#include "clutter-event.h"
#include "clutter-input-device-evdev.h"
#include "clutter-main.h"
#include "clutter-private.h"
#include "clutter-xkb-utils.h"

#include "clutter-device-manager-evdev.h"

#define CLUTTER_DEVICE_MANAGER_EVDEV_GET_PRIVATE(obj)               \
  (G_TYPE_INSTANCE_GET_PRIVATE ((obj),                              \
                                CLUTTER_TYPE_DEVICE_MANAGER_EVDEV,  \
                                ClutterDeviceManagerEvdevPrivate))

G_DEFINE_TYPE (ClutterDeviceManagerEvdev,
               clutter_device_manager_evdev,
               CLUTTER_TYPE_DEVICE_MANAGER);

struct _ClutterDeviceManagerEvdevPrivate
{
  GUdevClient *udev_client;

  GSList *devices;          /* list of ClutterInputDeviceEvdevs */
  GSList *event_sources;    /* list of the event sources */

  ClutterInputDevice *core_pointer;
  ClutterInputDevice *core_keyboard;
};

static const gchar *subsystems[] = { "input", NULL };

/*
 * ClutterEventSource management
 *
 * The device manager is responsible for managing the GSource when devices
 * appear and disappear from the system
 */


const char *option_xkb_layout = "us";
const char *option_xkb_variant = "";
const char *option_xkb_options = "";

/*
 * ClutterEventSource for reading input devices
 */

typedef struct _ClutterEventSource  ClutterEventSource;

struct _ClutterEventSource
{
  GSource source;

  ClutterInputDeviceEvdev *device;    /* back pointer to the evdev device */
  GPollFD event_poll_fd;              /* file descriptor of the /dev node */
  struct xkb_desc *xkb;               /* compiled xkb keymap */
  uint32_t modifier_state;            /* remember the modifier state */
};

static gboolean
clutter_event_prepare (GSource *source,
                       gint    *timeout)
{
  gboolean retval;

  clutter_threads_enter ();

  *timeout = -1;
  retval = clutter_events_pending ();

  clutter_threads_leave ();

  return retval;
}

static gboolean
clutter_event_check (GSource *source)
{
  ClutterEventSource *event_source = (ClutterEventSource *) source;
  gboolean retval;

  clutter_threads_enter ();

  retval = ((event_source->event_poll_fd.revents & G_IO_IN) ||
            clutter_events_pending ());

  clutter_threads_leave ();

  return retval;
}

static gboolean
clutter_event_dispatch (GSource     *g_source,
                        GSourceFunc  callback,
                        gpointer     user_data)
{
  ClutterEventSource *source = (ClutterEventSource *) g_source;
  ClutterInputDevice *input_device = (ClutterInputDevice *) source->device;
  ClutterMainContext *clutter_context;
  struct input_event ev[8];
  ClutterEvent *event = NULL;
  ClutterStage *stage;
  gint len, i;

  clutter_threads_enter ();

  clutter_context = _clutter_context_get_default ();
  stage = CLUTTER_STAGE (clutter_stage_get_default ());

  /* Don't queue more events if we haven't finished handling the previous batch
   */
  if (!clutter_events_pending ())
    {
       len = read (source->event_poll_fd.fd, &ev, sizeof (ev));
       if (len < 0 || len % sizeof (ev[0]) != 0)
       {
         if (errno != EAGAIN)
           {
             ClutterDeviceManager *manager;
             ClutterInputDevice *device;
             const gchar *device_path;

             device = CLUTTER_INPUT_DEVICE (source->device);

             if (CLUTTER_HAS_DEBUG (EVENT))
               {
                 device_path =
                   _clutter_input_device_evdev_get_device_path (source->device);

                 CLUTTER_NOTE (EVENT, "Could not read device (%s), removing.",
                               device_path);
               }

             /* remove the faulty device */
             manager = clutter_device_manager_get_default ();
             _clutter_device_manager_remove_device (manager, device);

           }
         goto out;
       }

       for (i = 0; i < len / sizeof (ev[0]); i++)
         {
           struct input_event *e = &ev[i];
           uint32_t _time;

           _time = e->time.tv_sec * 1000 + e->time.tv_usec / 1000;

           switch (e->type)
             {
             case EV_KEY:

               /* don't repeat mouse buttons */
               if (e->code >= BTN_MOUSE && e->code < KEY_OK)
                 if (e->value == 2)
                   continue;

               event =
                 _clutter_key_event_new_from_evdev (input_device,
                                                    stage,
                                                    source->xkb,
                                                    _time, e->code, e->value,
                                                    &source->modifier_state);

               break;
             case EV_SYN:
               /* Nothing to do here? */
               break;
             case EV_MSC:
               /* Nothing to do here? */
               break;
             case EV_ABS:
             case EV_REL:
             default:
               g_warning ("Unhandled event of type %d", e->type);
               break;
             }

           if (event)
             {
               g_queue_push_head (clutter_context->events_queue, event);
               event = NULL;
             }
         }
    }

  /* Pop an event off the queue if any */
  event = clutter_event_get ();

  if (event)
    {
      /* forward the event into clutter for emission etc. */
      clutter_do_event (event);
      clutter_event_free (event);
    }

out:
  clutter_threads_leave ();

  return TRUE;
}
static GSourceFuncs event_funcs = {
  clutter_event_prepare,
  clutter_event_check,
  clutter_event_dispatch,
  NULL
};

static GSource *
clutter_event_source_new (ClutterInputDeviceEvdev *input_device)
{
  GSource *source = g_source_new (&event_funcs, sizeof (ClutterEventSource));
  ClutterEventSource *event_source = (ClutterEventSource *) source;
  const gchar *node_path;
  gint fd;

  /* grab the udev input device node and open it */
  node_path = _clutter_input_device_evdev_get_device_path (input_device);

  CLUTTER_NOTE (EVENT, "Creating GSource for device %s", node_path);

  fd = open (node_path, O_RDONLY | O_NONBLOCK);
  if (fd < 0)
    {
      g_warning ("Could not open device %s: %s", node_path, strerror (errno));
      return NULL;
    }

  /* setup the source */
  event_source->device = input_device;
  event_source->event_poll_fd.fd = fd;
  event_source->event_poll_fd.events = G_IO_IN;

  /* create the xkb description */
  event_source->xkb = _clutter_xkb_desc_new (NULL,
                                             option_xkb_layout,
                                             option_xkb_variant,
                                             option_xkb_options);
  if (G_UNLIKELY (event_source->xkb == NULL))
    {
      g_warning ("Could not compile keymap %s:%s:%s", option_xkb_layout,
                 option_xkb_variant, option_xkb_options);
      close (fd);
      g_source_unref (source);
      return NULL;
    }

  /* and finally configure and attach the GSource */
  g_source_set_priority (source, CLUTTER_PRIORITY_EVENTS);
  g_source_add_poll (source, &event_source->event_poll_fd);
  g_source_set_can_recurse (source, TRUE);
  g_source_attach (source, NULL);

  return source;
}

static void
clutter_event_source_free (ClutterEventSource *source)
{
  GSource *g_source = (GSource *) source;
  const gchar *node_path;

  node_path = _clutter_input_device_evdev_get_device_path (source->device);

  CLUTTER_NOTE (EVENT, "Removing GSource for device %s", node_path);

  /* ignore the return value of close, it's not like we can do something
   * about it */
  close (source->event_poll_fd.fd);

  g_source_destroy (g_source);
  g_source_unref (g_source);
}

static ClutterEventSource *
find_source_by_device (ClutterDeviceManagerEvdev *manager,
                       ClutterInputDevice        *device)
{
  ClutterDeviceManagerEvdevPrivate *priv = manager->priv;
  GSList *l;

  for (l = priv->event_sources; l; l = g_slist_next (l))
    {
      ClutterEventSource *source = l->data;

      if (source->device == (ClutterInputDeviceEvdev *) device)
        return source;
    }

  return NULL;
}

static gboolean
is_evdev (const gchar *sysfs_path)
{
  GRegex *regex;
  gboolean match;

  regex = g_regex_new ("/input[0-9]+/event[0-9]+$", 0, 0, NULL);
  match = g_regex_match (regex, sysfs_path, 0, NULL);

  g_regex_unref (regex);
  return match;
}

static void
evdev_add_device (ClutterDeviceManagerEvdev *manager_evdev,
                  GUdevDevice               *udev_device)
{
  ClutterDeviceManager *manager = (ClutterDeviceManager *) manager_evdev;
  ClutterInputDeviceType type = CLUTTER_EXTENSION_DEVICE;
  ClutterInputDevice *device;
  const gchar *device_file, *sysfs_path;
  const gchar * const *keys;
  guint i;

  device_file = g_udev_device_get_device_file (udev_device);
  sysfs_path = g_udev_device_get_sysfs_path (udev_device);

  if (device_file == NULL || sysfs_path == NULL)
    return;

  if (g_udev_device_get_property (udev_device, "ID_INPUT") == NULL)
    return;

  /* Make sure to only add evdev devices, ie the device with a sysfs path that
   * finishes by input%d/event%d (We don't rely on the node name as this
   * policy is enforced by udev rules Vs API/ABI guarantees of sysfs) */
  if (!is_evdev (sysfs_path))
    return;

  keys = g_udev_device_get_property_keys (udev_device);
  for (i = 0; keys[i]; i++)
    {
      /* Clutter assumes that device types are exclusive in the
       * ClutterInputDevice API */
      if (strcmp (keys[i], "ID_INPUT_KEY") == 0)
        type = CLUTTER_KEYBOARD_DEVICE;
      else if (strcmp (keys[i], "ID_INPUT_MOUSE") == 0)
        type = CLUTTER_POINTER_DEVICE;
      else if (strcmp (keys[i], "ID_INPUT_JOYSTICK") == 0)
        type = CLUTTER_JOYSTICK_DEVICE;
      else if (strcmp (keys[i], "ID_INPUT_TABLET") == 0)
        type = CLUTTER_TABLET_DEVICE;
      else if (strcmp (keys[i], "ID_INPUT_TOUCHPAD") == 0)
        type = CLUTTER_TOUCHPAD_DEVICE;
      else if (strcmp (keys[i], "ID_INPUT_TOUCHSCREEN") == 0)
        type = CLUTTER_TOUCHSCREEN_DEVICE;
    }

  device = g_object_new (CLUTTER_TYPE_INPUT_DEVICE_EVDEV,
                         "id", 0,
                         "name", "Evdev device", /* FIXME */
                         "device-type", type,
                         "sysfs-path", sysfs_path,
                         "device-path", device_file,
                         NULL);
  _clutter_device_manager_add_device (manager, device);

  CLUTTER_NOTE (EVENT, "Added device %s, type %d, sysfs %s",
                device_file, type, sysfs_path);
}

static ClutterInputDeviceEvdev *
find_device_by_udev_device (ClutterDeviceManagerEvdev *manager_evdev,
                            GUdevDevice               *udev_device)
{
  ClutterDeviceManagerEvdevPrivate *priv = manager_evdev->priv;
  GSList *l;
  const gchar *sysfs_path;

  sysfs_path = g_udev_device_get_sysfs_path (udev_device);
  if (sysfs_path == NULL)
    {
      g_message ("device file is NULL");
      return NULL;
    }

  for (l = priv->devices; l; l = g_slist_next (l))
    {
      ClutterInputDeviceEvdev *device = l->data;

      if (strcmp (sysfs_path,
                  _clutter_input_device_evdev_get_sysfs_path (device)) == 0)
        {
          return device;
        }
    }

  return NULL;
}

static void
evdev_remove_device (ClutterDeviceManagerEvdev *manager_evdev,
                     GUdevDevice               *device)
{
  ClutterDeviceManager *manager = CLUTTER_DEVICE_MANAGER (manager_evdev);
  ClutterInputDeviceEvdev *device_evdev;
  ClutterInputDevice *input_device;

  device_evdev = find_device_by_udev_device (manager_evdev, device);
  if (device_evdev == NULL)
      return;

  input_device = CLUTTER_INPUT_DEVICE (device_evdev);
  _clutter_device_manager_remove_device (manager, input_device);
}

static void
on_uevent (GUdevClient *client,
           gchar       *action,
           GUdevDevice *device,
           gpointer     data)
{
  ClutterDeviceManagerEvdev *manager = CLUTTER_DEVICE_MANAGER_EVDEV (data);

  if (g_strcmp0 (action, "add") == 0)
    evdev_add_device (manager, device);
  else if (g_strcmp0 (action, "remove") == 0)
    evdev_remove_device (manager, device);
}

/*
 * ClutterDeviceManager implementation
 */

static void
clutter_device_manager_evdev_add_device (ClutterDeviceManager *manager,
                                         ClutterInputDevice   *device)
{
  ClutterDeviceManagerEvdev *manager_evdev;
  ClutterDeviceManagerEvdevPrivate *priv;
  ClutterInputDeviceType device_type;
  ClutterInputDeviceEvdev *device_evdev;
  gboolean is_pointer, is_keyboard;
  GSource *source;

  manager_evdev = CLUTTER_DEVICE_MANAGER_EVDEV (manager);
  priv = manager_evdev->priv;

  device_evdev = CLUTTER_INPUT_DEVICE_EVDEV (device);

  device_type = clutter_input_device_get_device_type (device);
  is_pointer  = device_type == CLUTTER_POINTER_DEVICE;
  is_keyboard = device_type == CLUTTER_KEYBOARD_DEVICE;

  priv->devices = g_slist_prepend (priv->devices, device);

  if (is_pointer && priv->core_pointer == NULL)
    priv->core_pointer = device;

  if (is_keyboard && priv->core_keyboard == NULL)
    priv->core_keyboard = device;

  /* Install the GSource for this device */
  source = clutter_event_source_new (device_evdev);
  if (G_LIKELY (source))
    priv->event_sources = g_slist_prepend (priv->event_sources, source);
}

static void
clutter_device_manager_evdev_remove_device (ClutterDeviceManager *manager,
                                            ClutterInputDevice   *device)
{
  ClutterDeviceManagerEvdev *manager_evdev;
  ClutterDeviceManagerEvdevPrivate *priv;
  ClutterEventSource *source;

  manager_evdev = CLUTTER_DEVICE_MANAGER_EVDEV (manager);
  priv = manager_evdev->priv;

  /* Remove the device */
  priv->devices = g_slist_remove (priv->devices, device);

  /* Remove the source */
  source = find_source_by_device (manager_evdev, device);
  if (G_UNLIKELY (source == NULL))
    {
      g_warning ("Trying to remove a device without a source installed ?!");
      return;
    }

  clutter_event_source_free (source);
  priv->event_sources = g_slist_remove (priv->event_sources, source);
}

static const GSList *
clutter_device_manager_evdev_get_devices (ClutterDeviceManager *manager)
{
  return CLUTTER_DEVICE_MANAGER_EVDEV (manager)->priv->devices;
}

static ClutterInputDevice *
clutter_device_manager_evdev_get_core_device (ClutterDeviceManager   *manager,
                                              ClutterInputDeviceType  type)
{
  ClutterDeviceManagerEvdev *manager_evdev;
  ClutterDeviceManagerEvdevPrivate *priv;

  manager_evdev = CLUTTER_DEVICE_MANAGER_EVDEV (manager);
  priv = manager_evdev->priv;

  switch (type)
    {
    case CLUTTER_POINTER_DEVICE:
      return priv->core_pointer;

    case CLUTTER_KEYBOARD_DEVICE:
      return priv->core_keyboard;

    case CLUTTER_EXTENSION_DEVICE:
    default:
      return NULL;
    }

  return NULL;
}

static ClutterInputDevice *
clutter_device_manager_evdev_get_device (ClutterDeviceManager *manager,
                                         gint                  id)
{
  ClutterDeviceManagerEvdev *manager_evdev;
  ClutterDeviceManagerEvdevPrivate *priv;
  GSList *l;

  manager_evdev = CLUTTER_DEVICE_MANAGER_EVDEV (manager);
  priv = manager_evdev->priv;

  for (l = priv->devices; l; l = l->next)
    {
      ClutterInputDevice *device = l->data;

      if (clutter_input_device_get_device_id (device) == id)
        return device;
    }

  return NULL;
}

/*
 * GObject implementation
 */

static void
clutter_device_manager_evdev_constructed (GObject *gobject)
{
  ClutterDeviceManagerEvdev *manager_evdev;
  ClutterDeviceManagerEvdevPrivate *priv;
  GList *devices, *l;

  manager_evdev = CLUTTER_DEVICE_MANAGER_EVDEV (gobject);
  priv = manager_evdev->priv;

  priv->udev_client = g_udev_client_new (subsystems);

  devices = g_udev_client_query_by_subsystem (priv->udev_client, subsystems[0]);
  for (l = devices; l; l = g_list_next (l))
    {
      GUdevDevice *device = l->data;

      evdev_add_device (manager_evdev, device);
      g_object_unref (device);
    }
  g_list_free (devices);

  /* subcribe for events on input devices */
  g_signal_connect (priv->udev_client, "uevent",
                    G_CALLBACK (on_uevent), manager_evdev);
}

static void
clutter_device_manager_evdev_finalize (GObject *object)
{
  ClutterDeviceManagerEvdev *manager_evdev;
  ClutterDeviceManagerEvdevPrivate *priv;
  GSList *l;

  manager_evdev = CLUTTER_DEVICE_MANAGER_EVDEV (object);
  priv = manager_evdev->priv;

  g_object_unref (priv->udev_client);

  for (l = priv->devices; l; l = g_slist_next (l))
    {
      ClutterInputDevice *device = l->data;

      g_object_unref (device);
    }
  g_slist_free (priv->devices);

  for (l = priv->event_sources; l; l = g_slist_next (l))
    {
      ClutterEventSource *source = l->data;

      clutter_event_source_free (source);
    }
  g_slist_free (priv->event_sources);

  G_OBJECT_CLASS (clutter_device_manager_evdev_parent_class)->finalize (object);
}

static void
clutter_device_manager_evdev_class_init (ClutterDeviceManagerEvdevClass *klass)
{
  ClutterDeviceManagerClass *manager_class;
  GObjectClass *gobject_class = (GObjectClass *) klass;

  g_type_class_add_private (klass, sizeof (ClutterDeviceManagerEvdevPrivate));

  gobject_class->constructed = clutter_device_manager_evdev_constructed;
  gobject_class->finalize = clutter_device_manager_evdev_finalize;

  manager_class = CLUTTER_DEVICE_MANAGER_CLASS (klass);
  manager_class->add_device = clutter_device_manager_evdev_add_device;
  manager_class->remove_device = clutter_device_manager_evdev_remove_device;
  manager_class->get_devices = clutter_device_manager_evdev_get_devices;
  manager_class->get_core_device = clutter_device_manager_evdev_get_core_device;
  manager_class->get_device = clutter_device_manager_evdev_get_device;
}

static void
clutter_device_manager_evdev_init (ClutterDeviceManagerEvdev *self)
{
  self->priv = CLUTTER_DEVICE_MANAGER_EVDEV_GET_PRIVATE (self);
}

/*
 * _clutter_events_evdev_init() and _clutter_events_evdev_uninit() are the two
 * symbol to use the evdev event backend from the EGL backend
 */

void
_clutter_events_evdev_init (ClutterBackend *backend)
{
  CLUTTER_NOTE (EVENT, "Initializing evdev backend");

  /* We just have to create the singleon here */
  clutter_device_manager_get_default ();
}

void
_clutter_events_evdev_uninit (ClutterBackend *backend)
{
  ClutterDeviceManager *manager;

  manager = clutter_device_manager_get_default ();
  g_object_unref (manager);
}
