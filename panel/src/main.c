/* NovaSearch Panel Plugin */

#include <gtk/gtk.h>
#include <libxfce4panel/libxfce4panel.h>
#include <libxfce4util/libxfce4util.h>
#include <glib.h>
#include <gio/gdesktopappinfo.h>
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
static void nova_search_save_keyboard_shortcut_to_config(const gchar *shortcut);
static void nova_search_start_shortcut_capture(GtkButton *button, gpointer user_data);
static gboolean nova_search_capture_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data);
static void nova_search_open_donation_link(GtkButton *button, gpointer user_data);
static void nova_search_save_config_file(GtkButton *button, gpointer user_data);
static gchar* nova_search_parse_desktop_file_field(const gchar *file_path, const gchar *field);
static gchar* nova_search_get_desktop_icon(const gchar *file_path);
static gchar* nova_search_get_desktop_exec(const gchar *file_path);
static gboolean nova_search_is_desktop_file(const gchar *file_path);
static void nova_search_launch_desktop_application(const gchar *file_path);

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
    gtk_window_set_default_size(GTK_WINDOW(ns_plugin->search_window), 700, 500);
    gtk_window_set_position(GTK_WINDOW(ns_plugin->search_window), GTK_WIN_POS_CENTER);
    gtk_window_set_type_hint(GTK_WINDOW(ns_plugin->search_window), GDK_WINDOW_TYPE_HINT_UTILITY);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(ns_plugin->search_window), TRUE);
    gtk_window_set_skip_pager_hint(GTK_WINDOW(ns_plugin->search_window), TRUE);
    gtk_window_set_decorated(GTK_WINDOW(ns_plugin->search_window), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(ns_plugin->search_window), FALSE);
    
    /* Apply custom CSS for modern look */
    GtkCssProvider *css_provider = gtk_css_provider_new();
    const gchar *css_data = 
        "window.novasearch {"
        "  background-color: @theme_base_color;"
        "  border-radius: 12px;"
        "  border: 1px solid @borders;"
        "  box-shadow: 0 8px 32px rgba(0, 0, 0, 0.3);"
        "}"
        ".novasearch-entry {"
        "  font-size: 18px;"
        "  padding: 12px 16px;"
        "  border-radius: 8px;"
        "  border: 1px solid @borders;"
        "  background-color: @theme_base_color;"
        "}"
        ".novasearch-results {"
        "  background-color: @theme_base_color;"
        "  border: none;"
        "}"
        ".novasearch-result-row {"
        "  padding: 8px 12px;"
        "  border-radius: 6px;"
        "  margin: 2px 4px;"
        "}"
        ".novasearch-result-row:hover {"
        "  background-color: @theme_selected_bg_color;"
        "}"
        ".novasearch-result-row:selected {"
        "  background-color: @theme_selected_bg_color;"
        "  color: @theme_selected_fg_color;"
        "}"
        ".novasearch-filename {"
        "  font-weight: 600;"
        "  font-size: 14px;"
        "}"
        ".novasearch-path {"
        "  font-size: 12px;"
        "  opacity: 0.7;"
        "}";
    
    gtk_css_provider_load_from_data(css_provider, css_data, -1, NULL);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(css_provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );
    g_object_unref(css_provider);
    
    /* Add CSS class to window */
    GtkStyleContext *window_context = gtk_widget_get_style_context(ns_plugin->search_window);
    gtk_style_context_add_class(window_context, "novasearch");
    
    /* Create main vertical box */
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 20);
    gtk_container_add(GTK_CONTAINER(ns_plugin->search_window), vbox);
    
    /* Create search entry */
    ns_plugin->search_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(ns_plugin->search_entry), "Search files and folders...");
    gtk_entry_set_icon_from_icon_name(GTK_ENTRY(ns_plugin->search_entry), 
                                      GTK_ENTRY_ICON_PRIMARY, 
                                      "system-search");
    
    /* Apply CSS class to search entry */
    GtkStyleContext *entry_context = gtk_widget_get_style_context(ns_plugin->search_entry);
    gtk_style_context_add_class(entry_context, "novasearch-entry");
    
    gtk_box_pack_start(GTK_BOX(vbox), ns_plugin->search_entry, FALSE, FALSE, 0);
    
    /* Connect search entry changed signal */
    g_signal_connect(G_OBJECT(ns_plugin->search_entry), "changed",
                     G_CALLBACK(nova_search_entry_changed), ns_plugin);
    
    /* Create scrolled window for results */
    ns_plugin->results_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(ns_plugin->results_scroll),
                                   GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(ns_plugin->results_scroll),
                                        GTK_SHADOW_NONE);
    
    /* Apply CSS class to results scroll */
    GtkStyleContext *scroll_context = gtk_widget_get_style_context(ns_plugin->results_scroll);
    gtk_style_context_add_class(scroll_context, "novasearch-results");
    
    gtk_box_pack_start(GTK_BOX(vbox), ns_plugin->results_scroll, TRUE, TRUE, 0);
    
    /* Create results list box */
    ns_plugin->results_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(ns_plugin->results_list), 
                                     GTK_SELECTION_SINGLE);
    gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(ns_plugin->results_list), 
                                               TRUE);
    
    /* Apply CSS class to results list */
    GtkStyleContext *list_context = gtk_widget_get_style_context(ns_plugin->results_list);
    gtk_style_context_add_class(list_context, "novasearch-results");
    
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
    
    /* Apply CSS class to row */
    GtkStyleContext *row_context = gtk_widget_get_style_context(row);
    gtk_style_context_add_class(row_context, "novasearch-result-row");
    
    /* Create horizontal box for row content */
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 8);
    gtk_container_add(GTK_CONTAINER(row), hbox);
    
    /* Add file type icon */
    const char *icon_name = nova_search_get_file_icon_name(result->file_type);
    GtkWidget *icon = NULL;
    
    /* Special handling for .desktop files */
    if (nova_search_is_desktop_file(result->path)) {
        gchar *desktop_icon = nova_search_get_desktop_icon(result->path);
        if (desktop_icon && strlen(desktop_icon) > 0) {
            /* Try to load the application icon */
            icon = gtk_image_new_from_icon_name(desktop_icon, GTK_ICON_SIZE_LARGE_TOOLBAR);
            
            /* If the icon doesn't exist, fall back to application-x-executable */
            GtkIconTheme *icon_theme = gtk_icon_theme_get_default();
            if (!gtk_icon_theme_has_icon(icon_theme, desktop_icon)) {
                gtk_image_set_from_icon_name(GTK_IMAGE(icon), "application-x-executable", GTK_ICON_SIZE_LARGE_TOOLBAR);
            }
            g_free(desktop_icon);
        } else {
            /* Fallback to generic application icon */
            icon = gtk_image_new_from_icon_name("application-x-executable", GTK_ICON_SIZE_LARGE_TOOLBAR);
        }
    } else {
        /* Use regular file type icon */
        icon = gtk_image_new_from_icon_name(icon_name, GTK_ICON_SIZE_LARGE_TOOLBAR);
    }
    
    gtk_widget_set_margin_end(icon, 4);
    gtk_box_pack_start(GTK_BOX(hbox), icon, FALSE, FALSE, 0);
    
    /* Create vertical box for text content */
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_pack_start(GTK_BOX(hbox), vbox, TRUE, TRUE, 0);
    
    /* Add filename label */
    gchar *display_name = NULL;
    
    /* Special handling for .desktop files - show application name */
    if (nova_search_is_desktop_file(result->path)) {
        gchar *app_name = nova_search_parse_desktop_file_field(result->path, "Name");
        if (app_name && strlen(app_name) > 0) {
            display_name = app_name;
        } else {
            /* Fallback to filename without .desktop extension */
            gchar *basename = g_path_get_basename(result->filename);
            if (g_str_has_suffix(basename, ".desktop")) {
                display_name = g_strndup(basename, strlen(basename) - 8); /* Remove .desktop */
            } else {
                display_name = g_strdup(basename);
            }
            g_free(basename);
            if (app_name) g_free(app_name);
        }
    } else {
        display_name = g_strdup(result->filename);
    }
    
    GtkWidget *filename_label = gtk_label_new(display_name);
    g_free(display_name);
    
    gtk_label_set_xalign(GTK_LABEL(filename_label), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(filename_label), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(filename_label, TRUE);
    
    /* Apply CSS class to filename */
    GtkStyleContext *filename_context = gtk_widget_get_style_context(filename_label);
    gtk_style_context_add_class(filename_context, "novasearch-filename");
    
    gtk_box_pack_start(GTK_BOX(vbox), filename_label, FALSE, FALSE, 0);
    
    /* Add path label */
    GtkWidget *path_label = gtk_label_new(result->path);
    gtk_label_set_xalign(GTK_LABEL(path_label), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(path_label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_set_hexpand(path_label, TRUE);
    
    /* Apply CSS class to path */
    GtkStyleContext *path_context = gtk_widget_get_style_context(path_label);
    gtk_style_context_add_class(path_context, "novasearch-path");
    
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
    
    /* Special handling for .desktop files - launch the application */
    if (nova_search_is_desktop_file(file_path)) {
        nova_search_launch_desktop_application(file_path);
        
        /* Record file launch for usage tracking */
        if (ns_plugin->db && ns_plugin->db->is_connected) {
            nova_search_db_record_launch(ns_plugin->db, file_path);
        }
        
        /* Hide window after launching application */
        nova_search_hide_window(ns_plugin);
        return;
    }
    
    /* Regular file handling */
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
        /* Record file launch for usage tracking */
        if (ns_plugin->db && ns_plugin->db->is_connected) {
            nova_search_db_record_launch(ns_plugin->db, file_path);
        }
        
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
    gtk_about_dialog_set_version(GTK_ABOUT_DIALOG(dialog), "0.1.0");
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
    NovaSearchPlugin *ns_plugin = g_object_get_data(G_OBJECT(plugin), "plugin-data");
    
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "NovaSearch Settings",
        GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(plugin))),
        GTK_DIALOG_DESTROY_WITH_PARENT,
        "Apply", GTK_RESPONSE_APPLY,
        "Close", GTK_RESPONSE_CLOSE,
        NULL
    );
    
    gtk_window_set_default_size(GTK_WINDOW(dialog), 500, 400);
    gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);
    
    /* Make dialog focusable for key capture */
    gtk_widget_set_can_focus(dialog, TRUE);
    gtk_window_set_focus_on_map(GTK_WINDOW(dialog), TRUE);
    
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    
    /* Create notebook for tabs */
    GtkWidget *notebook = gtk_notebook_new();
    gtk_box_pack_start(GTK_BOX(content), notebook, TRUE, TRUE, 6);
    
    /* === SETTINGS TAB === */
    GtkWidget *settings_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER(settings_vbox), 12);
    
    /* Keyboard shortcut section */
    GtkWidget *shortcut_frame = gtk_frame_new("Global Keyboard Shortcut");
    gtk_box_pack_start(GTK_BOX(settings_vbox), shortcut_frame, FALSE, FALSE, 0);
    
    GtkWidget *shortcut_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(shortcut_vbox), 12);
    gtk_container_add(GTK_CONTAINER(shortcut_frame), shortcut_vbox);
    
    GtkWidget *shortcut_label = gtk_label_new("Click the button below and press your desired key combination:");
    gtk_label_set_xalign(GTK_LABEL(shortcut_label), 0.0);
    gtk_box_pack_start(GTK_BOX(shortcut_vbox), shortcut_label, FALSE, FALSE, 0);
    
    /* Create horizontal box for shortcut display and button */
    GtkWidget *shortcut_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(shortcut_vbox), shortcut_hbox, FALSE, FALSE, 0);
    
    /* Current shortcut display */
    GtkWidget *shortcut_display = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(shortcut_display), 0.0);
    gtk_widget_set_hexpand(shortcut_display, TRUE);
    
    /* Style the shortcut display */
    GtkStyleContext *display_context = gtk_widget_get_style_context(shortcut_display);
    gtk_style_context_add_class(display_context, "monospace");
    
    PangoAttrList *display_attrs = pango_attr_list_new();
    PangoAttribute *display_weight = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
    PangoAttribute *display_size = pango_attr_size_new(12 * PANGO_SCALE);
    pango_attr_list_insert(display_attrs, display_weight);
    pango_attr_list_insert(display_attrs, display_size);
    gtk_label_set_attributes(GTK_LABEL(shortcut_display), display_attrs);
    pango_attr_list_unref(display_attrs);
    
    /* Read current shortcut from config */
    gchar *current_shortcut = nova_search_read_keyboard_shortcut_from_config();
    if (current_shortcut) {
        gchar *display_text = g_strdup_printf("Current: %s", current_shortcut);
        gtk_label_set_text(GTK_LABEL(shortcut_display), display_text);
        g_free(display_text);
        g_free(current_shortcut);
    } else {
        gtk_label_set_text(GTK_LABEL(shortcut_display), "Current: Super+Space");
    }
    
    gtk_box_pack_start(GTK_BOX(shortcut_hbox), shortcut_display, TRUE, TRUE, 0);
    
    /* Capture button */
    GtkWidget *capture_button = gtk_button_new_with_label("Set New Shortcut");
    gtk_widget_set_size_request(capture_button, 150, -1);
    gtk_box_pack_start(GTK_BOX(shortcut_hbox), capture_button, FALSE, FALSE, 0);
    
    /* Store references for the capture system */
    g_object_set_data(G_OBJECT(dialog), "shortcut-display", shortcut_display);
    g_object_set_data(G_OBJECT(dialog), "capture-button", capture_button);
    g_object_set_data(G_OBJECT(dialog), "plugin-data", ns_plugin);
    
    /* Connect capture button signal */
    g_signal_connect(G_OBJECT(capture_button), "clicked",
                     G_CALLBACK(nova_search_start_shortcut_capture), dialog);
    
    GtkWidget *shortcut_help = gtk_label_new(
        "Examples: Super+Space, Ctrl+Alt+F, Alt+F1\n"
        "Use Super (Windows key), Ctrl, Alt, Shift as modifiers"
    );
    gtk_label_set_xalign(GTK_LABEL(shortcut_help), 0.0);
    gtk_widget_set_margin_top(shortcut_help, 8);
    
    /* Make help text smaller */
    PangoAttrList *attrs = pango_attr_list_new();
    PangoAttribute *scale_attr = pango_attr_scale_new(PANGO_SCALE_SMALL);
    pango_attr_list_insert(attrs, scale_attr);
    gtk_label_set_attributes(GTK_LABEL(shortcut_help), attrs);
    pango_attr_list_unref(attrs);
    
    gtk_box_pack_start(GTK_BOX(shortcut_vbox), shortcut_help, FALSE, FALSE, 0);
    
    /* Database info section */
    GtkWidget *db_frame = gtk_frame_new("Database Information");
    gtk_box_pack_start(GTK_BOX(settings_vbox), db_frame, FALSE, FALSE, 0);
    
    GtkWidget *db_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(db_vbox), 12);
    gtk_container_add(GTK_CONTAINER(db_frame), db_vbox);
    
    /* Get database stats */
    gchar *db_info_text = g_strdup("Database: ~/.local/share/novasearch/index.db\nConfiguration: ~/.config/novasearch/config.toml");
    
    GtkWidget *db_info_label = gtk_label_new(db_info_text);
    gtk_label_set_xalign(GTK_LABEL(db_info_label), 0.0);
    gtk_label_set_selectable(GTK_LABEL(db_info_label), TRUE);
    gtk_box_pack_start(GTK_BOX(db_vbox), db_info_label, FALSE, FALSE, 0);
    g_free(db_info_text);
    
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), settings_vbox, gtk_label_new("Hotkeys"));
    
    /* === CONFIGURATION TAB === */
    GtkWidget *config_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER(config_vbox), 12);
    
    /* Configuration file editing */
    GtkWidget *config_frame = gtk_frame_new("Configuration File Editor");
    gtk_box_pack_start(GTK_BOX(config_vbox), config_frame, TRUE, TRUE, 0);
    
    GtkWidget *config_inner_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(config_inner_vbox), 12);
    gtk_container_add(GTK_CONTAINER(config_frame), config_inner_vbox);
    
    GtkWidget *config_info_label = gtk_label_new(
        "Edit your NovaSearch configuration directly below.\n"
        "Changes will be saved to ~/.config/novasearch/config.toml"
    );
    gtk_label_set_xalign(GTK_LABEL(config_info_label), 0.0);
    gtk_box_pack_start(GTK_BOX(config_inner_vbox), config_info_label, FALSE, FALSE, 0);
    
    /* Scrolled window for text view */
    GtkWidget *config_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(config_scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(config_scroll), GTK_SHADOW_IN);
    gtk_widget_set_size_request(config_scroll, -1, 300);
    gtk_box_pack_start(GTK_BOX(config_inner_vbox), config_scroll, TRUE, TRUE, 0);
    
    /* Text view for config editing */
    GtkWidget *config_textview = gtk_text_view_new();
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(config_textview), TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(config_textview), GTK_WRAP_NONE);
    gtk_container_add(GTK_CONTAINER(config_scroll), config_textview);
    
    /* Load current config */
    gchar *config_path = g_build_filename(g_get_user_config_dir(), "novasearch", "config.toml", NULL);
    gchar *config_contents = NULL;
    
    if (g_file_test(config_path, G_FILE_TEST_EXISTS)) {
        GError *error = NULL;
        if (g_file_get_contents(config_path, &config_contents, NULL, &error)) {
            GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(config_textview));
            gtk_text_buffer_set_text(buffer, config_contents, -1);
            g_free(config_contents);
        } else {
            g_warning("Failed to read config file: %s", error ? error->message : "Unknown error");
            if (error) g_error_free(error);
        }
    } else {
        /* Create default config */
        const gchar *default_config = 
            "[indexing]\n"
            "include_paths = [\"/home/kamil\"]\n"
            "exclude_patterns = [\".*\", \"*.tmp\", \"*.log\"]\n"
            "\n"
            "[performance]\n"
            "max_cpu_percent = 10\n"
            "max_memory_mb = 100\n"
            "batch_size = 100\n"
            "flush_interval_ms = 1000\n"
            "\n"
            "[ui]\n"
            "keyboard_shortcut = \"Super+Space\"\n"
            "max_results = 50\n";
        
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(config_textview));
        gtk_text_buffer_set_text(buffer, default_config, -1);
    }
    g_free(config_path);
    
    /* Save config button */
    GtkWidget *save_config_button = gtk_button_new_with_label("Save Configuration");
    gtk_widget_set_halign(save_config_button, GTK_ALIGN_END);
    gtk_box_pack_start(GTK_BOX(config_inner_vbox), save_config_button, FALSE, FALSE, 0);
    
    /* Store references */
    g_object_set_data(G_OBJECT(dialog), "config-textview", config_textview);
    g_signal_connect(G_OBJECT(save_config_button), "clicked",
                     G_CALLBACK(nova_search_save_config_file), dialog);
    
    /* Configuration help */
    GtkWidget *config_help_frame = gtk_frame_new("Configuration Help");
    gtk_box_pack_start(GTK_BOX(config_vbox), config_help_frame, FALSE, FALSE, 0);
    
    GtkWidget *config_help_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(config_help_vbox), 8);
    gtk_container_add(GTK_CONTAINER(config_help_frame), config_help_vbox);
    
    const gchar *help_sections[] = {
        "• [indexing] - Configure which paths to index and exclude patterns",
        "• [performance] - Set CPU/memory limits and batch processing options", 
        "• [ui] - User interface settings like keyboard shortcuts and result limits",
        NULL
    };
    
    for (int i = 0; help_sections[i] != NULL; i++) {
        GtkWidget *help_label = gtk_label_new(help_sections[i]);
        gtk_label_set_xalign(GTK_LABEL(help_label), 0.0);
        
        PangoAttrList *help_attrs = pango_attr_list_new();
        PangoAttribute *help_scale = pango_attr_scale_new(PANGO_SCALE_SMALL);
        pango_attr_list_insert(help_attrs, help_scale);
        gtk_label_set_attributes(GTK_LABEL(help_label), help_attrs);
        pango_attr_list_unref(help_attrs);
        
        gtk_box_pack_start(GTK_BOX(config_help_vbox), help_label, FALSE, FALSE, 0);
    }
    
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), config_vbox, gtk_label_new("Configuration"));
    
    /* === ABOUT TAB === */
    GtkWidget *about_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_container_set_border_width(GTK_CONTAINER(about_vbox), 20);
    
    /* Logo/Icon */
    GtkWidget *icon = gtk_image_new_from_icon_name("system-search", GTK_ICON_SIZE_DIALOG);
    gtk_box_pack_start(GTK_BOX(about_vbox), icon, FALSE, FALSE, 0);
    
    /* Title */
    GtkWidget *title_label = gtk_label_new("NovaSearch");
    PangoAttrList *title_attrs = pango_attr_list_new();
    PangoAttribute *title_size = pango_attr_size_new(24 * PANGO_SCALE);
    PangoAttribute *title_weight = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
    pango_attr_list_insert(title_attrs, title_size);
    pango_attr_list_insert(title_attrs, title_weight);
    gtk_label_set_attributes(GTK_LABEL(title_label), title_attrs);
    pango_attr_list_unref(title_attrs);
    gtk_box_pack_start(GTK_BOX(about_vbox), title_label, FALSE, FALSE, 0);
    
    /* Version */
    GtkWidget *version_label = gtk_label_new("Version 0.1.0");
    gtk_box_pack_start(GTK_BOX(about_vbox), version_label, FALSE, FALSE, 0);
    
    /* Description */
    GtkWidget *desc_label = gtk_label_new(
        "Fast system-wide file search for Linux with XFCE4 integration.\n"
        "Provides real-time file indexing and intelligent search ranking\n"
        "based on usage patterns, similar to macOS Spotlight."
    );
    gtk_label_set_justify(GTK_LABEL(desc_label), GTK_JUSTIFY_CENTER);
    gtk_label_set_line_wrap(GTK_LABEL(desc_label), TRUE);
    gtk_box_pack_start(GTK_BOX(about_vbox), desc_label, FALSE, FALSE, 0);
    
    /* Components info */
    GtkWidget *components_frame = gtk_frame_new("Components");
    gtk_box_pack_start(GTK_BOX(about_vbox), components_frame, FALSE, FALSE, 0);
    
    GtkWidget *components_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(components_vbox), 12);
    gtk_container_add(GTK_CONTAINER(components_frame), components_vbox);
    
    GtkWidget *daemon_label = gtk_label_new("• Daemon: Real-time filesystem indexing (Rust)");
    gtk_label_set_xalign(GTK_LABEL(daemon_label), 0.0);
    gtk_box_pack_start(GTK_BOX(components_vbox), daemon_label, FALSE, FALSE, 0);
    
    GtkWidget *panel_label = gtk_label_new("• Panel Plugin: XFCE4 integration (C + GTK3)");
    gtk_label_set_xalign(GTK_LABEL(panel_label), 0.0);
    gtk_box_pack_start(GTK_BOX(components_vbox), panel_label, FALSE, FALSE, 0);
    
    GtkWidget *db_label = gtk_label_new("• Database: SQLite with usage tracking");
    gtk_label_set_xalign(GTK_LABEL(db_label), 0.0);
    gtk_box_pack_start(GTK_BOX(components_vbox), db_label, FALSE, FALSE, 0);
    
    /* Author info */
    GtkWidget *author_frame = gtk_frame_new("Author & License");
    gtk_box_pack_start(GTK_BOX(about_vbox), author_frame, FALSE, FALSE, 0);
    
    GtkWidget *author_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(author_vbox), 12);
    gtk_container_add(GTK_CONTAINER(author_frame), author_vbox);
    
    GtkWidget *author_label = gtk_label_new("Created by Kamil 'Novik' Nowicki");
    gtk_label_set_xalign(GTK_LABEL(author_label), 0.0);
    gtk_box_pack_start(GTK_BOX(author_vbox), author_label, FALSE, FALSE, 0);
    
    GtkWidget *license_label = gtk_label_new("Licensed under GPL-3.0");
    gtk_label_set_xalign(GTK_LABEL(license_label), 0.0);
    gtk_box_pack_start(GTK_BOX(author_vbox), license_label, FALSE, FALSE, 0);
    
    GtkWidget *github_label = gtk_label_new("GitHub: https://github.com/novik133/NovaSearch");
    gtk_label_set_xalign(GTK_LABEL(github_label), 0.0);
    gtk_label_set_selectable(GTK_LABEL(github_label), TRUE);
    gtk_box_pack_start(GTK_BOX(author_vbox), github_label, FALSE, FALSE, 0);
    
    /* Donation section */
    GtkWidget *donation_frame = gtk_frame_new("Support Development");
    gtk_box_pack_start(GTK_BOX(about_vbox), donation_frame, FALSE, FALSE, 0);
    
    GtkWidget *donation_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER(donation_vbox), 12);
    gtk_container_add(GTK_CONTAINER(donation_frame), donation_vbox);
    
    GtkWidget *donation_text = gtk_label_new(
        "If you like NovaSearch and find it useful, please consider\n"
        "supporting its development with a small donation."
    );
    gtk_label_set_justify(GTK_LABEL(donation_text), GTK_JUSTIFY_CENTER);
    gtk_label_set_line_wrap(GTK_LABEL(donation_text), TRUE);
    gtk_box_pack_start(GTK_BOX(donation_vbox), donation_text, FALSE, FALSE, 0);
    
    /* Ko-fi donation button */
    GtkWidget *donation_button = gtk_button_new();
    gtk_widget_set_size_request(donation_button, 200, 40);
    
    /* Create button content with icon and text */
    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(button_box, GTK_ALIGN_CENTER);
    
    /* Coffee icon (heart icon as fallback) */
    GtkWidget *coffee_icon = gtk_image_new_from_icon_name("emblem-favorite", GTK_ICON_SIZE_BUTTON);
    gtk_box_pack_start(GTK_BOX(button_box), coffee_icon, FALSE, FALSE, 0);
    
    GtkWidget *button_label = gtk_label_new("Support on Ko-fi");
    PangoAttrList *button_attrs = pango_attr_list_new();
    PangoAttribute *button_weight = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
    pango_attr_list_insert(button_attrs, button_weight);
    gtk_label_set_attributes(GTK_LABEL(button_label), button_attrs);
    pango_attr_list_unref(button_attrs);
    gtk_box_pack_start(GTK_BOX(button_box), button_label, FALSE, FALSE, 0);
    
    gtk_container_add(GTK_CONTAINER(donation_button), button_box);
    
    /* Style the donation button */
    GtkStyleContext *button_context = gtk_widget_get_style_context(donation_button);
    gtk_style_context_add_class(button_context, "suggested-action");
    
    /* Center the button */
    gtk_widget_set_halign(donation_button, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(donation_vbox), donation_button, FALSE, FALSE, 0);
    
    /* Connect donation button signal */
    g_signal_connect(G_OBJECT(donation_button), "clicked",
                     G_CALLBACK(nova_search_open_donation_link), NULL);
    
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), about_vbox, gtk_label_new("About"));
    
    /* Store shortcut entry for later access */
    g_object_set_data(G_OBJECT(dialog), "plugin-data", ns_plugin);
    
    gtk_widget_show_all(content);
    
    /* Handle dialog response */
    gint response;
    do {
        response = gtk_dialog_run(GTK_DIALOG(dialog));
        
        if (response == GTK_RESPONSE_APPLY) {
            /* Get the captured shortcut */
            gchar *captured_shortcut = g_object_get_data(G_OBJECT(dialog), "captured-shortcut");
            
            if (captured_shortcut && strlen(captured_shortcut) > 0) {
                /* Save to config file */
                nova_search_save_keyboard_shortcut_to_config(captured_shortcut);
                
                /* Re-register shortcut */
                if (ns_plugin) {
                    nova_search_unregister_keyboard_shortcut(ns_plugin);
                    nova_search_register_keyboard_shortcut(ns_plugin);
                }
                
                /* Update display */
                GtkWidget *shortcut_display = g_object_get_data(G_OBJECT(dialog), "shortcut-display");
                if (shortcut_display) {
                    gchar *display_text = g_strdup_printf("Current: %s", captured_shortcut);
                    gtk_label_set_text(GTK_LABEL(shortcut_display), display_text);
                    g_free(display_text);
                }
                
                /* Show confirmation */
                GtkWidget *info_dialog = gtk_message_dialog_new(
                    GTK_WINDOW(dialog),
                    GTK_DIALOG_MODAL,
                    GTK_MESSAGE_INFO,
                    GTK_BUTTONS_OK,
                    "Settings saved successfully!\nNew keyboard shortcut: %s",
                    captured_shortcut
                );
                gtk_dialog_run(GTK_DIALOG(info_dialog));
                gtk_widget_destroy(info_dialog);
            } else {
                /* No shortcut captured */
                GtkWidget *warning_dialog = gtk_message_dialog_new(
                    GTK_WINDOW(dialog),
                    GTK_DIALOG_MODAL,
                    GTK_MESSAGE_WARNING,
                    GTK_BUTTONS_OK,
                    "Please capture a keyboard shortcut first by clicking 'Set New Shortcut' and pressing your desired key combination."
                );
                gtk_dialog_run(GTK_DIALOG(warning_dialog));
                gtk_widget_destroy(warning_dialog);
            }
        }
    } while (response == GTK_RESPONSE_APPLY);
    
    /* Clean up captured shortcut data */
    gchar *captured_shortcut = g_object_get_data(G_OBJECT(dialog), "captured-shortcut");
    if (captured_shortcut) {
        g_free(captured_shortcut);
    }
    
    gtk_widget_destroy(dialog);
}

/* Start keyboard shortcut capture */
static void nova_search_start_shortcut_capture(GtkButton *button, gpointer user_data) {
    (void)button; /* Unused parameter */
    GtkWidget *dialog = GTK_WIDGET(user_data);
    GtkWidget *capture_button = g_object_get_data(G_OBJECT(dialog), "capture-button");
    
    /* Change button text and make it sensitive to key presses */
    gtk_button_set_label(GTK_BUTTON(capture_button), "Press keys now...");
    gtk_widget_set_sensitive(capture_button, FALSE);
    
    /* Make dialog modal and grab focus */
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_widget_set_can_focus(dialog, TRUE);
    
    /* Set up key capture on the dialog */
    g_signal_connect(G_OBJECT(dialog), "key-press-event",
                     G_CALLBACK(nova_search_capture_key_press), dialog);
    
    /* Grab keyboard focus */
    gtk_widget_grab_focus(dialog);
    gtk_window_present(GTK_WINDOW(dialog));
}

/* Capture key press for shortcut */
static gboolean nova_search_capture_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
    (void)widget; /* Unused parameter */
    GtkWidget *dialog = GTK_WIDGET(user_data);
    GtkWidget *capture_button = g_object_get_data(G_OBJECT(dialog), "capture-button");
    GtkWidget *shortcut_display = g_object_get_data(G_OBJECT(dialog), "shortcut-display");
    
    /* Ignore modifier-only key presses */
    if (event->keyval == GDK_KEY_Control_L || event->keyval == GDK_KEY_Control_R ||
        event->keyval == GDK_KEY_Alt_L || event->keyval == GDK_KEY_Alt_R ||
        event->keyval == GDK_KEY_Shift_L || event->keyval == GDK_KEY_Shift_R ||
        event->keyval == GDK_KEY_Super_L || event->keyval == GDK_KEY_Super_R ||
        event->keyval == GDK_KEY_Meta_L || event->keyval == GDK_KEY_Meta_R) {
        return TRUE; /* Consume the event but don't process it */
    }
    
    /* Build shortcut string */
    GString *shortcut = g_string_new("");
    
    /* Add modifiers */
    if (event->state & GDK_SUPER_MASK) {
        g_string_append(shortcut, "Super+");
    }
    if (event->state & GDK_CONTROL_MASK) {
        g_string_append(shortcut, "Ctrl+");
    }
    if (event->state & GDK_MOD1_MASK) {  /* Alt key */
        g_string_append(shortcut, "Alt+");
    }
    if (event->state & GDK_SHIFT_MASK) {
        g_string_append(shortcut, "Shift+");
    }
    
    /* Add the main key */
    const gchar *key_name = gdk_keyval_name(event->keyval);
    if (key_name) {
        /* Convert some key names to more user-friendly versions */
        if (g_strcmp0(key_name, "space") == 0) {
            g_string_append(shortcut, "Space");
        } else if (g_strcmp0(key_name, "Return") == 0) {
            g_string_append(shortcut, "Enter");
        } else if (g_strcmp0(key_name, "Escape") == 0) {
            g_string_append(shortcut, "Escape");
        } else if (g_strcmp0(key_name, "Tab") == 0) {
            g_string_append(shortcut, "Tab");
        } else if (g_str_has_prefix(key_name, "F") && g_ascii_isdigit(key_name[1])) {
            /* Function keys */
            g_string_append(shortcut, key_name);
        } else if (g_ascii_isalnum(key_name[0]) && strlen(key_name) == 1) {
            /* Single letter/number */
            gchar upper_key = g_ascii_toupper(key_name[0]);
            g_string_append_c(shortcut, upper_key);
        } else {
            /* Use the key name as-is */
            g_string_append(shortcut, key_name);
        }
    }
    
    /* Only accept shortcuts with at least one modifier or function key */
    if ((event->state & (GDK_SUPER_MASK | GDK_CONTROL_MASK | GDK_MOD1_MASK)) || 
        (key_name && g_str_has_prefix(key_name, "F"))) {
        
        /* Store the captured shortcut */
        gchar *shortcut_str = g_string_free(shortcut, FALSE);
        g_object_set_data_full(G_OBJECT(dialog), "captured-shortcut", 
                               g_strdup(shortcut_str), g_free);
        
        /* Update display */
        gchar *display_text = g_strdup_printf("New: %s", shortcut_str);
        gtk_label_set_text(GTK_LABEL(shortcut_display), display_text);
        g_free(display_text);
        g_free(shortcut_str);
        
        /* Reset button and dialog state */
        gtk_button_set_label(GTK_BUTTON(capture_button), "Set New Shortcut");
        gtk_widget_set_sensitive(capture_button, TRUE);
        gtk_window_set_modal(GTK_WINDOW(dialog), FALSE);
        
        /* Disconnect key capture */
        g_signal_handlers_disconnect_by_func(G_OBJECT(dialog), 
                                             G_CALLBACK(nova_search_capture_key_press), 
                                             dialog);
        
        return TRUE; /* Event handled */
    } else {
        g_string_free(shortcut, TRUE);
        
        /* If it's Escape, cancel the capture */
        if (event->keyval == GDK_KEY_Escape) {
            /* Reset button and dialog state */
            gtk_button_set_label(GTK_BUTTON(capture_button), "Set New Shortcut");
            gtk_widget_set_sensitive(capture_button, TRUE);
            gtk_window_set_modal(GTK_WINDOW(dialog), FALSE);
            
            /* Disconnect key capture */
            g_signal_handlers_disconnect_by_func(G_OBJECT(dialog), 
                                                 G_CALLBACK(nova_search_capture_key_press), 
                                                 dialog);
            return TRUE;
        }
        
        return FALSE; /* Let other handlers process this event */
    }
}

/* Open donation link */
static void nova_search_open_donation_link(GtkButton *button, gpointer user_data) {
    (void)button;    /* Unused parameter */
    (void)user_data; /* Unused parameter */
    
    const gchar *url = "https://ko-fi.com/novadesktop";
    GError *error = NULL;
    
    /* Try to open the URL */
    if (!gtk_show_uri_on_window(NULL, url, GDK_CURRENT_TIME, &error)) {
        /* Fallback to xdg-open */
        if (error) {
            g_error_free(error);
            error = NULL;
        }
        
        gchar *command = g_strdup_printf("xdg-open '%s'", url);
        if (!g_spawn_command_line_async(command, &error)) {
            /* Show error dialog */
            GtkWidget *error_dialog = gtk_message_dialog_new(
                NULL,
                GTK_DIALOG_MODAL,
                GTK_MESSAGE_ERROR,
                GTK_BUTTONS_OK,
                "Could not open donation link.\nPlease visit: %s",
                url
            );
            gtk_dialog_run(GTK_DIALOG(error_dialog));
            gtk_widget_destroy(error_dialog);
            
            if (error) {
                g_error_free(error);
            }
        }
        g_free(command);
    }
}

/* Save keyboard shortcut to config file */
static void nova_search_save_keyboard_shortcut_to_config(const gchar *shortcut) {
    if (!shortcut) {
        return;
    }
    
    /* Ensure config directory exists */
    gchar *config_dir = g_build_filename(g_get_user_config_dir(), "novasearch", NULL);
    g_mkdir_with_parents(config_dir, 0755);
    
    gchar *config_path = g_build_filename(config_dir, "config.toml", NULL);
    g_free(config_dir);
    
    /* Read existing config or create new one */
    gchar *contents = NULL;
    gsize length = 0;
    GError *error = NULL;
    
    if (g_file_test(config_path, G_FILE_TEST_EXISTS)) {
        if (!g_file_get_contents(config_path, &contents, &length, &error)) {
            g_warning("Failed to read config file: %s", error ? error->message : "Unknown error");
            if (error) {
                g_error_free(error);
            }
            g_free(config_path);
            return;
        }
    }
    
    /* Parse and update config */
    GString *new_config = g_string_new("");
    
    if (contents) {
        gchar **lines = g_strsplit(contents, "\n", -1);
        gboolean in_ui_section = FALSE;
        gboolean shortcut_updated = FALSE;
        
        for (gint i = 0; lines[i] != NULL; i++) {
            gchar *line = g_strstrip(g_strdup(lines[i]));
            
            /* Check for [ui] section */
            if (g_strcmp0(line, "[ui]") == 0) {
                in_ui_section = TRUE;
                g_string_append_printf(new_config, "%s\n", line);
                g_free(line);
                continue;
            }
            
            /* Check for other sections */
            if (line[0] == '[') {
                /* If we were in [ui] section and didn't update shortcut, add it */
                if (in_ui_section && !shortcut_updated) {
                    g_string_append_printf(new_config, "keyboard_shortcut = \"%s\"\n", shortcut);
                    shortcut_updated = TRUE;
                }
                in_ui_section = FALSE;
                g_string_append_printf(new_config, "%s\n", line);
                g_free(line);
                continue;
            }
            
            /* Update keyboard_shortcut in [ui] section */
            if (in_ui_section && g_str_has_prefix(line, "keyboard_shortcut")) {
                g_string_append_printf(new_config, "keyboard_shortcut = \"%s\"\n", shortcut);
                shortcut_updated = TRUE;
                g_free(line);
                continue;
            }
            
            /* Keep other lines as-is */
            g_string_append_printf(new_config, "%s\n", line);
            g_free(line);
        }
        
        /* If we were in [ui] section at end of file and didn't update shortcut, add it */
        if (in_ui_section && !shortcut_updated) {
            g_string_append_printf(new_config, "keyboard_shortcut = \"%s\"\n", shortcut);
            shortcut_updated = TRUE;
        }
        
        /* If no [ui] section found, add it */
        if (!shortcut_updated) {
            g_string_append_printf(new_config, "\n[ui]\nkeyboard_shortcut = \"%s\"\n", shortcut);
        }
        
        g_strfreev(lines);
        g_free(contents);
    } else {
        /* Create new config file */
        g_string_append_printf(new_config, 
            "[indexing]\n"
            "include_paths = [\"%s\"]\n"
            "exclude_patterns = [\".*\"]\n"
            "\n"
            "[performance]\n"
            "max_cpu_percent = 10\n"
            "max_memory_mb = 100\n"
            "batch_size = 100\n"
            "flush_interval_ms = 1000\n"
            "\n"
            "[ui]\n"
            "keyboard_shortcut = \"%s\"\n"
            "max_results = 50\n",
            g_get_home_dir(),
            shortcut
        );
    }
    
    /* Write updated config */
    if (!g_file_set_contents(config_path, new_config->str, -1, &error)) {
        g_warning("Failed to save config file: %s", error ? error->message : "Unknown error");
        if (error) {
            g_error_free(error);
        }
    }
    
    g_string_free(new_config, TRUE);
    g_free(config_path);
}

/* Save configuration file */
static void nova_search_save_config_file(GtkButton *button, gpointer user_data) {
    (void)button; /* Unused parameter */
    GtkWidget *dialog = GTK_WIDGET(user_data);
    GtkWidget *config_textview = g_object_get_data(G_OBJECT(dialog), "config-textview");
    
    if (!config_textview) {
        g_warning("Config textview not found");
        return;
    }
    
    /* Get text from the text view */
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(config_textview));
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    gchar *config_text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
    
    if (!config_text) {
        g_warning("Failed to get config text");
        return;
    }
    
    /* Ensure config directory exists */
    gchar *config_dir = g_build_filename(g_get_user_config_dir(), "novasearch", NULL);
    if (g_mkdir_with_parents(config_dir, 0755) != 0) {
        g_warning("Failed to create config directory: %s", config_dir);
        g_free(config_dir);
        g_free(config_text);
        return;
    }
    
    /* Write config file */
    gchar *config_path = g_build_filename(config_dir, "config.toml", NULL);
    GError *error = NULL;
    
    if (!g_file_set_contents(config_path, config_text, -1, &error)) {
        /* Show error dialog */
        GtkWidget *error_dialog = gtk_message_dialog_new(
            GTK_WINDOW(dialog),
            GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_ERROR,
            GTK_BUTTONS_OK,
            "Failed to save configuration file:\n%s",
            error ? error->message : "Unknown error"
        );
        gtk_dialog_run(GTK_DIALOG(error_dialog));
        gtk_widget_destroy(error_dialog);
        
        if (error) {
            g_error_free(error);
        }
    } else {
        /* Show success dialog */
        GtkWidget *success_dialog = gtk_message_dialog_new(
            GTK_WINDOW(dialog),
            GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_INFO,
            GTK_BUTTONS_OK,
            "Configuration saved successfully!\n\nPath: %s\n\nRestart the daemon for changes to take effect.",
            config_path
        );
        gtk_dialog_run(GTK_DIALOG(success_dialog));
        gtk_widget_destroy(success_dialog);
    }
    
    g_free(config_dir);
    g_free(config_path);
    g_free(config_text);
}

/* Check if a file is a .desktop file */
static gboolean nova_search_is_desktop_file(const gchar *file_path) {
    return file_path && g_str_has_suffix(file_path, ".desktop");
}

/* Parse a specific field from a .desktop file */
static gchar* nova_search_parse_desktop_file_field(const gchar *file_path, const gchar *field) {
    if (!file_path || !field) {
        return NULL;
    }
    
    GKeyFile *key_file = g_key_file_new();
    GError *error = NULL;
    gchar *value = NULL;
    
    if (g_key_file_load_from_file(key_file, file_path, G_KEY_FILE_NONE, &error)) {
        value = g_key_file_get_string(key_file, "Desktop Entry", field, NULL);
    } else {
        g_warning("Failed to parse desktop file %s: %s", file_path, error ? error->message : "Unknown error");
        if (error) {
            g_error_free(error);
        }
    }
    
    g_key_file_free(key_file);
    return value;
}

/* Get icon name from .desktop file */
static gchar* nova_search_get_desktop_icon(const gchar *file_path) {
    return nova_search_parse_desktop_file_field(file_path, "Icon");
}

/* Get executable command from .desktop file */
static gchar* nova_search_get_desktop_exec(const gchar *file_path) {
    return nova_search_parse_desktop_file_field(file_path, "Exec");
}

/* Launch a desktop application */
static void nova_search_launch_desktop_application(const gchar *file_path) {
    if (!file_path) {
        return;
    }
    
    GDesktopAppInfo *app_info = g_desktop_app_info_new_from_filename(file_path);
    if (app_info) {
        GError *error = NULL;
        gboolean success = g_app_info_launch(G_APP_INFO(app_info), NULL, NULL, &error);
        
        if (!success) {
            g_warning("Failed to launch application from %s: %s", 
                     file_path, error ? error->message : "Unknown error");
            if (error) {
                g_error_free(error);
            }
        }
        
        g_object_unref(app_info);
    } else {
        g_warning("Failed to create app info from %s", file_path);
    }
}

/* Plugin construction function */
static void nova_search_plugin_construct(XfcePanelPlugin *plugin) {
    /* Create plugin instance */
    NovaSearchPlugin *ns_plugin = nova_search_plugin_new(plugin);
    
    /* Store plugin data for access from configure dialog */
    g_object_set_data(G_OBJECT(plugin), "plugin-data", ns_plugin);
    
    /* Enable configure menu item */
    xfce_panel_plugin_menu_show_configure(plugin);
    xfce_panel_plugin_menu_show_about(plugin);
    
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
