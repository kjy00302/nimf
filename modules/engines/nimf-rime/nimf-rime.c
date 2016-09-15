/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 2; tab-width: 2 -*- */
/*
 * nimf-rime.c
 * This file is part of Nimf.
 *
 * Copyright (C) 2016 Hodong Kim <cogniti@gmail.com>
 *
 * Nimf is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Nimf is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program;  If not, see <http://www.gnu.org/licenses/>.
 */

#include <nimf.h>
#undef Bool
#include <rime_api.h>
#include <glib/gi18n.h>

#define NIMF_TYPE_RIME             (nimf_rime_get_type ())
#define NIMF_RIME(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), NIMF_TYPE_RIME, NimfRime))
#define NIMF_RIME_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), NIMF_TYPE_RIME, NimfRimeClass))
#define NIMF_IS_RIME(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), NIMF_TYPE_RIME))
#define NIMF_IS_RIME_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), NIMF_TYPE_RIME))
#define NIMF_RIME_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), NIMF_TYPE_RIME, NimfRimeClass))

typedef struct _NimfRime      NimfRime;
typedef struct _NimfRimeClass NimfRimeClass;

struct _NimfRime
{
  NimfEngine parent_instance;

  NimfCandidate    *candidate;
  gchar            *id;
  GString          *preedit;
  NimfPreeditAttr **preedit_attrs;
  NimfPreeditState  preedit_state;
  gint              cursor_pos;
  RimeSessionId     session_id;
  gint              current_page;
  gint              n_pages;
};

struct _NimfRimeClass
{
  /*< private >*/
  NimfEngineClass parent_class;
};

static gint nimf_rime_ref_count = 0;

G_DEFINE_DYNAMIC_TYPE (NimfRime, nimf_rime, NIMF_TYPE_ENGINE);

static void nimf_rime_update_preedit (NimfEngine  *engine,
                                      NimfContext *target,
                                      const gchar *new_preedit,
                                      gint         cursor_pos)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  NimfRime *rime = NIMF_RIME (engine);

  if (rime->preedit_state == NIMF_PREEDIT_STATE_END && rime->preedit->len > 0)
  {
    rime->preedit_state = NIMF_PREEDIT_STATE_START;
    nimf_engine_emit_preedit_start (engine, target);
  }

  g_string_assign (rime->preedit, new_preedit);
  rime->cursor_pos = cursor_pos;
  rime->preedit_attrs[0]->start_index = 0;
  rime->preedit_attrs[0]->end_index = g_utf8_strlen (rime->preedit->str, -1);
  nimf_engine_emit_preedit_changed (engine, target, rime->preedit->str,
                                    rime->preedit_attrs, cursor_pos);

  if (rime->preedit_state == NIMF_PREEDIT_STATE_START && rime->preedit->len == 0)
  {
    rime->preedit_state = NIMF_PREEDIT_STATE_END;
    nimf_engine_emit_preedit_end (engine, target);
  }
}

void nimf_rime_reset (NimfEngine  *engine,
                      NimfContext *target)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  NimfRime *rime = NIMF_RIME (engine);

  nimf_candidate_hide_window (rime->candidate);
  nimf_rime_update_preedit (engine, target, "", 0);
  RimeProcessKey (rime->session_id, NIMF_KEY_Escape, 0);
}

void
nimf_rime_focus_in (NimfEngine  *engine,
                    NimfContext *context)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);
}

void
nimf_rime_focus_out (NimfEngine  *engine,
                     NimfContext *target)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  nimf_candidate_hide_window (NIMF_RIME (engine)->candidate);
  nimf_rime_reset (engine, target);
}

static void
nimf_rime_update_candidate (NimfEngine  *engine,
                            NimfContext *target)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  NimfRime *rime = NIMF_RIME (engine);
  int i;

  RIME_STRUCT (RimeContext, context);

  if (!RimeGetContext (rime->session_id, &context) ||
      context.composition.length == 0)
  {
    nimf_candidate_hide_window (rime->candidate);
    RimeFreeContext (&context);
    return;
  }

  rime->current_page = context.menu.page_no + 1;
  rime->n_pages = context.menu.page_no + 1;

  if (!context.menu.is_last_page)
    rime->n_pages++;

  nimf_candidate_clear (rime->candidate, target);

  for (i = 0; i < context.menu.num_candidates; i++)
  {
    nimf_candidate_append (rime->candidate,
                           context.menu.candidates[i].text,
                           context.menu.candidates[i].comment);
    nimf_candidate_select_item_by_index_in_page (rime->candidate,
                                                 context.menu.highlighted_candidate_index);
  }

  nimf_candidate_set_page_values (rime->candidate, target,
                                  context.menu.page_no + 1, rime->n_pages, 5);
  RimeFreeContext (&context);
}

static void nimf_rime_update_preedit2 (NimfEngine  *engine,
                                       NimfContext *target)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  NimfRime *rime = NIMF_RIME (engine);

  RIME_STRUCT (RimeContext, context);

  if (!RimeGetContext (rime->session_id, &context) ||
      context.composition.length == 0)
  {
    nimf_rime_update_preedit (engine, target, "", 0);
    nimf_candidate_hide_window (rime->candidate);
    RimeFreeContext (&context);
    return;
  }

  if (context.commit_text_preview)
    nimf_rime_update_preedit (engine, target, context.commit_text_preview,
                              g_utf8_strlen (context.commit_text_preview, -1));
  else
    nimf_rime_update_preedit (engine, target, "", 0);

  nimf_candidate_set_auxiliary_text (rime->candidate,
                                     context.composition.preedit,
                                     context.composition.cursor_pos);
  RimeFreeContext (&context);
}

static void nimf_rime_update (NimfEngine  *engine,
                              NimfContext *target)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  NimfRime *rime = NIMF_RIME (engine);

  /* commit text */
  RIME_STRUCT (RimeCommit, commit);

  if (RimeGetCommit (rime->session_id, &commit))
  {
    nimf_engine_emit_commit (engine, target, commit.text);
    RimeFreeCommit (&commit);
  }

  RIME_STRUCT (RimeContext, context);

  if (!RimeGetContext (rime->session_id, &context) ||
      context.composition.length == 0)
  {
    nimf_rime_update_preedit (engine, target, "", 0);
    nimf_candidate_hide_window (rime->candidate);
    RimeFreeContext (&context);
    return;
  }

  nimf_rime_update_preedit2 (engine, target);

  if (context.menu.num_candidates)
  {
    nimf_rime_update_candidate (engine, target);

    if (!nimf_candidate_is_window_visible (rime->candidate))
      nimf_candidate_show_window (rime->candidate, target, TRUE);
  }
  else
  {
    nimf_candidate_clear (rime->candidate, target);
  }

  RimeFreeContext (&context);
}

static void
on_candidate_clicked (NimfEngine  *engine,
                      NimfContext *target,
                      gchar       *text,
                      gint         index)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  NimfRime *rime = NIMF_RIME (engine);
  RimeApi  *api  = rime_get_api();

  if (RIME_API_AVAILABLE (api, select_candidate))
  {
    RIME_STRUCT(RimeContext, context);

    if (!RimeGetContext (rime->session_id, &context) ||
        context.composition.length == 0)
    {
      RimeFreeContext (&context);
      return;
    }

    api->select_candidate (rime->session_id,
                           context.menu.page_no * context.menu.page_size + index);
    RimeFreeContext (&context);
    nimf_rime_update (engine, target);
  }
}

static gboolean
nimf_rime_page_up (NimfEngine *engine, NimfContext *target)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  RimeProcessKey (NIMF_RIME (engine)->session_id, NIMF_KEY_Page_Up, 0);
  nimf_rime_update_candidate (engine, target);

  return TRUE;
}

static gboolean
nimf_rime_page_down (NimfEngine *engine, NimfContext *target)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  RimeProcessKey (NIMF_RIME (engine)->session_id, NIMF_KEY_Page_Down, 0);
  nimf_rime_update_candidate (engine, target);

  return TRUE;
}

static void
on_candidate_scrolled (NimfEngine  *engine,
                       NimfContext *target,
                       gdouble      value)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  NimfRime *rime = NIMF_RIME (engine);

  if ((gint) value == rime->current_page)
    return;

  while (rime->n_pages > 1)
  {
    gint d = (gint) value - rime->current_page;

    if (d > 0)
      nimf_rime_page_down (engine, target);
    else if (d < 0)
      nimf_rime_page_up (engine, target);
    else if (d == 0)
      break;
  }

  nimf_rime_update_preedit2 (engine, target);
}

gboolean
nimf_rime_filter_event (NimfEngine  *engine,
                        NimfContext *target,
                        NimfEvent   *event)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  NimfRime *rime = NIMF_RIME (engine);

  gboolean retval;

  if (event->key.type == NIMF_EVENT_KEY_RELEASE)
    return FALSE;

  retval = RimeProcessKey (rime->session_id, event->key.keyval,
                           event->key.state & NIMF_MODIFIER_MASK);
  nimf_rime_update (engine, target);

  return retval;
}

static void
nimf_rime_init (NimfRime *rime)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  rime->candidate = nimf_candidate_get_default ();
  rime->id        = g_strdup ("nimf-rime");
  rime->preedit   = g_string_new ("");
  rime->preedit_attrs  = g_malloc0_n (2, sizeof (NimfPreeditAttr *));
  rime->preedit_attrs[0] = nimf_preedit_attr_new (NIMF_PREEDIT_ATTR_UNDERLINE, 0, 0);
  rime->preedit_attrs[1] = NULL;

  if (nimf_rime_ref_count == 0)
  {
    gchar *user_data_dir;

    user_data_dir = g_strconcat (g_getenv ("HOME"), "/.config/nimf/rime", NULL);

    if (!g_file_test (user_data_dir, G_FILE_TEST_IS_DIR))
      g_mkdir_with_parents (user_data_dir, 0700);

    RIME_STRUCT (RimeTraits, traits);
    traits.shared_data_dir        = "/usr/share/rime-data";
    traits.user_data_dir          = user_data_dir;
    traits.distribution_name      = _("Rime");
    traits.distribution_code_name = "nimf-rime";
    traits.distribution_version   = "1.2";
    traits.app_name               = "rime.nimf";

    RimeInitialize (&traits);
    RimeStartMaintenance (False);

    g_free (user_data_dir);
  }

  nimf_rime_ref_count++;

  rime->session_id = RimeCreateSession();
}

static void
nimf_rime_finalize (GObject *object)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  NimfRime *rime = NIMF_RIME (object);

  g_string_free (rime->preedit, TRUE);
  nimf_preedit_attr_freev (rime->preedit_attrs);
  g_free (rime->id);

  if (rime->session_id)
  {
    RimeDestroySession (rime->session_id);
    rime->session_id = 0;
  }

  if (--nimf_rime_ref_count == 0)
    RimeFinalize ();

  G_OBJECT_CLASS (nimf_rime_parent_class)->finalize (object);
}

const gchar *
nimf_rime_get_id (NimfEngine *engine)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  g_return_val_if_fail (NIMF_IS_ENGINE (engine), NULL);

  return NIMF_RIME (engine)->id;
}

const gchar *
nimf_rime_get_icon_name (NimfEngine *engine)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  g_return_val_if_fail (NIMF_IS_ENGINE (engine), NULL);

  return NIMF_RIME (engine)->id;
}

static void
nimf_rime_class_init (NimfRimeClass *class)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  GObjectClass *object_class = G_OBJECT_CLASS (class);
  NimfEngineClass *engine_class = NIMF_ENGINE_CLASS (class);

  engine_class->filter_event       = nimf_rime_filter_event;
  engine_class->reset              = nimf_rime_reset;
  engine_class->focus_in           = nimf_rime_focus_in;
  engine_class->focus_out          = nimf_rime_focus_out;

  engine_class->candidate_page_up   = nimf_rime_page_up;
  engine_class->candidate_page_down = nimf_rime_page_down;
  engine_class->candidate_clicked   = on_candidate_clicked;
  engine_class->candidate_scrolled  = on_candidate_scrolled;

  engine_class->get_id             = nimf_rime_get_id;
  engine_class->get_icon_name      = nimf_rime_get_icon_name;

  object_class->finalize = nimf_rime_finalize;
}

static void
nimf_rime_class_finalize (NimfRimeClass *class)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);
}

void module_register_type (GTypeModule *type_module)
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  nimf_rime_register_type (type_module);
}

GType module_get_type ()
{
  g_debug (G_STRLOC ": %s", G_STRFUNC);

  return nimf_rime_get_type ();
}
