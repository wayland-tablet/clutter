/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2011  Intel Corp.
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
 *  Emmanuele Bassi <ebassi@linux.intel.com>
 *  Robert Bragg <robert@linux.intel.com>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "clutter-input-device-wayland.h"
#include "clutter-device-manager-wayland.h"

#include "clutter-backend.h"
#include "clutter-debug.h"
#include "clutter-device-manager-private.h"
#include "clutter-private.h"

#include "evdev/clutter-xkb-utils.h"

enum
{
  PROP_0
};

G_DEFINE_TYPE (ClutterDeviceManagerWayland,
               clutter_device_manager_wayland,
               CLUTTER_TYPE_DEVICE_MANAGER);

static void
clutter_device_manager_wayland_add_device (ClutterDeviceManager *manager,
                                           ClutterInputDevice   *device)
{
  ClutterDeviceManagerWayland *manager_wayland = CLUTTER_DEVICE_MANAGER_WAYLAND (manager);
  ClutterInputDeviceType device_type;
  gboolean is_pointer, is_keyboard;

  device_type = clutter_input_device_get_device_type (device);
  is_pointer  = (device_type == CLUTTER_POINTER_DEVICE)  ? TRUE : FALSE;
  is_keyboard = (device_type == CLUTTER_KEYBOARD_DEVICE) ? TRUE : FALSE;

  manager_wayland->devices = g_slist_prepend (manager_wayland->devices, device);

  if (is_pointer && manager_wayland->core_pointer == NULL)
    manager_wayland->core_pointer = device;

  if (is_keyboard && manager_wayland->core_keyboard == NULL)
    manager_wayland->core_keyboard = device;
}

static void
clutter_device_manager_wayland_remove_device (ClutterDeviceManager *manager,
                                              ClutterInputDevice   *device)
{
  ClutterDeviceManagerWayland *manager_wayland = CLUTTER_DEVICE_MANAGER_WAYLAND (manager);

  manager_wayland->devices = g_slist_remove (manager_wayland->devices, device);
}

static const GSList *
clutter_device_manager_wayland_get_devices (ClutterDeviceManager *manager)
{
  return CLUTTER_DEVICE_MANAGER_WAYLAND (manager)->devices;
}

static ClutterInputDevice *
clutter_device_manager_wayland_get_core_device (ClutterDeviceManager *manager,
                                                ClutterInputDeviceType type)
{
  ClutterDeviceManagerWayland *manager_wayland;

  manager_wayland = CLUTTER_DEVICE_MANAGER_WAYLAND (manager);

  switch (type)
    {
    case CLUTTER_POINTER_DEVICE:
      return manager_wayland->core_pointer;

    case CLUTTER_KEYBOARD_DEVICE:
      return manager_wayland->core_keyboard;

    case CLUTTER_EXTENSION_DEVICE:
    default:
      return NULL;
    }

  return NULL;
}

static ClutterInputDevice *
clutter_device_manager_wayland_get_device (ClutterDeviceManager *manager,
                                       gint                  id)
{
  ClutterDeviceManagerWayland *manager_wayland = CLUTTER_DEVICE_MANAGER_WAYLAND (manager);
  GSList *l;

  for (l = manager_wayland->devices; l != NULL; l = l->next)
    {
      ClutterInputDevice *device = l->data;

      if (clutter_input_device_get_device_id (device) == id)
        return device;
    }

  return NULL;
}

static void
clutter_device_manager_wayland_class_init (ClutterDeviceManagerWaylandClass *klass)
{
  ClutterDeviceManagerClass *manager_class;

  manager_class = CLUTTER_DEVICE_MANAGER_CLASS (klass);
  manager_class->add_device = clutter_device_manager_wayland_add_device;
  manager_class->remove_device = clutter_device_manager_wayland_remove_device;
  manager_class->get_devices = clutter_device_manager_wayland_get_devices;
  manager_class->get_core_device = clutter_device_manager_wayland_get_core_device;
  manager_class->get_device = clutter_device_manager_wayland_get_device;
}

static void
clutter_device_manager_wayland_init (ClutterDeviceManagerWayland *self)
{
}

const char *option_xkb_layout = "us";
const char *option_xkb_variant = "";
const char *option_xkb_options = "";

void
_clutter_wayland_add_input_group (ClutterBackendCogl *backend_cogl,
                                  uint32_t id)
{
  ClutterDeviceManager *manager = clutter_device_manager_get_default ();
  ClutterInputDeviceWayland *device;

  device = g_object_new (CLUTTER_TYPE_INPUT_DEVICE_WAYLAND,
                         "id", id,
                         "device-type", CLUTTER_POINTER_DEVICE,
                         "name", "wayland device",
                         "enabled", TRUE,
                         NULL);

  device->input_device =
    wl_input_device_create (backend_cogl->wayland_display, id, 1);
  wl_input_device_add_listener (device->input_device,
                                &_clutter_input_device_wayland_listener, device);
  wl_input_device_set_user_data (device->input_device, device);

  device->xkb = _clutter_xkb_desc_new (NULL,
                                       option_xkb_layout,
                                       option_xkb_variant,
                                       option_xkb_options);
  if (!device->xkb)
    CLUTTER_NOTE (BACKEND, "Failed to compile keymap");

  _clutter_device_manager_add_device (manager, CLUTTER_INPUT_DEVICE (device));
}

void
_clutter_events_wayland_init (ClutterBackendCogl *backend_cogl)
{
  CLUTTER_NOTE (EVENT, "Initializing evdev backend");

  /* We just have to create the singleon here */
  clutter_device_manager_get_default ();
}

void
_clutter_events_wayland_uninit (ClutterBackendCogl *backend_cogl)
{
  ClutterDeviceManager *manager;

  manager = clutter_device_manager_get_default ();
  g_object_unref (manager);
}
