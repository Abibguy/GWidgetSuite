#include <gtk/gtk.h>
#include <cairo.h>
#include <pango/pangocairo.h>
#include <ctime>
#include <string>
#include <vector>
#include <cmath>
#include <sys/time.h>
#include <unistd.h>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <map>
#include <algorithm>

// ---------------- CONFIG ----------------
const int SCREEN_WIDTH  = 1920;
const int SCREEN_HEIGHT = 1080;
const int TOP_MARGIN    = 625;
const int RIGHT_MARGIN  = 30;
const double OPACITY    = 0.75;

// Dark Mode Color Palette
const double BG_DARK     = 0.12;  // Dark background
const double BG_MID      = 0.16;  // Card background
const double BG_LIGHT    = 0.20;  // Lighter elements
const double ACCENT_RED  = 0.98;  // macOS red
const double ACCENT_GREEN = 0.26;
const double ACCENT_BLUE = 0.26;
const double ACCENT_ORANGE_R = 1.0;  // Orange accent
const double ACCENT_ORANGE_G = 0.58;
const double ACCENT_ORANGE_B = 0.0;
const double TEXT_PRIMARY = 0.95;   // Main text
const double TEXT_SECONDARY = 0.70; // Secondary text
const double BORDER_COLOR = 0.25;   // Subtle borders

const int WIDGET_SPACING = 12;
const int CARD_RADIUS = 16;

struct CalendarNote {
    std::string date;
    std::string message;
    bool is_important;
};

struct TodoItem {
    std::string text;
    bool completed;
    std::string time;
};

class CombinedDashboardWidget {
private:
    GtkWidget *window;
    GtkWidget *overlay;
    GtkWidget *note_popup;
    GtkWidget *note_entry;
    GtkWidget *todo_popup;
    GtkWidget *todo_entry;
    
    // Calendar data
    struct tm current_date;
    struct tm display_date;
    std::vector<CalendarNote> notes;
    std::string selected_date_str;
    bool showing_note_popup;
    int hover_day = -1;
    
    // Todo data
    std::vector<TodoItem> todos;
    bool showing_todo_popup;
    int hover_todo_item = -1;
    
    // Layout dimensions
    int total_width = 380;
    int calendar_height = 260;
    int base_todo_height = 120;
    int item_height = 35;
    int total_height;
    
    void loadNotes() {
        notes.clear();
        std::ifstream file(getNotesFilePath());
        if (!file.good()) return;
        
        std::string line;
        while (std::getline(file, line)) {
            if (line.empty()) continue;
            
            std::istringstream ss(line);
            std::string date, important_str, message;
            
            if (std::getline(ss, date, '|') &&
                std::getline(ss, important_str, '|') &&
                std::getline(ss, message)) {
                
                CalendarNote note;
                note.date = date;
                note.message = message;
                note.is_important = (important_str == "1");
                notes.push_back(note);
            }
        }
    }
    
    void loadTodos() {
        todos.clear();
        std::ifstream file(getTodosFilePath());
        if (!file.good()) return;
        
        std::string line;
        while (std::getline(file, line)) {
            if (line.empty()) continue;
            
            std::istringstream ss(line);
            std::string completed_str, time_str, text;
            
            if (std::getline(ss, completed_str, '|') &&
                std::getline(ss, time_str, '|') &&
                std::getline(ss, text)) {
                
                TodoItem todo;
                todo.completed = (completed_str == "1");
                todo.time = time_str;
                todo.text = text;
                todos.push_back(todo);
            }
        }
    }
    
    void saveNotes() {
        std::ofstream file(getNotesFilePath());
        if (!file.good()) return;
        
        for (const auto& note : notes) {
            file << note.date << "|" << (note.is_important ? "1" : "0") << "|" << note.message << "\n";
        }
    }
    
    void saveTodos() {
        std::ofstream file(getTodosFilePath());
        if (!file.good()) return;
        
        for (const auto& todo : todos) {
            file << (todo.completed ? "1" : "0") << "|" << todo.time << "|" << todo.text << "\n";
        }
    }
    
    std::string getNotesFilePath() {
        const char* home = getenv("HOME");
        if (!home) return "./dashboard-notes.txt";
        return std::string(home) + "/.config/dashboard-notes.txt";
    }
    
    std::string getTodosFilePath() {
        const char* home = getenv("HOME");
        if (!home) return "./dashboard-todos.txt";
        return std::string(home) + "/.config/dashboard-todos.txt";
    }
    
    std::string formatDate(const struct tm& date) {
        char buffer[32];
        strftime(buffer, sizeof(buffer), "%Y-%m-%d", &date);
        return std::string(buffer);
    }
    
    bool hasNoteForDate(const std::string& date) {
        return std::any_of(notes.begin(), notes.end(),
                          [&date](const CalendarNote& note) { return note.date == date; });
    }
    
    std::string getNoteForDate(const std::string& date) {
        for (const auto& note : notes) {
            if (note.date == date) {
                return note.message;
            }
        }
        return "";
    }
    
    void updateWindowSize() {
        if (!window) return; // Safety check
        
        int visible_items = std::max(0, std::min(8, (int)todos.size()));
        int todo_content_height = base_todo_height + visible_items * item_height;
        
        total_height = calendar_height + todo_content_height + 2 * WIDGET_SPACING;
        
        gtk_window_resize(GTK_WINDOW(window), total_width, total_height);
        
        // Force immediate redraw
        while (gtk_events_pending()) {
            gtk_main_iteration();
        }
        gtk_widget_queue_draw(window);
    }
    
    void navigateMonth(int direction) {
        display_date.tm_mon += direction;
        if (display_date.tm_mon > 11) {
            display_date.tm_mon = 0;
            display_date.tm_year++;
        } else if (display_date.tm_mon < 0) {
            display_date.tm_mon = 11;
            display_date.tm_year--;
        }
        mktime(&display_date);
        gtk_widget_queue_draw(window);
    }
    
    void drawRoundedRect(cairo_t *cr, double x, double y, double w, double h, double radius) {
        cairo_new_sub_path(cr);
        cairo_arc(cr, x + radius, y + radius, radius, M_PI, 3 * M_PI / 2);
        cairo_arc(cr, x + w - radius, y + radius, radius, 3 * M_PI / 2, 0);
        cairo_arc(cr, x + w - radius, y + h - radius, radius, 0, M_PI / 2);
        cairo_arc(cr, x + radius, y + h - radius, radius, M_PI / 2, M_PI);
        cairo_close_path(cr);
    }
    
    void drawTrashIcon(cairo_t *cr, int x, int y) {
        cairo_set_source_rgba(cr, ACCENT_RED, 0.3, 0.3, 0.8);
        cairo_set_line_width(cr, 1.5);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
        
        // Trash can body
        cairo_rectangle(cr, x - 4, y + 2, 8, 10);
        cairo_stroke_preserve(cr);
        
        // Trash can lid
        cairo_new_path(cr);
        cairo_move_to(cr, x - 5, y + 2);
        cairo_line_to(cr, x + 5, y + 2);
        cairo_stroke(cr);
        
        // Handle
        cairo_move_to(cr, x - 2, y);
        cairo_line_to(cr, x + 2, y);
        cairo_stroke(cr);
        
        // Vertical lines inside trash
        cairo_set_line_width(cr, 1);
        cairo_move_to(cr, x - 1, y + 4);
        cairo_line_to(cr, x - 1, y + 9);
        cairo_move_to(cr, x + 1, y + 4);
        cairo_line_to(cr, x + 1, y + 9);
        cairo_stroke(cr);
    }

public:
    CombinedDashboardWidget() {
        // Initialize dates
        time_t now = time(nullptr);
        localtime_r(&now, &current_date);
        display_date = current_date;
        showing_note_popup = false;
        showing_todo_popup = false;
        window = nullptr;
        overlay = nullptr;
        note_popup = nullptr;
        note_entry = nullptr;
        todo_popup = nullptr;
        todo_entry = nullptr;
        
        loadNotes();
        loadTodos();
        
        // Calculate initial size (without calling GTK functions)
        int visible_items = std::max(0, std::min(8, (int)todos.size()));
        int todo_content_height = base_todo_height + visible_items * item_height;
        total_height = calendar_height + todo_content_height + 2 * WIDGET_SPACING;
    }
    
    void run() {
        gtk_init(nullptr, nullptr);

        window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
        gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
        gtk_widget_set_app_paintable(window, TRUE);

        gtk_window_set_skip_taskbar_hint(GTK_WINDOW(window), TRUE);
        gtk_window_set_skip_pager_hint(GTK_WINDOW(window), TRUE);

        overlay = gtk_overlay_new();
        gtk_container_add(GTK_CONTAINER(window), overlay);
        
        GtkWidget *drawing_area = gtk_drawing_area_new();
        gtk_overlay_add_overlay(GTK_OVERLAY(overlay), drawing_area);

        g_signal_connect(window, "screen-changed", G_CALLBACK(on_screen_changed), nullptr);
        g_signal_connect(drawing_area, "draw", G_CALLBACK(on_draw), this);
        g_signal_connect(window, "button-press-event", G_CALLBACK(on_button_press), this);
        g_signal_connect(window, "motion-notify-event", G_CALLBACK(on_motion_notify), this);
        g_signal_connect(window, "leave-notify-event", G_CALLBACK(on_leave_notify), this);
        g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), nullptr);

        gtk_widget_add_events(window, GDK_BUTTON_PRESS_MASK | GDK_POINTER_MOTION_MASK | GDK_LEAVE_NOTIFY_MASK);

        gtk_window_set_default_size(GTK_WINDOW(window), total_width, total_height);
        
        int x = SCREEN_WIDTH - total_width - RIGHT_MARGIN;
        int y = TOP_MARGIN;
        gtk_window_move(GTK_WINDOW(window), x, y);

        gtk_widget_show_all(window);

        g_timeout_add(1000, (GSourceFunc)update_display, this);

        gtk_main();
    }

    static gboolean update_display(gpointer data) {
        auto *self = static_cast<CombinedDashboardWidget*>(data);
        if (!self || !self->window) return FALSE;
        
        time_t now = time(nullptr);
        localtime_r(&now, &self->current_date);
        gtk_widget_queue_draw(self->window);
        return TRUE;
    }

    static gboolean on_draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
        auto *self = static_cast<CombinedDashboardWidget*>(data);
        if (!self) return FALSE;

        GtkAllocation allocation;
        gtk_widget_get_allocation(widget, &allocation);
        int w = allocation.width;
        int h = allocation.height;

        cairo_set_antialias(cr, CAIRO_ANTIALIAS_SUBPIXEL);

        // Dark background
        cairo_set_source_rgba(cr, BG_DARK, BG_DARK, BG_DARK, OPACITY);
        cairo_rectangle(cr, 0, 0, w, h);
        cairo_fill(cr);

        int y_offset = 0;
        
        // Draw calendar card
        self->drawCalendarCard(cr, 0, y_offset, w, self->calendar_height);
        y_offset += self->calendar_height + WIDGET_SPACING;
        
        // Draw todo card
        int todo_height = self->base_todo_height + std::max(0, std::min(8, (int)self->todos.size())) * self->item_height;
        self->drawTodoCard(cr, 0, y_offset, w, todo_height);

        return FALSE;
    }
    
    void drawCalendarCard(cairo_t *cr, int x, int y, int w, int h) {
        // Card background
        drawRoundedRect(cr, x, y, w, h, CARD_RADIUS);
        cairo_set_source_rgba(cr, BG_MID, BG_MID, BG_MID, 1.0);
        cairo_fill_preserve(cr);
        
        // Card border
        cairo_set_source_rgba(cr, BORDER_COLOR, BORDER_COLOR, BORDER_COLOR, 0.3);
        cairo_set_line_width(cr, 1);
        cairo_stroke(cr);
        
        // Header
        int header_height = 40;
        PangoLayout *layout = pango_cairo_create_layout(cr);
        PangoFontDescription *desc = pango_font_description_new();
        
        // Month/Year title
        pango_font_description_set_family(desc, "Sans");
        pango_font_description_set_weight(desc, PANGO_WEIGHT_SEMIBOLD);
        pango_font_description_set_absolute_size(desc, 16 * PANGO_SCALE);
        pango_layout_set_font_description(layout, desc);
        
        char title[64];
        strftime(title, sizeof(title), "%B %Y", &display_date);
        pango_layout_set_text(layout, title, -1);
        
        int text_w, text_h;
        pango_layout_get_pixel_size(layout, &text_w, &text_h);
        
        cairo_set_source_rgb(cr, TEXT_PRIMARY, TEXT_PRIMARY, TEXT_PRIMARY);
        cairo_move_to(cr, x + (w - text_w) / 2, y + (header_height - text_h) / 2);
        pango_cairo_show_layout(cr, layout);
        
        // Navigation arrows
        int arrow_size = 6;
        int arrow_y = y + header_height / 2;
        
        cairo_set_source_rgba(cr, TEXT_SECONDARY, TEXT_SECONDARY, TEXT_SECONDARY, 0.8);
        cairo_set_line_width(cr, 2);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        
        // Left arrow
        cairo_move_to(cr, x + 20 + arrow_size, arrow_y - arrow_size/2);
        cairo_line_to(cr, x + 20, arrow_y);
        cairo_line_to(cr, x + 20 + arrow_size, arrow_y + arrow_size/2);
        cairo_stroke(cr);
        
        // Right arrow
        cairo_move_to(cr, x + w - 20 - arrow_size, arrow_y - arrow_size/2);
        cairo_line_to(cr, x + w - 20, arrow_y);
        cairo_line_to(cr, x + w - 20 - arrow_size, arrow_y + arrow_size/2);
        cairo_stroke(cr);
        
        // Calendar grid
        int cal_start_x = x + 20;
        int cal_start_y = y + header_height + 10;
        int cell_width = (w - 40) / 7;
        int cell_height = 28;
        
        // Day headers
        pango_font_description_set_weight(desc, PANGO_WEIGHT_MEDIUM);
        pango_font_description_set_absolute_size(desc, 11 * PANGO_SCALE);
        pango_layout_set_font_description(layout, desc);
        
        const char* day_names[] = {"S", "M", "T", "W", "T", "F", "S"};
        cairo_set_source_rgba(cr, TEXT_SECONDARY, TEXT_SECONDARY, TEXT_SECONDARY, 0.7);
        
        for (int i = 0; i < 7; i++) {
            pango_layout_set_text(layout, day_names[i], -1);
            int dtext_w, dtext_h;
            pango_layout_get_pixel_size(layout, &dtext_w, &dtext_h);
            
            int dx = cal_start_x + i * cell_width + (cell_width - dtext_w) / 2;
            int dy = cal_start_y;
            cairo_move_to(cr, dx, dy);
            pango_cairo_show_layout(cr, layout);
        }
        
        // Calendar days
        pango_font_description_set_weight(desc, PANGO_WEIGHT_NORMAL);
        pango_font_description_set_absolute_size(desc, 13 * PANGO_SCALE);
        pango_layout_set_font_description(layout, desc);
        
        struct tm first_day = display_date;
        first_day.tm_mday = 1;
        mktime(&first_day);
        
        int first_weekday = first_day.tm_wday;
        
        struct tm next_month = display_date;
        next_month.tm_mon++;
        if (next_month.tm_mon > 11) {
            next_month.tm_mon = 0;
            next_month.tm_year++;
        }
        next_month.tm_mday = 1;
        mktime(&next_month);
        next_month.tm_mday = 0;
        mktime(&next_month);
        int days_in_month = next_month.tm_mday;
        
        int current_day = 1;
        
        for (int day_pos = 0; day_pos < 42 && current_day <= days_in_month; day_pos++) {
            if (day_pos >= first_weekday) {
                int col = day_pos % 7;
                int row = day_pos / 7;
                
                int dx = cal_start_x + col * cell_width;
                int dy = cal_start_y + 20 + row * cell_height;
                
                bool is_today = (current_day == current_date.tm_mday &&
                               display_date.tm_mon == current_date.tm_mon &&
                               display_date.tm_year == current_date.tm_year);
                
                struct tm day_tm = display_date;
                day_tm.tm_mday = current_day;
                mktime(&day_tm);
                std::string date_str = formatDate(day_tm);
                bool has_note = hasNoteForDate(date_str);
                bool is_hovered = (hover_day == current_day);
                
                // Hover effect
                if (is_hovered && !is_today) {
                    cairo_set_source_rgba(cr, BG_LIGHT, BG_LIGHT, BG_LIGHT, 0.8);
                    cairo_arc(cr, dx + cell_width/2, dy + cell_height/2, 12, 0, 2 * M_PI);
                    cairo_fill(cr);
                }
                
                if (is_today) {
                    cairo_set_source_rgb(cr, ACCENT_RED, ACCENT_GREEN, ACCENT_BLUE);
                    cairo_arc(cr, dx + cell_width/2, dy + cell_height/2, 12, 0, 2 * M_PI);
                    cairo_fill(cr);
                } else if (has_note) {
                    cairo_set_source_rgba(cr, ACCENT_ORANGE_R, ACCENT_ORANGE_G, ACCENT_ORANGE_B, 0.3);
                    cairo_arc(cr, dx + cell_width/2, dy + cell_height/2, 10, 0, 2 * M_PI);
                    cairo_fill(cr);
                }
                
                char day_str[3];
                snprintf(day_str, sizeof(day_str), "%d", current_day);
                pango_layout_set_text(layout, day_str, -1);
                
                int dtext_w, dtext_h;
                pango_layout_get_pixel_size(layout, &dtext_w, &dtext_h);
                
                if (is_today) {
                    cairo_set_source_rgb(cr, 1, 1, 1);
                } else {
                    cairo_set_source_rgb(cr, TEXT_PRIMARY, TEXT_PRIMARY, TEXT_PRIMARY);
                }
                
                cairo_move_to(cr, dx + (cell_width - dtext_w) / 2, dy + (cell_height - dtext_h) / 2);
                pango_cairo_show_layout(cr, layout);
                
                current_day++;
            }
        }
        
        g_object_unref(layout);
        pango_font_description_free(desc);
    }
    
    void drawTodoCard(cairo_t *cr, int x, int y, int w, int h) {
        // Card background
        drawRoundedRect(cr, x, y, w, h, CARD_RADIUS);
        cairo_set_source_rgba(cr, BG_MID, BG_MID, BG_MID, 1.0);
        cairo_fill_preserve(cr);
        
        cairo_set_source_rgba(cr, BORDER_COLOR, BORDER_COLOR, BORDER_COLOR, 0.3);
        cairo_set_line_width(cr, 1);
        cairo_stroke(cr);
        
        // Header
        PangoLayout *layout = pango_cairo_create_layout(cr);
        PangoFontDescription *desc = pango_font_description_new();
        
        pango_font_description_set_family(desc, "Sans");
        pango_font_description_set_weight(desc, PANGO_WEIGHT_SEMIBOLD);
        pango_font_description_set_absolute_size(desc, 16 * PANGO_SCALE);
        pango_layout_set_font_description(layout, desc);
        
        pango_layout_set_text(layout, "TODO LIST", -1);
        
        cairo_set_source_rgb(cr, TEXT_PRIMARY, TEXT_PRIMARY, TEXT_PRIMARY);
        cairo_move_to(cr, x + 20, y + 15);
        pango_cairo_show_layout(cr, layout);
        
        // Add button (plus icon)
        cairo_set_source_rgba(cr, ACCENT_ORANGE_R, ACCENT_ORANGE_G, ACCENT_ORANGE_B, 0.8);
        cairo_set_line_width(cr, 2);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        
        int plus_x = x + w - 30;
        int plus_y = y + 25;
        int plus_size = 6;
        
        cairo_move_to(cr, plus_x - plus_size, plus_y);
        cairo_line_to(cr, plus_x + plus_size, plus_y);
        cairo_move_to(cr, plus_x, plus_y - plus_size);
        cairo_line_to(cr, plus_x, plus_y + plus_size);
        cairo_stroke(cr);
        
        // Empty state message
        if (todos.empty()) {
            pango_font_description_set_weight(desc, PANGO_WEIGHT_NORMAL);
            pango_font_description_set_absolute_size(desc, 14 * PANGO_SCALE);
            pango_layout_set_font_description(layout, desc);
            
            cairo_set_source_rgba(cr, TEXT_SECONDARY, TEXT_SECONDARY, TEXT_SECONDARY, 0.7);
            pango_layout_set_text(layout, "No tasks yet. Click + to add one!", -1);
            
            int text_w, text_h;
            pango_layout_get_pixel_size(layout, &text_w, &text_h);
            
            cairo_move_to(cr, x + (w - text_w) / 2, y + 80);
            pango_cairo_show_layout(cr, layout);
            
            g_object_unref(layout);
            pango_font_description_free(desc);
            return;
        }
        
        // Todo items
        int item_start_y = y + 55;
        
        pango_font_description_set_weight(desc, PANGO_WEIGHT_NORMAL);
        pango_font_description_set_absolute_size(desc, 13 * PANGO_SCALE);
        pango_layout_set_font_description(layout, desc);
        
        int visible_items = std::min(8, (int)todos.size());
        
        for (int i = 0; i < visible_items; i++) {
            if (i >= (int)todos.size()) break; // Safety check
            
            const auto& todo = todos[i];
            int current_item_y = item_start_y + i * item_height;
            
            // Hover effect for todo items
            if (hover_todo_item == i) {
                cairo_set_source_rgba(cr, BG_LIGHT, BG_LIGHT, BG_LIGHT, 0.3);
                cairo_rectangle(cr, x + 10, current_item_y - 2, w - 20, item_height);
                cairo_fill(cr);
            }
            
            // Checkbox
            int checkbox_x = x + 20;
            int checkbox_y = current_item_y + 8;
            int checkbox_size = 12;
            
            if (todo.completed) {
                cairo_set_source_rgba(cr, ACCENT_ORANGE_R, ACCENT_ORANGE_G, ACCENT_ORANGE_B, 1.0);
                cairo_arc(cr, checkbox_x + checkbox_size/2, checkbox_y + checkbox_size/2, checkbox_size/2, 0, 2 * M_PI);
                cairo_fill(cr);
                
                // Checkmark
                cairo_set_source_rgb(cr, 1, 1, 1);
                cairo_set_line_width(cr, 2);
                cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
                cairo_move_to(cr, checkbox_x + 3, checkbox_y + 6);
                cairo_line_to(cr, checkbox_x + 5, checkbox_y + 8);
                cairo_line_to(cr, checkbox_x + 9, checkbox_y + 4);
                cairo_stroke(cr);
            } else {
                cairo_set_source_rgba(cr, BORDER_COLOR, BORDER_COLOR, BORDER_COLOR, 0.5);
                cairo_set_line_width(cr, 1);
                cairo_arc(cr, checkbox_x + checkbox_size/2, checkbox_y + checkbox_size/2, checkbox_size/2, 0, 2 * M_PI);
                cairo_stroke(cr);
            }
            
            // Task text
            cairo_set_source_rgba(cr, todo.completed ? TEXT_SECONDARY : TEXT_PRIMARY, 
                                 todo.completed ? TEXT_SECONDARY : TEXT_PRIMARY, 
                                 todo.completed ? TEXT_SECONDARY : TEXT_PRIMARY, 
                                 todo.completed ? 0.6 : 1.0);
            
            pango_layout_set_text(layout, todo.text.c_str(), -1);
            cairo_move_to(cr, checkbox_x + checkbox_size + 12, current_item_y + 3);
            pango_cairo_show_layout(cr, layout);
            
            // Delete button (trash icon)
            if (hover_todo_item == i) {
                int trash_x = x + w - 25;
                int trash_y = current_item_y + 10;
                drawTrashIcon(cr, trash_x, trash_y);
            }
            
            // Time
            if (!todo.time.empty()) {
                pango_font_description_set_absolute_size(desc, 11 * PANGO_SCALE);
                pango_layout_set_font_description(layout, desc);
                
                pango_layout_set_text(layout, todo.time.c_str(), -1);
                int time_w, time_h;
                pango_layout_get_pixel_size(layout, &time_w, &time_h);
                
                cairo_set_source_rgba(cr, TEXT_SECONDARY, TEXT_SECONDARY, TEXT_SECONDARY, 0.7);
                cairo_move_to(cr, x + w - time_w - 40, current_item_y + 3);
                pango_cairo_show_layout(cr, layout);
                
                pango_font_description_set_absolute_size(desc, 13 * PANGO_SCALE);
                pango_layout_set_font_description(layout, desc);
            }
        }
        
        g_object_unref(layout);
        pango_font_description_free(desc);
    }

    static void on_screen_changed(GtkWidget *widget, GdkScreen *old_screen, gpointer user_data) {
        GdkScreen *screen = gtk_widget_get_screen(widget);
        GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
        if (!visual) visual = gdk_screen_get_system_visual(screen);
        gtk_widget_set_visual(widget, visual);
    }

    static gboolean on_motion_notify(GtkWidget *widget, GdkEventMotion *event, gpointer user_data) {
        auto *self = static_cast<CombinedDashboardWidget*>(user_data);
        if (!self) return FALSE;
        
        if (self->showing_note_popup || self->showing_todo_popup) {
            return FALSE;
        }
        
        // Reset hover states
        self->hover_day = -1;
        self->hover_todo_item = -1;
        
        // Calendar hover
        if (event->y <= self->calendar_height) {
            int cal_start_x = 20;
            int cal_start_y = 70;
            int cell_width = (self->total_width - 40) / 7;
            int cell_height = 28;
            
            if (event->x >= cal_start_x && event->x <= cal_start_x + 7 * cell_width &&
                event->y >= cal_start_y && event->y <= cal_start_y + 6 * cell_height) {
                
                int col = (event->x - cal_start_x) / cell_width;
                int row = (event->y - cal_start_y) / cell_height;
                
                if (col >= 0 && col < 7 && row >= 0) {
                    struct tm first_day = self->display_date;
                    first_day.tm_mday = 1;
                    mktime(&first_day);
                    
                    int day_pos = row * 7 + col;
                    int clicked_day = day_pos - first_day.tm_wday + 1;
                    
                    if (clicked_day > 0) {
                        struct tm next_month = self->display_date;
                        next_month.tm_mon++;
                        if (next_month.tm_mon > 11) {
                            next_month.tm_mon = 0;
                            next_month.tm_year++;
                        }
                        next_month.tm_mday = 1;
                        mktime(&next_month);
                        next_month.tm_mday = 0;
                        mktime(&next_month);
                        
                        if (clicked_day <= next_month.tm_mday) {
                            self->hover_day = clicked_day;
                        }
                    }
                }
            }
        } else {
            // Todo hover
            int todo_start_y = self->calendar_height + WIDGET_SPACING;
            if (event->y >= todo_start_y + 55 && !self->todos.empty()) {
                int item_y = todo_start_y + 55;
                int hovered_item = (event->y - item_y) / self->item_height;
                
                if (hovered_item >= 0 && hovered_item < (int)self->todos.size() && hovered_item < 8) {
                    self->hover_todo_item = hovered_item;
                }
            }
        }
        
        gtk_widget_queue_draw(self->window);
        return FALSE;
    }
    
    static gboolean on_leave_notify(GtkWidget *widget, GdkEventCrossing *event, gpointer user_data) {
        auto *self = static_cast<CombinedDashboardWidget*>(user_data);
        if (!self) return FALSE;
        
        self->hover_day = -1;
        self->hover_todo_item = -1;
        gtk_widget_queue_draw(self->window);
        return FALSE;
    }

    static gboolean on_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
        auto *self = static_cast<CombinedDashboardWidget*>(user_data);
        if (!self) return FALSE;
        
        if (self->showing_note_popup || self->showing_todo_popup) {
            return FALSE;
        }
        
        if (event->button == 1) {
            int calendar_end_y = self->calendar_height;
            int todo_start_y = calendar_end_y + WIDGET_SPACING;
            
            if (event->y <= calendar_end_y) {
                // Calendar area
                if (event->y <= 40) {
                    // Header area - navigation arrows
                    if (event->x < 50) {
                        self->navigateMonth(-1);
                        return TRUE;
                    } else if (event->x > self->total_width - 50) {
                        self->navigateMonth(1);
                        return TRUE;
                    }
                } else {
                    // Calendar grid clicks
                    int cal_start_x = 20;
                    int cal_start_y = 70;
                    int cell_width = (self->total_width - 40) / 7;
                    int cell_height = 28;
                    
                    if (event->x >= cal_start_x && event->x <= cal_start_x + 7 * cell_width &&
                        event->y >= cal_start_y && event->y <= cal_start_y + 6 * cell_height) {
                        
                        int col = (event->x - cal_start_x) / cell_width;
                        int row = (event->y - cal_start_y) / cell_height;
                        
                        if (col >= 0 && col < 7 && row >= 0) {
                            struct tm first_day = self->display_date;
                            first_day.tm_mday = 1;
                            mktime(&first_day);
                            
                            int day_pos = row * 7 + col;
                            int clicked_day = day_pos - first_day.tm_wday + 1;
                            
                            if (clicked_day > 0) {
                                struct tm clicked_tm = self->display_date;
                                clicked_tm.tm_mday = clicked_day;
                                mktime(&clicked_tm);
                                
                                struct tm next_month = self->display_date;
                                next_month.tm_mon++;
                                if (next_month.tm_mon > 11) {
                                    next_month.tm_mon = 0;
                                    next_month.tm_year++;
                                }
                                next_month.tm_mday = 1;
                                mktime(&next_month);
                                next_month.tm_mday = 0;
                                mktime(&next_month);
                                
                                if (clicked_day <= next_month.tm_mday) {
                                    std::string date_str = self->formatDate(clicked_tm);
                                    self->showNotePopup(date_str);
                                    return TRUE;
                                }
                            }
                        }
                    }
                }
            } else if (event->y >= todo_start_y) {
                // Todo area
                if (event->x >= self->total_width - 50 && event->y <= todo_start_y + 50) {
                    // Plus button clicked
                    self->showTodoPopup();
                    return TRUE;
                } else if (!self->todos.empty()) {
                    // Check for checkbox or delete clicks
                    int item_y = todo_start_y + 55;
                    int clicked_item = (event->y - item_y) / self->item_height;
                    
                    if (clicked_item >= 0 && clicked_item < (int)self->todos.size() && clicked_item < 8) {
                        if (event->x >= 20 && event->x <= 44) {
                            // Checkbox clicked
                            self->todos[clicked_item].completed = !self->todos[clicked_item].completed;
                            self->saveTodos();
                            gtk_widget_queue_draw(self->window);
                            return TRUE;
                        } else if (event->x >= self->total_width - 35 && event->x <= self->total_width - 15) {
                            // Delete button clicked
                            if (clicked_item < (int)self->todos.size()) {
                                self->todos.erase(self->todos.begin() + clicked_item);
                                self->saveTodos();
                                self->updateWindowSize();
                                return TRUE;
                            }
                        }
                    }
                }
            }
            
            // Default drag behavior
            gtk_window_begin_move_drag(GTK_WINDOW(widget),
                                       event->button,
                                       (int)event->x_root,
                                       (int)event->y_root,
                                       event->time);
        }
        return TRUE;
    }
    
    void showNotePopup(const std::string& date) {
        selected_date_str = date;
        showing_note_popup = true;
        
        if (note_popup) {
            gtk_widget_destroy(note_popup);
            note_popup = nullptr;
        }
        
        // Create styled popup
        note_popup = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
        gtk_widget_set_size_request(note_popup, 320, 140);
        
        // Add some styling with CSS
        GtkCssProvider *provider = gtk_css_provider_new();
        gtk_css_provider_load_from_data(provider,
            "box { "
            "  background-color: rgba(40, 40, 45, 0.95); "
            "  border-radius: 12px; "
            "  padding: 16px; "
            "  box-shadow: 0 8px 24px rgba(0, 0, 0, 0.4); "
            "} "
            "entry { "
            "  background-color: rgba(55, 55, 60, 0.8); "
            "  border: 1px solid rgba(70, 70, 75, 0.6); "
            "  border-radius: 8px; "
            "  padding: 8px 12px; "
            "  color: #f0f0f0; "
            "  font-size: 14px; "
            "} "
            "button { "
            "  border-radius: 6px; "
            "  padding: 8px 16px; "
            "  font-weight: 500; "
            "} "
            ".save-btn { "
            "  background: linear-gradient(135deg, #ff9500, #ff7b00); "
            "  color: white; "
            "  border: none; "
            "} "
            ".cancel-btn { "
            "  background-color: rgba(70, 70, 75, 0.8); "
            "  color: #f0f0f0; "
            "  border: 1px solid rgba(90, 90, 95, 0.6); "
            "}", -1, NULL);
        
        GtkStyleContext *context = gtk_widget_get_style_context(note_popup);
        gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_USER);
        
        // Date label
        char date_label[64];
        struct tm date_tm = {};
        strptime(date.c_str(), "%Y-%m-%d", &date_tm);
        strftime(date_label, sizeof(date_label), "Note for %B %d, %Y", &date_tm);
        
        GtkWidget *label = gtk_label_new(date_label);
        gtk_widget_set_halign(label, GTK_ALIGN_START);
        gtk_box_pack_start(GTK_BOX(note_popup), label, FALSE, FALSE, 0);
        
        note_entry = gtk_entry_new();
        gtk_entry_set_placeholder_text(GTK_ENTRY(note_entry), "Enter your note...");
        
        // Pre-fill with existing note if any
        std::string existing_note = getNoteForDate(date);
        if (!existing_note.empty()) {
            gtk_entry_set_text(GTK_ENTRY(note_entry), existing_note.c_str());
        }
        
        GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_widget_set_halign(button_box, GTK_ALIGN_END);
        
        GtkWidget *save_btn = gtk_button_new_with_label("Save");
        GtkWidget *cancel_btn = gtk_button_new_with_label("Cancel");
        
        gtk_style_context_add_class(gtk_widget_get_style_context(save_btn), "save-btn");
        gtk_style_context_add_class(gtk_widget_get_style_context(cancel_btn), "cancel-btn");
        
        gtk_box_pack_start(GTK_BOX(button_box), cancel_btn, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(button_box), save_btn, FALSE, FALSE, 0);
        
        gtk_box_pack_start(GTK_BOX(note_popup), note_entry, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(note_popup), button_box, FALSE, FALSE, 0);
        
        g_signal_connect(save_btn, "clicked", G_CALLBACK(on_save_note), this);
        g_signal_connect(cancel_btn, "clicked", G_CALLBACK(on_cancel_note), this);
        g_signal_connect(note_entry, "activate", G_CALLBACK(on_save_note), this);
        
        gtk_overlay_add_overlay(GTK_OVERLAY(overlay), note_popup);
        gtk_widget_set_halign(note_popup, GTK_ALIGN_CENTER);
        gtk_widget_set_valign(note_popup, GTK_ALIGN_CENTER);
        
        gtk_widget_show_all(note_popup);
        gtk_widget_grab_focus(note_entry);
        
        g_object_unref(provider);
    }
    
    void showTodoPopup() {
        showing_todo_popup = true;
        
        if (todo_popup) {
            gtk_widget_destroy(todo_popup);
            todo_popup = nullptr;
        }
        
        todo_popup = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
        gtk_widget_set_size_request(todo_popup, 320, 120);
        
        // Style similar to note popup
        GtkCssProvider *provider = gtk_css_provider_new();
        gtk_css_provider_load_from_data(provider,
            "box { "
            "  background-color: rgba(40, 40, 45, 0.95); "
            "  border-radius: 12px; "
            "  padding: 16px; "
            "  box-shadow: 0 8px 24px rgba(0, 0, 0, 0.4); "
            "} "
            "entry { "
            "  background-color: rgba(55, 55, 60, 0.8); "
            "  border: 1px solid rgba(70, 70, 75, 0.6); "
            "  border-radius: 8px; "
            "  padding: 8px 12px; "
            "  color: #f0f0f0; "
            "  font-size: 14px; "
            "} "
            "button { "
            "  border-radius: 6px; "
            "  padding: 8px 16px; "
            "  font-weight: 500; "
            "} "
            ".add-btn { "
            "  background: linear-gradient(135deg, #ff9500, #ff7b00); "
            "  color: white; "
            "  border: none; "
            "} "
            ".cancel-btn { "
            "  background-color: rgba(70, 70, 75, 0.8); "
            "  color: #f0f0f0; "
            "  border: 1px solid rgba(90, 90, 95, 0.6); "
            "}", -1, NULL);
        
        GtkStyleContext *context = gtk_widget_get_style_context(todo_popup);
        gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_USER);
        
        GtkWidget *label = gtk_label_new("Add New Task");
        gtk_widget_set_halign(label, GTK_ALIGN_START);
        gtk_box_pack_start(GTK_BOX(todo_popup), label, FALSE, FALSE, 0);
        
        todo_entry = gtk_entry_new();
        gtk_entry_set_placeholder_text(GTK_ENTRY(todo_entry), "Enter task description...");
        
        GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_widget_set_halign(button_box, GTK_ALIGN_END);
        
        GtkWidget *add_btn = gtk_button_new_with_label("Add Task");
        GtkWidget *cancel_btn = gtk_button_new_with_label("Cancel");
        
        gtk_style_context_add_class(gtk_widget_get_style_context(add_btn), "add-btn");
        gtk_style_context_add_class(gtk_widget_get_style_context(cancel_btn), "cancel-btn");
        
        gtk_box_pack_start(GTK_BOX(button_box), cancel_btn, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(button_box), add_btn, FALSE, FALSE, 0);
        
        gtk_box_pack_start(GTK_BOX(todo_popup), todo_entry, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(todo_popup), button_box, FALSE, FALSE, 0);
        
        g_signal_connect(add_btn, "clicked", G_CALLBACK(on_save_todo), this);
        g_signal_connect(cancel_btn, "clicked", G_CALLBACK(on_cancel_todo), this);
        g_signal_connect(todo_entry, "activate", G_CALLBACK(on_save_todo), this);
        
        gtk_overlay_add_overlay(GTK_OVERLAY(overlay), todo_popup);
        gtk_widget_set_halign(todo_popup, GTK_ALIGN_CENTER);
        gtk_widget_set_valign(todo_popup, GTK_ALIGN_CENTER);
        
        gtk_widget_show_all(todo_popup);
        gtk_widget_grab_focus(todo_entry);
        
        g_object_unref(provider);
    }
    
    void hideNotePopup() {
        showing_note_popup = false;
        if (note_popup) {
            gtk_widget_destroy(note_popup);
            note_popup = nullptr;
        }
    }
    
    void hideTodoPopup() {
        showing_todo_popup = false;
        if (todo_popup) {
            gtk_widget_destroy(todo_popup);
            todo_popup = nullptr;
        }
    }
    
    static void on_save_note(GtkWidget *widget, gpointer user_data) {
        auto *self = static_cast<CombinedDashboardWidget*>(user_data);
        if (!self || !self->note_entry) return;
        
        const char* text = gtk_entry_get_text(GTK_ENTRY(self->note_entry));
        if (!text) return;
        
        std::string message(text);
        
        if (!message.empty()) {
            // Remove existing note for this date
            self->notes.erase(
                std::remove_if(self->notes.begin(), self->notes.end(),
                              [&](const CalendarNote& note) { 
                                  return note.date == self->selected_date_str; 
                              }),
                self->notes.end());
            
            // Add new note
            CalendarNote new_note;
            new_note.date = self->selected_date_str;
            new_note.message = message;
            new_note.is_important = false;
            self->notes.push_back(new_note);
            
            self->saveNotes();
        } else {
            // Remove note if text is empty
            self->notes.erase(
                std::remove_if(self->notes.begin(), self->notes.end(),
                              [&](const CalendarNote& note) { 
                                  return note.date == self->selected_date_str; 
                              }),
                self->notes.end());
            self->saveNotes();
        }
        
        self->hideNotePopup();
        gtk_widget_queue_draw(self->window);
    }
    
    static void on_save_todo(GtkWidget *widget, gpointer user_data) {
        auto *self = static_cast<CombinedDashboardWidget*>(user_data);
        if (!self || !self->todo_entry) return;
        
        const char* text = gtk_entry_get_text(GTK_ENTRY(self->todo_entry));
        if (!text) return;
        
        std::string task(text);
        
        if (!task.empty()) {
            TodoItem new_todo;
            new_todo.text = task;
            new_todo.completed = false;
            new_todo.time = "";
            self->todos.insert(self->todos.begin(), new_todo);
            
            self->saveTodos();
            self->updateWindowSize();
        }
        
        self->hideTodoPopup();
    }
    
    static void on_cancel_note(GtkWidget *widget, gpointer user_data) {
        auto *self = static_cast<CombinedDashboardWidget*>(user_data);
        if (self) self->hideNotePopup();
    }
    
    static void on_cancel_todo(GtkWidget *widget, gpointer user_data) {
        auto *self = static_cast<CombinedDashboardWidget*>(user_data);
        if (self) self->hideTodoPopup();
    }
};

int main(int argc, char** argv) {
    CombinedDashboardWidget dashboard;
    dashboard.run();
    return 0;
}
