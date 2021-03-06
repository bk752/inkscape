/**
 * @file
 * Main UI stuff.
 */
/* Authors:
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   Frank Felfe <innerspace@iname.com>
 *   bulia byak <buliabyak@users.sf.net>
 *   Jon A. Cruz <jon@joncruz.org>
 *   Abhishek Sharma
 *   Kris De Gussem <Kris.DeGussem@gmail.com>
 *
 * Copyright (C) 2012 Kris De Gussem
 * Copyright (C) 2010 authors
 * Copyright (C) 1999-2005 authors
 * Copyright (C) 2004 David Turner
 * Copyright (C) 2001-2002 Ximian, Inc.
 *
 * Released under GNU GPL, read the file 'COPYING' for more information
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "ui/dialog/dialog-manager.h"
#include <gtkmm/icontheme.h>
#include "file.h"
#include <glibmm/miscutils.h>

#if WITH_GTKMM_3_22
# include <gdkmm/monitor.h>
#endif

#include "inkscape.h"
#include "extension/db.h"
#include "extension/effect.h"
#include "extension/input.h"
#include "preferences.h"
#include "shortcuts.h"
#include "document.h"

#include "ui/interface.h"
#include "desktop.h"
#include "selection-chemistry.h"
#include "svg-view-widget.h"
#include "widgets/desktop-widget.h"
#include "sp-item-group.h"
#include "sp-text.h"
#include "sp-flowtext.h"
#include "sp-namedview.h"
#include "sp-root.h"
#include "helper/action.h"
#include "helper/gnome-utils.h"
#include "helper/window.h"
#include "io/sys.h"
#include "ui/dialog-events.h"
#include "message-context.h"
#include "ui/uxmanager.h"
#include "ui/clipboard.h"

#include "display/sp-canvas.h"
#include "svg/svg-color.h"
#include "desktop-style.h"
#include "style.h"
#include "ui/tools/tool-base.h"
#include "gradient-drag.h"
#include "widgets/ege-paint-def.h"
#include "document-undo.h"
#include "sp-anchor.h"
#include "sp-clippath.h"
#include "sp-image.h"
#include "sp-mask.h"
#include "message-stack.h"
#include "ui/dialog/layer-properties.h"
#include "extension/find_extension_by_mime.h"

using Inkscape::DocumentUndo;

/* Drag and Drop */
typedef enum {
    URI_LIST,
    SVG_XML_DATA,
    SVG_DATA,
    PNG_DATA,
    JPEG_DATA,
    IMAGE_DATA,
    APP_X_INKY_COLOR,
    APP_X_COLOR,
    APP_OSWB_COLOR,
    APP_X_INK_PASTE
} ui_drop_target_info;

static GtkTargetEntry ui_drop_target_entries [] = {
    {(gchar *)"text/uri-list",                0, URI_LIST        },
    {(gchar *)"image/svg+xml",                0, SVG_XML_DATA    },
    {(gchar *)"image/svg",                    0, SVG_DATA        },
    {(gchar *)"image/png",                    0, PNG_DATA        },
    {(gchar *)"image/jpeg",                   0, JPEG_DATA       },
#if ENABLE_MAGIC_COLORS
    {(gchar *)"application/x-inkscape-color", 0, APP_X_INKY_COLOR},
#endif // ENABLE_MAGIC_COLORS
    {(gchar *)"application/x-oswb-color",     0, APP_OSWB_COLOR  },
    {(gchar *)"application/x-color",          0, APP_X_COLOR     },
    {(gchar *)"application/x-inkscape-paste", 0, APP_X_INK_PASTE }
};

static GtkTargetEntry *completeDropTargets = 0;
static int completeDropTargetsCount = 0;
static bool temporarily_block_actions = false;

#define ENTRIES_SIZE(n) sizeof(n)/sizeof(n[0])
static guint nui_drop_target_entries = ENTRIES_SIZE(ui_drop_target_entries);
static void sp_ui_import_files(gchar *buffer);
static void sp_ui_import_one_file(char const *filename);
static void sp_ui_import_one_file_with_check(gpointer filename, gpointer unused);
static void sp_ui_drag_data_received(GtkWidget *widget,
                                     GdkDragContext *drag_context,
                                     gint x, gint y,
                                     GtkSelectionData *data,
                                     guint info,
                                     guint event_time,
                                     gpointer user_data);
static void sp_ui_drag_motion( GtkWidget *widget,
                               GdkDragContext *drag_context,
                               gint x, gint y,
                               GtkSelectionData *data,
                               guint info,
                               guint event_time,
                               gpointer user_data );
static void sp_ui_drag_leave( GtkWidget *widget,
                              GdkDragContext *drag_context,
                              guint event_time,
                              gpointer user_data );
static void sp_ui_menu_item_set_name(GtkWidget *data,
                                     Glib::ustring const &name);
static void sp_recent_open(GtkRecentChooser *, gpointer);

static const int MIN_ONSCREEN_DISTANCE = 50;

void
sp_create_window(SPViewWidget *vw, bool editable)
{
    g_return_if_fail(vw != NULL);
    g_return_if_fail(SP_IS_VIEW_WIDGET(vw));

    Gtk::Window *win = Inkscape::UI::window_new("", TRUE);

    gtk_container_add(GTK_CONTAINER(win->gobj()), GTK_WIDGET(vw));
    gtk_widget_show(GTK_WIDGET(vw));

    if (editable) {
        g_object_set_data(G_OBJECT(vw), "window", win);

        SPDesktopWidget *desktop_widget = reinterpret_cast<SPDesktopWidget*>(vw);
        SPDesktop* desktop = desktop_widget->desktop;

        desktop_widget->window = win;

        win->set_data("desktop", desktop);
        win->set_data("desktopwidget", desktop_widget);

        win->signal_delete_event().connect(sigc::mem_fun(*(SPDesktop*)vw->view, &SPDesktop::onDeleteUI));
        win->signal_window_state_event().connect(sigc::mem_fun(*desktop, &SPDesktop::onWindowStateEvent));
        win->signal_focus_in_event().connect(sigc::mem_fun(*desktop_widget, &SPDesktopWidget::onFocusInEvent));

        Inkscape::Preferences *prefs = Inkscape::Preferences::get();
        gint prefs_geometry =
            (2==prefs->getInt("/options/savewindowgeometry/value", 0));
        if (prefs_geometry) {
            gint pw = prefs->getInt("/desktop/geometry/width", -1);
            gint ph = prefs->getInt("/desktop/geometry/height", -1);
            gint px = prefs->getInt("/desktop/geometry/x", -1);
            gint py = prefs->getInt("/desktop/geometry/y", -1);
            gint full = prefs->getBool("/desktop/geometry/fullscreen");
            gint maxed = prefs->getBool("/desktop/geometry/maximized");
            if (pw>0 && ph>0) {
#if WITH_GTKMM_3_22
                auto const display = Gdk::Display::get_default();
                auto const monitor = display->get_primary_monitor();

                // A Gdk::Rectangle in "application pixel" units
                Gdk::Rectangle screen_geometry;
                monitor->get_geometry(screen_geometry);
                auto const screen_width  = screen_geometry.get_width();
                auto const screen_height = screen_geometry.get_height();
#else
                auto const screen_width  = gdk_screen_width();
                auto const screen_height = gdk_screen_height();
#endif
                gint w = MIN(screen_width,  pw);
                gint h = MIN(screen_height, ph);
                gint x = MIN(screen_width  - MIN_ONSCREEN_DISTANCE, px);
                gint y = MIN(screen_height - MIN_ONSCREEN_DISTANCE, py);
                if (w>0 && h>0) {
                    x = MIN(screen_width - w,  x);
                    y = MIN(screen_height - h, y);
                    desktop->setWindowSize(w, h);
                }

                // Only restore xy for the first window so subsequent windows don't overlap exactly
                // with first.  (Maybe rule should be only restore xy if it's different from xy of
                // other desktops?)

                // Empirically it seems that active_desktop==this desktop only the first time a
                // desktop is created.
                SPDesktop *active_desktop = SP_ACTIVE_DESKTOP;
                if (active_desktop == desktop || active_desktop==NULL) {
                    desktop->setWindowPosition(Geom::Point(x, y));
                }
            }
            if (maxed) {
                win->maximize();
            }
            if (full) {
                win->fullscreen();
            }
        }

    } 

    if ( completeDropTargets == 0 || completeDropTargetsCount == 0 )
    {
        std::vector<gchar*> types;

        GSList *list = gdk_pixbuf_get_formats();
        while ( list ) {
            int i = 0;
            GdkPixbufFormat *one = (GdkPixbufFormat*)list->data;
            gchar** typesXX = gdk_pixbuf_format_get_mime_types(one);
            for ( i = 0; typesXX[i]; i++ ) {
                types.push_back(g_strdup(typesXX[i]));
            }
            g_strfreev(typesXX);

            list = g_slist_next(list);
        }
        completeDropTargetsCount = nui_drop_target_entries + types.size();
        completeDropTargets = new GtkTargetEntry[completeDropTargetsCount];
        for ( int i = 0; i < (int)nui_drop_target_entries; i++ ) {
            completeDropTargets[i] = ui_drop_target_entries[i];
        }
        int pos = nui_drop_target_entries;

        for (std::vector<gchar*>::iterator it = types.begin() ; it != types.end() ; ++it) {
            completeDropTargets[pos].target = *it;
            completeDropTargets[pos].flags = 0;
            completeDropTargets[pos].info = IMAGE_DATA;
            pos++;
        }
    }

    gtk_drag_dest_set((GtkWidget*)win->gobj(),
                      GTK_DEST_DEFAULT_ALL,
                      completeDropTargets,
                      completeDropTargetsCount,
                      GdkDragAction(GDK_ACTION_COPY | GDK_ACTION_MOVE));


    g_signal_connect(G_OBJECT(win->gobj()),
                     "drag_data_received",
                     G_CALLBACK(sp_ui_drag_data_received),
                     NULL);
    g_signal_connect(G_OBJECT(win->gobj()),
                     "drag_motion",
                     G_CALLBACK(sp_ui_drag_motion),
                     NULL);
    g_signal_connect(G_OBJECT(win->gobj()),
                     "drag_leave",
                     G_CALLBACK(sp_ui_drag_leave),
                     NULL);
    win->show();

    // needed because the first ACTIVATE_DESKTOP was sent when there was no window yet
    if ( SP_IS_DESKTOP_WIDGET(vw) ) {
        INKSCAPE.reactivate_desktop(SP_DESKTOP_WIDGET(vw)->desktop);
    }
}

void
sp_ui_new_view()
{
    SPDocument *document;
    SPViewWidget *dtw;

    document = SP_ACTIVE_DOCUMENT;
    if (!document) return;

    dtw = sp_desktop_widget_new(sp_document_namedview(document, NULL));
    g_return_if_fail(dtw != NULL);

    sp_create_window(dtw, TRUE);
    sp_namedview_window_from_document(static_cast<SPDesktop*>(dtw->view));
    sp_namedview_update_layers_from_document(static_cast<SPDesktop*>(dtw->view));
}

void sp_ui_new_view_preview()
{
    SPDocument *document = SP_ACTIVE_DOCUMENT;
    if ( document ) {
        SPViewWidget *dtw = reinterpret_cast<SPViewWidget *>(sp_svg_view_widget_new(document));
        g_return_if_fail(dtw != NULL);
        SP_SVG_VIEW_WIDGET(dtw)->setResize(true, 400.0, 400.0);

        sp_create_window(dtw, FALSE);
    }
}

void
sp_ui_close_view(GtkWidget */*widget*/)
{
    SPDesktop *dt = SP_ACTIVE_DESKTOP;

    if (dt == NULL) {
        return;
    }

    if (dt->shutdown()) {
        return; // Shutdown operation has been canceled, so do nothing
    }

    // If closing the last document, open a new document so Inkscape doesn't quit.
    std::list<SPDesktop *> desktops;
    INKSCAPE.get_all_desktops(desktops);
    if (desktops.size() == 1) {
        Glib::ustring templateUri = sp_file_default_template_uri();
        SPDocument *doc = SPDocument::createNewDoc( templateUri.c_str() , TRUE, true );
        // Set viewBox if it doesn't exist
        if (!doc->getRoot()->viewBox_set) {
            doc->setViewBox(Geom::Rect::from_xywh(0, 0, doc->getWidth().value(doc->getDisplayUnit()), doc->getHeight().value(doc->getDisplayUnit())));
        }
        dt->change_document(doc);
        sp_namedview_window_from_document(dt);
        sp_namedview_update_layers_from_document(dt);
        return;
    }

    // Shutdown can proceed; use the stored reference to the desktop here instead of the current SP_ACTIVE_DESKTOP,
    // because the user might have changed the focus in the meantime (see bug #381357 on Launchpad)
    dt->destroyWidget();
}


unsigned int
sp_ui_close_all(void)
{
    /* Iterate through all the windows, destroying each in the order they
       become active */
    while (SP_ACTIVE_DESKTOP) {
        SPDesktop *dt = SP_ACTIVE_DESKTOP;
        if (dt->shutdown()) {
            /* The user canceled the operation, so end doing the close */
            return FALSE;
        }
        // Shutdown can proceed; use the stored reference to the desktop here instead of the current SP_ACTIVE_DESKTOP,
        // because the user might have changed the focus in the meantime (see bug #381357 on Launchpad)
        dt->destroyWidget();
    }

    return TRUE;
}

/*
 * Some day when the right-click menus are ready to start working
 * smarter with the verbs, we'll need to change this NULL being
 * sent to sp_action_perform to something useful, or set some kind
 * of global "right-clicked position" variable for actions to
 * investigate when they're called.
 */
static void
sp_ui_menu_activate(void */*object*/, SPAction *action)
{
    if (!temporarily_block_actions) {
        sp_action_perform(action, NULL);
    }
}

static void
sp_ui_menu_select_action(void */*object*/, SPAction *action)
{
    sp_action_get_view(action)->tipsMessageContext()->set(Inkscape::NORMAL_MESSAGE, action->tip);
}

static void
sp_ui_menu_deselect_action(void */*object*/, SPAction *action)
{
    sp_action_get_view(action)->tipsMessageContext()->clear();
}

static void
sp_ui_menu_select(gpointer object, gpointer tip)
{
    Inkscape::UI::View::View *view = static_cast<Inkscape::UI::View::View*> (g_object_get_data(G_OBJECT(object), "view"));
    view->tipsMessageContext()->set(Inkscape::NORMAL_MESSAGE, (gchar *)tip);
}

static void
sp_ui_menu_deselect(gpointer object)
{
    Inkscape::UI::View::View *view = static_cast<Inkscape::UI::View::View*>  (g_object_get_data(G_OBJECT(object), "view"));
    view->tipsMessageContext()->clear();
}


void
sp_ui_dialog_title_string(Inkscape::Verb *verb, gchar *c)
{
    SPAction     *action;
    unsigned int shortcut;
    gchar        *s;
    gchar        *atitle;

    action = verb->get_action(Inkscape::ActionContext());
    if (!action)
        return;

    atitle = sp_action_get_title(action);

    s = g_stpcpy(c, atitle);

    g_free(atitle);

    shortcut = sp_shortcut_get_primary(verb);
    if (shortcut!=GDK_KEY_VoidSymbol) {
        gchar* key = sp_shortcut_get_label(shortcut);
        s = g_stpcpy(s, " (");
        s = g_stpcpy(s, key);
        g_stpcpy(s, ")");
        g_free(key);
    }
}

/**
 * Appends a custom menu UI from a verb.
 * 
 * @see ContextMenu::AppendItemFromVerb for a c++ified alternative. Consider dropping sp_ui_menu_append_item_from_verb when c++ifying interface.cpp.
 *
 * @param menu      The menu to which the item will be appended
 * @param verb      The verb from which the item's label, action and icon (optionally) will be read
 * @param view
 * @param show_icon True if an icon should be displayed before the menu item's label
 * @param radio     True if a radio button should be displayed next to the menu item
 * @param group     The radio button group that the item should belong to
 *
 * @details The show_icon flag should be used very sparingly because menu icons are not recommended
 *          any longer under the GNOME HIG.  Also, note that the text appears after the icon, and
 *          so will be indented relative to "normal" menu items.  As such, menus will look best if
 *          all the items with icons are grouped together between a pair of separators.
 */
static GtkWidget *sp_ui_menu_append_item_from_verb(GtkMenu                  *menu,
                                                   Inkscape::Verb           *verb,
                                                   Inkscape::UI::View::View *view,
                                                   bool                      show_icon = false,
                                                   bool                      radio     = false,
                                                   GSList                   *group     = NULL)
{
    GtkWidget *item;

    // Just create a menu separator if this isn't a real action.
    // Otherwise, create a real menu item
    if (verb->get_code() == SP_VERB_NONE) {
        item = gtk_separator_menu_item_new();
    } else {
        SPAction *action = verb->get_action(Inkscape::ActionContext(view));

        if (!action) return NULL;

        // Create the menu item itself, either as a radio menu item, or just
        // a regular menu item depending on whether the "radio" flag is set
        if (radio) {
            item = gtk_radio_menu_item_new(group);
        } else {
            item = gtk_menu_item_new();
        }

        // Create a box to contain all the widgets (icon, label, accelerator)
        // that will go inside the menu item
        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

        // Now create the label and add it to the menu item
        GtkWidget *label = gtk_accel_label_new(action->name);
        gtk_label_set_markup_with_mnemonic( GTK_LABEL(label), action->name);
        gtk_label_set_use_underline(GTK_LABEL(label), true);

#if GTK_CHECK_VERSION(3,16,0)
        gtk_label_set_xalign(GTK_LABEL(label), 0.0);
#else
        gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
#endif

        GtkAccelGroup *accel_group = sp_shortcut_get_accel_group();
        gtk_menu_set_accel_group(menu, accel_group);

        sp_shortcut_add_accelerator(item, sp_shortcut_get_primary(verb));
        gtk_accel_label_set_accel_widget(GTK_ACCEL_LABEL(label), item);

        GtkWidget *icon;

        // If there is an image associated with the action, then we can add it as an
        // icon for the menu item.  If not, give the label a bit more space
        if(show_icon && action->image) {
            icon = gtk_image_new_from_icon_name(action->image, GTK_ICON_SIZE_MENU);
        }
        else {
            icon = gtk_label_new(NULL); // A fake icon just to act as a placeholder
        }

        gtk_box_pack_start(GTK_BOX(box), icon, FALSE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(box), label, TRUE, TRUE, 0);

        // Finally, pack all the widgets into the menu item
        gtk_container_add(GTK_CONTAINER(item), box);


        action->signal_set_sensitive.connect(
            sigc::bind<0>(
                sigc::ptr_fun(&gtk_widget_set_sensitive),
                item));
        action->signal_set_name.connect(
            sigc::bind<0>(
                sigc::ptr_fun(&sp_ui_menu_item_set_name),
                item));

        if (!action->sensitive) {
            gtk_widget_set_sensitive(item, FALSE);
        }

        gtk_widget_set_events(item, GDK_KEY_PRESS_MASK);
        g_object_set_data(G_OBJECT(item), "view", (gpointer) view);
        g_signal_connect( G_OBJECT(item), "activate", G_CALLBACK(sp_ui_menu_activate), action );
        g_signal_connect( G_OBJECT(item), "select", G_CALLBACK(sp_ui_menu_select_action), action );
        g_signal_connect( G_OBJECT(item), "deselect", G_CALLBACK(sp_ui_menu_deselect_action), action );
    }

    gtk_widget_show_all(item);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    return item;

} // end of sp_ui_menu_append_item_from_verb


Glib::ustring getLayoutPrefPath( Inkscape::UI::View::View *view )
{
    Glib::ustring prefPath;

    if (reinterpret_cast<SPDesktop*>(view)->is_focusMode()) {
        prefPath = "/focus/";
    } else if (reinterpret_cast<SPDesktop*>(view)->is_fullscreen()) {
        prefPath = "/fullscreen/";
    } else {
        prefPath = "/window/";
    }

    return prefPath;
}

static void
checkitem_toggled(GtkCheckMenuItem *menuitem, gpointer user_data)
{
    gchar const *pref = (gchar const *) user_data;
    Inkscape::UI::View::View *view = (Inkscape::UI::View::View *) g_object_get_data(G_OBJECT(menuitem), "view");
    SPAction *action = (SPAction *) g_object_get_data(G_OBJECT(menuitem), "action");

    if (action) {

        sp_ui_menu_activate(menuitem, action);

    } else if (pref) {
        // All check menu items should have actions now, but just in case
        Glib::ustring pref_path = getLayoutPrefPath( view );
        pref_path += pref;
        pref_path += "/state";

        Inkscape::Preferences *prefs = Inkscape::Preferences::get();
        gboolean checked = gtk_check_menu_item_get_active(menuitem);
        prefs->setBool(pref_path, checked);

        reinterpret_cast<SPDesktop*>(view)->layoutWidget();
    }
}

static bool getViewStateFromPref(Inkscape::UI::View::View *view, gchar const *pref)
{
    Glib::ustring pref_path = getLayoutPrefPath( view );
    pref_path += pref;
    pref_path += "/state";

    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    return prefs->getBool(pref_path, true);
}

static gboolean checkitem_update(GtkWidget *widget, cairo_t * /*cr*/, gpointer user_data)
{
    GtkCheckMenuItem *menuitem=GTK_CHECK_MENU_ITEM(widget);

    gchar const *pref = (gchar const *) user_data;
    Inkscape::UI::View::View *view = (Inkscape::UI::View::View *) g_object_get_data(G_OBJECT(menuitem), "view");
    SPAction *action = (SPAction *) g_object_get_data(G_OBJECT(menuitem), "action");
    SPDesktop *dt = static_cast<SPDesktop*>(view);

    bool ison = false;
    if (action) {

        if (!strcmp(action->id, "ToggleGrid")) {
            ison = dt->gridsEnabled();
        }
        else if (!strcmp(action->id, "EditGuidesToggleLock")) {
            ison = dt->namedview->lockguides;
        }
        else if (!strcmp(action->id, "ToggleGuides")) {
            ison = dt->namedview->getGuides();
        }
        else if (!strcmp(action->id, "ViewCmsToggle")) {
            ison = dt->colorProfAdjustEnabled();
        }
        else  {
            ison = getViewStateFromPref(view, pref);
        }
    } else if (pref) {
        // The Show/Hide menu items without actions
        ison = getViewStateFromPref(view, pref);
    }

    g_signal_handlers_block_by_func(G_OBJECT(menuitem), (gpointer)(GCallback)checkitem_toggled, user_data);
    gtk_check_menu_item_set_active(menuitem, ison);
    g_signal_handlers_unblock_by_func(G_OBJECT(menuitem), (gpointer)(GCallback)checkitem_toggled, user_data);

    return FALSE;
}


static void taskToggled(GtkCheckMenuItem *menuitem, gpointer userData)
{
    if ( gtk_check_menu_item_get_active(menuitem) ) {
        gint taskNum = GPOINTER_TO_INT(userData);
        taskNum = (taskNum < 0) ? 0 : (taskNum > 2) ? 2 : taskNum;

        Inkscape::UI::View::View *view = (Inkscape::UI::View::View *) g_object_get_data(G_OBJECT(menuitem), "view");

        // note: this will change once more options are in the task set support:
        Inkscape::UI::UXManager::getInstance()->setTask( dynamic_cast<SPDesktop*>(view), taskNum );
    }
}


/**
 * Callback function to update the status of the radio buttons in the View -> Display mode menu (Normal, No Filters, Outline) and Color display mode.
 */
static gboolean update_view_menu(GtkWidget *widget, cairo_t * /*cr*/, gpointer user_data)
{
    SPAction *action = (SPAction *) user_data;
    g_assert(action->id != NULL);

    Inkscape::UI::View::View *view = (Inkscape::UI::View::View *) g_object_get_data(G_OBJECT(widget), "view");
    SPDesktop *dt = static_cast<SPDesktop*>(view);
    Inkscape::RenderMode mode = dt->getMode();
    Inkscape::ColorMode colormode = dt->getColorMode();

    bool new_state = false;
    if (!strcmp(action->id, "ViewModeNormal")) {
        new_state = mode == Inkscape::RENDERMODE_NORMAL;
    } else if (!strcmp(action->id, "ViewModeNoFilters")) {
        new_state = mode == Inkscape::RENDERMODE_NO_FILTERS;
    } else if (!strcmp(action->id, "ViewModeOutline")) {
        new_state = mode == Inkscape::RENDERMODE_OUTLINE;
    } else if (!strcmp(action->id, "ViewColorModeNormal")) {
        new_state = colormode == Inkscape::COLORMODE_NORMAL;
    } else if (!strcmp(action->id, "ViewColorModeGrayscale")) {
        new_state = colormode == Inkscape::COLORMODE_GRAYSCALE;
    } else if (!strcmp(action->id, "ViewColorModePrintColorsPreview")) {
        new_state = colormode == Inkscape::COLORMODE_PRINT_COLORS_PREVIEW;
    } else {
        g_warning("update_view_menu does not handle this verb");
    }

    if (new_state) { //only one of the radio buttons has to be activated; the others will automatically be deactivated
        if (!gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (widget))) {
            // When the GtkMenuItem version of the "activate" signal has been emitted by a GtkRadioMenuItem, there is a second
            // emission as the most recently active item is toggled to inactive. This is dealt with before the original signal is handled.
            // This emission however should not invoke any actions, hence we block it here:
            temporarily_block_actions = true;
            gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (widget), TRUE);
            temporarily_block_actions = false;
        }
    }

    return FALSE;
}

static void
sp_ui_menu_append_check_item_from_verb(GtkMenu *menu, Inkscape::UI::View::View *view, gchar const *label, gchar const *tip, gchar const *pref,
                                       void (*callback_toggle)(GtkCheckMenuItem *, gpointer user_data),
                                       gboolean (*callback_update)(GtkWidget *widget, cairo_t *cr, gpointer user_data),
                                       Inkscape::Verb *verb)
{
    unsigned int shortcut = (verb) ? sp_shortcut_get_primary(verb) : 0;
    SPAction *action = (verb) ? verb->get_action(Inkscape::ActionContext(view)) : 0;
    GtkWidget *item = gtk_check_menu_item_new_with_mnemonic(action ? action->name : label);

#if 0
    if (!action->sensitive) {
        gtk_widget_set_sensitive(item, FALSE);
    }
#endif

    sp_shortcut_add_accelerator(item, shortcut);

    gtk_widget_show(item);

    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    g_object_set_data(G_OBJECT(item), "view", (gpointer) view);
    g_object_set_data(G_OBJECT(item), "action", (gpointer) action);

    g_signal_connect( G_OBJECT(item), "toggled", (GCallback) callback_toggle, (void *) pref);

    g_signal_connect( G_OBJECT(item), "draw", (GCallback) callback_update, (void *) pref);

    (*callback_update)(item, NULL, (void *)pref);

    g_signal_connect( G_OBJECT(item), "select", G_CALLBACK(sp_ui_menu_select), (gpointer) (action ? action->tip : tip));
    g_signal_connect( G_OBJECT(item), "deselect", G_CALLBACK(sp_ui_menu_deselect), NULL);

}

static void
sp_recent_open(GtkRecentChooser *recent_menu, gpointer /*user_data*/)
{
    // dealing with the bizarre filename convention in Inkscape for now
    gchar *uri = gtk_recent_chooser_get_current_uri(GTK_RECENT_CHOOSER(recent_menu));
    gchar *local_fn = g_filename_from_uri(uri, NULL, NULL);
    gchar *utf8_fn = g_filename_to_utf8(local_fn, -1, NULL, NULL, NULL);
    sp_file_open(utf8_fn, NULL);
    g_free(utf8_fn);
    g_free(local_fn);
    g_free(uri);
}

static void
sp_ui_checkboxes_menus(GtkMenu *m, Inkscape::UI::View::View *view)
{
    //sp_ui_menu_append_check_item_from_verb(m, view, _("_Menu"), _("Show or hide the menu bar"), "menu",
    //                                       checkitem_toggled, checkitem_update, 0);
    sp_ui_menu_append_check_item_from_verb(m, view, NULL, NULL, "commands",
                                           checkitem_toggled, checkitem_update, Inkscape::Verb::get(SP_VERB_TOGGLE_COMMANDS_TOOLBAR));
    sp_ui_menu_append_check_item_from_verb(m, view,NULL, NULL, "snaptoolbox",
                                           checkitem_toggled, checkitem_update, Inkscape::Verb::get(SP_VERB_TOGGLE_SNAP_TOOLBAR));
    sp_ui_menu_append_check_item_from_verb(m, view, NULL, NULL, "toppanel",
                                           checkitem_toggled, checkitem_update, Inkscape::Verb::get(SP_VERB_TOGGLE_TOOL_TOOLBAR));
    sp_ui_menu_append_check_item_from_verb(m, view, NULL, NULL, "toolbox",
                                           checkitem_toggled, checkitem_update, Inkscape::Verb::get(SP_VERB_TOGGLE_TOOLBOX));
    sp_ui_menu_append_check_item_from_verb(m, view, NULL, NULL, "rulers",
                                           checkitem_toggled, checkitem_update, Inkscape::Verb::get(SP_VERB_TOGGLE_RULERS));
    sp_ui_menu_append_check_item_from_verb(m, view, NULL, NULL, "scrollbars",
                                           checkitem_toggled, checkitem_update, Inkscape::Verb::get(SP_VERB_TOGGLE_SCROLLBARS));
    sp_ui_menu_append_check_item_from_verb(m, view, NULL, NULL, "panels",
                                           checkitem_toggled, checkitem_update, Inkscape::Verb::get(SP_VERB_TOGGLE_PALETTE));
    sp_ui_menu_append_check_item_from_verb(m, view, NULL, NULL, "statusbar",
                                           checkitem_toggled, checkitem_update, Inkscape::Verb::get(SP_VERB_TOGGLE_STATUSBAR));
}


static void addTaskMenuItems(GtkMenu *menu, Inkscape::UI::View::View *view)
{
    gchar const* data[] = {
        C_("Interface setup", "Default"), _("Default interface setup"),
        C_("Interface setup", "Custom"), _("Setup for custom task"),
        C_("Interface setup", "Wide"), _("Setup for widescreen work"),
        0, 0
    };

    GSList *group = 0;
    int count = 0;
    gint active = Inkscape::UI::UXManager::getInstance()->getDefaultTask( dynamic_cast<SPDesktop*>(view) );
    for (gchar const **strs = data; strs[0]; strs += 2, count++)
    {
        GtkWidget *item = gtk_radio_menu_item_new_with_label( group, strs[0] );
        group = gtk_radio_menu_item_get_group( GTK_RADIO_MENU_ITEM(item) );
        if ( count == active )
        {
            gtk_check_menu_item_set_active( GTK_CHECK_MENU_ITEM(item), TRUE );
        }

        g_object_set_data( G_OBJECT(item), "view", view );
        g_signal_connect( G_OBJECT(item), "toggled", reinterpret_cast<GCallback>(taskToggled), GINT_TO_POINTER(count) );
        g_signal_connect( G_OBJECT(item), "select", G_CALLBACK(sp_ui_menu_select), const_cast<gchar*>(strs[1]) );
        g_signal_connect( G_OBJECT(item), "deselect", G_CALLBACK(sp_ui_menu_deselect), 0 );

        gtk_widget_show( item );
        gtk_menu_shell_append( GTK_MENU_SHELL(menu), item );
    }
}


/**
 * Observer that updates the recent list's max document count.
 */
class MaxRecentObserver : public Inkscape::Preferences::Observer {
public:
    MaxRecentObserver(GtkWidget *recent_menu) :
        Observer("/options/maxrecentdocuments/value"),
        _rm(recent_menu)
    {}
    virtual void notify(Inkscape::Preferences::Entry const &e) {
        gtk_recent_chooser_set_limit(GTK_RECENT_CHOOSER(_rm), e.getInt());
        // hack: the recent menu doesn't repopulate after changing the limit, so we force it
        g_signal_emit_by_name((gpointer) gtk_recent_manager_get_default(), "changed");
    }
private:
    GtkWidget *_rm;
};

/**
 * This function turns XML into a menu.
 *
 *  This function is realitively simple as it just goes through the XML
 *  and parses the individual elements.  In the case of a submenu, it
 *  just calls itself recursively.  Because it is only reasonable to have
 *  a couple of submenus, it is unlikely this will go more than two or
 *  three times.
 *
 *  In the case of an unrecognized verb, a menu item is made to identify
 *  the verb that is missing, and display that.  The menu item is also made
 *  insensitive.
 *
 * @param  menus  This is the XML that defines the menu
 * @param  menu   Menu to be added to
 * @param  view   The View that this menu is being built for
 */
static void sp_ui_build_dyn_menus(Inkscape::XML::Node *menus, GtkWidget *menu, Inkscape::UI::View::View *view)
{
    if (menus == NULL) return;
    if (menu == NULL)  return;
    GSList *group = NULL;

    for (Inkscape::XML::Node *menu_pntr = menus;
         menu_pntr != NULL;
         menu_pntr = menu_pntr->next()) {
        if (!strcmp(menu_pntr->name(), "submenu")) {
            GtkWidget *mitem;
            if (menu_pntr->attribute("_name") != NULL) {
                mitem = gtk_menu_item_new_with_mnemonic(_(menu_pntr->attribute("_name")));
            } else {
                mitem = gtk_menu_item_new_with_mnemonic(menu_pntr->attribute("name"));
            }
            GtkWidget *submenu = gtk_menu_new();
            sp_ui_build_dyn_menus(menu_pntr->firstChild(), submenu, view);
            gtk_menu_item_set_submenu(GTK_MENU_ITEM(mitem), GTK_WIDGET(submenu));
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), mitem);
            continue;
        }
        if (!strcmp(menu_pntr->name(), "verb")) {
            gchar const *verb_name = menu_pntr->attribute("verb-id");

            // Check if the "show-icon" attribute is set, and set the flag here accordingly
            bool show_icon = false;

            if(menu_pntr->attribute("show-icon") != NULL) {
                show_icon = true;
            }

            Inkscape::Verb *verb = Inkscape::Verb::getbyid(verb_name);

            if (verb != NULL) {
                if (menu_pntr->attribute("radio") != NULL) {
                    GtkWidget *item = sp_ui_menu_append_item_from_verb (GTK_MENU(menu), verb, view, show_icon, true, group);
                    group = gtk_radio_menu_item_get_group( GTK_RADIO_MENU_ITEM(item));
                    if (menu_pntr->attribute("default") != NULL) {
                        gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (item), TRUE);
                    }
                    if (verb->get_code() != SP_VERB_NONE) {
                        SPAction *action = verb->get_action(Inkscape::ActionContext(view));
                        g_signal_connect( G_OBJECT(item), "draw", (GCallback) update_view_menu, (void *) action);
                    }
                } else if (menu_pntr->attribute("check") != NULL) {
                    if (verb->get_code() != SP_VERB_NONE) {
                        SPAction *action = verb->get_action(Inkscape::ActionContext(view));
                        sp_ui_menu_append_check_item_from_verb(GTK_MENU(menu), view, action->name, action->tip, NULL,
                            checkitem_toggled, checkitem_update, verb);
                    }
                } else {
                    sp_ui_menu_append_item_from_verb(GTK_MENU(menu), verb, view, show_icon);
                    group = NULL;
                }
            } else {
                gchar string[120];
                g_snprintf(string, 120, _("Verb \"%s\" Unknown"), verb_name);
                string[119] = '\0'; /* may not be terminated */
                GtkWidget *item = gtk_menu_item_new_with_label(string);
                gtk_widget_set_sensitive(item, false);
                gtk_widget_show(item);
                gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
            }
            continue;
        }
        if (!strcmp(menu_pntr->name(), "separator")) {
            GtkWidget *item = gtk_separator_menu_item_new();
            gtk_widget_show(item);
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
            continue;
        }
        
        if (!strcmp(menu_pntr->name(), "recent-file-list")) {
            Inkscape::Preferences *prefs = Inkscape::Preferences::get();

            // create recent files menu
            int max_recent = prefs->getInt("/options/maxrecentdocuments/value");
            GtkWidget *recent_menu = gtk_recent_chooser_menu_new_for_manager(gtk_recent_manager_get_default());
            gtk_recent_chooser_set_limit(GTK_RECENT_CHOOSER(recent_menu), max_recent);
            // sort most recently used documents first to preserve previous behavior
            gtk_recent_chooser_set_sort_type(GTK_RECENT_CHOOSER(recent_menu), GTK_RECENT_SORT_MRU);
            g_signal_connect(G_OBJECT(recent_menu), "item-activated", G_CALLBACK(sp_recent_open), (gpointer) NULL);

            // add filter to only open files added by Inkscape
            GtkRecentFilter *inkscape_only_filter = gtk_recent_filter_new();
            gtk_recent_filter_add_application(inkscape_only_filter, g_get_prgname());
            gtk_recent_chooser_add_filter(GTK_RECENT_CHOOSER(recent_menu), inkscape_only_filter);

            gtk_recent_chooser_set_show_tips (GTK_RECENT_CHOOSER(recent_menu), TRUE);
            gtk_recent_chooser_set_show_not_found (GTK_RECENT_CHOOSER(recent_menu), FALSE);

            GtkWidget *recent_item = gtk_menu_item_new_with_mnemonic(_("Open _Recent"));
            gtk_menu_item_set_submenu(GTK_MENU_ITEM(recent_item), recent_menu);

            gtk_menu_shell_append(GTK_MENU_SHELL(menu), GTK_WIDGET(recent_item));
            // this will just sit and update the list's item count
            static MaxRecentObserver *mro = new MaxRecentObserver(recent_menu);
            prefs->addObserver(*mro);
            continue;
        }
        if (!strcmp(menu_pntr->name(), "objects-checkboxes")) {
            sp_ui_checkboxes_menus(GTK_MENU(menu), view);
            continue;
        }
        if (!strcmp(menu_pntr->name(), "task-checkboxes")) {
            addTaskMenuItems(GTK_MENU(menu), view);
            continue;
        }
    }
}

GtkWidget *sp_ui_main_menubar(Inkscape::UI::View::View *view)
{
    GtkWidget *mbar = gtk_menu_bar_new();
    sp_ui_build_dyn_menus(INKSCAPE.get_menus(), mbar, view);
    return mbar;
}


/* Drag and Drop */
void
sp_ui_drag_data_received(GtkWidget *widget,
                         GdkDragContext *drag_context,
                         gint x, gint y,
                         GtkSelectionData *data,
                         guint info,
                         guint /*event_time*/,
                         gpointer /*user_data*/)
{
    SPDocument *doc = SP_ACTIVE_DOCUMENT;
    SPDesktop *desktop = SP_ACTIVE_DESKTOP;

    switch (info) {
#if ENABLE_MAGIC_COLORS
        case APP_X_INKY_COLOR:
        {
            int destX = 0;
            int destY = 0;
            gtk_widget_translate_coordinates( widget, &(desktop->canvas->widget), x, y, &destX, &destY );
            Geom::Point where( sp_canvas_window_to_world( desktop->canvas, Geom::Point( destX, destY ) ) );

            SPItem *item = desktop->getItemAtPoint( where, true );
            if ( item )
            {
                bool fillnotstroke = (drag_context->action != GDK_ACTION_MOVE);

                if ( data->length >= 8 ) {
                    cmsHPROFILE srgbProf = cmsCreate_sRGBProfile();

                    gchar c[64] = {0};
                    // Careful about endian issues.
                    guint16* dataVals = (guint16*)data->data;
                    sp_svg_write_color( c, sizeof(c),
                                        SP_RGBA32_U_COMPOSE(
                                            0x0ff & (dataVals[0] >> 8),
                                            0x0ff & (dataVals[1] >> 8),
                                            0x0ff & (dataVals[2] >> 8),
                                            0xff // can't have transparency in the color itself
                                            //0x0ff & (data->data[3] >> 8),
                                            ));
                    SPCSSAttr *css = sp_repr_css_attr_new();
                    bool updatePerformed = false;

                    if ( data->length > 14 ) {
                        int flags = dataVals[4];

                        // piggie-backed palette entry info
                        int index = dataVals[5];
                        Glib::ustring palName;
                        for ( int i = 0; i < dataVals[6]; i++ ) {
                            palName += (gunichar)dataVals[7+i];
                        }

                        // Now hook in a magic tag of some sort.
                        if ( !palName.empty() && (flags & 1) ) {
                            gchar* str = g_strdup_printf("%d|", index);
                            palName.insert( 0, str );
                            g_free(str);
                            str = 0;

                            item->setAttribute( 
                                fillnotstroke ? "inkscape:x-fill-tag":"inkscape:x-stroke-tag",
                                palName.c_str(),
                                false );
                            item->updateRepr();

                            sp_repr_css_set_property( css, fillnotstroke ? "fill":"stroke", c );
                            updatePerformed = true;
                        }
                    }

                    if ( !updatePerformed ) {
                        sp_repr_css_set_property( css, fillnotstroke ? "fill":"stroke", c );
                    }

                    sp_desktop_apply_css_recursive( item, css, true );
                    item->updateRepr();

                    SPDocumentUndo::done( doc , SP_VERB_NONE,
                                      _("Drop color"));

                    if ( srgbProf ) {
                        cmsCloseProfile( srgbProf );
                    }
                }
            }
        }
        break;
#endif // ENABLE_MAGIC_COLORS

        case APP_X_COLOR:
        {
            int destX = 0;
            int destY = 0;
            gtk_widget_translate_coordinates( widget, GTK_WIDGET(desktop->canvas), x, y, &destX, &destY );
            Geom::Point where( sp_canvas_window_to_world( desktop->canvas, Geom::Point( destX, destY ) ) );
            Geom::Point const button_dt(desktop->w2d(where));
            Geom::Point const button_doc(desktop->dt2doc(button_dt));

            if ( gtk_selection_data_get_length (data) == 8 ) {
                gchar colorspec[64] = {0};
                // Careful about endian issues.
                guint16* dataVals = (guint16*)gtk_selection_data_get_data (data);
                sp_svg_write_color( colorspec, sizeof(colorspec),
                                    SP_RGBA32_U_COMPOSE(
                                        0x0ff & (dataVals[0] >> 8),
                                        0x0ff & (dataVals[1] >> 8),
                                        0x0ff & (dataVals[2] >> 8),
                                        0xff // can't have transparency in the color itself
                                        //0x0ff & (data->data[3] >> 8),
                                        ));

                SPItem *item = desktop->getItemAtPoint( where, true );

                bool consumed = false;
                if (desktop->event_context && desktop->event_context->get_drag()) {
                    consumed = desktop->event_context->get_drag()->dropColor(item, colorspec, button_dt);
                    if (consumed) {
                        DocumentUndo::done( doc , SP_VERB_NONE, _("Drop color on gradient") );
                        desktop->event_context->get_drag()->updateDraggers();
                    }
                }

                //if (!consumed && tools_active(desktop, TOOLS_TEXT)) {
                //    consumed = sp_text_context_drop_color(c, button_doc);
                //    if (consumed) {
                //        SPDocumentUndo::done( doc , SP_VERB_NONE, _("Drop color on gradient stop"));
                //    }
                //}

                if (!consumed && item) {
                    bool fillnotstroke = (gdk_drag_context_get_actions (drag_context) != GDK_ACTION_MOVE);
                    if (fillnotstroke &&
                        (SP_IS_SHAPE(item) || SP_IS_TEXT(item) || SP_IS_FLOWTEXT(item))) {
                        Path *livarot_path = Path_for_item(item, true, true);
                        livarot_path->ConvertWithBackData(0.04);

                        boost::optional<Path::cut_position> position = get_nearest_position_on_Path(livarot_path, button_doc);
                        if (position) {
                            Geom::Point nearest = get_point_on_Path(livarot_path, position->piece, position->t);
                            Geom::Point delta = nearest - button_doc;
                            Inkscape::Preferences *prefs = Inkscape::Preferences::get();
                            delta = desktop->d2w(delta);
                            double stroke_tolerance =
                                ( !item->style->stroke.isNone() ?
                                  desktop->current_zoom() *
                                  item->style->stroke_width.computed *
                                  item->i2dt_affine().descrim() * 0.5
                                  : 0.0)
                                + prefs->getIntLimited("/options/dragtolerance/value", 0, 0, 100);

                            if (Geom::L2 (delta) < stroke_tolerance) {
                                fillnotstroke = false;
                            }
                        }
                        delete livarot_path;
                    }

                    SPCSSAttr *css = sp_repr_css_attr_new();
                    sp_repr_css_set_property( css, fillnotstroke ? "fill":"stroke", colorspec );

                    sp_desktop_apply_css_recursive( item, css, true );
                    item->updateRepr();

                    DocumentUndo::done( doc , SP_VERB_NONE,
                                        _("Drop color") );
                }
            }
        }
        break;

        case APP_OSWB_COLOR:
        {
            bool worked = false;
            Glib::ustring colorspec;
            if ( gtk_selection_data_get_format (data) == 8 ) {
                ege::PaintDef color;
                worked = color.fromMIMEData("application/x-oswb-color",
                                            reinterpret_cast<char const *>(gtk_selection_data_get_data (data)),
                                            gtk_selection_data_get_length (data),
                                            gtk_selection_data_get_format (data));
                if ( worked ) {
                    if ( color.getType() == ege::PaintDef::CLEAR ) {
                        colorspec = ""; // TODO check if this is sufficient
                    } else if ( color.getType() == ege::PaintDef::NONE ) {
                        colorspec = "none";
                    } else {
                        unsigned int r = color.getR();
                        unsigned int g = color.getG();
                        unsigned int b = color.getB();

                        SPGradient* matches = 0;
                        std::vector<SPObject *> gradients = doc->getResourceList("gradient");
                        for (std::vector<SPObject *>::const_iterator item = gradients.begin(); item != gradients.end(); ++item) {
                            SPGradient* grad = SP_GRADIENT(*item);
                            if ( color.descr == grad->getId() ) {
                                if ( grad->hasStops() ) {
                                    matches = grad;
                                    break;
                                }
                            }
                        }
                        if (matches) {
                            colorspec = "url(#";
                            colorspec += matches->getId();
                            colorspec += ")";
                        } else {
                            gchar* tmp = g_strdup_printf("#%02x%02x%02x", r, g, b);
                            colorspec = tmp;
                            g_free(tmp);
                        }
                    }
                }
            }
            if ( worked ) {
                int destX = 0;
                int destY = 0;
                gtk_widget_translate_coordinates( widget, GTK_WIDGET(desktop->canvas), x, y, &destX, &destY );
                Geom::Point where( sp_canvas_window_to_world( desktop->canvas, Geom::Point( destX, destY ) ) );
                Geom::Point const button_dt(desktop->w2d(where));
                Geom::Point const button_doc(desktop->dt2doc(button_dt));

                SPItem *item = desktop->getItemAtPoint( where, true );

                bool consumed = false;
                if (desktop->event_context && desktop->event_context->get_drag()) {
                    consumed = desktop->event_context->get_drag()->dropColor(item, colorspec.c_str(), button_dt);
                    if (consumed) {
                        DocumentUndo::done( doc , SP_VERB_NONE, _("Drop color on gradient") );
                        desktop->event_context->get_drag()->updateDraggers();
                    }
                }

                if (!consumed && item) {
                    bool fillnotstroke = (gdk_drag_context_get_actions (drag_context) != GDK_ACTION_MOVE);
                    if (fillnotstroke &&
                        (SP_IS_SHAPE(item) || SP_IS_TEXT(item) || SP_IS_FLOWTEXT(item))) {
                        Path *livarot_path = Path_for_item(item, true, true);
                        livarot_path->ConvertWithBackData(0.04);

                        boost::optional<Path::cut_position> position = get_nearest_position_on_Path(livarot_path, button_doc);
                        if (position) {
                            Geom::Point nearest = get_point_on_Path(livarot_path, position->piece, position->t);
                            Geom::Point delta = nearest - button_doc;
                            Inkscape::Preferences *prefs = Inkscape::Preferences::get();
                            delta = desktop->d2w(delta);
                            double stroke_tolerance =
                                ( !item->style->stroke.isNone() ?
                                  desktop->current_zoom() *
                                  item->style->stroke_width.computed *
                                  item->i2dt_affine().descrim() * 0.5
                                  : 0.0)
                                + prefs->getIntLimited("/options/dragtolerance/value", 0, 0, 100);

                            if (Geom::L2 (delta) < stroke_tolerance) {
                                fillnotstroke = false;
                            }
                        }
                        delete livarot_path;
                    }

                    SPCSSAttr *css = sp_repr_css_attr_new();
                    sp_repr_css_set_property( css, fillnotstroke ? "fill":"stroke", colorspec.c_str() );

                    sp_desktop_apply_css_recursive( item, css, true );
                    item->updateRepr();

                    DocumentUndo::done( doc , SP_VERB_NONE,
                                        _("Drop color") );
                }
            }
        }
        break;

        case SVG_DATA:
        case SVG_XML_DATA: {
            gchar *svgdata = (gchar *)gtk_selection_data_get_data (data);

            Inkscape::XML::Document *rnewdoc = sp_repr_read_mem(svgdata, gtk_selection_data_get_length (data), SP_SVG_NS_URI);

            if (rnewdoc == NULL) {
                sp_ui_error_dialog(_("Could not parse SVG data"));
                return;
            }

            Inkscape::XML::Node *repr = rnewdoc->root();
            gchar const *style = repr->attribute("style");

            Inkscape::XML::Node *newgroup = rnewdoc->createElement("svg:g");
            newgroup->setAttribute("style", style);

            Inkscape::XML::Document * xml_doc =  doc->getReprDoc();
            for (Inkscape::XML::Node *child = repr->firstChild(); child != NULL; child = child->next()) {
                Inkscape::XML::Node *newchild = child->duplicate(xml_doc);
                newgroup->appendChild(newchild);
            }

            Inkscape::GC::release(rnewdoc);

            // Add it to the current layer

            // Greg's edits to add intelligent positioning of svg drops
            SPObject *new_obj = NULL;
            new_obj = desktop->currentLayer()->appendChildRepr(newgroup);

            Inkscape::Selection *selection = desktop->getSelection();
            selection->set(SP_ITEM(new_obj));

            // move to mouse pointer
            {
                desktop->getDocument()->ensureUpToDate();
                Geom::OptRect sel_bbox = selection->visualBounds();
                if (sel_bbox) {
                    Geom::Point m( desktop->point() - sel_bbox->midpoint() );
                    selection->moveRelative(m, false);
                }
            }

            Inkscape::GC::release(newgroup);
            DocumentUndo::done( doc, SP_VERB_NONE,
                             _("Drop SVG") );
            break;
        }

        case URI_LIST: {
            gchar *uri = (gchar *)gtk_selection_data_get_data (data);
            sp_ui_import_files(uri);
            break;
        }

        case APP_X_INK_PASTE: {
            Inkscape::UI::ClipboardManager *cm = Inkscape::UI::ClipboardManager::get();
            cm->paste(desktop);
            DocumentUndo::done( doc, SP_VERB_NONE, _("Drop Symbol") );
            break;
        }

        case PNG_DATA:
        case JPEG_DATA:
        case IMAGE_DATA: {
            Inkscape::Extension::Extension *ext = Inkscape::Extension::find_by_mime((info == JPEG_DATA ? "image/jpeg" : "image/png"));
            bool save = (strcmp(ext->get_param_optiongroup("link"), "embed") == 0);
            ext->set_param_optiongroup("link", "embed");
            ext->set_gui(false);

            gchar *filename = g_build_filename( g_get_tmp_dir(), "inkscape-dnd-import", NULL );
            g_file_set_contents(filename, 
                reinterpret_cast<gchar const *>(gtk_selection_data_get_data (data)), 
                gtk_selection_data_get_length (data), 
                NULL);
            file_import(doc, filename, ext);
            g_free(filename);

            ext->set_param_optiongroup("link", save ? "embed" : "link");
            ext->set_gui(true);
            DocumentUndo::done( doc , SP_VERB_NONE,
                                _("Drop bitmap image") );
            break;
        }
    }
}

#include "ui/tools/gradient-tool.h"

void sp_ui_drag_motion( GtkWidget */*widget*/,
                        GdkDragContext */*drag_context*/,
                        gint /*x*/, gint /*y*/,
                        GtkSelectionData */*data*/,
                        guint /*info*/,
                        guint /*event_time*/,
                        gpointer /*user_data*/)
{
//     SPDocument *doc = SP_ACTIVE_DOCUMENT;
//     SPDesktop *desktop = SP_ACTIVE_DESKTOP;


//     g_message("drag-n-drop motion (%4d, %4d)  at %d", x, y, event_time);
}

static void sp_ui_drag_leave( GtkWidget */*widget*/,
                              GdkDragContext */*drag_context*/,
                              guint /*event_time*/,
                              gpointer /*user_data*/ )
{
//     g_message("drag-n-drop leave                at %d", event_time);
}

static void
sp_ui_import_files(gchar *buffer)
{
    GList *list = gnome_uri_list_extract_filenames(buffer);
    if (!list)
        return;
    g_list_foreach(list, sp_ui_import_one_file_with_check, NULL);
    g_list_foreach(list, (GFunc) g_free, NULL);
    g_list_free(list);
}

static void
sp_ui_import_one_file_with_check(gpointer filename, gpointer /*unused*/)
{
    if (filename) {
        if (strlen((char const *)filename) > 2)
            sp_ui_import_one_file((char const *)filename);
    }
}

static void
sp_ui_import_one_file(char const *filename)
{
    SPDocument *doc = SP_ACTIVE_DOCUMENT;
    if (!doc) return;

    if (filename == NULL) return;

    // Pass off to common implementation
    // TODO might need to get the proper type of Inkscape::Extension::Extension
    file_import( doc, filename, NULL );
}

void
sp_ui_error_dialog(gchar const *message)
{
    GtkWidget *dlg;
    gchar *safeMsg = Inkscape::IO::sanitizeString(message);

    dlg = gtk_message_dialog_new(NULL, GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR,
                                 GTK_BUTTONS_CLOSE, "%s", safeMsg);
    sp_transientize(dlg);
    gtk_window_set_resizable(GTK_WINDOW(dlg), FALSE);
    gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
    g_free(safeMsg);
}

bool
sp_ui_overwrite_file(gchar const *filename)
{
    bool return_value = FALSE;

    if (Inkscape::IO::file_test(filename, G_FILE_TEST_EXISTS)) {
        Gtk::Window *window = SP_ACTIVE_DESKTOP->getToplevel();
        gchar* baseName = g_path_get_basename( filename );
        gchar* dirName = g_path_get_dirname( filename );
        GtkWidget* dialog = gtk_message_dialog_new_with_markup( window->gobj(),
                                                                (GtkDialogFlags)(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
                                                                GTK_MESSAGE_QUESTION,
                                                                GTK_BUTTONS_NONE,
                                                                _( "<span weight=\"bold\" size=\"larger\">A file named \"%s\" already exists. Do you want to replace it?</span>\n\n"
                                                                   "The file already exists in \"%s\". Replacing it will overwrite its contents." ),
                                                                baseName,
                                                                dirName
            );
        gtk_dialog_add_buttons( GTK_DIALOG(dialog),
                                _("_Cancel"), GTK_RESPONSE_NO,
                                _("Replace"), GTK_RESPONSE_YES,
                                NULL );
        gtk_dialog_set_default_response( GTK_DIALOG(dialog), GTK_RESPONSE_YES );

        if ( gtk_dialog_run( GTK_DIALOG(dialog) ) == GTK_RESPONSE_YES ) {
            return_value = TRUE;
        } else {
            return_value = FALSE;
        }
        gtk_widget_destroy(dialog);
        g_free( baseName );
        g_free( dirName );
    } else {
        return_value = TRUE;
    }

    return return_value;
}

static void
sp_ui_menu_item_set_name(GtkWidget *data, Glib::ustring const &name)
{
    if (data || GTK_IS_BIN (data)) {
        void *child = gtk_bin_get_child (GTK_BIN (data));
        //child is either
        //- a GtkBox, whose first child is a label displaying name if the menu
        //item has an accel key
        //- a GtkLabel if the menu has no accel key
        if (child != NULL){
            if (GTK_IS_LABEL(child)) {
                gtk_label_set_markup_with_mnemonic(GTK_LABEL (child), name.c_str());
            } else if (GTK_IS_BOX(child)) {
                GList *children = gtk_container_get_children(GTK_CONTAINER(child));

                // Label is second child in list
                GtkWidget *label = GTK_WIDGET(children->next->data);

                gtk_label_set_markup_with_mnemonic(
                GTK_LABEL (label),
                name.c_str());
            }//else sp_ui_menu_append_item_from_verb has been modified and can set
            //a menu item in yet another way...
        }
    }
}
/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:fileencoding=utf-8:textwidth=99 :
