// SPDX-License-Identifier: AGPL-3.0-or-later
// gmux - A GTK4 terminal multiplexer with project-based workflow
// Uses VTE (Virtual Terminal Emulator) library

#include <gtk/gtk.h>
#include <vte/vte.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <json-glib/json-glib.h>
#include "themes.h"

typedef struct _SubTab SubTab;
typedef struct _Project Project;

typedef struct {
    GdkRGBA foreground;
    GdkRGBA background;
    GdkRGBA palette[16];
    GdkRGBA bold_color;
    gboolean bold_color_set;
    GdkRGBA cursor_bg;
    GdkRGBA cursor_fg;
    gboolean cursor_colors_set;
    GdkRGBA highlight_bg;
    GdkRGBA highlight_fg;
    gboolean highlight_colors_set;
    PangoFontDescription *font;
    VteCursorShape cursor_shape;
    VteCursorBlinkMode cursor_blink;
    gboolean bold_is_bright;
    gboolean use_theme_colors;
    gboolean loaded;
} TerminalTheme;

typedef struct {
    char *font_family;    // NULL = use profile value
    double font_size;     // -1.0 = use profile value
    double opacity;       // 0.0-1.0, default 1.0
    int cursor_shape;     // -1 = profile, 0=block, 1=ibeam, 2=underline
    int cursor_blink;     // -1 = profile, 0=system, 1=on, 2=off
} TerminalSettings;

typedef enum {
    SORT_NONE,
    SORT_ALPHA,
    SORT_MRU
} SortMode;

typedef struct {
    GtkWidget *window;
    GtkWidget *sidebar;
    GtkWidget *notebook;
    GtkWidget *sidebar_box;
    GList *projects;
    void *active_project;
    TerminalTheme theme;
    TerminalSettings settings;
    char *theme_name;
    GtkCssProvider *css_provider;
    int last_width;
    int last_height;
    gboolean last_maximized;
    SortMode sort_mode;
    GtkWidget *sort_button;
} AppState;

typedef struct {
    char *name;
    char *working_dir;
} SavedSubTab;

struct _SubTab {
    VteTerminal *terminal;
    GtkWidget *scrolled;
    GtkWidget *tab_widget;
    GtkWidget *tab_button;
    GtkWidget *tab_label;
    char *name;
    Project *parent_tab;
    gboolean closing;
};

struct _Project {
    GtkWidget *list_row;
    GtkWidget *tab_container;
    GtkWidget *tab_header;
    GtkWidget *tabs_box;
    GtkWidget *terminal_stack;
    GList *subtabs;
    SubTab *active_subtab;
    int subtab_counter;
    char *name;
    char *path;
    AppState *app;
    gboolean initialized;
    gint64 last_used;
    int insert_order;
    GList *saved_subtabs;       // List of SavedSubTab* (pending restore)
    int saved_active_subtab;    // Index to activate on restore
    GtkWidget *tab_count_label; // Badge showing number of open tabs
};

static Project* create_project(AppState *app, const char *name, const char *path, gboolean init_terminal);
static SubTab* create_subtab(Project *project, const char *name, const char *working_dir);
static void close_subtab(SubTab *subtab);
static void on_subtab_button_clicked(GtkButton *button, gpointer user_data);
static char* get_sort_config_path(void);
static void on_project_selected(GtkListBox *box, GtkListBoxRow *row, gpointer user_data);
static void setup_tabs_box_drag_reorder(Project *project);

//=============================================================================
// Config Persistence
//=============================================================================

static char* get_data_dir(void) {
    char *dir = g_build_filename(g_get_user_data_dir(), "gmux", NULL);
    g_mkdir_with_parents(dir, 0755);
    return dir;
}

static void migrate_config_to_data(void) {
    char *data_dir = get_data_dir();
    char *new_projects = g_build_filename(data_dir, "projects.conf", NULL);

    // Only migrate if new location doesn't exist yet
    if (!g_file_test(new_projects, G_FILE_TEST_EXISTS)) {
        char *old_dir = g_build_filename(g_get_user_config_dir(), "gmux", NULL);
        char *old_projects = g_build_filename(old_dir, "projects.conf", NULL);
        char *old_theme = g_build_filename(old_dir, "theme.conf", NULL);

        if (g_file_test(old_projects, G_FILE_TEST_EXISTS)) {
            char *contents = NULL;
            gsize len = 0;
            if (g_file_get_contents(old_projects, &contents, &len, NULL)) {
                g_file_set_contents(new_projects, contents, len, NULL);
                g_free(contents);
            }
        }

        char *new_theme = g_build_filename(data_dir, "theme.conf", NULL);
        if (g_file_test(old_theme, G_FILE_TEST_EXISTS) &&
            !g_file_test(new_theme, G_FILE_TEST_EXISTS)) {
            char *contents = NULL;
            gsize len = 0;
            if (g_file_get_contents(old_theme, &contents, &len, NULL)) {
                g_file_set_contents(new_theme, contents, len, NULL);
                g_free(contents);
            }
        }

        g_free(new_theme);
        g_free(old_projects);
        g_free(old_theme);
        g_free(old_dir);
    }

    g_free(new_projects);
    g_free(data_dir);
}

static char* get_session_config_path(void) {
    char *dir = get_data_dir();
    char *path = g_build_filename(dir, "session.json", NULL);
    g_free(dir);
    return path;
}

static char* get_legacy_projects_path(void) {
    char *dir = get_data_dir();
    char *path = g_build_filename(dir, "projects.conf", NULL);
    g_free(dir);
    return path;
}

static char* get_theme_config_path(void) {
    char *dir = get_data_dir();
    char *path = g_build_filename(dir, "theme.conf", NULL);
    g_free(dir);
    return path;
}

static char* get_window_config_path(void) {
    char *dir = get_data_dir();
    char *path = g_build_filename(dir, "window.conf", NULL);
    g_free(dir);
    return path;
}

static void save_window_geometry(AppState *app) {
    if (app->last_width <= 0 || app->last_height <= 0) return;

    char *path = get_window_config_path();
    FILE *fp = fopen(path, "w");
    g_free(path);
    if (!fp) return;

    fprintf(fp, "%d\n%d\n%d\n", app->last_width, app->last_height,
            app->last_maximized ? 1 : 0);
    fclose(fp);
}

static void on_window_size_changed(GtkWidget *widget, GParamSpec *pspec,
                                   gpointer user_data) {
    AppState *app = (AppState *)user_data;
    (void)pspec;
    (void)widget;

    gboolean maximized = gtk_window_is_maximized(GTK_WINDOW(app->window));
    app->last_maximized = maximized;

    // Only track size when not maximized, so we restore the windowed size
    if (!maximized) {
        app->last_width = gtk_widget_get_width(app->window);
        app->last_height = gtk_widget_get_height(app->window);
    }
}

static void load_window_geometry(AppState *app) {
    char *path = get_window_config_path();
    FILE *fp = fopen(path, "r");
    g_free(path);
    if (!fp) return;

    int width = 0, height = 0, maximized = 0;
    if (fscanf(fp, "%d\n%d\n%d", &width, &height, &maximized) == 3) {
        if (width > 0 && height > 0) {
            gtk_window_set_default_size(GTK_WINDOW(app->window), width, height);
        }
        if (maximized) {
            gtk_window_maximize(GTK_WINDOW(app->window));
        }
    }
    fclose(fp);
}

static void save_session(AppState *app) {
    JsonBuilder *builder = json_builder_new();

    json_builder_begin_object(builder);

    // active_project_index
    int active_idx = 0;
    if (app->active_project) {
        int idx = g_list_index(app->projects, app->active_project);
        if (idx >= 0) active_idx = idx;
    }
    json_builder_set_member_name(builder, "active_project_index");
    json_builder_add_int_value(builder, active_idx);

    // sort_mode
    json_builder_set_member_name(builder, "sort_mode");
    switch (app->sort_mode) {
        case SORT_ALPHA: json_builder_add_string_value(builder, "alpha"); break;
        case SORT_MRU:   json_builder_add_string_value(builder, "mru");   break;
        default:         json_builder_add_string_value(builder, "none");   break;
    }

    // projects array
    json_builder_set_member_name(builder, "projects");
    json_builder_begin_array(builder);

    for (GList *l = app->projects; l != NULL; l = l->next) {
        Project *project = (Project *)l->data;

        json_builder_begin_object(builder);

        json_builder_set_member_name(builder, "name");
        json_builder_add_string_value(builder, project->name);

        json_builder_set_member_name(builder, "path");
        json_builder_add_string_value(builder, project->path);

        json_builder_set_member_name(builder, "last_used");
        json_builder_add_int_value(builder, project->last_used);

        if (project->initialized) {
            int active_sub_idx = 0;
            if (project->active_subtab) {
                int idx = g_list_index(project->subtabs, project->active_subtab);
                if (idx >= 0) active_sub_idx = idx;
            }
            json_builder_set_member_name(builder, "active_subtab_index");
            json_builder_add_int_value(builder, active_sub_idx);

            json_builder_set_member_name(builder, "subtabs");
            json_builder_begin_array(builder);

            for (GList *sl = project->subtabs; sl != NULL; sl = sl->next) {
                SubTab *subtab = (SubTab *)sl->data;

                json_builder_begin_object(builder);
                json_builder_set_member_name(builder, "name");
                json_builder_add_string_value(builder, subtab->name);

                const char *cwd = NULL;
                if (subtab->terminal) {
                    const char *uri = vte_terminal_get_current_directory_uri(subtab->terminal);
                    if (uri) {
                        char *path = g_filename_from_uri(uri, NULL, NULL);
                        if (path) {
                            json_builder_set_member_name(builder, "working_dir");
                            json_builder_add_string_value(builder, path);
                            g_free(path);
                            cwd = "set";
                        }
                    }
                }
                if (!cwd) {
                    json_builder_set_member_name(builder, "working_dir");
                    json_builder_add_string_value(builder, project->path);
                }

                json_builder_end_object(builder);
            }

            json_builder_end_array(builder);
        } else if (project->saved_subtabs) {
            json_builder_set_member_name(builder, "active_subtab_index");
            json_builder_add_int_value(builder, project->saved_active_subtab);

            json_builder_set_member_name(builder, "subtabs");
            json_builder_begin_array(builder);

            for (GList *sl = project->saved_subtabs; sl != NULL; sl = sl->next) {
                SavedSubTab *saved = (SavedSubTab *)sl->data;
                json_builder_begin_object(builder);
                json_builder_set_member_name(builder, "name");
                json_builder_add_string_value(builder, saved->name);
                json_builder_set_member_name(builder, "working_dir");
                json_builder_add_string_value(builder, saved->working_dir);
                json_builder_end_object(builder);
            }

            json_builder_end_array(builder);
        } else {
            json_builder_set_member_name(builder, "active_subtab_index");
            json_builder_add_int_value(builder, 0);
            json_builder_set_member_name(builder, "subtabs");
            json_builder_begin_array(builder);
            json_builder_end_array(builder);
        }

        json_builder_end_object(builder);
    }

    json_builder_end_array(builder);
    json_builder_end_object(builder);

    JsonGenerator *gen = json_generator_new();
    json_generator_set_pretty(gen, TRUE);
    JsonNode *root = json_builder_get_root(builder);
    json_generator_set_root(gen, root);

    char *session_path = get_session_config_path();
    json_generator_to_file(gen, session_path, NULL);
    g_free(session_path);

    json_node_free(root);
    g_object_unref(gen);
    g_object_unref(builder);
}

static void load_session(AppState *app) {
    char *session_path = get_session_config_path();
    gboolean session_exists = g_file_test(session_path, G_FILE_TEST_EXISTS);

    if (!session_exists) {
        g_free(session_path);

        // Migration: try loading legacy projects.conf
        char *legacy_path = get_legacy_projects_path();
        FILE *fp = fopen(legacy_path, "r");
        g_free(legacy_path);
        if (!fp) return;

        char line[4096];
        while (fgets(line, sizeof(line), fp)) {
            size_t len = strlen(line);
            if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';
            if (line[0] == '\0') continue;

            gint64 last_used = 0;
            const char *path = line;

            char *pipe = strchr(line, '|');
            if (pipe && pipe != line) {
                *pipe = '\0';
                last_used = g_ascii_strtoll(line, NULL, 10);
                path = pipe + 1;
            }

            char *basename = g_path_get_basename(path);
            Project *project = create_project(app, basename, path, FALSE);
            project->last_used = last_used;
            g_free(basename);
        }
        fclose(fp);

        // Also load legacy sort mode
        char *sort_path = get_sort_config_path();
        FILE *sfp = fopen(sort_path, "r");
        g_free(sort_path);
        if (sfp) {
            char sline[64];
            if (fgets(sline, sizeof(sline), sfp)) {
                size_t slen = strlen(sline);
                if (slen > 0 && sline[slen - 1] == '\n') sline[slen - 1] = '\0';
                if (strcmp(sline, "alpha") == 0)
                    app->sort_mode = SORT_ALPHA;
                else if (strcmp(sline, "mru") == 0)
                    app->sort_mode = SORT_MRU;
            }
            fclose(sfp);
        }

        // Save as session.json so migration only happens once
        save_session(app);
        return;
    }

    // Parse session.json
    JsonParser *parser = json_parser_new();
    if (!json_parser_load_from_file(parser, session_path, NULL)) {
        g_free(session_path);
        g_object_unref(parser);
        return;
    }
    g_free(session_path);

    JsonNode *root = json_parser_get_root(parser);
    if (!root || !JSON_NODE_HOLDS_OBJECT(root)) {
        g_object_unref(parser);
        return;
    }

    JsonObject *root_obj = json_node_get_object(root);

    // Read sort_mode
    if (json_object_has_member(root_obj, "sort_mode")) {
        const char *mode_str = json_object_get_string_member(root_obj, "sort_mode");
        if (g_strcmp0(mode_str, "alpha") == 0)
            app->sort_mode = SORT_ALPHA;
        else if (g_strcmp0(mode_str, "mru") == 0)
            app->sort_mode = SORT_MRU;
        else
            app->sort_mode = SORT_NONE;
    }

    // Read active_project_index
    int active_project_index = 0;
    if (json_object_has_member(root_obj, "active_project_index"))
        active_project_index = (int)json_object_get_int_member(root_obj, "active_project_index");

    // Read projects array
    if (!json_object_has_member(root_obj, "projects")) {
        g_object_unref(parser);
        return;
    }

    JsonArray *projects_arr = json_object_get_array_member(root_obj, "projects");
    guint n_projects = json_array_get_length(projects_arr);

    for (guint i = 0; i < n_projects; i++) {
        JsonObject *proj_obj = json_array_get_object_element(projects_arr, i);
        if (!proj_obj) continue;

        const char *name = json_object_get_string_member(proj_obj, "name");
        const char *path = json_object_get_string_member(proj_obj, "path");
        if (!name || !path) continue;

        Project *project = create_project(app, name, path, FALSE);

        if (json_object_has_member(proj_obj, "last_used"))
            project->last_used = json_object_get_int_member(proj_obj, "last_used");

        // Store subtab metadata for lazy restore (don't spawn terminals yet)
        if (json_object_has_member(proj_obj, "subtabs")) {
            JsonArray *subtabs_arr = json_object_get_array_member(proj_obj, "subtabs");
            guint n_subtabs = json_array_get_length(subtabs_arr);

            for (guint j = 0; j < n_subtabs; j++) {
                JsonObject *sub_obj = json_array_get_object_element(subtabs_arr, j);
                if (!sub_obj) continue;

                const char *sub_name = json_object_get_string_member(sub_obj, "name");
                const char *working_dir = json_object_get_string_member(sub_obj, "working_dir");
                if (!sub_name) sub_name = "Tab";
                if (!working_dir) working_dir = path;

                SavedSubTab *saved = g_new0(SavedSubTab, 1);
                saved->name = g_strdup(sub_name);
                saved->working_dir = g_strdup(working_dir);
                project->saved_subtabs = g_list_append(project->saved_subtabs, saved);
            }

            project->saved_active_subtab = 0;
            if (json_object_has_member(proj_obj, "active_subtab_index"))
                project->saved_active_subtab = (int)json_object_get_int_member(proj_obj, "active_subtab_index");
        }
    }

    // Select the active project
    GList *active_link = g_list_nth(app->projects, active_project_index);
    if (active_link) {
        Project *active_proj = (Project *)active_link->data;
        app->active_project = active_proj;
        gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook), active_project_index);
        gtk_list_box_select_row(GTK_LIST_BOX(app->sidebar),
                               GTK_LIST_BOX_ROW(active_proj->list_row));
    }

    g_object_unref(parser);
}

static void save_theme_name(const char *name) {
    char *config_path = get_theme_config_path();
    FILE *fp = fopen(config_path, "w");
    g_free(config_path);
    if (!fp) return;
    fprintf(fp, "%s\n", name);
    fclose(fp);
}

static char* load_theme_name(void) {
    char *config_path = get_theme_config_path();
    FILE *fp = fopen(config_path, "r");
    g_free(config_path);
    if (!fp) return NULL;

    char line[256];
    char *name = NULL;
    if (fgets(line, sizeof(line), fp)) {
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';
        if (line[0] != '\0') {
            name = g_strdup(line);
        }
    }
    fclose(fp);
    return name;
}

static char* get_settings_config_path(void) {
    char *dir = get_data_dir();
    char *path = g_build_filename(dir, "settings.conf", NULL);
    g_free(dir);
    return path;
}

static void save_terminal_settings(TerminalSettings *s) {
    char *path = get_settings_config_path();
    FILE *fp = fopen(path, "w");
    g_free(path);
    if (!fp) return;

    if (s->font_family)
        fprintf(fp, "font_family=%s\n", s->font_family);
    if (s->font_size > 0)
        fprintf(fp, "font_size=%.1f\n", s->font_size);
    if (s->opacity < 1.0)
        fprintf(fp, "opacity=%.2f\n", s->opacity);
    if (s->cursor_shape >= 0)
        fprintf(fp, "cursor_shape=%d\n", s->cursor_shape);
    if (s->cursor_blink >= 0)
        fprintf(fp, "cursor_blink=%d\n", s->cursor_blink);

    fclose(fp);
}

static void load_terminal_settings(TerminalSettings *s) {
    // Set defaults
    s->font_family = NULL;
    s->font_size = -1.0;
    s->opacity = 1.0;
    s->cursor_shape = -1;
    s->cursor_blink = -1;

    char *path = get_settings_config_path();
    FILE *fp = fopen(path, "r");
    g_free(path);
    if (!fp) return;

    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';
        if (line[0] == '\0') continue;

        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = line;
        char *val = eq + 1;

        if (strcmp(key, "font_family") == 0) {
            g_free(s->font_family);
            s->font_family = g_strdup(val);
        } else if (strcmp(key, "font_size") == 0) {
            s->font_size = atof(val);
        } else if (strcmp(key, "opacity") == 0) {
            s->opacity = atof(val);
            if (s->opacity < 0.0) s->opacity = 0.0;
            if (s->opacity > 1.0) s->opacity = 1.0;
        } else if (strcmp(key, "cursor_shape") == 0) {
            s->cursor_shape = atoi(val);
        } else if (strcmp(key, "cursor_blink") == 0) {
            s->cursor_blink = atoi(val);
        }
    }
    fclose(fp);
}

//=============================================================================
// Sort Mode Persistence & Sorting
//=============================================================================

static char* get_sort_config_path(void) {
    char *dir = get_data_dir();
    char *path = g_build_filename(dir, "sort.conf", NULL);
    g_free(dir);
    return path;
}

static int sort_func_insertion(GtkListBoxRow *row1, GtkListBoxRow *row2, gpointer user_data) {
    (void)user_data;
    Project *p1 = (Project *)g_object_get_data(G_OBJECT(row1), "project");
    Project *p2 = (Project *)g_object_get_data(G_OBJECT(row2), "project");
    if (!p1 || !p2) return 0;
    printf("[sort] insertion: %s(%d) vs %s(%d)\n", p1->name, p1->insert_order, p2->name, p2->insert_order);
    return p1->insert_order - p2->insert_order;
}

static int sort_func_alpha(GtkListBoxRow *row1, GtkListBoxRow *row2, gpointer user_data) {
    (void)user_data;
    Project *p1 = (Project *)g_object_get_data(G_OBJECT(row1), "project");
    Project *p2 = (Project *)g_object_get_data(G_OBJECT(row2), "project");
    if (!p1 || !p2) return 0;
    int result = g_ascii_strcasecmp(p1->name, p2->name);
    printf("[sort] alpha: %s vs %s = %d\n", p1->name, p2->name, result);
    return result;
}

static int sort_func_mru(GtkListBoxRow *row1, GtkListBoxRow *row2, gpointer user_data) {
    (void)user_data;
    Project *p1 = (Project *)g_object_get_data(G_OBJECT(row1), "project");
    Project *p2 = (Project *)g_object_get_data(G_OBJECT(row2), "project");
    if (!p1 || !p2) return 0;
    int result;
    // Descending: most recent first
    if (p2->last_used > p1->last_used) result = 1;
    else if (p2->last_used < p1->last_used) result = -1;
    else result = 0;
    printf("[sort] mru: %s(%" G_GINT64_FORMAT ") vs %s(%" G_GINT64_FORMAT ") = %d\n",
           p1->name, p1->last_used, p2->name, p2->last_used, result);
    return result;
}

static void update_sort_button(AppState *app) {
    if (!app->sort_button) return;

    const char *icon;
    const char *tooltip;
    switch (app->sort_mode) {
        case SORT_ALPHA:
            icon = "view-sort-ascending-symbolic";
            tooltip = "Sort: A-Z";
            break;
        case SORT_MRU:
            icon = "document-open-recent-symbolic";
            tooltip = "Sort: Recent";
            break;
        default:
            icon = "view-list-symbolic";
            tooltip = "Sort: Manual";
            break;
    }
    gtk_button_set_icon_name(GTK_BUTTON(app->sort_button), icon);
    gtk_widget_set_tooltip_text(app->sort_button, tooltip);
}

static void apply_sort(AppState *app) {
    const char *mode_name;
    switch (app->sort_mode) {
        case SORT_ALPHA:
            mode_name = "ALPHA";
            gtk_list_box_set_sort_func(GTK_LIST_BOX(app->sidebar),
                                       sort_func_alpha, app, NULL);
            break;
        case SORT_MRU:
            mode_name = "MRU";
            gtk_list_box_set_sort_func(GTK_LIST_BOX(app->sidebar),
                                       sort_func_mru, app, NULL);
            break;
        default:
            mode_name = "NONE";
            gtk_list_box_set_sort_func(GTK_LIST_BOX(app->sidebar),
                                       sort_func_insertion, app, NULL);
            break;
    }
    printf("[sort] apply_sort: mode=%s, n_projects=%d\n", mode_name, g_list_length(app->projects));
    gtk_list_box_invalidate_sort(GTK_LIST_BOX(app->sidebar));
    update_sort_button(app);
}

static void on_sort_clicked(GtkButton *button, gpointer user_data) {
    AppState *app = (AppState *)user_data;
    (void)button;

    // Cycle: NONE -> ALPHA -> MRU -> NONE
    SortMode old = app->sort_mode;
    switch (app->sort_mode) {
        case SORT_NONE:  app->sort_mode = SORT_ALPHA; break;
        case SORT_ALPHA: app->sort_mode = SORT_MRU;   break;
        case SORT_MRU:   app->sort_mode = SORT_NONE;  break;
    }
    printf("[sort] on_sort_clicked: %d -> %d\n", old, app->sort_mode);
    apply_sort(app);
    save_session(app);
}

//=============================================================================
// Built-in Theme System
//=============================================================================

static const ThemePreset* find_builtin_theme(const char *name) {
    for (size_t i = 0; i < BUILTIN_THEME_COUNT; i++) {
        if (g_strcmp0(builtin_themes[i].name, name) == 0)
            return &builtin_themes[i];
    }
    return NULL;
}

static void load_builtin_theme(AppState *app, const char *name) {
    const ThemePreset *preset = find_builtin_theme(name);
    if (!preset) {
        preset = &builtin_themes[0]; // Default to first theme (Dracula)
        name = preset->name;
    }

    TerminalTheme *theme = &app->theme;
    if (theme->font) {
        pango_font_description_free(theme->font);
    }
    memset(theme, 0, sizeof(TerminalTheme));

    // Parse colors from preset
    gdk_rgba_parse(&theme->foreground, preset->foreground);
    gdk_rgba_parse(&theme->background, preset->background);

    for (int i = 0; i < 16; i++) {
        gdk_rgba_parse(&theme->palette[i], preset->palette[i]);
    }

    // Cursor color
    theme->cursor_colors_set = TRUE;
    gdk_rgba_parse(&theme->cursor_bg, preset->cursor);
    theme->cursor_fg = theme->background;

    theme->use_theme_colors = FALSE;
    theme->cursor_shape = VTE_CURSOR_SHAPE_BLOCK;
    theme->cursor_blink = VTE_CURSOR_BLINK_SYSTEM;
    theme->loaded = TRUE;

    // Update stored theme name
    g_free(app->theme_name);
    app->theme_name = g_strdup(name);

    printf("Loaded built-in theme: %s\n", name);
}

static void apply_theme(VteTerminal *terminal, TerminalTheme *theme) {
    if (!theme->loaded) return;

    if (!theme->use_theme_colors) {
        vte_terminal_set_colors(terminal, &theme->foreground, &theme->background,
                                theme->palette, 16);
    }

    if (theme->bold_color_set) {
        vte_terminal_set_color_bold(terminal, &theme->bold_color);
    }

    if (theme->cursor_colors_set) {
        vte_terminal_set_color_cursor(terminal, &theme->cursor_bg);
        vte_terminal_set_color_cursor_foreground(terminal, &theme->cursor_fg);
    }

    if (theme->highlight_colors_set) {
        vte_terminal_set_color_highlight(terminal, &theme->highlight_bg);
        vte_terminal_set_color_highlight_foreground(terminal, &theme->highlight_fg);
    }

    if (theme->font) {
        vte_terminal_set_font(terminal, theme->font);
    }

    vte_terminal_set_cursor_shape(terminal, theme->cursor_shape);
    vte_terminal_set_cursor_blink_mode(terminal, theme->cursor_blink);
    vte_terminal_set_bold_is_bright(terminal, theme->bold_is_bright);
}

static void apply_settings_overrides(AppState *app);

static void apply_theme_to_all_terminals(AppState *app) {
    for (GList *l = app->projects; l != NULL; l = l->next) {
        Project *project = (Project *)l->data;
        for (GList *sl = project->subtabs; sl != NULL; sl = sl->next) {
            SubTab *subtab = (SubTab *)sl->data;
            apply_theme(subtab->terminal, &app->theme);
        }
    }
    apply_settings_overrides(app);
}

static void apply_settings_overrides(AppState *app) {
    TerminalSettings *s = &app->settings;
    TerminalTheme *theme = &app->theme;

    for (GList *l = app->projects; l != NULL; l = l->next) {
        Project *project = (Project *)l->data;
        for (GList *sl = project->subtabs; sl != NULL; sl = sl->next) {
            SubTab *subtab = (SubTab *)sl->data;
            VteTerminal *term = subtab->terminal;

            // Font override
            if (s->font_family || s->font_size > 0) {
                PangoFontDescription *fd = pango_font_description_copy(
                    theme->font ? theme->font : vte_terminal_get_font(term));
                if (s->font_family)
                    pango_font_description_set_family(fd, s->font_family);
                if (s->font_size > 0)
                    pango_font_description_set_size(fd, (int)(s->font_size * PANGO_SCALE));
                vte_terminal_set_font(term, fd);
                pango_font_description_free(fd);
            }

            // Opacity override
            if (s->opacity < 1.0 && theme->loaded) {
                GdkRGBA bg = theme->background;
                bg.alpha = s->opacity;
                vte_terminal_set_color_background(term, &bg);
            }

            // Cursor shape override
            if (s->cursor_shape >= 0) {
                vte_terminal_set_cursor_shape(term, (VteCursorShape)s->cursor_shape);
            }

            // Cursor blink override
            if (s->cursor_blink >= 0) {
                vte_terminal_set_cursor_blink_mode(term, (VteCursorBlinkMode)s->cursor_blink);
            }
        }
    }
}

static void apply_ui_theme(AppState *app) {
    TerminalTheme *theme = &app->theme;

    if (!app->css_provider) {
        app->css_provider = gtk_css_provider_new();
        gtk_style_context_add_provider_for_display(
            gdk_display_get_default(),
            GTK_STYLE_PROVIDER(app->css_provider),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 200);
    }

    // Structural CSS — always applied regardless of theme
    GString *css = g_string_new("");

    g_string_append(css,
        ".gmux-headerbar { min-height: 28px; padding-top: 2px; padding-bottom: 2px; box-shadow: none; }\n"
        ".gmux-headerbar button:not(.titlebutton),"
        ".gmux-headerbar button.flat:not(.titlebutton) {"
        "  min-height: 22px; min-width: 22px; padding: 2px; border-radius: 0;"
        "}\n"
        ".gmux-toolbar { padding: 4px 2px; }\n"
        ".gmux-toolbar button {"
        "  border-radius: 0; padding: 0 10px;"
        "  min-height: 40px; min-width: 40px;"
        "}\n"
        ".gmux-tab-header { padding: 0; }\n"
        ".gmux-tab-item { margin: 0; padding: 0; spacing: 0; border-radius: 0; }\n"
        ".gmux-tab-item-active { background-color: alpha(@theme_selected_bg_color, 0.55); }\n"
        ".gmux-tab-item-active > button { color: @theme_selected_fg_color; }\n"
        ".gmux-tab-header > box > .gmux-tab-item > button,"
        ".gmux-tab-header > box > button,"
        ".gmux-tab-header > button {"
        "  background: none; border: none; box-shadow: none;"
        "  border-radius: 0; padding: 3px 8px; margin: 0; min-height: 28px;"
        "  font-size: 0.85em;"
        "}\n"
        ".gmux-tab-close {"
        "  background: none; border: none; box-shadow: none;"
        "  min-height: 28px; min-width: 22px;"
        "  max-height: 28px; max-width: 22px;"
        "  padding: 3px 6px; margin: 0;"
        "  opacity: 0.4; border-radius: 3px;"
        "  -gtk-icon-size: 12px;"
        "}\n"
        ".gmux-tab-item-active > .gmux-tab-close { opacity: 0.75; }\n"
        ".gmux-tab-close:hover { opacity: 1.0; background-color: alpha(@theme_fg_color, 0.16); }\n"
        ".gmux-tab-dragging { opacity: 0.5; }\n"
        "window.background.csd,"
        "window.background.csd decoration { border-radius: 0; }\n"
    );

    if (!theme->loaded || theme->use_theme_colors) {
        gtk_css_provider_load_from_string(app->css_provider, css->str);
        g_string_free(css, TRUE);
        return;
    }

    GdkRGBA bg = theme->background;
    GdkRGBA fg = theme->foreground;

    double luminance = 0.299 * bg.red + 0.587 * bg.green + 0.114 * bg.blue;
    gboolean is_dark = luminance < 0.5;

    // Scale channels proportionally to preserve hue and saturation.
    // Multiplying all channels by the same factor keeps their ratio identical.
    double s1 = is_dark ? 1.3  : 0.85;  // sidebar — slightly lighter/darker than bg
    double s3 = is_dark ? 1.6  : 0.70;  // button hover

    GdkRGBA sidebar_bg = { fmin(bg.red*s1,1), fmin(bg.green*s1,1), fmin(bg.blue*s1,1), 1.0 };
    GdkRGBA toolbar_bg = bg;  // title bar / toolbar = exact terminal background color
    GdkRGBA btn_hover  = { fmin(bg.red*s3,1), fmin(bg.green*s3,1), fmin(bg.blue*s3,1), 1.0 };
    GdkRGBA fg_dim     = { fg.red*0.7 + bg.red*0.3, fg.green*0.7 + bg.green*0.3, fg.blue*0.7 + bg.blue*0.3, 1.0 };
    GdkRGBA accent     = theme->palette[4]; accent.alpha = 0.4;

    char *s_bg       = gdk_rgba_to_string(&bg);
    char *s_fg       = gdk_rgba_to_string(&fg);
    char *s_sidebar  = gdk_rgba_to_string(&sidebar_bg);
    char *s_toolbar  = gdk_rgba_to_string(&toolbar_bg);
    char *s_hover    = gdk_rgba_to_string(&btn_hover);
    char *s_fg_dim   = gdk_rgba_to_string(&fg_dim);
    char *s_accent   = gdk_rgba_to_string(&accent);

    // Window
    g_string_append_printf(css,
        "window.background { background-color: %s; color: %s; }\n", s_bg, s_fg);
    g_string_append_printf(css,
        "window.background.csd,"
        "window.background.csd decoration { border-radius: 0; }\n");

    // Headerbar — compact
    g_string_append_printf(css,
        ".gmux-headerbar {"
        "  background-image: none;"
        "  background-color: %s;"
        "  color: %s;"
        "  min-height: 28px;"
        "  padding-top: 2px;"
        "  padding-bottom: 2px;"
        "  border-bottom: 1px solid alpha(%s, 0.25);"
        "  box-shadow: none;"
        "}\n", s_toolbar, s_fg, s_fg);
    g_string_append_printf(css,
        ".gmux-headerbar label,"
        ".gmux-headerbar windowtitle label { color: %s; font-size: 0.9em; }\n", s_fg_dim);
    g_string_append_printf(css,
        ".gmux-headerbar button:not(.titlebutton),"
        ".gmux-headerbar button.flat:not(.titlebutton) {"
        "  background-image: none; background-color: transparent;"
        "  color: %s; box-shadow: none;"
        "  min-height: 22px; min-width: 22px;"
        "  padding: 2px;"
        "  border-radius: 0;"
        "}\n", s_fg_dim);
    g_string_append_printf(css,
        ".gmux-headerbar button:not(.titlebutton):hover {"
        "  background-color: %s;"
        "}\n", s_hover);

    // Sidebar
    g_string_append_printf(css,
        ".gmux-sidebar { background-color: %s; }\n", s_sidebar);
    g_string_append_printf(css,
        ".gmux-sidebar scrolledwindow,"
        ".gmux-sidebar scrolledwindow > viewport { background: none; background-color: transparent; }\n");
    g_string_append_printf(css,
        ".gmux-sidebar list,"
        ".gmux-sidebar listbox { background: none; background-color: transparent; color: %s; }\n", s_fg);
    g_string_append_printf(css,
        ".gmux-sidebar list > row,"
        ".gmux-sidebar listbox > row {"
        "  background: none; background-color: transparent; color: %s;"
        "  margin: 0; border-radius: 0;"
        "  transition: background-color 150ms ease;"
        "}\n", s_fg);
    g_string_append_printf(css,
        ".gmux-sidebar list > row:hover,"
        ".gmux-sidebar listbox > row:hover { background-color: alpha(%s, 0.08); }\n", s_fg);
    g_string_append_printf(css,
        ".gmux-sidebar list > row:selected,"
        ".gmux-sidebar listbox > row:selected { background-color: %s; }\n", s_accent);
    g_string_append_printf(css,
        ".gmux-sidebar label { color: %s; }\n", s_fg);
    g_string_append_printf(css,
        ".gmux-sidebar button {"
        "  background: none; color: %s; border: none; box-shadow: none;"
        "  border-radius: 0; padding: 2px; min-height: 20px; min-width: 20px;"
        "  opacity: 0.6;"
        "}\n", s_fg);
    g_string_append_printf(css,
        ".gmux-sidebar button:hover { background-color: %s; opacity: 1.0; }\n", s_hover);
    g_string_append_printf(css,
        ".gmux-tab-count {"
        "  background-color: alpha(%s, 0.25);"
        "  color: %s;"
        "  border-radius: 8px;"
        "  padding: 3px 5px;"
        "  min-height: 0;"
        "  min-width: 0;"
        "  font-size: 11px;"
        "  font-weight: 600;"
        "  margin: 0;"
        "  line-height: 1;"
        "}\n", s_fg, s_fg);

    // Toolbar — with subtle bottom separator
    g_string_append_printf(css,
        ".gmux-toolbar {"
        "  background-color: %s;"
        "  border-bottom: 2px solid alpha(%s, 0.35);"
        "  padding: 4px 2px;"
        "}\n", s_toolbar, s_fg);
    g_string_append_printf(css,
        ".gmux-toolbar button {"
        "  background: none; color: %s; border: none; box-shadow: none;"
        "  border-radius: 0; padding: 0 6px;"
        "  min-height: 28px; min-width: 28px;"
        "}\n", s_fg_dim);
    g_string_append_printf(css,
        ".gmux-toolbar button:hover { background-color: %s; }\n", s_hover);

    // Tab header
    g_string_append_printf(css,
        ".gmux-tab-header {"
        "  background-color: %s;"
        "  border-bottom: 1px solid alpha(%s, 0.25);"
        "  padding: 0;"
        "}\n", s_sidebar, s_fg);
    g_string_append_printf(css,
        ".gmux-tab-item {"
        "  margin: 0; padding: 0; spacing: 0; border-radius: 0;"
        "  transition: background-color 150ms ease;"
        "}\n");
    g_string_append_printf(css,
        ".gmux-tab-item-active { background-color: %s; }\n", s_accent);
    g_string_append_printf(css,
        ".gmux-tab-item-active > button { color: %s; }\n", s_fg);
    g_string_append_printf(css,
        ".gmux-tab-header > box > .gmux-tab-item > button,"
        ".gmux-tab-header > box > button,"
        ".gmux-tab-header > button {"
        "  background: none; color: %s; border: none; box-shadow: none;"
        "  border-radius: 0; padding: 3px 8px; margin: 0; min-height: 28px;"
        "  font-size: 0.85em;"
        "}\n", s_fg_dim);
    g_string_append_printf(css,
        ".gmux-tab-header > box > .gmux-tab-item > button:hover,"
        ".gmux-tab-header > box > button:hover { background-color: transparent; }\n");
    g_string_append_printf(css,
        ".gmux-tab-close {"
        "  background: none; border: none; box-shadow: none;"
        "  min-height: 28px; min-width: 22px;"
        "  max-height: 28px; max-width: 22px;"
        "  padding: 3px 6px; margin: 0;"
        "  opacity: 0.4;"
        "  border-radius: 3px;"
        "  color: %s;"
        "  -gtk-icon-size: 12px;"
        "}\n", s_fg);
    g_string_append_printf(css,
        ".gmux-tab-item-active > .gmux-tab-close {"
        "  background-color: transparent; color: %s; opacity: 0.75;"
        "}\n", s_fg);
    g_string_append_printf(css,
        ".gmux-tab-close:hover { opacity: 1.0; background-color: alpha(%s, 0.2); }\n", s_fg);
    g_string_append_printf(css,
        ".gmux-tab-dragging { opacity: 0.5; }\n");

    // Misc
    g_string_append_printf(css,
        "notebook > header { background-color: %s; }\n", s_bg);
    g_string_append_printf(css,
        "paned > separator { min-width: 1px; background-color: alpha(%s, 0.25); }\n", s_fg);
    g_string_append_printf(css,
        "scrollbar { background: transparent; }\n"
        "scrollbar slider {"
        "  background-color: alpha(%s, 0.2); min-width: 4px; min-height: 4px;"
        "  border-radius: 0; border: 2px solid transparent;"
        "}\n", s_fg);
    g_string_append_printf(css,
        "scrollbar slider:hover { background-color: alpha(%s, 0.4); }\n", s_fg);

    // Settings dialog theming
    g_string_append_printf(css,
        ".gmux-settings { background-color: %s; color: %s; }\n", s_sidebar, s_fg);
    g_string_append_printf(css,
        ".gmux-settings listbox { background-color: transparent; }\n");
    g_string_append_printf(css,
        ".gmux-settings listbox row { background-color: transparent; padding: 4px 8px; }\n");
    g_string_append_printf(css,
        ".gmux-settings listbox row:hover { background-color: alpha(%s, 0.15); }\n", s_fg);
    g_string_append_printf(css,
        ".gmux-settings listbox row:selected { background-color: %s; }\n", s_accent);
    g_string_append_printf(css,
        ".gmux-settings-heading { font-weight: bold; font-size: 1.1em; }\n");
    g_string_append_printf(css,
        ".gmux-settings scale trough { background-color: alpha(%s, 0.2); }\n", s_fg);
    g_string_append_printf(css,
        ".gmux-settings scale highlight { background-color: %s; }\n", s_accent);
    g_string_append_printf(css,
        ".gmux-settings spinbutton { background-color: alpha(%s, 0.1); color: %s; }\n", s_fg, s_fg);
    g_string_append_printf(css,
        ".gmux-settings dropdown { background-color: alpha(%s, 0.1); color: %s; }\n", s_fg, s_fg);
    g_string_append_printf(css,
        ".gmux-settings dropdown button { background-color: transparent; color: %s; }\n", s_fg);
    g_string_append_printf(css,
        ".gmux-settings dropdown button:hover { background-color: %s; }\n", s_hover);
    // Dropdown popover (top-level, needs global selector)
    g_string_append_printf(css,
        "popover.background > contents {"
        "  background-color: %s; color: %s;"
        "  border: 1px solid alpha(%s, 0.25);"
        "}\n", s_sidebar, s_fg, s_fg);
    g_string_append_printf(css,
        "popover listview,"
        "popover listview > row { background-color: transparent; color: %s; }\n", s_fg);
    g_string_append_printf(css,
        "popover listview > row:hover { background-color: alpha(%s, 0.15); }\n", s_fg);
    g_string_append_printf(css,
        "popover listview > row:selected,"
        "popover listview > row:checked { background-color: %s; }\n", s_accent);
    g_string_append_printf(css,
        "popover label { color: %s; }\n", s_fg);
    g_string_append_printf(css,
        ".gmux-settings button { background-color: alpha(%s, 0.1); color: %s; }\n", s_fg, s_fg);
    g_string_append_printf(css,
        ".gmux-settings button:hover { background-color: %s; }\n", s_hover);
    g_string_append_printf(css,
        ".gmux-settings .gmux-about-dim { color: %s; }\n", s_fg_dim);
    g_string_append_printf(css,
        ".gmux-settings separator { background-color: alpha(%s, 0.2); min-height: 1px; }\n", s_fg);

    gtk_css_provider_load_from_string(app->css_provider, css->str);

    g_string_free(css, TRUE);
    g_free(s_bg);
    g_free(s_fg);
    g_free(s_sidebar);
    g_free(s_toolbar);
    g_free(s_hover);
    g_free(s_fg_dim);
    g_free(s_accent);
}

//=============================================================================
// Settings Panel
//=============================================================================

static void on_theme_dropdown_changed(GtkDropDown *dropdown, GParamSpec *pspec,
                                      gpointer user_data) {
    (void)pspec;
    AppState *app = (AppState *)user_data;

    guint sel = gtk_drop_down_get_selected(dropdown);
    if (sel == GTK_INVALID_LIST_POSITION || sel >= BUILTIN_THEME_COUNT) return;

    const char *name = builtin_themes[sel].name;
    if (g_strcmp0(app->theme_name, name) == 0) return;

    save_theme_name(name);
    load_builtin_theme(app, name);
    apply_theme_to_all_terminals(app);
    apply_ui_theme(app);
}

static GtkWidget* build_theme_section(AppState *app) {
    GtkWidget *section = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(section, 16);
    gtk_widget_set_margin_end(section, 16);
    gtk_widget_set_margin_top(section, 16);

    GtkWidget *heading = gtk_label_new("Theme");
    gtk_label_set_xalign(GTK_LABEL(heading), 0.0);
    gtk_widget_add_css_class(heading, "gmux-settings-heading");
    gtk_box_append(GTK_BOX(section), heading);

    // Build theme name list and find active index
    GtkStringList *names = gtk_string_list_new(NULL);
    guint active_idx = 0;

    for (size_t i = 0; i < BUILTIN_THEME_COUNT; i++) {
        gtk_string_list_append(names, builtin_themes[i].name);
        if (g_strcmp0(app->theme_name, builtin_themes[i].name) == 0)
            active_idx = (guint)i;
    }

    GtkWidget *dropdown = gtk_drop_down_new(G_LIST_MODEL(names), NULL);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(dropdown), active_idx);

    g_signal_connect(dropdown, "notify::selected",
                     G_CALLBACK(on_theme_dropdown_changed), app);

    gtk_box_append(GTK_BOX(section), dropdown);
    return section;
}

// Terminal settings callbacks
static void on_font_size_changed(GtkSpinButton *spin, gpointer user_data) {
    AppState *app = (AppState *)user_data;
    app->settings.font_size = gtk_spin_button_get_value(spin);
    save_terminal_settings(&app->settings);
    apply_settings_overrides(app);
}

static void on_opacity_changed(GtkRange *range, gpointer user_data) {
    AppState *app = (AppState *)user_data;
    app->settings.opacity = gtk_range_get_value(range);
    save_terminal_settings(&app->settings);
    apply_settings_overrides(app);
}

static void on_cursor_shape_changed(GtkDropDown *dropdown, GParamSpec *pspec, gpointer user_data) {
    (void)pspec;
    AppState *app = (AppState *)user_data;
    guint sel = gtk_drop_down_get_selected(dropdown);
    app->settings.cursor_shape = (sel == 0) ? -1 : (int)(sel - 1);
    save_terminal_settings(&app->settings);
    apply_settings_overrides(app);
}

static void on_cursor_blink_changed(GtkDropDown *dropdown, GParamSpec *pspec, gpointer user_data) {
    (void)pspec;
    AppState *app = (AppState *)user_data;
    guint sel = gtk_drop_down_get_selected(dropdown);
    app->settings.cursor_blink = (sel == 0) ? -1 : (int)(sel - 1);
    save_terminal_settings(&app->settings);
    apply_settings_overrides(app);
}

#if GTK_CHECK_VERSION(4,10,0)
static void on_font_dialog_response(GObject *source, GAsyncResult *result, gpointer user_data) {
    AppState *app = (AppState *)user_data;
    GtkFontDialog *dialog = GTK_FONT_DIALOG(source);

    PangoFontDescription *fd = gtk_font_dialog_choose_font_finish(dialog, result, NULL);
    if (!fd) return;

    g_free(app->settings.font_family);
    app->settings.font_family = g_strdup(pango_font_description_get_family(fd));

    int size = pango_font_description_get_size(fd);
    if (size > 0 && !pango_font_description_get_size_is_absolute(fd))
        app->settings.font_size = (double)size / PANGO_SCALE;

    pango_font_description_free(fd);

    save_terminal_settings(&app->settings);
    apply_settings_overrides(app);
}

static void on_font_button_clicked(GtkButton *button, gpointer user_data) {
    AppState *app = (AppState *)user_data;
    GtkFontDialog *dialog = gtk_font_dialog_new();
    gtk_font_dialog_set_title(dialog, "Select Font");

    // Set initial font from current settings
    PangoFontDescription *initial = pango_font_description_new();
    if (app->settings.font_family)
        pango_font_description_set_family(initial, app->settings.font_family);
    else if (app->theme.font)
        pango_font_description_set_family(initial,
            pango_font_description_get_family(app->theme.font));
    else
        pango_font_description_set_family(initial, "Monospace");

    if (app->settings.font_size > 0)
        pango_font_description_set_size(initial, (int)(app->settings.font_size * PANGO_SCALE));
    else if (app->theme.font)
        pango_font_description_set_size(initial, pango_font_description_get_size(app->theme.font));
    else
        pango_font_description_set_size(initial, 12 * PANGO_SCALE);

    GtkWidget *toplevel = gtk_widget_get_ancestor(GTK_WIDGET(button), GTK_TYPE_WINDOW);
    gtk_font_dialog_choose_font(dialog, GTK_WINDOW(toplevel), initial, NULL,
                                on_font_dialog_response, app);
    pango_font_description_free(initial);
    g_object_unref(dialog);
}
#else
static void on_font_entry_activate(GtkEntry *entry, gpointer user_data) {
    AppState *app = (AppState *)user_data;
    const char *text = gtk_editable_get_text(GTK_EDITABLE(entry));
    g_free(app->settings.font_family);
    app->settings.font_family = (text && text[0]) ? g_strdup(text) : NULL;
    save_terminal_settings(&app->settings);
    apply_settings_overrides(app);
}
#endif

static GtkWidget* build_terminal_section(AppState *app) {
    GtkWidget *section = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(section, 16);
    gtk_widget_set_margin_end(section, 16);
    gtk_widget_set_margin_top(section, 16);

    GtkWidget *heading = gtk_label_new("Terminal");
    gtk_label_set_xalign(GTK_LABEL(heading), 0.0);
    gtk_widget_add_css_class(heading, "gmux-settings-heading");
    gtk_box_append(GTK_BOX(section), heading);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    int row = 0;

    // Font Family
    GtkWidget *font_label = gtk_label_new("Font Family");
    gtk_label_set_xalign(GTK_LABEL(font_label), 0.0);
    gtk_widget_set_hexpand(font_label, TRUE);
    gtk_grid_attach(GTK_GRID(grid), font_label, 0, row, 1, 1);

#if GTK_CHECK_VERSION(4,10,0)
    // Build display text for font button
    const char *font_display = app->settings.font_family ? app->settings.font_family :
        (app->theme.font ? pango_font_description_get_family(app->theme.font) : "Monospace");
    GtkWidget *font_btn = gtk_button_new_with_label(font_display);
    g_signal_connect(font_btn, "clicked", G_CALLBACK(on_font_button_clicked), app);
    gtk_grid_attach(GTK_GRID(grid), font_btn, 1, row, 1, 1);
#else
    GtkWidget *font_entry = gtk_entry_new();
    if (app->settings.font_family)
        gtk_editable_set_text(GTK_EDITABLE(font_entry), app->settings.font_family);
    gtk_entry_set_placeholder_text(GTK_ENTRY(font_entry), "Monospace");
    g_signal_connect(font_entry, "activate", G_CALLBACK(on_font_entry_activate), app);
    gtk_grid_attach(GTK_GRID(grid), font_entry, 1, row, 1, 1);
#endif
    row++;

    // Font Size
    GtkWidget *size_label = gtk_label_new("Font Size");
    gtk_label_set_xalign(GTK_LABEL(size_label), 0.0);
    gtk_grid_attach(GTK_GRID(grid), size_label, 0, row, 1, 1);

    GtkWidget *size_spin = gtk_spin_button_new_with_range(6, 36, 1);
    double current_size = app->settings.font_size > 0 ? app->settings.font_size :
        (app->theme.font ? (double)pango_font_description_get_size(app->theme.font) / PANGO_SCALE : 12.0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(size_spin), current_size);
    g_signal_connect(size_spin, "value-changed", G_CALLBACK(on_font_size_changed), app);
    gtk_grid_attach(GTK_GRID(grid), size_spin, 1, row, 1, 1);
    row++;

    // Opacity
    GtkWidget *opacity_label = gtk_label_new("Opacity");
    gtk_label_set_xalign(GTK_LABEL(opacity_label), 0.0);
    gtk_grid_attach(GTK_GRID(grid), opacity_label, 0, row, 1, 1);

    GtkWidget *opacity_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.1, 1.0, 0.05);
    gtk_range_set_value(GTK_RANGE(opacity_scale), app->settings.opacity);
    gtk_widget_set_size_request(opacity_scale, 150, -1);
    g_signal_connect(opacity_scale, "value-changed", G_CALLBACK(on_opacity_changed), app);
    gtk_grid_attach(GTK_GRID(grid), opacity_scale, 1, row, 1, 1);
    row++;

    // Cursor Shape
    GtkWidget *shape_label = gtk_label_new("Cursor Shape");
    gtk_label_set_xalign(GTK_LABEL(shape_label), 0.0);
    gtk_grid_attach(GTK_GRID(grid), shape_label, 0, row, 1, 1);

    const char *shape_items[] = { "Default", "Block", "I-Beam", "Underline", NULL };
    GtkStringList *shape_model = gtk_string_list_new(shape_items);
    GtkWidget *shape_dropdown = gtk_drop_down_new(G_LIST_MODEL(shape_model), NULL);
    guint shape_sel = (app->settings.cursor_shape < 0) ? 0 : (guint)(app->settings.cursor_shape + 1);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(shape_dropdown), shape_sel);
    g_signal_connect(shape_dropdown, "notify::selected", G_CALLBACK(on_cursor_shape_changed), app);
    gtk_grid_attach(GTK_GRID(grid), shape_dropdown, 1, row, 1, 1);
    row++;

    // Cursor Blink
    GtkWidget *blink_label = gtk_label_new("Cursor Blink");
    gtk_label_set_xalign(GTK_LABEL(blink_label), 0.0);
    gtk_grid_attach(GTK_GRID(grid), blink_label, 0, row, 1, 1);

    const char *blink_items[] = { "Default", "System", "On", "Off", NULL };
    GtkStringList *blink_model = gtk_string_list_new(blink_items);
    GtkWidget *blink_dropdown = gtk_drop_down_new(G_LIST_MODEL(blink_model), NULL);
    guint blink_sel = (app->settings.cursor_blink < 0) ? 0 : (guint)(app->settings.cursor_blink + 1);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(blink_dropdown), blink_sel);
    g_signal_connect(blink_dropdown, "notify::selected", G_CALLBACK(on_cursor_blink_changed), app);
    gtk_grid_attach(GTK_GRID(grid), blink_dropdown, 1, row, 1, 1);

    gtk_box_append(GTK_BOX(section), grid);
    return section;
}

static void on_about_link_clicked(GtkButton *button, gpointer user_data) {
    (void)user_data;
    GtkWidget *toplevel = gtk_widget_get_ancestor(GTK_WIDGET(button), GTK_TYPE_WINDOW);
    gtk_show_uri(toplevel ? GTK_WINDOW(toplevel) : NULL,
                 "https://github.com/mehmetkocer", GDK_CURRENT_TIME);
}

static GtkWidget* build_about_section(void) {
    GtkWidget *section = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_start(section, 16);
    gtk_widget_set_margin_end(section, 16);
    gtk_widget_set_margin_top(section, 24);
    gtk_widget_set_margin_bottom(section, 16);

    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_append(GTK_BOX(section), sep);

    GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_top(spacer, 12);
    gtk_box_append(GTK_BOX(section), spacer);

    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), "<b><big>gmux</big></b>");
    gtk_label_set_xalign(GTK_LABEL(title), 0.5);
    gtk_box_append(GTK_BOX(section), title);

    GtkWidget *version = gtk_label_new("Version 0.1.0");
    gtk_label_set_xalign(GTK_LABEL(version), 0.5);
    gtk_widget_add_css_class(version, "gmux-about-dim");
    gtk_box_append(GTK_BOX(section), version);

    GtkWidget *built_with = gtk_label_new("Built with GTK4 and VTE");
    gtk_label_set_xalign(GTK_LABEL(built_with), 0.5);
    gtk_widget_add_css_class(built_with, "gmux-about-dim");
    gtk_box_append(GTK_BOX(section), built_with);

    GtkWidget *created = gtk_label_new("Created with Claude, AI by Anthropic");
    gtk_label_set_xalign(GTK_LABEL(created), 0.5);
    gtk_widget_add_css_class(created, "gmux-about-dim");
    gtk_box_append(GTK_BOX(section), created);

    GtkWidget *link_btn = gtk_button_new_with_label("github.com/mehmetkocer");
    gtk_widget_set_halign(link_btn, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(link_btn, 4);
    g_signal_connect(link_btn, "clicked", G_CALLBACK(on_about_link_clicked), NULL);
    gtk_box_append(GTK_BOX(section), link_btn);

    return section;
}

static void on_settings_clicked(GtkButton *button, gpointer user_data) {
    AppState *app = (AppState *)user_data;
    (void)button;

    GtkWidget *dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog), "Settings");
    gtk_window_set_default_size(GTK_WINDOW(dialog), 400, 550);
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(app->window));
    gtk_widget_add_css_class(dialog, "gmux-settings");

    GtkWidget *scrolled = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_window_set_child(GTK_WINDOW(dialog), scrolled);

    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), content);

    gtk_box_append(GTK_BOX(content), build_theme_section(app));
    gtk_box_append(GTK_BOX(content), build_terminal_section(app));
    gtk_box_append(GTK_BOX(content), build_about_section());

    gtk_window_present(GTK_WINDOW(dialog));
}

//=============================================================================
// URL Handling
//=============================================================================

#define URL_REGEX "(https?|ftp)://[\\w\\-]+(\\.[\\w\\-]+)*(:[0-9]+)?(/[\\w\\-.~:/?#\\[\\]@!$&'()*+,;=%]*)?"

static void setup_url_matching(VteTerminal *terminal) {
    GError *error = NULL;
    // PCRE2 flags: CASELESS=0x8, MULTILINE=0x400
    VteRegex *regex = vte_regex_new_for_match(URL_REGEX, -1,
                                               0x00000008u | 0x00000400u,
                                               &error);
    if (regex) {
        int tag = vte_terminal_match_add_regex(terminal, regex, 0);
        vte_terminal_match_set_cursor_name(terminal, tag, "pointer");
        vte_regex_unref(regex);
    } else {
        if (error) {
            g_warning("Failed to compile URL regex: %s", error->message);
            g_error_free(error);
        }
    }
}

static void on_terminal_clicked(GtkGestureClick *gesture, int n_press,
                                double x, double y, gpointer user_data) {
    VteTerminal *terminal = VTE_TERMINAL(user_data);
    (void)n_press;

    GdkModifierType mods = gtk_event_controller_get_current_event_state(
        GTK_EVENT_CONTROLLER(gesture));

    if (!(mods & GDK_CONTROL_MASK)) return;

    // Check for OSC 8 hyperlinks first, then regex matches
    char *url = vte_terminal_check_hyperlink_at(terminal, x, y);
    if (!url) {
        int tag = -1;
        url = vte_terminal_check_match_at(terminal, x, y, &tag);
    }

    if (url) {
        GtkWidget *toplevel = gtk_widget_get_ancestor(GTK_WIDGET(terminal), GTK_TYPE_WINDOW);
        GtkUriLauncher *launcher = gtk_uri_launcher_new(url);
        gtk_uri_launcher_launch(launcher, toplevel ? GTK_WINDOW(toplevel) : NULL, NULL, NULL, NULL);
        g_object_unref(launcher);
        g_free(url);

        gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    }
}

//=============================================================================
// Terminal Callbacks
//=============================================================================

static void on_terminal_title_changed(VteTerminal *terminal, gpointer user_data) {
    SubTab *subtab = (SubTab *)user_data;
    const char *title = vte_terminal_get_window_title(terminal);

    if (title && *title) {
        g_free(subtab->name);
        subtab->name = g_strdup(title);

        if (subtab->tab_label && GTK_IS_LABEL(subtab->tab_label)) {
            gtk_label_set_text(GTK_LABEL(subtab->tab_label), subtab->name);
        }
    }
}

static void on_terminal_child_exited(VteTerminal *terminal, int status, gpointer user_data) {
    SubTab *subtab = (SubTab *)user_data;
    (void)status;
    (void)terminal;

    if (subtab->closing) {
        return;
    }

    printf("Terminal '%s' child process exited\n", subtab->name);

    // Auto-close the tab when the shell exits
    close_subtab(subtab);
}

//=============================================================================
// SubTab Management
//=============================================================================

static void on_subtab_button_clicked(GtkButton *button, gpointer user_data) {
    SubTab *subtab = (SubTab *)user_data;
    Project *project = subtab->parent_tab;
    (void)button;

    // Switch to this subtab in the stack
    gtk_stack_set_visible_child(GTK_STACK(project->terminal_stack), subtab->scrolled);
    project->active_subtab = subtab;

    // Update tab styles - active state is on the whole tab row
    for (GList *l = project->subtabs; l != NULL; l = l->next) {
        SubTab *st = (SubTab *)l->data;
        GtkWidget *row = st->tab_widget;
        GtkWidget *btn = st->tab_button;
        if (!row || !btn) continue;
        if (st == subtab) {
            gtk_widget_add_css_class(row, "gmux-tab-item-active");
        } else {
            gtk_widget_remove_css_class(row, "gmux-tab-item-active");
        }
    }

    // Focus the terminal
    gtk_widget_grab_focus(GTK_WIDGET(subtab->terminal));
}

static SubTab* create_subtab(Project *project, const char *name, const char *working_dir);

static void update_tab_count_badge(Project *project) {
    if (!project->tab_count_label) return;
    guint count = g_list_length(project->subtabs);
    if (count > 0) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%u", count);
        gtk_label_set_text(GTK_LABEL(project->tab_count_label), buf);
        gtk_widget_set_visible(project->tab_count_label, TRUE);
    } else {
        gtk_widget_set_visible(project->tab_count_label, FALSE);
    }
}

static void close_subtab(SubTab *subtab) {
    if (!subtab || subtab->closing) {
        return;
    }
    subtab->closing = TRUE;

    Project *project = subtab->parent_tab;
    gboolean was_last = (g_list_length(project->subtabs) == 1);

    if (subtab->terminal) {
        g_signal_handlers_disconnect_by_data(subtab->terminal, subtab);
    }

    // If this was the active subtab, switch to an adjacent one first
    if (project->active_subtab == subtab && !was_last) {
        GList *link = g_list_find(project->subtabs, subtab);
        GList *next = link->next ? link->next : link->prev;
        if (next) {
            SubTab *new_active = (SubTab *)next->data;
            on_subtab_button_clicked(GTK_BUTTON(new_active->tab_button), new_active);
        }
    }

    // Remove tab button from header
    gtk_box_remove(GTK_BOX(project->tabs_box), subtab->tab_widget);

    // Remove terminal from stack
    gtk_stack_remove(GTK_STACK(project->terminal_stack), subtab->scrolled);

    // Remove from subtab list
    project->subtabs = g_list_remove(project->subtabs, subtab);

    printf("Closed subtab: %s\n", subtab->name);

    // If this was the active tab and no adjacent tab was selected, clear active state
    if (project->active_subtab == subtab) {
        project->active_subtab = NULL;
    }

    // Free subtab resources
    g_free(subtab->name);
    g_free(subtab);

    // Mark as uninitialized when all tabs are closed; tab creation remains lazy.
    if (was_last) {
        project->initialized = FALSE;
    }

    update_tab_count_badge(project);
    save_session(project->app);
}

static void on_close_subtab_clicked(GtkButton *button, gpointer user_data) {
    SubTab *subtab = (SubTab *)user_data;
    (void)button;
    close_subtab(subtab);
}

static void free_saved_subtabs(Project *project) {
    for (GList *sl = project->saved_subtabs; sl != NULL; sl = sl->next) {
        SavedSubTab *saved = (SavedSubTab *)sl->data;
        g_free(saved->name);
        g_free(saved->working_dir);
        g_free(saved);
    }
    g_list_free(project->saved_subtabs);
    project->saved_subtabs = NULL;
}

static void on_add_subtab_clicked(GtkButton *button, gpointer user_data) {
    Project *project = (Project *)user_data;
    (void)button;

    if (!project->initialized) {
        free_saved_subtabs(project);
        create_subtab(project, "Tab 1", project->path);
        project->subtab_counter = 1;
        project->initialized = TRUE;
        return;
    }

    project->subtab_counter++;
    char name[64];
    snprintf(name, sizeof(name), "Tab %d", project->subtab_counter);

    create_subtab(project, name, project->path);
}

//=============================================================================
// SubTab Drag-and-Drop Reordering (gesture-based, like GtkNotebook)
//=============================================================================

static GtkWidget *find_tab_button_ancestor(GtkWidget *widget, GtkWidget *tabs_box) {
    while (widget && widget != tabs_box) {
        if (g_object_get_data(G_OBJECT(widget), "subtab"))
            return widget;
        widget = gtk_widget_get_parent(widget);
    }
    return NULL;
}

static gboolean is_close_button_hit(GtkWidget *widget, GtkWidget *tabs_box) {
    while (widget && widget != tabs_box) {
        if (gtk_widget_has_css_class(widget, "gmux-tab-close"))
            return TRUE;
        widget = gtk_widget_get_parent(widget);
    }
    return FALSE;
}

static void rebuild_subtabs_list(Project *project) {
    g_list_free(project->subtabs);
    project->subtabs = NULL;
    for (GtkWidget *child = gtk_widget_get_first_child(project->tabs_box);
         child != NULL;
         child = gtk_widget_get_next_sibling(child)) {
        SubTab *st = g_object_get_data(G_OBJECT(child), "subtab");
        if (st)
            project->subtabs = g_list_append(project->subtabs, st);
    }
}

static void on_tab_drag_begin(GtkGestureDrag *gesture, double start_x, double start_y, gpointer user_data) {
    Project *project = (Project *)user_data;

    GtkWidget *picked = gtk_widget_pick(project->tabs_box, start_x, start_y, GTK_PICK_DEFAULT);
    if (is_close_button_hit(picked, project->tabs_box)) {
        // Let close button clicks pass through untouched.
        gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_DENIED);
        return;
    }

    GtkWidget *tab_btn = find_tab_button_ancestor(picked, project->tabs_box);
    if (!tab_btn) {
        gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_DENIED);
        return;
    }

    g_object_set_data(G_OBJECT(project->tabs_box), "drag-tab", tab_btn);
    g_object_set_data(G_OBJECT(project->tabs_box), "drag-active", GINT_TO_POINTER(FALSE));
    double *sx = g_new(double, 1);
    *sx = start_x;
    g_object_set_data_full(G_OBJECT(project->tabs_box), "drag-start-x", sx, g_free);
}

static void on_tab_drag_update(GtkGestureDrag *gesture, double offset_x, double offset_y, gpointer user_data) {
    Project *project = (Project *)user_data;
    (void)offset_y;

    GtkWidget *dragged_btn = g_object_get_data(G_OBJECT(project->tabs_box), "drag-tab");
    if (!dragged_btn) return;

    double *start_x_ptr = g_object_get_data(G_OBJECT(project->tabs_box), "drag-start-x");
    if (!start_x_ptr) return;

    gboolean active = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(project->tabs_box), "drag-active"));

    // Require minimum movement to distinguish click from drag
    if (!active) {
        if (fabs(offset_x) < 6.0) return;
        g_object_set_data(G_OBJECT(project->tabs_box), "drag-active", GINT_TO_POINTER(TRUE));
        gtk_widget_add_css_class(dragged_btn, "gmux-tab-dragging");
        gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    }

    double current_x = *start_x_ptr + offset_x;

    // Reorder in real-time based on midpoint crossing
    for (GtkWidget *child = gtk_widget_get_first_child(project->tabs_box);
         child != NULL;
         child = gtk_widget_get_next_sibling(child)) {
        if (child == dragged_btn) continue;

        graphene_rect_t child_bounds, dragged_bounds;
        if (!gtk_widget_compute_bounds(child, project->tabs_box, &child_bounds)) continue;
        if (!gtk_widget_compute_bounds(dragged_btn, project->tabs_box, &dragged_bounds)) continue;

        double child_mid = child_bounds.origin.x + child_bounds.size.width / 2.0;
        double dragged_mid = dragged_bounds.origin.x + dragged_bounds.size.width / 2.0;

        if (dragged_mid > child_mid && current_x < child_mid) {
            // Moving left past this child's midpoint
            GtkWidget *prev = gtk_widget_get_prev_sibling(child);
            if (prev != dragged_btn)
                gtk_box_reorder_child_after(GTK_BOX(project->tabs_box), dragged_btn, prev);
            return;
        }
        if (dragged_mid < child_mid && current_x > child_mid) {
            // Moving right past this child's midpoint
            gtk_box_reorder_child_after(GTK_BOX(project->tabs_box), dragged_btn, child);
            return;
        }
    }
}

static void on_tab_drag_end(GtkGestureDrag *gesture, double offset_x, double offset_y, gpointer user_data) {
    Project *project = (Project *)user_data;
    (void)gesture; (void)offset_x; (void)offset_y;

    GtkWidget *dragged_btn = g_object_get_data(G_OBJECT(project->tabs_box), "drag-tab");
    gboolean active = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(project->tabs_box), "drag-active"));

    if (dragged_btn && active) {
        gtk_widget_remove_css_class(dragged_btn, "gmux-tab-dragging");
        rebuild_subtabs_list(project);
        save_session(project->app);
    }

    g_object_set_data(G_OBJECT(project->tabs_box), "drag-tab", NULL);
    g_object_set_data(G_OBJECT(project->tabs_box), "drag-active", GINT_TO_POINTER(FALSE));
}

static void setup_tabs_box_drag_reorder(Project *project) {
    GtkGesture *drag = gtk_gesture_drag_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(drag), GDK_BUTTON_PRIMARY);
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(drag), GTK_PHASE_CAPTURE);
    g_signal_connect(drag, "drag-begin", G_CALLBACK(on_tab_drag_begin), project);
    g_signal_connect(drag, "drag-update", G_CALLBACK(on_tab_drag_update), project);
    g_signal_connect(drag, "drag-end", G_CALLBACK(on_tab_drag_end), project);
    gtk_widget_add_controller(project->tabs_box, GTK_EVENT_CONTROLLER(drag));
}

static SubTab* create_subtab(Project *project, const char *name, const char *working_dir) {
    SubTab *subtab = g_new0(SubTab, 1);
    subtab->name = g_strdup(name);
    subtab->parent_tab = project;

    // Create VTE terminal
    subtab->terminal = VTE_TERMINAL(vte_terminal_new());

    // Configure terminal
    vte_terminal_set_scrollback_lines(subtab->terminal, 10000);
    vte_terminal_set_mouse_autohide(subtab->terminal, TRUE);
    vte_terminal_set_allow_hyperlink(subtab->terminal, TRUE);
    setup_url_matching(subtab->terminal);

    // Ctrl+click to open URLs
    GtkGesture *click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), GDK_BUTTON_PRIMARY);
    g_signal_connect(click, "pressed", G_CALLBACK(on_terminal_clicked), subtab->terminal);
    gtk_widget_add_controller(GTK_WIDGET(subtab->terminal), GTK_EVENT_CONTROLLER(click));

    // Make terminal scrollable
    subtab->scrolled = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(subtab->scrolled),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(subtab->scrolled), GTK_WIDGET(subtab->terminal));

    gtk_widget_set_hexpand(subtab->scrolled, TRUE);
    gtk_widget_set_vexpand(subtab->scrolled, TRUE);

    // Connect signals
    g_signal_connect(subtab->terminal, "window-title-changed",
                     G_CALLBACK(on_terminal_title_changed), subtab);
    g_signal_connect(subtab->terminal, "child-exited",
                     G_CALLBACK(on_terminal_child_exited), subtab);

    // Add to stack
    gtk_stack_add_child(GTK_STACK(project->terminal_stack), subtab->scrolled);

    // Create tab row with separate select and close buttons
    subtab->tab_widget = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(subtab->tab_widget, "gmux-tab-item");

    subtab->tab_button = gtk_button_new();
    subtab->tab_label = gtk_label_new(name);
    gtk_button_set_child(GTK_BUTTON(subtab->tab_button), subtab->tab_label);
    g_signal_connect(subtab->tab_button, "clicked",
                     G_CALLBACK(on_subtab_button_clicked), subtab);
    gtk_box_append(GTK_BOX(subtab->tab_widget), subtab->tab_button);

    GtkWidget *close_btn = gtk_button_new_from_icon_name("window-close-symbolic");
    gtk_widget_add_css_class(close_btn, "gmux-tab-close");
    gtk_widget_set_tooltip_text(close_btn, "Close tab");
    gtk_widget_set_focus_on_click(close_btn, FALSE);
    g_signal_connect(close_btn, "clicked",
                     G_CALLBACK(on_close_subtab_clicked), subtab);
    gtk_box_append(GTK_BOX(subtab->tab_widget), close_btn);

    gtk_box_append(GTK_BOX(project->tabs_box), subtab->tab_widget);

    // Store subtab pointer on the button for drag-reorder lookups
    g_object_set_data(G_OBJECT(subtab->tab_widget), "subtab", subtab);

    // Spawn shell in terminal
    char *argv[] = { g_strdup(g_getenv("SHELL") ?: "/bin/bash"), NULL };

    vte_terminal_spawn_async(
        subtab->terminal,
        VTE_PTY_DEFAULT,
        working_dir,
        argv,
        NULL,  // inherit parent environment
        G_SPAWN_DEFAULT,
        NULL, NULL,  // child setup
        NULL,  // child setup data
        -1,  // timeout
        NULL,  // cancellable
        NULL,  // callback
        NULL   // user data
    );

    g_free(argv[0]);

    project->subtabs = g_list_append(project->subtabs, subtab);

    // Switch to this subtab first (so it's visible/realized)
    on_subtab_button_clicked(GTK_BUTTON(subtab->tab_button), subtab);

    // Apply theme + settings AFTER terminal is in widget tree, visible, and realized
    if (project->app->theme.loaded) {
        apply_theme(subtab->terminal, &project->app->theme);
    }
    {
        TerminalSettings *s = &project->app->settings;
        if (s->font_family || s->font_size > 0) {
            PangoFontDescription *fd = pango_font_description_new();
            if (s->font_family)
                pango_font_description_set_family(fd, s->font_family);
            if (s->font_size > 0)
                pango_font_description_set_size(fd, (int)(s->font_size * PANGO_SCALE));
            vte_terminal_set_font(subtab->terminal, fd);
            pango_font_description_free(fd);
        }
        if (s->opacity < 1.0 && project->app->theme.loaded) {
            GdkRGBA bg = project->app->theme.background;
            bg.alpha = s->opacity;
            vte_terminal_set_color_background(subtab->terminal, &bg);
        }
        if (s->cursor_shape >= 0)
            vte_terminal_set_cursor_shape(subtab->terminal, (VteCursorShape)s->cursor_shape);
        if (s->cursor_blink >= 0)
            vte_terminal_set_cursor_blink_mode(subtab->terminal, (VteCursorBlinkMode)s->cursor_blink);
    }

    printf("Created subtab: %s\n", name);

    update_tab_count_badge(project);

    return subtab;
}

//=============================================================================
// Project Management
//=============================================================================

static void on_project_selected(GtkListBox *box, GtkListBoxRow *row, gpointer user_data) {
    AppState *app = (AppState *)user_data;
    (void)box;

    printf("[sort] on_project_selected: row=%p\n", (void *)row);
    if (!row) return;

    Project *project = (Project *)g_object_get_data(G_OBJECT(row), "project");
    printf("[sort] on_project_selected: project=%s, sort_mode=%d\n",
           project ? project->name : "NULL", app->sort_mode);
    if (project) {
        int page_num = g_list_index(app->projects, project);
        if (page_num >= 0) {
            gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook), page_num);
            app->active_project = project;

            // Update MRU timestamp (sort only triggered by sort button)
            project->last_used = g_get_real_time();

            // Lazy initialization: create terminal on first click
            if (!project->initialized) {
                if (project->saved_subtabs) {
                    // Restore saved subtabs from session
                    for (GList *sl = project->saved_subtabs; sl != NULL; sl = sl->next) {
                        SavedSubTab *saved = (SavedSubTab *)sl->data;
                        create_subtab(project, saved->name, saved->working_dir);
                    }
                    project->subtab_counter = (int)g_list_length(project->saved_subtabs);

                    // Activate the saved active subtab
                    GList *active_link = g_list_nth(project->subtabs, project->saved_active_subtab);
                    if (active_link) {
                        SubTab *active_sub = (SubTab *)active_link->data;
                        on_subtab_button_clicked(GTK_BUTTON(active_sub->tab_button), active_sub);
                    }

                    // Free saved subtab data
                    free_saved_subtabs(project);
                } else {
                    create_subtab(project, "Tab 1", project->path);
                    project->subtab_counter = 1;
                }
                project->initialized = TRUE;
            }

            if (project->active_subtab) {
                gtk_widget_grab_focus(GTK_WIDGET(project->active_subtab->terminal));
            }
        }
    }
}

static void on_sidebar_add_subtab_clicked(GtkButton *button, gpointer user_data) {
    Project *project = (Project *)user_data;
    (void)button;

    if (!project->initialized) {
        free_saved_subtabs(project);
        create_subtab(project, "Tab 1", project->path);
        project->subtab_counter = 1;
        project->initialized = TRUE;
        return;
    }

    project->subtab_counter++;
    char name[64];
    snprintf(name, sizeof(name), "Tab %d", project->subtab_counter);

    create_subtab(project, name, project->path);
}

static Project* create_project(AppState *app, const char *name, const char *path, gboolean init_terminal) {
    Project *project = g_new0(Project, 1);
    project->name = g_strdup(name);
    project->path = g_strdup(path);
    project->app = app;
    project->subtab_counter = 0;

    // Create main container (vertical box)
    project->tab_container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(project->tab_container, TRUE);
    gtk_widget_set_vexpand(project->tab_container, TRUE);

    // Create tab header (horizontal box for tabs + plus button)
    project->tab_header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(project->tab_header, "gmux-tab-header");

    // Create tabs box (for individual tab buttons)
    project->tabs_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(project->tabs_box, TRUE);

    // Enable gesture-based tab reordering
    setup_tabs_box_drag_reorder(project);

    // Create plus button for adding new tabs
    GtkWidget *add_subtab_button = gtk_button_new_from_icon_name("list-add-symbolic");
    gtk_widget_set_tooltip_text(add_subtab_button, "Add new tab");
    g_signal_connect(add_subtab_button, "clicked",
                     G_CALLBACK(on_add_subtab_clicked), project);

    // Add tabs box and plus button to header
    gtk_box_append(GTK_BOX(project->tab_header), project->tabs_box);
    gtk_box_append(GTK_BOX(project->tab_header), add_subtab_button);

    // Create stack for terminal content
    project->terminal_stack = gtk_stack_new();
    gtk_widget_set_hexpand(project->terminal_stack, TRUE);
    gtk_widget_set_vexpand(project->terminal_stack, TRUE);

    // Add header and stack to container
    gtk_box_append(GTK_BOX(project->tab_container), project->tab_header);
    gtk_box_append(GTK_BOX(project->tab_container), project->terminal_stack);

    // Add to notebook
    gtk_notebook_append_page(GTK_NOTEBOOK(app->notebook), project->tab_container, NULL);
    gtk_notebook_set_show_tabs(GTK_NOTEBOOK(app->notebook), FALSE);

    // Create sidebar row: horizontal box with label + "+" button
    GtkWidget *row_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

    GtkWidget *label = gtk_label_new(name);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    gtk_widget_set_hexpand(label, TRUE);
    gtk_widget_set_margin_start(label, 12);
    gtk_widget_set_margin_top(label, 8);
    gtk_widget_set_margin_bottom(label, 8);

    // Tab count badge (hidden until terminals are created)
    project->tab_count_label = gtk_label_new("");
    gtk_widget_add_css_class(project->tab_count_label, "gmux-tab-count");
    gtk_widget_set_visible(project->tab_count_label, FALSE);
    gtk_widget_set_valign(project->tab_count_label, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_end(project->tab_count_label, 4);

    GtkWidget *row_add_button = gtk_button_new_from_icon_name("list-add-symbolic");
    gtk_widget_set_tooltip_text(row_add_button, "New terminal in this project");
    gtk_widget_set_margin_end(row_add_button, 6);
    g_signal_connect(row_add_button, "clicked",
                     G_CALLBACK(on_sidebar_add_subtab_clicked), project);

    gtk_box_append(GTK_BOX(row_box), label);
    gtk_box_append(GTK_BOX(row_box), project->tab_count_label);
    gtk_box_append(GTK_BOX(row_box), row_add_button);

    project->list_row = gtk_list_box_row_new();
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(project->list_row), row_box);
    g_object_set_data(G_OBJECT(project->list_row), "project", project);
    gtk_list_box_append(GTK_LIST_BOX(app->sidebar), project->list_row);

    project->insert_order = (int)g_list_length(app->projects);
    app->projects = g_list_append(app->projects, project);
    app->active_project = project;

    if (init_terminal) {
        // Create the first subtab
        create_subtab(project, "Tab 1", project->path);
        project->subtab_counter = 1;
        project->initialized = TRUE;
    }

    // Select this project (only when actually initializing a terminal,
    // otherwise load_session handles selection after all projects are loaded)
    if (init_terminal) {
        int page_num = g_list_length(app->projects) - 1;
        gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook), page_num);
        gtk_list_box_select_row(GTK_LIST_BOX(app->sidebar), GTK_LIST_BOX_ROW(project->list_row));
    }

    printf("Created project: %s (%s)\n", name, path);

    return project;
}

static void on_folder_selected(GObject *source, GAsyncResult *result, gpointer user_data) {
    AppState *app = (AppState *)user_data;
    GtkFileDialog *dialog = GTK_FILE_DIALOG(source);

    GFile *folder = gtk_file_dialog_select_folder_finish(dialog, result, NULL);
    if (!folder) return;

    char *path = g_file_get_path(folder);
    char *basename = g_file_get_basename(folder);

    create_project(app, basename, path, TRUE);
    save_session(app);

    g_free(path);
    g_free(basename);
    g_object_unref(folder);
}

static void on_add_project_clicked(GtkButton *button, gpointer user_data) {
    AppState *app = (AppState *)user_data;
    (void)button;

    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Select Project Folder");

    gtk_file_dialog_select_folder(dialog, GTK_WINDOW(app->window), NULL,
                                  on_folder_selected, app);
    g_object_unref(dialog);
}

static void on_remove_project_clicked(GtkButton *button, gpointer user_data) {
    AppState *app = (AppState *)user_data;
    (void)button;

    if (!app->active_project) {
        return;
    }

    Project *project = (Project *)app->active_project;

    // Remove from notebook
    int page_num = gtk_notebook_page_num(GTK_NOTEBOOK(app->notebook), project->tab_container);
    if (page_num >= 0) {
        gtk_notebook_remove_page(GTK_NOTEBOOK(app->notebook), page_num);
    }

    // Remove from sidebar
    gtk_list_box_remove(GTK_LIST_BOX(app->sidebar), project->list_row);

    // Free subtabs
    for (GList *l = project->subtabs; l != NULL; l = l->next) {
        SubTab *subtab = (SubTab *)l->data;
        g_free(subtab->name);
        g_free(subtab);
    }
    g_list_free(project->subtabs);
    free_saved_subtabs(project);

    // Free resources
    app->projects = g_list_remove(app->projects, project);
    g_free(project->name);
    g_free(project->path);
    g_free(project);

    save_session(app);

    // Select another project
    if (app->projects) {
        app->active_project = (Project *)app->projects->data;
        gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook), 0);
        gtk_list_box_select_row(GTK_LIST_BOX(app->sidebar),
                               GTK_LIST_BOX_ROW(((Project *)app->active_project)->list_row));
        if (((Project *)app->active_project)->active_subtab) {
            gtk_widget_grab_focus(GTK_WIDGET(((Project *)app->active_project)->active_subtab->terminal));
        }
    } else {
        app->active_project = NULL;
    }
}

static void on_window_destroy(GtkWidget *widget, gpointer user_data) {
    AppState *app = (AppState *)user_data;
    (void)widget;

    save_window_geometry(app);
    save_session(app);

    // Clean up all projects and subtabs
    for (GList *l = app->projects; l != NULL; l = l->next) {
        Project *project = (Project *)l->data;

        // Free subtabs
        for (GList *sl = project->subtabs; sl != NULL; sl = sl->next) {
            SubTab *subtab = (SubTab *)sl->data;
            g_free(subtab->name);
            g_free(subtab);
        }
        g_list_free(project->subtabs);
        free_saved_subtabs(project);

        g_free(project->name);
        g_free(project->path);
        g_free(project);
    }
    g_list_free(app->projects);

    // Clean up settings resources
    g_free(app->settings.font_family);

    // Clean up theme resources
    if (app->theme.font) {
        pango_font_description_free(app->theme.font);
    }
    if (app->css_provider) {
        gtk_style_context_remove_provider_for_display(
            gdk_display_get_default(),
            GTK_STYLE_PROVIDER(app->css_provider));
        g_object_unref(app->css_provider);
    }
    g_free(app->theme_name);
}

//=============================================================================
// Keyboard Shortcuts
//=============================================================================

static gboolean on_key_pressed(GtkEventControllerKey *controller,
                               guint keyval, guint keycode,
                               GdkModifierType modifiers, gpointer user_data) {
    AppState *app = (AppState *)user_data;
    (void)controller;
    (void)keycode;

    Project *project = (Project *)app->active_project;
    if (!project || !project->active_subtab) return FALSE;

    VteTerminal *terminal = project->active_subtab->terminal;
    gboolean ctrl_shift = (modifiers & (GDK_CONTROL_MASK | GDK_SHIFT_MASK)) ==
                          (GDK_CONTROL_MASK | GDK_SHIFT_MASK);

    if (ctrl_shift && (keyval == GDK_KEY_C || keyval == GDK_KEY_c)) {
        vte_terminal_copy_clipboard_format(terminal, VTE_FORMAT_TEXT);
        return TRUE;
    }

    if (ctrl_shift && (keyval == GDK_KEY_V || keyval == GDK_KEY_v)) {
        vte_terminal_paste_clipboard(terminal);
        return TRUE;
    }

    return FALSE;
}

//=============================================================================
// Application Setup
//=============================================================================

static void activate(GtkApplication *app, gpointer user_data) {
    (void)user_data;

    AppState *state = g_new0(AppState, 1);

    // Create window
    state->window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(state->window), "gmux");
    gtk_window_set_icon_name(GTK_WINDOW(state->window), "utilities-terminal");
    gtk_window_set_default_size(GTK_WINDOW(state->window), 1200, 800);
    load_window_geometry(state);

    // Create explicit headerbar so we can style it (skip on KDE — it forces SSD)
    const char *desktop = g_getenv("XDG_CURRENT_DESKTOP");
    gboolean is_kde = desktop && g_strstr_len(desktop, -1, "KDE") != NULL;
    if (!is_kde) {
        GtkWidget *headerbar = gtk_header_bar_new();
        gtk_widget_add_css_class(headerbar, "gmux-headerbar");
        gtk_window_set_titlebar(GTK_WINDOW(state->window), headerbar);
    }

    // Create horizontal paned
    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_window_set_child(GTK_WINDOW(state->window), paned);

    // Create sidebar with toolbar
    GtkWidget *sidebar_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(sidebar_box, 200, -1);
    gtk_widget_add_css_class(sidebar_box, "gmux-sidebar");
    state->sidebar_box = sidebar_box;

    // Toolbar
    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(toolbar, "gmux-toolbar");

    GtkWidget *add_button = gtk_button_new_from_icon_name("folder-new-symbolic");
    gtk_widget_set_tooltip_text(add_button, "Add Project");
    GtkWidget *remove_button = gtk_button_new_from_icon_name("list-remove-symbolic");
    gtk_widget_set_tooltip_text(remove_button, "Remove Project");
    GtkWidget *settings_button = gtk_button_new_from_icon_name("preferences-system-symbolic");
    gtk_widget_set_tooltip_text(settings_button, "Settings");

    state->sort_button = gtk_button_new_from_icon_name("view-list-symbolic");
    gtk_widget_set_tooltip_text(state->sort_button, "Sort: Manual");

    gtk_box_append(GTK_BOX(toolbar), add_button);
    gtk_box_append(GTK_BOX(toolbar), remove_button);
    gtk_box_append(GTK_BOX(toolbar), settings_button);
    gtk_box_append(GTK_BOX(toolbar), state->sort_button);

    // Sidebar list
    GtkWidget *scrolled = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scrolled, TRUE);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);

    state->sidebar = gtk_list_box_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), state->sidebar);

    gtk_box_append(GTK_BOX(sidebar_box), toolbar);
    gtk_box_append(GTK_BOX(sidebar_box), scrolled);

    // Create notebook for content
    state->notebook = gtk_notebook_new();
    gtk_widget_set_hexpand(state->notebook, TRUE);
    gtk_widget_set_vexpand(state->notebook, TRUE);

    // Add to paned
    gtk_paned_set_start_child(GTK_PANED(paned), sidebar_box);
    gtk_paned_set_resize_start_child(GTK_PANED(paned), FALSE);
    gtk_paned_set_shrink_start_child(GTK_PANED(paned), FALSE);

    gtk_paned_set_end_child(GTK_PANED(paned), state->notebook);
    gtk_paned_set_resize_end_child(GTK_PANED(paned), TRUE);
    gtk_paned_set_shrink_end_child(GTK_PANED(paned), TRUE);

    gtk_paned_set_position(GTK_PANED(paned), 200);
    gtk_paned_set_wide_handle(GTK_PANED(paned), TRUE);

    // Connect signals
    g_signal_connect(add_button, "clicked", G_CALLBACK(on_add_project_clicked), state);
    g_signal_connect(remove_button, "clicked", G_CALLBACK(on_remove_project_clicked), state);
    g_signal_connect(settings_button, "clicked", G_CALLBACK(on_settings_clicked), state);
    g_signal_connect(state->sort_button, "clicked", G_CALLBACK(on_sort_clicked), state);
    g_signal_connect(state->sidebar, "row-selected", G_CALLBACK(on_project_selected), state);
    g_signal_connect(state->window, "destroy", G_CALLBACK(on_window_destroy), state);
    g_signal_connect(state->window, "notify::default-width", G_CALLBACK(on_window_size_changed), state);
    g_signal_connect(state->window, "notify::default-height", G_CALLBACK(on_window_size_changed), state);
    g_signal_connect(state->window, "notify::maximized", G_CALLBACK(on_window_size_changed), state);

    // Add keyboard shortcut controller for copy/paste
    GtkEventController *key_controller = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(key_controller, GTK_PHASE_CAPTURE);
    g_signal_connect(key_controller, "key-pressed", G_CALLBACK(on_key_pressed), state);
    gtk_widget_add_controller(state->window, key_controller);

    // Migrate old config data to XDG data dir
    migrate_config_to_data();

    // Load built-in theme before creating projects
    char *saved_theme = load_theme_name();
    load_builtin_theme(state, saved_theme ? saved_theme : "Dracula");
    g_free(saved_theme);
    load_terminal_settings(&state->settings);
    apply_ui_theme(state);

    // Restore session (projects, subtabs, sort mode)
    load_session(state);
    apply_sort(state);

    // Apply settings overrides after projects are loaded
    apply_settings_overrides(state);

    gtk_window_present(GTK_WINDOW(state->window));
}

int main(int argc, char *argv[]) {
    GtkApplication *app = gtk_application_new("com.gmux.terminal",
                                             G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);

    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

    return status;
}
