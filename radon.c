#include <gtk/gtk.h>
#include <adwaita.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdbool.h>

#define NSM_BIN "/usr/bin/nsm"
#define ICON_LOCAL "%s/.local/share/icons/radon.png"
#define ICON_SYS "/usr/share/radon.png"

typedef struct {
    GtkWidget *window;
    GtkWidget *toast_overlay;
    GtkWidget *manual_list;
    GtkWidget *stack;
} RadonApp;

typedef struct {
    char name[512];
    char kind[16];
} SnapEntry;

static char *get_icon_path(void)
{
    static char path[1024];
    const char *home = g_get_home_dir();
    snprintf(path, sizeof(path), ICON_LOCAL, home);
    if (g_file_test(path, G_FILE_TEST_EXISTS))
        return g_strdup(path);
    if (g_file_test(ICON_SYS, G_FILE_TEST_EXISTS))
        return g_strdup(ICON_SYS);
    return NULL;
}

static void show_toast(RadonApp *app, const char *msg)
{
    AdwToast *toast = adw_toast_new(msg);
    adw_toast_set_timeout(toast, 3);
    adw_toast_overlay_add_toast(ADW_TOAST_OVERLAY(app->toast_overlay), toast);
}

static char *run_nsm_with_password(const char *password, const char *args, int *exit_code)
{
    char tmpfile[] = "/tmp/radon_out_XXXXXX";
    int fd = mkstemp(tmpfile);
    if (fd >= 0) close(fd);

    char cmd[1200];
    snprintf(cmd, sizeof(cmd), "sudo -S %s %s > %s 2>&1", NSM_BIN, args, tmpfile);

    int rc = -1;
    FILE *sp = popen(cmd, "w");
    if (sp) {
        fprintf(sp, "%s\n", password);
        fflush(sp);
        int st = pclose(sp);
        rc = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
    }

    GString *out = g_string_new(NULL);
    char *content = NULL;
    gsize len = 0;
    if (g_file_get_contents(tmpfile, &content, &len, NULL)) {
        g_string_append(out, content);
        g_free(content);
    }
    unlink(tmpfile);

    *exit_code = rc;
    return g_string_free(out, FALSE);
}

static GList *parse_section(const char *output, const char *header)
{
    GList *list = NULL;
    char *pos = strstr(output, header);
    if (!pos) return NULL;
    pos = strchr(pos, '\n');
    if (!pos) return NULL;
    pos++;

    char *next_section = strstr(pos, "[Home]");

    char *section_end = next_section ? next_section : pos + strlen(pos);
    char *segment = g_strndup(pos, section_end - pos);

    char **lines = g_strsplit(segment, "\n", -1);
    for (int i = 0; lines[i]; i++) {
        char *line = g_strstrip(lines[i]);
        if (strlen(line) == 0) continue;
        if (strcmp(line, "(none)") == 0) continue;
        if (strchr(line, '[')) continue;
        list = g_list_append(list, g_strdup(line));
    }
    g_strfreev(lines);
    g_free(segment);
    return list;
}

typedef struct {
    RadonApp *app;
    GtkWidget *entry;
    char action[16];
    char snap_name[512];
    char create_kind[16];
    GtkWidget *dialog;
} PasswordCtx;

static bool refresh_lists(RadonApp *app, const char *password);

static void execute_pending_action(PasswordCtx *ctx)
{
    const char *password = gtk_editable_get_text(GTK_EDITABLE(ctx->entry));

    char args[600];
    int rc = 0;
    char *out = NULL;
    bool is_refresh = strcmp(ctx->action, "refresh") == 0;

    if (strcmp(ctx->action, "delete") == 0) {
        snprintf(args, sizeof(args), "delete %s", ctx->snap_name);
        out = run_nsm_with_password(password, args, &rc);
    } else if (strcmp(ctx->action, "rollback") == 0) {
        snprintf(args, sizeof(args), "rollback %s", ctx->snap_name);
        out = run_nsm_with_password(password, args, &rc);
    } else if (strcmp(ctx->action, "create") == 0) {
        snprintf(args, sizeof(args), "create %s", ctx->snap_name);
        out = run_nsm_with_password(password, args, &rc);
    }

    bool list_ok = refresh_lists(ctx->app, password);

    if (!is_refresh && rc != 0) {
        char msg[700];
        snprintf(msg, sizeof(msg), "Failed: %s", out ? out : "unknown error");
        show_toast(ctx->app, msg);
    } else if (is_refresh) {
        show_toast(ctx->app, list_ok ? "Snapshot list refreshed" : "Authentication failed or nsm error");
    } else {
        show_toast(ctx->app, "Operation completed successfully");
    }

    g_free(out);
    gtk_window_destroy(GTK_WINDOW(ctx->dialog));
    g_free(ctx);
}

static void on_password_entry_activate(GtkEntry *entry, gpointer user_data)
{
    PasswordCtx *ctx = user_data;
    execute_pending_action(ctx);
}

static void on_password_confirm_clicked(GtkButton *button, gpointer user_data)
{
    PasswordCtx *ctx = user_data;
    execute_pending_action(ctx);
}

static void prompt_password(RadonApp *app, const char *action, const char *snap_name)
{
    GtkWidget *dialog = adw_window_new();
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(app->window));
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 360, -1);
    gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);

    GtkWidget *toolbar_view = adw_toolbar_view_new();
    GtkWidget *header = adw_header_bar_new();
    adw_header_bar_set_show_end_title_buttons(ADW_HEADER_BAR(header), TRUE);
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar_view), header);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_top(box, 24);
    gtk_widget_set_margin_bottom(box, 24);
    gtk_widget_set_margin_start(box, 24);
    gtk_widget_set_margin_end(box, 24);

    GtkWidget *icon = gtk_image_new_from_icon_name("dialog-password-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(icon), 48);
    gtk_widget_set_halign(icon, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(box), icon);

    GtkWidget *label = gtk_label_new(
        strcmp(action, "refresh") == 0
            ? "Radon needs administrator access to manage snapshots."
            : "Authentication is required to continue.");
    gtk_widget_set_halign(label, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(box), label);

    GtkWidget *entry = gtk_password_entry_new();
    gtk_password_entry_set_show_peek_icon(GTK_PASSWORD_ENTRY(entry), TRUE);
    gtk_widget_set_hexpand(entry, TRUE);
    gtk_box_append(GTK_BOX(box), entry);

    GtkWidget *confirm = gtk_button_new_with_label("Authenticate");
    gtk_widget_add_css_class(confirm, "suggested-action");
    gtk_widget_add_css_class(confirm, "pill");
    gtk_box_append(GTK_BOX(box), confirm);

    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar_view), box);
    adw_window_set_content(ADW_WINDOW(dialog), toolbar_view);

    PasswordCtx *ctx = g_new0(PasswordCtx, 1);
    ctx->app = app;
    ctx->entry = entry;
    snprintf(ctx->action, sizeof(ctx->action), "%s", action);
    snprintf(ctx->snap_name, sizeof(ctx->snap_name), "%s", snap_name ? snap_name : "");
    ctx->dialog = dialog;

    g_signal_connect(entry, "activate", G_CALLBACK(on_password_entry_activate), ctx);
    g_signal_connect(confirm, "clicked", G_CALLBACK(on_password_confirm_clicked), ctx);

    gtk_window_present(GTK_WINDOW(dialog));
    gtk_widget_grab_focus(entry);
}

static void on_rollback_clicked(GtkButton *button, gpointer user_data)
{
    SnapEntry *entry = user_data;
    RadonApp *app = g_object_get_data(G_OBJECT(button), "radon-app");
    prompt_password(app, "rollback", entry->name);
}

static void on_delete_clicked(GtkButton *button, gpointer user_data)
{
    SnapEntry *entry = user_data;
    RadonApp *app = g_object_get_data(G_OBJECT(button), "radon-app");
    prompt_password(app, "delete", entry->name);
}

static GtkWidget *build_snapshot_row(RadonApp *app, const char *name)
{
    AdwActionRow *row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), name);

    SnapEntry *entry = g_new0(SnapEntry, 1);
    snprintf(entry->name, sizeof(entry->name), "%s", name);
    g_object_set_data_full(G_OBJECT(row), "snap-entry", entry, g_free);

    GtkWidget *rollback_btn = gtk_button_new_from_icon_name("edit-undo-symbolic");
    gtk_widget_add_css_class(rollback_btn, "flat");
    gtk_widget_set_valign(rollback_btn, GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(rollback_btn, "Rollback to this snapshot");
    g_object_set_data(G_OBJECT(rollback_btn), "radon-app", app);
    g_signal_connect(rollback_btn, "clicked", G_CALLBACK(on_rollback_clicked), entry);

    GtkWidget *delete_btn = gtk_button_new_from_icon_name("user-trash-symbolic");
    gtk_widget_add_css_class(delete_btn, "flat");
    gtk_widget_set_valign(delete_btn, GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(delete_btn, "Delete this snapshot");
    g_object_set_data(G_OBJECT(delete_btn), "radon-app", app);
    g_signal_connect(delete_btn, "clicked", G_CALLBACK(on_delete_clicked), entry);

    adw_action_row_add_suffix(row, rollback_btn);
    adw_action_row_add_suffix(row, delete_btn);

    return GTK_WIDGET(row);
}

static void clear_listbox(GtkWidget *listbox)
{
    GtkWidget *child = gtk_widget_get_first_child(listbox);
    while (child) {
        GtkWidget *next = gtk_widget_get_next_sibling(child);
        gtk_list_box_remove(GTK_LIST_BOX(listbox), child);
        child = next;
    }
}

static void populate_listbox(RadonApp *app, GtkWidget *listbox, GList *names)
{
    clear_listbox(listbox);
    if (!names) {
        AdwActionRow *row = ADW_ACTION_ROW(adw_action_row_new());
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), "No snapshots");
        gtk_widget_set_sensitive(GTK_WIDGET(row), FALSE);
        gtk_list_box_append(GTK_LIST_BOX(listbox), GTK_WIDGET(row));
        return;
    }
    for (GList *l = names; l; l = l->next) {
        GtkWidget *row = build_snapshot_row(app, (const char *)l->data);
        gtk_list_box_append(GTK_LIST_BOX(listbox), row);
    }
}

static void free_string_list(GList *list)
{
    g_list_free_full(list, g_free);
}

static bool refresh_lists(RadonApp *app, const char *password)
{
    int rc = 0;
    char *out = run_nsm_with_password(password, "list", &rc);

    if (rc != 0) {
        g_free(out);
        populate_listbox(app, app->manual_list, NULL);
        return false;
    }

    char *manual_start = strstr(out, "Manual Snapshots");
    char *apt_start = strstr(out, "apt Snapshots");
    char manual_section[8192] = "";
    if (manual_start) {
        size_t len = apt_start ? (size_t)(apt_start - manual_start) : strlen(manual_start);
        if (len >= sizeof(manual_section)) len = sizeof(manual_section) - 1;
        strncpy(manual_section, manual_start, len);
        manual_section[len] = '\0';
    }

    GList *manual_root = parse_section(manual_section, "[Root]");

    populate_listbox(app, app->manual_list, manual_root);

    free_string_list(manual_root);
    g_free(out);
    return true;
}

typedef struct {
    RadonApp *app;
    GtkWidget *name_entry;
    GtkWidget *dialog;
} CreateCtx;

static void on_create_proceed(GtkButton *button, gpointer user_data)
{
    CreateCtx *ctx = user_data;
    const char *name = gtk_editable_get_text(GTK_EDITABLE(ctx->name_entry));
    if (!name || strlen(name) == 0) {
        show_toast(ctx->app, "Please enter a snapshot name");
        return;
    }
    if (strncmp(name, "apt", 3) == 0 || strcmp(name, "nsmd") == 0) {
        show_toast(ctx->app, "Reserved name. Radon only creates manual snapshots.");
        return;
    }
    gtk_window_destroy(GTK_WINDOW(ctx->dialog));
    RadonApp *app = ctx->app;
    char saved_name[512];
    snprintf(saved_name, sizeof(saved_name), "%s", name);
    g_free(ctx);
    prompt_password(app, "create", saved_name);
}

static void on_new_snapshot_clicked(GtkButton *button, gpointer user_data)
{
    RadonApp *app = user_data;

    GtkWidget *dialog = adw_window_new();
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(app->window));
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 380, -1);
    gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);

    GtkWidget *toolbar_view = adw_toolbar_view_new();
    GtkWidget *header = adw_header_bar_new();
    GtkWidget *title = adw_window_title_new("New Snapshot", NULL);
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(header), title);
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar_view), header);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_top(box, 20);
    gtk_widget_set_margin_bottom(box, 20);
    gtk_widget_set_margin_start(box, 20);
    gtk_widget_set_margin_end(box, 20);

    GtkWidget *group = adw_preferences_group_new();
    adw_preferences_group_set_description(ADW_PREFERENCES_GROUP(group), "Creates a manual root snapshot via nsm.");

    GtkWidget *name_row = adw_entry_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(name_row), "Snapshot Name");
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(group), name_row);

    gtk_box_append(GTK_BOX(box), group);

    GtkWidget *create_btn = gtk_button_new_with_label("Create Manual Snapshot");
    gtk_widget_add_css_class(create_btn, "suggested-action");
    gtk_widget_add_css_class(create_btn, "pill");
    gtk_box_append(GTK_BOX(box), create_btn);

    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar_view), box);
    adw_window_set_content(ADW_WINDOW(dialog), toolbar_view);

    CreateCtx *ctx = g_new0(CreateCtx, 1);
    ctx->app = app;
    ctx->name_entry = name_row;
    ctx->dialog = dialog;
    g_signal_connect(create_btn, "clicked", G_CALLBACK(on_create_proceed), ctx);

    gtk_window_present(GTK_WINDOW(dialog));
}

static void on_refresh_clicked(GtkButton *button, gpointer user_data)
{
    RadonApp *app = user_data;
    prompt_password(app, "refresh", NULL);
}

static void activate(GtkApplication *gtk_app, gpointer user_data)
{
    RadonApp *app = g_new0(RadonApp, 1);

    app->window = adw_application_window_new(GTK_APPLICATION(gtk_app));
    gtk_window_set_title(GTK_WINDOW(app->window), "Radon");
    gtk_window_set_default_size(GTK_WINDOW(app->window), 640, 560);

    char *icon_path = get_icon_path();
    if (icon_path) {
        GdkTexture *texture = gdk_texture_new_from_filename(icon_path, NULL);
        if (texture) {
            gtk_window_set_icon_name(GTK_WINDOW(app->window), "radon");
            g_object_unref(texture);
        }
        g_free(icon_path);
    }

    app->toast_overlay = adw_toast_overlay_new();

    GtkWidget *toolbar_view = adw_toolbar_view_new();
    GtkWidget *header = adw_header_bar_new();

    GtkWidget *win_title = adw_window_title_new("Radon", "Snapshot Manager");
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(header), win_title);

    GtkWidget *refresh_btn = gtk_button_new_from_icon_name("view-refresh-symbolic");
    gtk_widget_set_tooltip_text(refresh_btn, "Refresh");
    g_signal_connect(refresh_btn, "clicked", G_CALLBACK(on_refresh_clicked), app);
    adw_header_bar_pack_start(ADW_HEADER_BAR(header), refresh_btn);

    GtkWidget *new_btn = gtk_button_new_from_icon_name("list-add-symbolic");
    gtk_widget_set_tooltip_text(new_btn, "New Manual Snapshot");
    gtk_widget_add_css_class(new_btn, "suggested-action");
    g_signal_connect(new_btn, "clicked", G_CALLBACK(on_new_snapshot_clicked), app);
    adw_header_bar_pack_end(ADW_HEADER_BAR(header), new_btn);

    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar_view), header);

    GtkWidget *scrolled = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scrolled, TRUE);

    GtkWidget *content_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 24);
    gtk_widget_set_margin_top(content_box, 20);
    gtk_widget_set_margin_bottom(content_box, 20);
    gtk_widget_set_margin_start(content_box, 20);
    gtk_widget_set_margin_end(content_box, 20);

    GtkWidget *clamp = adw_clamp_new();
    adw_clamp_set_maximum_size(ADW_CLAMP(clamp), 700);
    adw_clamp_set_child(ADW_CLAMP(clamp), content_box);

    GtkWidget *manual_group = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(manual_group), "Manual Snapshots");
    adw_preferences_group_set_description(ADW_PREFERENCES_GROUP(manual_group), "Root snapshots created manually through Radon or nsm.");

    app->manual_list = gtk_list_box_new();
    gtk_widget_add_css_class(app->manual_list, "boxed-list");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(app->manual_list), GTK_SELECTION_NONE);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(manual_group), app->manual_list);

    gtk_box_append(GTK_BOX(content_box), manual_group);

    GtkWidget *info_group = adw_preferences_group_new();
    adw_preferences_group_set_description(ADW_PREFERENCES_GROUP(info_group),
        "Radon only manages manual snapshots. Automatic apt and daemon snapshots are handled by nsm itself.");
    gtk_box_append(GTK_BOX(content_box), info_group);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), clamp);

    GtkWidget *toast_content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(toast_content), scrolled);
    adw_toast_overlay_set_child(ADW_TOAST_OVERLAY(app->toast_overlay), toast_content);

    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar_view), app->toast_overlay);
    adw_application_window_set_content(ADW_APPLICATION_WINDOW(app->window), toolbar_view);

    prompt_password(app, "refresh", NULL);

    gtk_window_present(GTK_WINDOW(app->window));
}

int main(int argc, char *argv[])
{
    AdwApplication *app = adw_application_new("io.arvor.Radon", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
