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

// ---------------- CONFIG ----------------
const int SCREEN_WIDTH  = 1920;
const int SCREEN_HEIGHT = 1080;
const int TOP_MARGIN    = 410;
const int RIGHT_MARGIN  = 30;
const double OPACITY    = 0.85;
const double BG_RED   = 0.12;
const double BG_GREEN = 0.12;
const double BG_BLUE  = 0.13;

// Your timezone (system will auto-detect, but you can override)
const std::string YOUR_TIMEZONE = "Asia/Singapore";

struct TimezoneConfig {
    std::string name;
    std::string status;
    std::string tz_identifier;  // IANA timezone identifier
};

const TimezoneConfig TIMEZONE_CONFIGS[] = {
    {"Singapore", "Today", "Asia/Singapore"},
    {"Kolkata", "Today", "Asia/Kolkata"}, 
    {"Moscow", "Today", "Europe/Moscow"},
    {"Helsinki", "Today", "Europe/Helsinki"}
};

class SystemTimeManager {
private:
    struct TimezoneInfo {
        std::string identifier;
        int utc_offset_seconds;
        bool is_dst;
        time_t last_updated;
    };
    
    std::vector<TimezoneInfo> tz_cache;
    time_t cache_duration = 3600; // 1 hour cache
    
public:
    // Method 3A: Direct system timezone query using /usr/share/zoneinfo
    std::pair<int, bool> getSystemTimezoneOffset(const std::string& tz_identifier) {
        // Use timedatectl if available (systemd systems)
        std::string cmd = "TZ='" + tz_identifier + "' date '+%z %Z'";
        
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) return {0, false};
        
        char buffer[256];
        std::string result = "";
        while (fgets(buffer, sizeof buffer, pipe) != NULL) {
            result += buffer;
        }
        pclose(pipe);
        
        if (result.length() >= 5) {
            // Parse "+0300 EEST" format
            int sign = (result[0] == '+') ? 1 : -1;
            int hours = std::stoi(result.substr(1, 2));
            int mins = std::stoi(result.substr(3, 2));
            
            // Check if DST is active by looking at timezone abbreviation
            bool is_dst = (result.find("DST") != std::string::npos ||
                          result.find("EDT") != std::string::npos ||
                          result.find("PDT") != std::string::npos ||
                          result.find("MDT") != std::string::npos ||
                          result.find("CDT") != std::string::npos ||
                          result.find("EEST") != std::string::npos ||  // Helsinki summer
                          result.find("CEST") != std::string::npos ||
                          result.find("BST") != std::string::npos);
            
            return {sign * (hours * 3600 + mins * 60), is_dst};
        }
        
        return {0, false};
    }
    
    // Method 3B: Use system's localtime_r with timezone switching
    time_t getTimezoneTime(const std::string& tz_identifier, struct tm* result_tm) {
        // Save current timezone
        const char* old_tz = getenv("TZ");
        
        // Set target timezone
        setenv("TZ", tz_identifier.c_str(), 1);
        tzset();
        
        // Get current UTC time
        time_t utc_now = time(nullptr);
        
        // Convert to local time in target timezone
        localtime_r(&utc_now, result_tm);
        
        // Restore original timezone
        if (old_tz) {
            setenv("TZ", old_tz, 1);
        } else {
            unsetenv("TZ");
        }
        tzset();
        
        return utc_now;
    }
    
    // Method 3C: NTP-style time sync check (ensures system time is accurate)
    bool checkSystemTimeSync() {
        // Check if systemd-timesyncd or ntpd is running and synced
        FILE* pipe = popen("timedatectl status | grep -E '(NTP synchronized|System clock synchronized)'", "r");
        if (!pipe) return true; // Assume synced if can't check
        
        char buffer[256];
        std::string result = "";
        while (fgets(buffer, sizeof buffer, pipe) != NULL) {
            result += buffer;
        }
        pclose(pipe);
        
        // Check for either "NTP synchronized: yes" or "System clock synchronized: yes"
        return (result.find("yes") != std::string::npos);
    }
    
    // Method 3D: Get timezone info directly from /etc/localtime and zoneinfo
    std::string getCurrentSystemTimezone() {
        // Method 1: Check /etc/timezone (Debian/Ubuntu)
        std::ifstream timezone_file("/etc/timezone");
        if (timezone_file.is_open()) {
            std::string tz;
            std::getline(timezone_file, tz);
            timezone_file.close();
            if (!tz.empty()) return tz;
        }
        
        // Method 2: Parse /etc/localtime symlink (most modern systems)
        char link_target[256];
        ssize_t len = readlink("/etc/localtime", link_target, sizeof(link_target) - 1);
        if (len != -1) {
            link_target[len] = '\0';
            std::string target(link_target);
            size_t pos = target.find("/zoneinfo/");
            if (pos != std::string::npos) {
                return target.substr(pos + 10); // Skip "/zoneinfo/"
            }
        }
        
        // Method 3: Use timedatectl
        FILE* pipe = popen("timedatectl show --property=Timezone --value", "r");
        if (pipe) {
            char buffer[256];
            if (fgets(buffer, sizeof(buffer), pipe)) {
                pclose(pipe);
                std::string tz(buffer);
                // Remove trailing newline
                if (!tz.empty() && tz.back() == '\n') {
                    tz.pop_back();
                }
                return tz;
            }
            pclose(pipe);
        }
        
        return "UTC"; // Fallback
    }
};

struct TimeZone {
    std::string name;
    std::string status;
    std::string tz_identifier;
    int utc_offset_seconds;
    bool is_dst;
    int relative_hours;
    int relative_mins;
    time_t last_updated;
};

class MultiClockWidget {
private:
    GtkWidget *window;
    std::vector<TimeZone> timezones;
    SystemTimeManager time_mgr;
    std::string user_timezone;
    int user_utc_offset = 0;
    time_t last_tz_update = 0;
    
    void updateTimezoneData() {
        time_t now = time(nullptr);
        
        // Update every 15 minutes (to catch DST changes quickly)
        if (now - last_tz_update < 900 && last_tz_update != 0) {
            return;
        }
        
        last_tz_update = now;
        
        // Ensure system time is synced with NTP
        if (!time_mgr.checkSystemTimeSync()) {
            g_print("Warning: System time may not be NTP synchronized\n");
        }
        
        // Get user's current timezone and offset
        user_timezone = time_mgr.getCurrentSystemTimezone();
        auto user_info = time_mgr.getSystemTimezoneOffset(user_timezone);
        user_utc_offset = user_info.first;
        
        // Update all configured timezones
        for (auto& tz : timezones) {
            auto info = time_mgr.getSystemTimezoneOffset(tz.tz_identifier);
            tz.utc_offset_seconds = info.first;
            tz.is_dst = info.second;
            
            // Calculate relative offset from user's current timezone
            int diff_seconds = tz.utc_offset_seconds - user_utc_offset;
            tz.relative_hours = diff_seconds / 3600;
            tz.relative_mins = (diff_seconds % 3600) / 60;
            
            tz.last_updated = now;
            
            // Debug output
            g_print("Updated %s: UTC%+d:%02d (DST: %s)\n", 
                   tz.name.c_str(), 
                   tz.utc_offset_seconds / 3600,
                   abs(tz.utc_offset_seconds % 3600) / 60,
                   tz.is_dst ? "Yes" : "No");
        }
    }

public:
    MultiClockWidget() {
        // Initialize timezones from config
        int config_count = sizeof(TIMEZONE_CONFIGS) / sizeof(TIMEZONE_CONFIGS[0]);
        for (int i = 0; i < config_count && i < 4; i++) {
            const auto& config = TIMEZONE_CONFIGS[i];
            
            TimeZone tz;
            tz.name = config.name;
            tz.status = config.status;
            tz.tz_identifier = config.tz_identifier;
            tz.utc_offset_seconds = 0;
            tz.is_dst = false;
            tz.relative_hours = 0;
            tz.relative_mins = 0;
            tz.last_updated = 0;
            
            timezones.push_back(tz);
        }
        
        // Initial timezone data update
        updateTimezoneData();

        gtk_init(nullptr, nullptr);

        window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
        gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
        gtk_widget_set_app_paintable(window, TRUE);

        gtk_window_set_skip_taskbar_hint(GTK_WINDOW(window), TRUE);
        gtk_window_set_skip_pager_hint(GTK_WINDOW(window), TRUE);

        g_signal_connect(window, "screen-changed", G_CALLBACK(on_screen_changed), nullptr);
        g_signal_connect(window, "draw", G_CALLBACK(on_draw), this);
        g_signal_connect(window, "button-press-event", G_CALLBACK(on_button_press), nullptr);
        g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), nullptr);

        gtk_widget_add_events(window, GDK_BUTTON_PRESS_MASK);

        int width = 520;
        int height = 160;
        gtk_window_set_default_size(GTK_WINDOW(window), width, height);
        
        int x = SCREEN_WIDTH - width - RIGHT_MARGIN;
        int y = TOP_MARGIN;
        gtk_window_move(GTK_WINDOW(window), x, y);

        gtk_widget_show_all(window);

        // Update display every second
        g_timeout_add(1000, (GSourceFunc)update_time, this);
        // Check for timezone changes every 15 minutes
        g_timeout_add(900000, (GSourceFunc)update_timezones, this);

        gtk_main();
    }

    static gboolean update_time(gpointer data) {
        auto *self = static_cast<MultiClockWidget*>(data);
        gtk_widget_queue_draw(self->window);
        return TRUE;
    }
    
    static gboolean update_timezones(gpointer data) {
        auto *self = static_cast<MultiClockWidget*>(data);
        self->updateTimezoneData();
        return TRUE;
    }

    static gboolean on_draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
        auto *self = static_cast<MultiClockWidget*>(data);

        GtkAllocation allocation;
        gtk_widget_get_allocation(widget, &allocation);
        int w = allocation.width;
        int h = allocation.height;

        cairo_set_antialias(cr, CAIRO_ANTIALIAS_SUBPIXEL);

        cairo_set_source_rgba(cr, BG_RED, BG_GREEN, BG_BLUE, OPACITY);
        cairo_rectangle(cr, 0, 0, w, h);
        cairo_fill(cr);

        int clock_width = w / 4;
        int clock_height = h;

        for (int i = 0; i < 4 && i < self->timezones.size(); i++) {
            int x = i * clock_width;
            int y = 0;

            self->draw_analog_clock(cr, x, y, clock_width, clock_height, self->timezones[i]);
        }

        return FALSE;
    }

    void draw_analog_clock(cairo_t *cr, int x, int y, int w, int h, const TimeZone &tz) {
        // Get current time in target timezone using system libraries
        struct tm tz_tm;
        time_mgr.getTimezoneTime(tz.tz_identifier, &tz_tm);

        int hours = tz_tm.tm_hour % 12;
        int minutes = tz_tm.tm_min;
        int seconds = tz_tm.tm_sec;
        int hour24 = tz_tm.tm_hour;

        // Determine if it's day or night
        bool is_day = (hour24 >= 6 && hour24 < 18);

        // Clock center and radius
        int center_x = x + w/2;
        int center_y = y + (h-60)/2;
        int radius = std::min(w-15, h-70) / 2;

        // Draw clock face with DST indicator
        if (is_day) {
            if (tz.is_dst) {
                // Slightly warmer white for DST
                cairo_set_source_rgba(cr, 1.0, 0.98, 0.94, 0.95);
            } else {
                cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.95);
            }
        } else {
            if (tz.is_dst) {
                // Slightly lighter dark for DST
                cairo_set_source_rgba(cr, 0.15, 0.15, 0.15, 0.95);
            } else {
                cairo_set_source_rgba(cr, 0.12, 0.12, 0.12, 0.95);
            }
        }
        cairo_arc(cr, center_x, center_y, radius, 0, 2 * M_PI);
        cairo_fill(cr);

        // Outer ring with DST color coding
        if (tz.is_dst) {
            cairo_set_source_rgba(cr, 1.0, 0.8, 0.2, 0.4); // Golden ring for DST
        } else {
            cairo_set_source_rgba(cr, is_day ? 0.8 : 0.3, is_day ? 0.8 : 0.3, is_day ? 0.8 : 0.3, 0.6);
        }
        cairo_set_line_width(cr, 1.0);
        cairo_arc(cr, center_x, center_y, radius, 0, 2 * M_PI);
        cairo_stroke(cr);

        // Hour markers
        double marker_r = is_day ? 0.1 : 0.8;
        double marker_g = is_day ? 0.1 : 0.8;
        double marker_b = is_day ? 0.1 : 0.8;
        cairo_set_source_rgba(cr, marker_r, marker_g, marker_b, 0.9);
        
        for (int i = 0; i < 12; i++) {
            double angle = (i * 30 - 90) * M_PI / 180;
            bool is_major = (i % 3 == 0);
            
            if (is_major) {
                cairo_set_line_width(cr, 2);
                int x1 = center_x + (radius - 10) * cos(angle);
                int y1 = center_y + (radius - 10) * sin(angle);
                int x2 = center_x + (radius - 3) * cos(angle);
                int y2 = center_y + (radius - 3) * sin(angle);
                cairo_move_to(cr, x1, y1);
                cairo_line_to(cr, x2, y2);
                cairo_stroke(cr);
            } else {
                cairo_set_line_width(cr, 1);
                int dot_x = center_x + (radius - 6) * cos(angle);
                int dot_y = center_y + (radius - 6) * sin(angle);
                cairo_arc(cr, dot_x, dot_y, 1, 0, 2 * M_PI);
                cairo_fill(cr);
            }
        }

        // Draw hands (same as before)
        double hand_r = is_day ? 0.1 : 0.95;
        double hand_g = is_day ? 0.1 : 0.95;
        double hand_b = is_day ? 0.1 : 0.95;

        // Hour hand
        double hour_angle = ((hours + minutes/60.0) * 30 - 90) * M_PI / 180;
        cairo_set_source_rgba(cr, is_day ? 0.0 : 1.0, is_day ? 0.0 : 1.0, is_day ? 0.0 : 1.0, 0.3);
        cairo_set_line_width(cr, 4);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        cairo_move_to(cr, center_x+0.5, center_y+0.5);
        cairo_line_to(cr, 
            center_x+0.5 + (radius * 0.5) * cos(hour_angle),
            center_y+0.5 + (radius * 0.5) * sin(hour_angle));
        cairo_stroke(cr);
        
        cairo_set_source_rgb(cr, hand_r, hand_g, hand_b);
        cairo_set_line_width(cr, 3);
        cairo_move_to(cr, center_x, center_y);
        cairo_line_to(cr, 
            center_x + (radius * 0.5) * cos(hour_angle),
            center_y + (radius * 0.5) * sin(hour_angle));
        cairo_stroke(cr);

        // Minute hand
        double minute_angle = (minutes * 6 - 90) * M_PI / 180;
        cairo_set_source_rgba(cr, is_day ? 0.0 : 1.0, is_day ? 0.0 : 1.0, is_day ? 0.0 : 1.0, 0.3);
        cairo_set_line_width(cr, 3);
        cairo_move_to(cr, center_x+0.5, center_y+0.5);
        cairo_line_to(cr,
            center_x+0.5 + (radius * 0.75) * cos(minute_angle),
            center_y+0.5 + (radius * 0.75) * sin(minute_angle));
        cairo_stroke(cr);
        
        cairo_set_source_rgb(cr, hand_r, hand_g, hand_b);
        cairo_set_line_width(cr, 2);
        cairo_move_to(cr, center_x, center_y);
        cairo_line_to(cr,
            center_x + (radius * 0.75) * cos(minute_angle),
            center_y + (radius * 0.75) * sin(minute_angle));
        cairo_stroke(cr);

        // Second hand
        double second_angle = (seconds * 6 - 90) * M_PI / 180;
        cairo_set_source_rgb(cr, 1, 0.2, 0.2);
        cairo_set_line_width(cr, 1);
        cairo_move_to(cr, center_x, center_y);
        cairo_line_to(cr,
            center_x + (radius * 0.85) * cos(second_angle),
            center_y + (radius * 0.85) * sin(second_angle));
        cairo_stroke(cr);

        // Center dot
        cairo_set_source_rgba(cr, is_day ? 0.2 : 0.8, is_day ? 0.2 : 0.8, is_day ? 0.2 : 0.8, 0.8);
        cairo_arc(cr, center_x, center_y, 3, 0, 2 * M_PI);
        cairo_fill(cr);
        cairo_set_source_rgb(cr, hand_r, hand_g, hand_b);
        cairo_arc(cr, center_x, center_y, 1.5, 0, 2 * M_PI);
        cairo_fill(cr);

        // Text labels
        int text_start_y = center_y + radius + 10;

        PangoLayout *layout = pango_cairo_create_layout(cr);
        PangoFontDescription *desc = pango_font_description_new();
        
        pango_font_description_set_family(desc, "SF Pro Display");
        pango_font_description_set_weight(desc, PANGO_WEIGHT_MEDIUM);
        pango_font_description_set_absolute_size(desc, 11 * PANGO_SCALE);
        pango_layout_set_font_description(layout, desc);
        
        // City name with DST indicator
        std::string city_text = tz.name;
        if (tz.is_dst) city_text += " ⚡"; // Lightning bolt for DST
        pango_layout_set_text(layout, city_text.c_str(), -1);
        
        int text_w, text_h;
        pango_layout_get_pixel_size(layout, &text_w, &text_h);
        
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_move_to(cr, center_x - text_w/2, text_start_y);
        pango_cairo_show_layout(cr, layout);

        // Status
        pango_font_description_set_weight(desc, PANGO_WEIGHT_NORMAL);
        pango_font_description_set_absolute_size(desc, 9 * PANGO_SCALE);
        pango_layout_set_font_description(layout, desc);
        pango_layout_set_text(layout, tz.status.c_str(), -1);
        pango_layout_get_pixel_size(layout, &text_w, &text_h);
        
        cairo_set_source_rgba(cr, 0.8, 0.8, 0.8, 0.9);
        cairo_move_to(cr, center_x - text_w/2, text_start_y + 13);
        pango_cairo_show_layout(cr, layout);

        // Relative time offset
        char offset_str[32];
        if (tz.relative_hours > 0) {
            if (tz.relative_mins != 0) {
                snprintf(offset_str, sizeof(offset_str), "+%d:%02dHRS", tz.relative_hours, abs(tz.relative_mins));
            } else {
                snprintf(offset_str, sizeof(offset_str), "+%dHRS", tz.relative_hours);
            }
        } else if (tz.relative_hours < 0) {
            if (tz.relative_mins != 0) {
                snprintf(offset_str, sizeof(offset_str), "%d:%02dHRS", tz.relative_hours, abs(tz.relative_mins));
            } else {
                snprintf(offset_str, sizeof(offset_str), "%dHRS", tz.relative_hours);
            }
        } else {
            if (tz.relative_mins > 0) {
                snprintf(offset_str, sizeof(offset_str), "+%dMINS", tz.relative_mins);
            } else if (tz.relative_mins < 0) {
                snprintf(offset_str, sizeof(offset_str), "%dMINS", tz.relative_mins);
            } else {
                snprintf(offset_str, sizeof(offset_str), "SAME");
            }
        }
        
        pango_layout_set_text(layout, offset_str, -1);
        pango_layout_get_pixel_size(layout, &text_w, &text_h);
        cairo_move_to(cr, center_x - text_w/2, text_start_y + 25);
        pango_cairo_show_layout(cr, layout);

        g_object_unref(layout);
        pango_font_description_free(desc);
    }

    static void on_screen_changed(GtkWidget *widget, GdkScreen *old_screen, gpointer user_data) {
        GdkScreen *screen = gtk_widget_get_screen(widget);
        GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
        if (!visual) visual = gdk_screen_get_system_visual(screen);
        gtk_widget_set_visual(widget, visual);
    }

    static gboolean on_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
        if (event->button == 1) {
            gtk_window_begin_move_drag(GTK_WINDOW(widget),
                                       event->button,
                                       (int)event->x_root,
                                       (int)event->y_root,
                                       event->time);
        }
        return TRUE;
    }
};

int main(int argc, char** argv) {
    MultiClockWidget clock;
    return 0;
}
