/* NovaSearch Panel Plugin */

#include <gtk/gtk.h>
#include <libxfce4panel/libxfce4panel.h>
#include <libxfce4util/libxfce4util.h>
#include <glib.h>
#include <keybinder.h>
#include "database.h"

/* Default keyboard shortcut */
#define DEFAULT_KEYBOARD_SHORTCUT "<Super>space"

/* Plugin structure */
typedef struct {
    XfcePanelPlugin *plugin;
    GtkWidget *button;
    GtkWidget *search_window;
    GtkWidget *search_entry;
    GtkWidget *results_list;
    GtkWidget *results_scroll;
    NovaSearchDB *db;
    guint debounce_timer;
    gchar *keyboard_shortcut;
    gboolean shortcut_registered;
} NovaSearchPlugin;

/* Forward declarations */
static void nova_search_plugin_construct(XfcePanelPlugin *plugin);
static NovaSearchPlugin* nova_search_plugin_new(XfcePanelPlugin *plugin);
static void nova_search_plugin_free(XfcePanelPlugin *plugin, NovaSearchPlugin *ns_plugin);
static void nova_search_show_window(NovaSearchPlugin *ns_plugin);
static void nova_search_hide_window(NovaSearchPlugin *ns_plugin);
static void nova_search_about_dialog(XfcePanelPlugin *plugin);
static void nova_search_configure_dialog(XfcePanelPlugin *plugin);
static void nova_search_create_window(NovaSearchPlugin *ns_plugin);
static gboolean nova_search_window_key_press(GtkWidget *widget, GdkEventKey *event, NovaSearchPlugin *ns_plugin);
static gboolean nova_search_window_focus_out(GtkWidget *widget, GdkEventFocus *event, NovaSearchPlugin *ns_plugin);
static void nova_search_entry_changed(GtkEntry *entry, NovaSearchPlugin *ns_plugin);
static gboolean nova_search_execute_query_delayed(gpointer user_data);
static void nova_search_execute_query(NovaSearchPlugin *ns_plugin, const char *query);
static void nova_search_clear_results(NovaSearchPlugin *ns_plugin);
static GtkWidget* nova_search_create_result_row(SearchResult *result);
static const char* nova_search_get_file_icon_name(const char *file_type);
static void nova_search_open_file(NovaSearchPlugin *ns_plugin, const char *file_path);
static void nova_search_row_activated(GtkListBox *list_box, GtkListBoxRow *row, NovaSearchPlugin *ns_plugin);
static gboolean nova_search_row_button_press(GtkWidget *widget, GdkEventButton *event, NovaSearchPlugin *ns_plugin);
static void nova_search_open_containing_folder(GtkMenuItem *menu_item, gpointer user_data);
static gchar* nova_search_read_keyboard_shortcut_from_config(void);
static gchar* nova_search_convert_shortcut_format(const gchar *shortcut);
static gboolean nova_search_register_keyboard_shortcut(NovaSearchPlugin *ns_plugin);
static void nova_search_unregister_keyboard_shortcut(NovaSearchPlugin *ns_plugin);
static void nova_search_keyboard_shortcut_callback(const char *keystring, gpointer user_data);

/* Plugin registration macro */
XFCE_PANEL_PLUGIN_REGISTER(nova_search_plugin_construct);

/* Create new plugin instance */
static NovaSearchPlugin* nova_search_plugin_new(XfcePanelPlugin *plugin) {
    NovaSearchPlugin *ns_plugin = g_slice_new0(NovaSearchPlugin);
    
    ns_plugin->plugin = plugin;
    ns_plugin->search_window = NULL;
    ns_plugin->search_entry = NULL;
    ns_plugin->results_list = NULL;
    ns_plugin->results_scroll = NULL;
    ns_plugin->debounce_timer = 0;
    ns_plugin->keyboard_shortcut = NULL;
    ns_plugin->shortcut_registered = FALSE;
    
    /* Initialize database connection */
    char *db_path = g_build_filename(g_get_user_data_dir(), 
                                      "novasearch", 
                                      "index.db", 
                                      NULL);
    ns_plugin->db = nova_search_db_new(db_path);
    g_free(db_path);
    
    if (!ns_plugin->db) {
        g_warning("Failed to create database connection");
    }
    
    /* Create panel button */
    ns_plugin->button = xfce_panel_create_button();
    GtkWidget *icon = gtk_image_new_from_icon_name("system-search", GTK_ICON_SIZE_BUTTON);
    gtk_button_set_image(GTK_BUTTON(ns_plugin->button), icon);
    gtk_widget_set_tooltip_text(ns_plugin->button, "NovaSearch - Fast File Search");
    gtk_widget_show(ns_plugin->button);
    
    /* Connect button click signal */
    g_signal_connect_swapped(G_OBJECT(ns_plugin->button), "clicked",
                              G_CALLBACK(nova_search_show_window), ns_plugin);
    
    gtk_container_add(GTK_CONTAINER(plugin), ns_plugin->button);
    xfce_panel_plugin_add_action_widget(plugin, ns_plugin->button);
    
    /* Register keyboard shortcut */
    if (!nova_search_register_keyboard_shortcut(ns_plugin)) {
        g_warning("Failed to register keyboard shortcut");
    }
    
    return ns_plugin;
}

/* Free plugin resources */
static void nova_search_plugin_free(XfcePanelPlugin *plugin, NovaSearchPlugin *ns_plugin) {
    (void)plugin; /* Unused parameter */
    
    if (!ns_plugin) {
        return;
    }
    
    /* Unregister keyboard shortcut */
    nova_search_unregister_keyboard_shortcut(ns_plugin);
    
    /* Remove debounce timer if active */
    if (ns_plugin->debounce_timer > 0) {
        g_source_remove(ns_plugin->debounce_timer);
        ns_plugin->debounce_timer = 0;
    }
    
    /* Destroy search window if it exists */
    if (ns_plugin->search_window) {
        gtk_widget_destroy(ns_plugin->search_window);
        ns_plugin->search_window = NULL;
    }
    
    /* Close database connection */
    if (ns_plugin->db) {
        nova_search_db_free(ns_plugin->db);
        ns_plugin->db = NULL;
    }
    
    /* Free keyboard shortcut string */
    if (ns_plugin->keyboard_shortcut) {
        g_free(ns_plugin->keyboard_shortcut);
        ns_plugin->keyboard_shortcut = NULL;
    }
    
    g_slice_free(NovaSearchPlugin, ns_plugin);
}

/* Create search window with GTK3 widgets */
static void nova_search_create_window(NovaSearchPlugin *ns_plugin) {
    if (ns_plugin->search_window) {
        return; /* Window already exists */
    }
    
    /* Create main window */
    ns_plugin->search_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(ns_plugin->search_window), "NovaSearch");
    gtk_window_set_default_size(GTK_WINDOW(ns_plugin->search_window), 600, 400);
    gtk_window_set_position(GTK_WINDOW(ns_plugin->search_window), GTK_WIN_POS_CENTER);
    gtk_window_set_type_hint(GTK_WINDOW(ns_plugin->search_window), GDK_WINDOW_TYPE_HINT_DIALOG);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(ns_plugin->search_window), TRUE);
    gtk_window_set_skip_pager_hint(GTK_WINDOW(ns_plugin->search_window), TRUE);
    gtk_window_set_decorated(GTK_WINDOW(ns_plugin->search_window), TRUE);
    
    /* Create main vertical box */
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 12);
    gtk_container_add(GTK_CONTAINER(ns_plugin->search_window), vbox);
    
    /* Create search entry */
    ns_plugin->search_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(ns_plugin->search_entry), "Search files...");
    gtk_entry_set_icon_from_icon_name(GTK_ENTRY(ns_plugin->search_entry), 
                                      GTK_ENTRY_ICON_PRIMARY, 
                                      "system-search");
    gtk_box_pack_start(GTK_BOX(vbox), ns_plugin->search_entry, FALSE, FALSE, 0);
    
    /* Connect search entry changed signal */
    g_signal_connect(G_OBJECT(ns_plugin->search_entry), "changed",
                     G_CALLBACK(nova_search_entry_changed), ns_plugin);
    
    /* Create scrolled window for results */
    ns_plugin->results_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(ns_plugin->results_scroll),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(ns_plugin->results_scroll),
                                        GTK_SHADOW_IN);
    gtk_box_pack_start(GTK_BOX(vbox), ns_plugin->results_scroll, TRUE, TRUE, 0);
    
    /* Create results list box */
    ns_plugin->results_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(ns_plugin->results_list), 
                                     GTK_SELECTION_SINGLE);
    gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(ns_plugin->results_list), 
                                               TRUE);
    gtk_container_add(GTK_CONTAINER(ns_plugin->results_scroll), ns_plugin->results_list);
    
    /* Connect row activation signal for mouse clicks */
    g_signal_connect(G_OBJECT(ns_plugin->results_list), "row-activated",
                     G_CALLBACK(nova_search_row_activated), ns_plugin);
    
    /* Connect button press event for right-click context menu */
    g_signal_connect(G_OBJECT(ns_plugin->results_list), "button-press-event",
                     G_CALLBACK(nova_search_row_button_press), ns_plugin);
    
    /* Connect window signals */
    g_signal_connect(G_OBJECT(ns_plugin->search_window), "key-press-event",
                     G_CALLBACK(nova_search_window_key_press), ns_plugin);
    
    g_signal_connect(G_OBJECT(ns_plugin->search_window), "focus-out-event",
                     G_CALLBACK(nova_search_window_focus_out), ns_plugin);
    
    g_signal_connect(G_OBJECT(ns_plugin->search_window), "delete-event",
                     G_CALLBACK(gtk_widget_hide_on_delete), NULL);
    
    /* Show all widgets */
    gtk_widget_show_all(vbox);
}

/* Handle window key press events */
static gboolean nova_search_window_key_press(GtkWidget *widget, GdkEventKey *event, 
                                              NovaSearchPlugin *ns_plugin) {
    /* Handle Escape key to hide window */
    if (event->keyval == GDK_KEY_Escape) {
        nova_search_hide_window(ns_plugin);
        return TRUE;
    }
    
    /* Handle Enter key to open selected file */
    if (event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_KP_Enter) {
        if (ns_plugin->results_list) {
            GtkListBoxRow *selected_row = gtk_list_box_get_selected_row(
                GTK_LIST_BOX(ns_plugin->results_list));
            
            if (selected_row) {
                const char *file_path = g_object_get_data(G_OBJECT(selected_row), "result-path");
                if (file_path) {
                    nova_search_open_file(ns_plugin, file_path);
                }
            }
        }
        return TRUE;
    }
    
    /* Handle keyboard navigation in results list */
    if (ns_plugin->results_list) {
        GtkListBoxRow *selected_row = gtk_list_box_get_selected_row(
            GTK_LIST_BOX(ns_plugin->results_list));
        
        /* Handle Up arrow key */
        if (event->keyval == GDK_KEY_Up) {
            if (selected_row) {
                gint current_index = gtk_list_box_row_get_index(selected_row);
                if (current_index > 0) {
                    /* Move to previous row */
                    GtkListBoxRow *prev_row = gtk_list_box_get_row_at_index(
                        GTK_LIST_BOX(ns_plugin->results_list), current_index - 1);
                    if (prev_row) {
                        gtk_list_box_select_row(GTK_LIST_BOX(ns_plugin->results_list), prev_row);
                    }
                } else {
                    /* Wrap to last row */
                    GList *children = gtk_container_get_children(
                        GTK_CONTAINER(ns_plugin->results_list));
                    gint num_children = g_list_length(children);
                    g_list_free(children);
                    
                    if (num_children > 0) {
                        GtkListBoxRow *last_row = gtk_list_box_get_row_at_index(
                            GTK_LIST_BOX(ns_plugin->results_list), num_children - 1);
                        if (last_row) {
                            gtk_list_box_select_row(GTK_LIST_BOX(ns_plugin->results_list), last_row);
                        }
                    }
                }
            } else {
                /* No selection, select last row */
                GList *children = gtk_container_get_children(
                    GTK_CONTAINER(ns_plugin->results_list));
                gint num_children = g_list_length(children);
                g_list_free(children);
                
                if (num_children > 0) {
                    GtkListBoxRow *last_row = gtk_list_box_get_row_at_index(
                        GTK_LIST_BOX(ns_plugin->results_list), num_children - 1);
                    if (last_row) {
                        gtk_list_box_select_row(GTK_LIST_BOX(ns_plugin->results_list), last_row);
                    }
                }
            }
            return TRUE;
        }
        
        /* Handle Down arrow key */
        if (event->keyval == GDK_KEY_Down) {
            GList *children = gtk_container_get_children(
                GTK_CONTAINER(ns_plugin->results_list));
            gint num_children = g_list_length(children);
            g_list_free(children);
            
            if (num_children == 0) {
                return TRUE;
            }
            
            if (selected_row) {
                gint current_index = gtk_list_box_row_get_index(selected_row);
                if (current_index < num_children - 1) {
                    /* Move to next row */
                    GtkListBoxRow *next_row = gtk_list_box_get_row_at_index(
                        GTK_LIST_BOX(ns_plugin->results_list), current_index + 1);
                    if (next_row) {
                        gtk_list_box_select_row(GTK_LIST_BOX(ns_plugin->results_list), next_row);
                    }
                } else {
                    /* Wrap to first row */
                    GtkListBoxRow *first_row = gtk_list_box_get_row_at_index(
                        GTK_LIST_BOX(ns_plugin->results_list), 0);
                    if (first_row) {
                        gtk_list_box_select_row(GTK_LIST_BOX(ns_plugin->results_list), first_row);
                    }
                }
            } else {
                /* No selection, select first row */
                GtkListBoxRow *first_row = gtk_list_box_get_row_at_index(
                    GTK_LIST_BOX(ns_plugin->results_list), 0);
                if (first_row) {
                    gtk_list_box_select_row(GTK_LIST_BOX(ns_plugin->results_list), first_row);
                }
            }
            return TRUE;
        }
        
        /* Handle Home key - select first result */
        if (event->keyval == GDK_KEY_Home) {
            GtkListBoxRow *first_row = gtk_list_box_get_row_at_index(
                GTK_LIST_BOX(ns_plugin->results_list), 0);
            if (first_row) {
                gtk_list_box_select_row(GTK_LIST_BOX(ns_plugin->results_list), first_row);
            }
            return TRUE;
        }
        
        /* Handle End key - select last result */
        if (event->keyval == GDK_KEY_End) {
            GList *children = gtk_container_get_children(
                GTK_CONTAINER(ns_plugin->results_list));
            gint num_children = g_list_length(children);
            g_list_free(children);
            
            if (num_children > 0) {
                GtkListBoxRow *last_row = gtk_list_box_get_row_at_index(
                    GTK_LIST_BOX(ns_plugin->results_list), num_children - 1);
                if (last_row) {
                    gtk_list_box_select_row(GTK_LIST_BOX(ns_plugin->results_list), last_row);
                }
            }
            return TRUE;
        }
    }
    
    return FALSE;
}

/* Handle window focus out event */
static gboolean nova_search_window_focus_out(GtkWidget *widget, GdkEventFocus *event, 
                                              NovaSearchPlugin *ns_plugin) {
    /* Hide window when it loses focus */
    nova_search_hide_window(ns_plugin);
    return FALSE;
}

/* Show search window (placeholder - will be implemented in subtask 9.2) */
static void nova_search_show_window(NovaSearchPlugin *ns_plugin) {
    if (!ns_plugin) {
        return;
    }
    
    /* Create window if it doesn't exist */
    if (!ns_plugin->search_window) {
        nova_search_create_window(ns_plugin);
    }
    
    /* Open database connection if not already open */
    if (ns_plugin->db && !ns_plugin->db->is_connected) {
        if (!nova_search_db_open(ns_plugin->db)) {
            GtkWidget *error_dialog = gtk_message_dialog_new(
                GTK_WINDOW(ns_plugin->search_window),
                GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                GTK_MESSAGE_ERROR,
                GTK_BUTTONS_OK,
                "Cannot connect to search index.\nIs the indexing daemon running?"
            );
            gtk_dialog_run(GTK_DIALOG(error_dialog));
            gtk_widget_destroy(error_dialog);
            return;
        }
    }
    
    /* Clear search entry */
    gtk_entry_set_text(GTK_ENTRY(ns_plugin->search_entry), "");
    
    /* Show window and focus search entry */
    gtk_window_present(GTK_WINDOW(ns_plugin->search_window));
    gtk_widget_grab_focus(ns_plugin->search_entry);
}

/* Hide search window (placeholder - will be implemented in subtask 9.2) */
static void nova_search_hide_window(NovaSearchPlugin *ns_plugin) {
    if (!ns_plugin || !ns_plugin->search_window) {
        return;
    }
    
    /* Clear search entry */
    gtk_entry_set_text(GTK_ENTRY(ns_plugin->search_entry), "");
    
    /* Hide window */
    gtk_widget_hide(ns_plugin->search_window);
}

/* Clear all results from the list */
static void nova_search_clear_results(NovaSearchPlugin *ns_plugin) {
    if (!ns_plugin || !ns_plugin->results_list) {
        return;
    }
    
    /* Remove all children from the list box */
    GList *children = gtk_container_get_children(GTK_CONTAINER(ns_plugin->results_list));
    for (GList *iter = children; iter != NULL; iter = g_list_next(iter)) {
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    }
    g_list_free(children);
}

/* Handle search entry text changed */
static void nova_search_entry_changed(GtkEntry *entry, NovaSearchPlugin *ns_plugin) {
    if (!ns_plugin) {
        return;
    }
    
    /* Cancel existing debounce timer if any */
    if (ns_plugin->debounce_timer > 0) {
        g_source_remove(ns_plugin->debounce_timer);
        ns_plugin->debounce_timer = 0;
    }
    
    /* Get current query text */
    const gchar *query = gtk_entry_get_text(entry);
    
    /* If query is empty, clear results immediately */
    if (!query || strlen(query) == 0) {
        nova_search_clear_results(ns_plugin);
        return;
    }
    
    /* Set up debounced query execution (200ms delay) */
    ns_plugin->debounce_timer = g_timeout_add(200, 
                                               nova_search_execute_query_delayed, 
                                               ns_plugin);
}

/* Debounced query execution callback */
static gboolean nova_search_execute_query_delayed(gpointer user_data) {
    NovaSearchPlugin *ns_plugin = (NovaSearchPlugin *)user_data;
    
    if (!ns_plugin || !ns_plugin->search_entry) {
        return G_SOURCE_REMOVE;
    }
    
    /* Get current query text */
    const gchar *query = gtk_entry_get_text(GTK_ENTRY(ns_plugin->search_entry));
    
    /* Execute query */
    nova_search_execute_query(ns_plugin, query);
    
    /* Clear timer ID */
    ns_plugin->debounce_timer = 0;
    
    return G_SOURCE_REMOVE;
}

/* Execute search query and update results (placeholder - will be implemented in subtask 9.4) */
static void nova_search_execute_query(NovaSearchPlugin *ns_plugin, const char *query) {
    if (!ns_plugin || !query || strlen(query) == 0) {
        return;
    }
    
    /* Clear existing results */
    nova_search_clear_results(ns_plugin);
    
    /* Check database connection */
    if (!ns_plugin->db || !ns_plugin->db->is_connected) {
        g_warning("Database not connected");
        return;
    }
    
    /* Query database */
    SearchResult *results = nova_search_db_query(ns_plugin->db, query, 50);
    
    if (!results) {
        /* No results found - list is already cleared */
        return;
    }
    
    /* Display results */
    SearchResult *current = results;
    while (current) {
        GtkWidget *row = nova_search_create_result_row(current);
        if (row) {
            gtk_list_box_insert(GTK_LIST_BOX(ns_plugin->results_list), row, -1);
            gtk_widget_show_all(row);
        }
        current = current->next;
    }
    
    /* Free results */
    nova_search_result_list_free(results);
}

/* Get appropriate icon name for file type */
static const char* nova_search_get_file_icon_name(const char *file_type) {
    if (!file_type) {
        return "text-x-generic";
    }
    
    if (strcmp(file_type, "Directory") == 0) {
        return "folder";
    } else if (strcmp(file_type, "Symlink") == 0) {
        return "emblem-symbolic-link";
    } else if (strcmp(file_type, "Regular") == 0) {
        return "text-x-generic";
    } else {
        return "text-x-generic";
    }
}

/* Create a custom list box row for a search result */
static GtkWidget* nova_search_create_result_row(SearchResult *result) {
    if (!result) {
        return NULL;
    }
    
    /* Create list box row */
    GtkWidget *row = gtk_list_box_row_new();
    gtk_widget_set_can_focus(row, TRUE);
    
    /* Create horizontal box for row content */
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 6);
    gtk_container_add(GTK_CONTAINER(row), hbox);
    
    /* Add file type icon */
    const char *icon_name = nova_search_get_file_icon_name(result->file_type);
    GtkWidget *icon = gtk_image_new_from_icon_name(icon_name, GTK_ICON_SIZE_DND);
    gtk_box_pack_start(GTK_BOX(hbox), icon, FALSE, FALSE, 0);
    
    /* Create vertical box for text content */
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_box_pack_start(GTK_BOX(hbox), vbox, TRUE, TRUE, 0);
    
    /* Add filename label */
    GtkWidget *filename_label = gtk_label_new(result->filename);
    gtk_label_set_xalign(GTK_LABEL(filename_label), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(filename_label), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(filename_label, TRUE);
    
    /* Make filename bold */
    PangoAttrList *attrs = pango_attr_list_new();
    PangoAttribute *weight_attr = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
    pango_attr_list_insert(attrs, weight_attr);
    gtk_label_set_attributes(GTK_LABEL(filename_label), attrs);
    pango_attr_list_unref(attrs);
    
    gtk_box_pack_start(GTK_BOX(vbox), filename_label, FALSE, FALSE, 0);
    
    /* Add path label */
    GtkWidget *path_label = gtk_label_new(result->path);
    gtk_label_set_xalign(GTK_LABEL(path_label), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(path_label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_set_hexpand(path_label, TRUE);
    
    /* Make path label smaller and dimmed */
    GtkStyleContext *context = gtk_widget_get_style_context(path_label);
    gtk_style_context_add_class(context, "dim-label");
    
    PangoAttrList *path_attrs = pango_attr_list_new();
    PangoAttribute *scale_attr = pango_attr_scale_new(PANGO_SCALE_SMALL);
    pango_attr_list_insert(path_attrs, scale_attr);
    gtk_label_set_attributes(GTK_LABEL(path_label), path_attrs);
    pango_attr_list_unref(path_attrs);
    
    gtk_box_pack_start(GTK_BOX(vbox), path_label, FALSE, FALSE, 0);
    
    /* Store result path as data on the row for later retrieval */
    g_object_set_data_full(G_OBJECT(row), "result-path", 
                           g_strdup(result->path), g_free);
    
    return row;
}

/* Open file with default application */
static void nova_search_open_file(NovaSearchPlugin *ns_plugin, const char *file_path) {
    if (!file_path) {
        return;
    }
    
    GError *error = NULL;
    gboolean success = FALSE;
    
    /* Try using gio to launch the file */
    GFile *file = g_file_new_for_path(file_path);
    if (file) {
        success = g_app_info_launch_default_for_uri(
            g_file_get_uri(file),
            NULL,
            &error
        );
        g_object_unref(file);
    }
    
    /* If gio fails, fall back to xdg-open */
    if (!success) {
        if (error) {
            g_warning("Failed to open file with gio: %s", error->message);
            g_error_free(error);
            error = NULL;
        }
        
        gchar *command = g_strdup_printf("xdg-open '%s'", file_path);
        success = g_spawn_command_line_async(command, &error);
        g_free(command);
    }
    
    if (success) {
        /* Hide window after successfully opening file */
        nova_search_hide_window(ns_plugin);
    } else {
        /* Display error dialog */
        const char *error_msg = error ? error->message : "Unknown error";
        GtkWidget *error_dialog = gtk_message_dialog_new(
            GTK_WINDOW(ns_plugin->search_window),
            GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_ERROR,
            GTK_BUTTONS_OK,
            "Cannot open file:\n%s",
            error_msg
        );
        gtk_dialog_run(GTK_DIALOG(error_dialog));
        gtk_widget_destroy(error_dialog);
        
        if (error) {
            g_error_free(error);
        }
    }
}

/* Handle row activation (mouse click) */
static void nova_search_row_activated(GtkListBox *list_box, GtkListBoxRow *row, 
                                       NovaSearchPlugin *ns_plugin) {
    if (!row) {
        return;
    }
    
    const char *file_path = g_object_get_data(G_OBJECT(row), "result-path");
    if (file_path) {
        nova_search_open_file(ns_plugin, file_path);
    }
}

/* Handle button press event for context menu */
static gboolean nova_search_row_button_press(GtkWidget *widget, GdkEventButton *event, 
                                              NovaSearchPlugin *ns_plugin) {
    /* Check for right-click */
    if (event->type == GDK_BUTTON_PRESS && event->button == 3) {
        /* Get the row at the click position */
        GtkListBoxRow *row = gtk_list_box_get_row_at_y(
            GTK_LIST_BOX(widget), (gint)event->y);
        
        if (!row) {
            return FALSE;
        }
        
        /* Select the row */
        gtk_list_box_select_row(GTK_LIST_BOX(widget), row);
        
        /* Get the file path from the row */
        const char *file_path = g_object_get_data(G_OBJECT(row), "result-path");
        if (!file_path) {
            return FALSE;
        }
        
        /* Create context menu */
        GtkWidget *menu = gtk_menu_new();
        
        /* Add "Open containing folder" menu item */
        GtkWidget *menu_item = gtk_menu_item_new_with_label("Open containing folder");
        g_object_set_data_full(G_OBJECT(menu_item), "file-path", 
                               g_strdup(file_path), g_free);
        g_signal_connect(G_OBJECT(menu_item), "activate",
                         G_CALLBACK(nova_search_open_containing_folder), ns_plugin);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
        
        /* Show menu */
        gtk_widget_show_all(menu);
        gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);
        
        return TRUE;
    }
    
    return FALSE;
}

/* Open containing folder in file manager */
static void nova_search_open_containing_folder(GtkMenuItem *menu_item, gpointer user_data) {
    NovaSearchPlugin *ns_plugin = (NovaSearchPlugin *)user_data;
    
    const char *file_path = g_object_get_data(G_OBJECT(menu_item), "file-path");
    if (!file_path) {
        return;
    }
    
    /* Get the directory path */
    gchar *dir_path = g_path_get_dirname(file_path);
    if (!dir_path) {
        return;
    }
    
    GError *error = NULL;
    gboolean success = FALSE;
    
    /* Try using gio to open the folder */
    GFile *dir = g_file_new_for_path(dir_path);
    if (dir) {
        success = g_app_info_launch_default_for_uri(
            g_file_get_uri(dir),
            NULL,
            &error
        );
        g_object_unref(dir);
    }
    
    /* If gio fails, fall back to xdg-open */
    if (!success) {
        if (error) {
            g_warning("Failed to open folder with gio: %s", error->message);
            g_error_free(error);
            error = NULL;
        }
        
        gchar *command = g_strdup_printf("xdg-open '%s'", dir_path);
        success = g_spawn_command_line_async(command, &error);
        g_free(command);
    }
    
    if (!success) {
        /* Display error dialog */
        const char *error_msg = error ? error->message : "Unknown error";
        GtkWidget *error_dialog = gtk_message_dialog_new(
            GTK_WINDOW(ns_plugin->search_window),
            GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_ERROR,
            GTK_BUTTONS_OK,
            "Cannot open folder:\n%s",
            error_msg
        );
        gtk_dialog_run(GTK_DIALOG(error_dialog));
        gtk_widget_destroy(error_dialog);
        
        if (error) {
            g_error_free(error);
        }
    }
    
    g_free(dir_path);
}

/* Read keyboard shortcut from config file */
static gchar* nova_search_read_keyboard_shortcut_from_config(void) {
    gchar *config_path = g_build_filename(g_get_user_config_dir(),
                                          "novasearch",
                                          "config.toml",
                                          NULL);
    
    if (!g_file_test(config_path, G_FILE_TEST_EXISTS)) {
        g_free(config_path);
        return NULL;
    }
    
    gchar *contents = NULL;
    gsize length = 0;
    GError *error = NULL;
    
    if (!g_file_get_contents(config_path, &contents, &length, &error)) {
        g_warning("Failed to read config file: %s", error ? error->message : "Unknown error");
        if (error) {
            g_error_free(error);
        }
        g_free(config_path);
        return NULL;
    }
    
    g_free(config_path);
    
    /* Parse TOML to find keyboard_shortcut in [ui] section */
    gchar **lines = g_strsplit(contents, "\n", -1);
    g_free(contents);
    
    gboolean in_ui_section = FALSE;
    gchar *shortcut = NULL;
    
    for (gint i = 0; lines[i] != NULL; i++) {
        gchar *line = g_strstrip(g_strdup(lines[i]));
        
        /* Check for [ui] section */
        if (g_strcmp0(line, "[ui]") == 0) {
            in_ui_section = TRUE;
            g_free(line);
            continue;
        }
        
        /* Check for other sections */
        if (line[0] == '[') {
            in_ui_section = FALSE;
            g_free(line);
            continue;
        }
        
        /* Look for keyboard_shortcut in [ui] section */
        if (in_ui_section && g_str_has_prefix(line, "keyboard_shortcut")) {
            gchar **parts = g_strsplit(line, "=", 2);
            if (parts[0] && parts[1]) {
                gchar *value = g_strstrip(g_strdup(parts[1]));
                /* Remove quotes */
                if (value[0] == '"' && value[strlen(value) - 1] == '"') {
                    shortcut = g_strndup(value + 1, strlen(value) - 2);
                } else {
                    shortcut = g_strdup(value);
                }
                g_free(value);
            }
            g_strfreev(parts);
            g_free(line);
            break;
        }
        
        g_free(line);
    }
    
    g_strfreev(lines);
    return shortcut;
}

/* Convert shortcut format from config (e.g., "Super+Space") to keybinder format (e.g., "<Super>space") */
static gchar* nova_search_convert_shortcut_format(const gchar *shortcut) {
    if (!shortcut) {
        return NULL;
    }
    
    /* Split by + */
    gchar **parts = g_strsplit(shortcut, "+", -1);
    if (!parts || !parts[0]) {
        g_strfreev(parts);
        return NULL;
    }
    
    GString *result = g_string_new("");
    
    /* Process each part */
    for (gint i = 0; parts[i] != NULL; i++) {
        gchar *part = g_strstrip(g_strdup(parts[i]));
        
        /* Check if this is a modifier key */
        if (g_ascii_strcasecmp(part, "Super") == 0 ||
            g_ascii_strcasecmp(part, "Ctrl") == 0 ||
            g_ascii_strcasecmp(part, "Control") == 0 ||
            g_ascii_strcasecmp(part, "Alt") == 0 ||
            g_ascii_strcasecmp(part, "Shift") == 0) {
            /* Add as modifier in angle brackets */
            g_string_append_printf(result, "<%s>", part);
        } else {
            /* This is the key itself - convert to lowercase */
            gchar *lower = g_ascii_strdown(part, -1);
            g_string_append(result, lower);
            g_free(lower);
        }
        
        g_free(part);
    }
    
    g_strfreev(parts);
    
    gchar *converted = g_string_free(result, FALSE);
    return converted;
}

/* Register global keyboard shortcut */
static gboolean nova_search_register_keyboard_shortcut(NovaSearchPlugin *ns_plugin) {
    if (!ns_plugin) {
        return FALSE;
    }
    
    /* Initialize keybinder */
    keybinder_init();
    
    /* Read shortcut from config file */
    gchar *config_shortcut = nova_search_read_keyboard_shortcut_from_config();
    gchar *shortcut_to_use = NULL;
    
    if (config_shortcut) {
        /* Convert format from config to keybinder format */
        shortcut_to_use = nova_search_convert_shortcut_format(config_shortcut);
        g_free(config_shortcut);
    }
    
    /* Fall back to default if no config or conversion failed */
    if (!shortcut_to_use) {
        shortcut_to_use = g_strdup(DEFAULT_KEYBOARD_SHORTCUT);
        g_message("Using default keyboard shortcut: %s", shortcut_to_use);
    } else {
        g_message("Using configured keyboard shortcut: %s", shortcut_to_use);
    }
    
    /* Try to bind the shortcut */
    gboolean success = keybinder_bind(shortcut_to_use, 
                                      nova_search_keyboard_shortcut_callback,
                                      ns_plugin);
    
    if (!success) {
        g_warning("Failed to register keyboard shortcut '%s', trying default", shortcut_to_use);
        g_free(shortcut_to_use);
        
        /* Try default shortcut as fallback */
        shortcut_to_use = g_strdup(DEFAULT_KEYBOARD_SHORTCUT);
        success = keybinder_bind(shortcut_to_use,
                                nova_search_keyboard_shortcut_callback,
                                ns_plugin);
        
        if (!success) {
            g_warning("Failed to register default keyboard shortcut");
            g_free(shortcut_to_use);
            return FALSE;
        }
        
        g_message("Registered fallback keyboard shortcut: %s", shortcut_to_use);
    }
    
    ns_plugin->keyboard_shortcut = shortcut_to_use;
    ns_plugin->shortcut_registered = TRUE;
    
    return TRUE;
}

/* Unregister global keyboard shortcut */
static void nova_search_unregister_keyboard_shortcut(NovaSearchPlugin *ns_plugin) {
    if (!ns_plugin || !ns_plugin->shortcut_registered) {
        return;
    }
    
    if (ns_plugin->keyboard_shortcut) {
        keybinder_unbind(ns_plugin->keyboard_shortcut, 
                        nova_search_keyboard_shortcut_callback);
        ns_plugin->shortcut_registered = FALSE;
    }
}

/* Keyboard shortcut callback */
static void nova_search_keyboard_shortcut_callback(const char *keystring, gpointer user_data) {
    (void)keystring; /* Unused parameter */
    
    NovaSearchPlugin *ns_plugin = (NovaSearchPlugin *)user_data;
    
    if (!ns_plugin) {
        return;
    }
    
    /* Show the search window */
    nova_search_show_window(ns_plugin);
}

/* Show about dialog */
static void nova_search_about_dialog(XfcePanelPlugin *plugin) {
    (void)plugin; /* Unused parameter */
    
    GtkWidget *dialog = gtk_about_dialog_new();
    
    gtk_about_dialog_set_program_name(GTK_ABOUT_DIALOG(dialog), "NovaSearch");
    gtk_about_dialog_set_version(GTK_ABOUT_DIALOG(dialog), "1.0.0");
    gtk_about_dialog_set_comments(GTK_ABOUT_DIALOG(dialog), 
                                   "Fast system-wide file search for Linux");
    gtk_about_dialog_set_website(GTK_ABOUT_DIALOG(dialog), 
                                  "https://github.com/novasearch/novasearch");
    gtk_about_dialog_set_logo_icon_name(GTK_ABOUT_DIALOG(dialog), "system-search");
    
    const gchar *authors[] = {
        "NovaSearch Contributors",
        NULL
    };
    gtk_about_dialog_set_authors(GTK_ABOUT_DIALOG(dialog), authors);
    
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

/* Show configuration dialog */
static void nova_search_configure_dialog(XfcePanelPlugin *plugin) {
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "NovaSearch Configuration",
        GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(plugin))),
        GTK_DIALOG_DESTROY_WITH_PARENT,
        "Close", GTK_RESPONSE_OK,
        NULL
    );
    
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *label = gtk_label_new(
        "NovaSearch configuration is managed through:\n"
        "~/.config/novasearch/config.toml\n\n"
        "Please edit the configuration file directly."
    );
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_box_pack_start(GTK_BOX(content), label, TRUE, TRUE, 6);
    gtk_widget_show_all(content);
    
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

/* Plugin construction function */
static void nova_search_plugin_construct(XfcePanelPlugin *plugin) {
    /* Create plugin instance */
    NovaSearchPlugin *ns_plugin = nova_search_plugin_new(plugin);
    
    /* Connect plugin signals */
    g_signal_connect(G_OBJECT(plugin), "free-data",
                     G_CALLBACK(nova_search_plugin_free), ns_plugin);
    
    g_signal_connect(G_OBJECT(plugin), "about",
                     G_CALLBACK(nova_search_about_dialog), NULL);
    
    g_signal_connect(G_OBJECT(plugin), "configure-plugin",
                     G_CALLBACK(nova_search_configure_dialog), NULL);
    
    /* Show the plugin */
    gtk_widget_show(GTK_WIDGET(plugin));
}
