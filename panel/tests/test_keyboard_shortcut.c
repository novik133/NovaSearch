/* Test for keyboard shortcut functionality */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <assert.h>
#include <unistd.h>

/* Default keyboard shortcut */
#define DEFAULT_KEYBOARD_SHORTCUT "<Super>space"

/* Function prototypes - copied from main.c */
static gchar* nova_search_read_keyboard_shortcut_from_config(void);
static gchar* nova_search_convert_shortcut_format(const gchar *shortcut);

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

/* Test cases */
void test_convert_shortcut_format() {
    printf("Testing shortcut format conversion...\n");
    
    /* Test 1: Super+Space */
    gchar *result = nova_search_convert_shortcut_format("Super+Space");
    assert(result != NULL);
    assert(g_strcmp0(result, "<Super>space") == 0);
    printf("  ✓ Super+Space -> %s\n", result);
    g_free(result);
    
    /* Test 2: Ctrl+Alt+F */
    result = nova_search_convert_shortcut_format("Ctrl+Alt+F");
    assert(result != NULL);
    assert(g_strcmp0(result, "<Ctrl><Alt>f") == 0);
    printf("  ✓ Ctrl+Alt+F -> %s\n", result);
    g_free(result);
    
    /* Test 3: Control+Shift+S */
    result = nova_search_convert_shortcut_format("Control+Shift+S");
    assert(result != NULL);
    assert(g_strcmp0(result, "<Control><Shift>s") == 0);
    printf("  ✓ Control+Shift+S -> %s\n", result);
    g_free(result);
    
    /* Test 4: Alt+Tab */
    result = nova_search_convert_shortcut_format("Alt+Tab");
    assert(result != NULL);
    assert(g_strcmp0(result, "<Alt>tab") == 0);
    printf("  ✓ Alt+Tab -> %s\n", result);
    g_free(result);
    
    /* Test 5: NULL input */
    result = nova_search_convert_shortcut_format(NULL);
    assert(result == NULL);
    printf("  ✓ NULL -> NULL\n");
    
    printf("All shortcut conversion tests passed!\n\n");
}

void test_read_config() {
    printf("Testing config file reading...\n");
    
    /* Test reading from actual config if it exists */
    gchar *shortcut = nova_search_read_keyboard_shortcut_from_config();
    
    if (shortcut) {
        printf("  ✓ Successfully read shortcut from config: %s\n", shortcut);
        
        /* Test conversion */
        gchar *converted = nova_search_convert_shortcut_format(shortcut);
        if (converted) {
            printf("  ✓ Converted to keybinder format: %s\n", converted);
            g_free(converted);
        }
        g_free(shortcut);
    } else {
        printf("  ℹ No config file found (will use default)\n");
    }
    
    printf("Config reading test completed!\n\n");
}

void test_default_shortcut() {
    printf("Testing default shortcut...\n");
    
    /* When no config exists, should return NULL and use default */
    gchar *shortcut = nova_search_read_keyboard_shortcut_from_config();
    
    if (shortcut == NULL) {
        printf("  ✓ No config file returns NULL (will use default: %s)\n", DEFAULT_KEYBOARD_SHORTCUT);
    } else {
        printf("  ℹ Config file exists with shortcut: %s\n", shortcut);
        g_free(shortcut);
    }
    
    printf("Default shortcut test completed!\n\n");
}

int main(int argc, char *argv[]) {
    printf("=== NovaSearch Keyboard Shortcut Tests ===\n\n");
    
    test_convert_shortcut_format();
    test_default_shortcut();
    test_read_config();
    
    printf("=== All tests completed successfully! ===\n");
    
    return 0;
}
