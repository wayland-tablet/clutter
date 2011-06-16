#include <stdlib.h>
#include <stdio.h>

#include <math.h>

#include <glib.h>
#include <gmodule.h>

#undef CLUTTER_DISABLE_DEPRECATED
#include <clutter/clutter.h>

static ClutterScript *script = NULL;
static guint merge_id = 0;

static const gchar *test_unmerge =
"["
"  {"
"    \"id\" : \"main-stage\","
"    \"type\" : \"ClutterStage\","
"    \"children\" : [ \"blue-button\" ]"
"  },"
"  {"
"    \"id\" : \"blue-button\","
"    \"type\" : \"ClutterRectangle\","
"    \"color\" : \"#0000ffff\","
"    \"x\" : 350,"
"    \"y\" : 50,"
"    \"width\" : 100,"
"    \"height\" : 100,"
"    \"visible\" : true,"
"    \"reactive\" : true"
"  }"
"]";

static gboolean
blue_button_press (ClutterActor       *actor,
                   ClutterButtonEvent *event,
                   gpointer            data)
{
  g_print ("[*] Pressed '%s'\n", clutter_get_script_id (G_OBJECT (actor)));
  g_print ("[*] Unmerging objects with merge id: %d\n", merge_id);

  clutter_script_unmerge_objects (script, merge_id);

  return TRUE;
}

static gboolean
red_button_press (ClutterActor *actor,
                  ClutterButtonEvent *event,
                  gpointer            data)
{
  g_print ("[*] Pressed '%s'\n", clutter_get_script_id (G_OBJECT (actor)));

  return TRUE;
}

G_MODULE_EXPORT int
test_script_main (int argc, char *argv[])
{
  GObject *stage, *blue_button, *red_button;
  GError *error = NULL;
  gchar *file;
  gint res;

  if (clutter_init (&argc, &argv) != CLUTTER_INIT_SUCCESS)
    return 1;

  script = clutter_script_new ();
  g_assert (CLUTTER_IS_SCRIPT (script));

  file = g_build_filename (TESTS_DATADIR, "test-script.json", NULL);
  clutter_script_load_from_file (script, file, &error);
  if (error)
    {
      g_print ("*** Error:\n"
               "***   %s\n", error->message);
      g_error_free (error);
      g_object_unref (script);
      g_free (file);
      return EXIT_FAILURE;
    }

  g_free (file);

  merge_id = clutter_script_load_from_data (script, test_unmerge, -1, &error);
  if (error)
    {
      g_print ("*** Error:\n"
               "***   %s\n", error->message);
      g_error_free (error);
      g_object_unref (script);
      return EXIT_FAILURE;
    }

  clutter_script_connect_signals (script, NULL);

  res = clutter_script_get_objects (script,
                                    "main-stage", &stage,
                                    "red-button", &red_button,
                                    "blue-button", &blue_button,
                                    NULL);
  g_assert (res == 3);

  clutter_actor_show (CLUTTER_ACTOR (stage));

  g_signal_connect (red_button,
                    "button-press-event",
                    G_CALLBACK (red_button_press),
                    NULL);

  g_signal_connect (blue_button,
                    "button-press-event",
                    G_CALLBACK (blue_button_press),
                    NULL);

  clutter_main ();

  g_object_unref (script);

  return EXIT_SUCCESS;
}
