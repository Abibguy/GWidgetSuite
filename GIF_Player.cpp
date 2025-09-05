#include <gtk/gtk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <iostream>

// ---------------- CONFIG ----------------
// Screen resolution
const int SCREEN_WIDTH  = 1920;
const int SCREEN_HEIGHT = 1080;

// Widget position
const int TOP_MARGIN    = 60;
const int RIGHT_MARGIN  = 30;

// Appearance
const double OPACITY = 0.85; // 0.0 = fully transparent, 1.0 = solid
// --------------------------------------

class GifPlayer {
public:
    GifPlayer(const char* gif_path, int top_margin = TOP_MARGIN, int right_margin = RIGHT_MARGIN) {
        gtk_init(nullptr, nullptr);

        // Load animated GIF
        GError *error = nullptr;
        animation = gdk_pixbuf_animation_new_from_file(gif_path, &error);
        if (!animation) {
            std::cerr << "Failed to load GIF: " << error->message << std::endl;
            g_error_free(error);
            return;
        }

        int gif_width = gdk_pixbuf_animation_get_width(animation);
        int gif_height = gdk_pixbuf_animation_get_height(animation);

        // Create window
        window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        gtk_window_set_default_size(GTK_WINDOW(window), gif_width, gif_height);
        gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
        gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
        gtk_widget_set_app_paintable(window, TRUE);

        // Hide from taskbar
        gtk_window_set_skip_taskbar_hint(GTK_WINDOW(window), TRUE);
        gtk_window_set_skip_pager_hint(GTK_WINDOW(window), TRUE);

        // Set transparency
        g_signal_connect(window, "screen-changed", G_CALLBACK(on_screen_changed), nullptr);
        on_screen_changed(window, nullptr, nullptr);

        // Position at top-right
        int x = SCREEN_WIDTH - gif_width - right_margin;
        int y = top_margin;
        gtk_window_move(GTK_WINDOW(window), x, y);

        // Add GIF image
        image = gtk_image_new_from_animation(animation);
        gtk_container_add(GTK_CONTAINER(window), image);

        // Connect signals
        g_signal_connect(window, "button-press-event", G_CALLBACK(on_button_press), nullptr);
        g_signal_connect(window, "draw", G_CALLBACK(on_draw), this);
        g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), nullptr);

        gtk_widget_add_events(window, GDK_BUTTON_PRESS_MASK);
        gtk_widget_show_all(window);

        gtk_main();
    }

    ~GifPlayer() {
        if (animation) {
            g_object_unref(animation);
        }
    }

private:
    GtkWidget *window;
    GtkWidget *image;
    GdkPixbufAnimation *animation;

    static void on_screen_changed(GtkWidget *widget, GdkScreen *old_screen, gpointer user_data) {
        GdkScreen *screen = gtk_widget_get_screen(widget);
        GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
        if (!visual) visual = gdk_screen_get_system_visual(screen);
        gtk_widget_set_visual(widget, visual);
    }

    static gboolean on_draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
        // Apply opacity to entire widget
        cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
        cairo_set_source_rgba(cr, 0, 0, 0, 0); // Clear background
        cairo_paint(cr);
        
        cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
        cairo_paint_with_alpha(cr, OPACITY);
        
        return FALSE;
    }

    static gboolean on_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
        if (event->button == 1) {
            gtk_window_begin_move_drag(GTK_WINDOW(widget),
                                       event->button,
                                       static_cast<int>(event->x_root),
                                       static_cast<int>(event->y_root),
                                       event->time);
        }
        return TRUE;
    }
};

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <gif_path>" << std::endl;
        return 1;
    }

    GifPlayer player(argv[1]);
    return 0;
}
