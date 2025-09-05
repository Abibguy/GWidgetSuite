//weather widget - Enhanced macOS Style Layout
#include <gtk/gtk.h>
#include <cairo.h>
#include <pango/pangocairo.h>
#include <curl/curl.h>
#include <ctime>
#include <string>
#include <cmath>
#include <iostream>
#include <sstream>
#include <algorithm>

// ---------------- CONFIG ----------------
const int SCREEN_WIDTH  = 1920;
const int SCREEN_HEIGHT = 1080;
const int TOP_MARGIN    = 110;
const int RIGHT_MARGIN  = 610;

// Weather API Configuration
const std::string API_KEY = "58f264aee97c46c9ba113306241409";

// Widget dimensions - perfect square
const int WIDGET_SIZE = 220;
const int CARD_RADIUS = 16;

// Display Configuration
const int DECIMAL_PLACES = 1;

// Location coordinates (will be auto-detected or use these as fallback)
const double FALLBACK_LAT = 1.3521;
const double FALLBACK_LON = 103.8198;

// Refresh rate limiting (in seconds)
const int MIN_REFRESH_INTERVAL = 120; // 2 minutes

struct LocationData {
    double latitude;
    double longitude;
    std::string city;
    std::string country;
};

struct WeatherData {
    std::string condition;
    std::string location;
    double temp_c;
    double feels_like;
    int humidity;
    double wind_speed;
    std::string wind_dir;
    double pressure;
    double uv_index;
    int visibility;
    std::string icon_code;
    bool is_day;
    std::string last_updated;
    std::string sunrise;
    std::string sunset;
};

struct WriteCallbackData {
    std::string data;
};

size_t WriteCallback(void *contents, size_t size, size_t nmemb, WriteCallbackData *userp) {
    size_t total_size = size * nmemb;
    userp->data.append((char*)contents, total_size);
    return total_size;
}

// Simple JSON parser
class SimpleJsonParser {
public:
    static std::string extractStringValue(const std::string& json, const std::string& key) {
        std::string search_key = "\"" + key + "\":\"";
        size_t pos = json.find(search_key);
        if (pos == std::string::npos) return "";
        
        pos += search_key.length();
        size_t end_pos = json.find("\"", pos);
        if (end_pos == std::string::npos) return "";
        
        return json.substr(pos, end_pos - pos);
    }
    
    static double extractDoubleValue(const std::string& json, const std::string& key) {
        std::string search_key = "\"" + key + "\":";
        size_t pos = json.find(search_key);
        if (pos == std::string::npos) return 0.0;
        
        pos += search_key.length();
        
        // Skip whitespace
        while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
        
        size_t end_pos = pos;
        while (end_pos < json.length() && 
               (std::isdigit(json[end_pos]) || json[end_pos] == '.' || json[end_pos] == '-')) {
            end_pos++;
        }
        
        if (end_pos == pos) return 0.0;
        
        std::string value_str = json.substr(pos, end_pos - pos);
        return std::stod(value_str);
    }
    
    static int extractIntValue(const std::string& json, const std::string& key) {
        return static_cast<int>(extractDoubleValue(json, key));
    }
};

class WeatherWidget {
private:
    GtkWidget *window;
    GtkWidget *overlay;
    WeatherData weather;
    LocationData location;
    bool data_loaded = false;
    bool location_loaded = false;
    time_t last_refresh_time = 0;
    
    void drawRoundedRect(cairo_t *cr, double x, double y, double w, double h, double radius) {
        cairo_new_sub_path(cr);
        cairo_arc(cr, x + radius, y + radius, radius, M_PI, 3 * M_PI / 2);
        cairo_arc(cr, x + w - radius, y + radius, radius, 3 * M_PI / 2, 0);
        cairo_arc(cr, x + w - radius, y + h - radius, radius, 0, M_PI / 2);
        cairo_arc(cr, x + radius, y + h - radius, radius, M_PI / 2, M_PI);
        cairo_close_path(cr);
    }
    
    void drawRefreshButton(cairo_t *cr, int x, int y) {
        double cx = x + 8;
        double cy = y + 8;
        double radius = 6;
        
        // Draw circle background
        cairo_arc(cr, cx, cy, radius, 0, 2 * M_PI);
        cairo_set_source_rgba(cr, 0.25, 0.25, 0.25, 0.8);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, 0.4, 0.4, 0.4, 0.6);
        cairo_set_line_width(cr, 0.5);
        cairo_stroke(cr);
        
        // Draw clock hands (refresh symbol)
        cairo_set_line_width(cr, 1.0);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        
        // Red hand (minute hand pointing up)
        cairo_set_source_rgba(cr, 0.9, 0.3, 0.3, 1.0);
        cairo_move_to(cr, cx, cy);
        cairo_line_to(cr, cx, cy - 4);
        cairo_stroke(cr);
        
        // White hand (hour hand pointing right)
        cairo_set_source_rgba(cr, 0.9, 0.9, 0.9, 1.0);
        cairo_move_to(cr, cx, cy);
        cairo_line_to(cr, cx + 3, cy);
        cairo_stroke(cr);
        
        // Center dot
        cairo_arc(cr, cx, cy, 0.8, 0, 2 * M_PI);
        cairo_set_source_rgba(cr, 0.9, 0.9, 0.9, 1.0);
        cairo_fill(cr);
    }
    
    bool isPointInRefreshButton(int x, int y) {
        int btn_x = 12;
        int btn_y = WIDGET_SIZE - 28;
        int btn_size = 16;
        
        int dx = x - (btn_x + btn_size/2);
        int dy = y - (btn_y + btn_size/2);
        return (dx*dx + dy*dy) <= (btn_size/2 * btn_size/2);
    }
    
    std::string getGnomeWeatherIcon() {
        std::string condition_lower = weather.condition;
        std::transform(condition_lower.begin(), condition_lower.end(), condition_lower.begin(), ::tolower);
        
        if (!weather.is_day) {
            if (condition_lower.find("clear") != std::string::npos) return "weather-clear-night";
            if (condition_lower.find("partly") != std::string::npos || condition_lower.find("few") != std::string::npos) return "weather-few-clouds-night";
            if (condition_lower.find("cloud") != std::string::npos) return "weather-overcast";
            if (condition_lower.find("rain") != std::string::npos) return "weather-showers";
            if (condition_lower.find("storm") != std::string::npos || condition_lower.find("thunder") != std::string::npos) return "weather-storm";
            return "weather-overcast";
        } else {
            if (condition_lower.find("sunny") != std::string::npos || condition_lower.find("clear") != std::string::npos) return "weather-clear";
            if (condition_lower.find("partly") != std::string::npos || condition_lower.find("few") != std::string::npos) return "weather-few-clouds";
            if (condition_lower.find("cloud") != std::string::npos || condition_lower.find("overcast") != std::string::npos) return "weather-overcast";
            if (condition_lower.find("rain") != std::string::npos || condition_lower.find("shower") != std::string::npos) return "weather-showers";
            if (condition_lower.find("storm") != std::string::npos || condition_lower.find("thunder") != std::string::npos) return "weather-storm";
            return "weather-few-clouds";
        }
    }
    
    void drawSystemWeatherIcon(cairo_t *cr, int x, int y, int size) {
        GtkIconTheme *icon_theme = gtk_icon_theme_get_default();
        std::string icon_name = getGnomeWeatherIcon();
        
        GdkPixbuf *pixbuf = gtk_icon_theme_load_icon(icon_theme, icon_name.c_str(), size, GTK_ICON_LOOKUP_USE_BUILTIN, nullptr);
        
        if (pixbuf) {
            gdk_cairo_set_source_pixbuf(cr, pixbuf, x, y);
            cairo_paint(cr);
            g_object_unref(pixbuf);
        } else {
            // Fallback to generic weather icon
            pixbuf = gtk_icon_theme_load_icon(icon_theme, "weather-overcast", size, GTK_ICON_LOOKUP_USE_BUILTIN, nullptr);
            if (pixbuf) {
                gdk_cairo_set_source_pixbuf(cr, pixbuf, x, y);
                cairo_paint(cr);
                g_object_unref(pixbuf);
            }
        }
    }
    
    void drawWeatherIcon(cairo_t *cr, int x, int y, int size) {
        // Clean weather icon without shadow to avoid rendering artifacts
        drawSystemWeatherIcon(cr, x, y, size);
    }
    
    std::string formatTime(const std::string& time_str) {
        if (time_str.length() >= 8) { // Format: "07:12 AM" or "07:12 PM"
            std::string time_part = time_str.substr(0, 5); // Get HH:MM
            std::string ampm = time_str.substr(6); // Get AM/PM
            
            // Convert to 24-hour format
            int hour = std::stoi(time_part.substr(0, 2));
            std::string minute = time_part.substr(3, 2);
            
            if (ampm == "PM" && hour != 12) {
                hour += 12;
            } else if (ampm == "AM" && hour == 12) {
                hour = 0;
            }
            
            char formatted_time[8];
            snprintf(formatted_time, sizeof(formatted_time), "%02d:%s", hour, minute.c_str());
            return std::string(formatted_time);
        }
        return time_str;
    }
    
    std::string fetchLocationData() {
        CURL *curl;
        WriteCallbackData response_data;
        
        curl = curl_easy_init();
        if (!curl) return "";
        
        // Use ipinfo.io for location detection
        curl_easy_setopt(curl, CURLOPT_URL, "https://ipinfo.io/json");
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        
        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
        
        if (res != CURLE_OK) return "";
        return response_data.data;
    }
    
    void parseLocationData(const std::string& json_data) {
        std::cout << "Location JSON Response: " << json_data << std::endl;
        
        std::string loc_str = SimpleJsonParser::extractStringValue(json_data, "loc");
        location.city = SimpleJsonParser::extractStringValue(json_data, "city");
        location.country = SimpleJsonParser::extractStringValue(json_data, "country");
        
        std::cout << "Extracted loc: " << loc_str << std::endl;
        std::cout << "City: " << location.city << std::endl;
        
        if (!loc_str.empty()) {
            size_t comma_pos = loc_str.find(",");
            if (comma_pos != std::string::npos) {
                location.latitude = std::stod(loc_str.substr(0, comma_pos));
                location.longitude = std::stod(loc_str.substr(comma_pos + 1));
                location_loaded = true;
                std::cout << "Coordinates: " << location.latitude << "," << location.longitude << std::endl;
            }
        }
    }
    
    std::string fetchWeatherData() {
        CURL *curl;
        WriteCallbackData response_data;
        
        curl = curl_easy_init();
        if (!curl) return "";
        
        std::ostringstream url_stream;
        if (location_loaded) {
            url_stream << "http://api.weatherapi.com/v1/current.json?key=" << API_KEY 
                       << "&q=" << location.latitude << "," << location.longitude << "&aqi=no";
        } else {
            // Use fallback coordinates
            url_stream << "http://api.weatherapi.com/v1/current.json?key=" << API_KEY 
                       << "&q=" << FALLBACK_LAT << "," << FALLBACK_LON << "&aqi=no";
        }
        std::string url = url_stream.str();
        
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
        
        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
        
        if (res != CURLE_OK) return "";
        return response_data.data;
    }
    
    std::string fetchAstronomyData() {
        CURL *curl;
        WriteCallbackData response_data;
        
        curl = curl_easy_init();
        if (!curl) return "";
        
        std::ostringstream url_stream;
        if (location_loaded) {
            url_stream << "http://api.weatherapi.com/v1/astronomy.json?key=" << API_KEY 
                       << "&q=" << location.latitude << "," << location.longitude;
        } else {
            url_stream << "http://api.weatherapi.com/v1/astronomy.json?key=" << API_KEY 
                       << "&q=" << FALLBACK_LAT << "," << FALLBACK_LON;
        }
        std::string url = url_stream.str();
        
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
        
        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
        
        if (res != CURLE_OK) return "";
        return response_data.data;
    }
    
    void parseWeatherData(const std::string& json_data) {
        std::cout << "Weather JSON Response: " << json_data << std::endl;
        
        weather.condition = extractNestedStringValue(json_data, "condition", "text");
        weather.location = extractNestedStringValue(json_data, "location", "name");
        weather.temp_c = extractDoubleValue(json_data, "temp_c");
        weather.feels_like = extractDoubleValue(json_data, "feelslike_c");
        weather.humidity = extractIntValue(json_data, "humidity");
        weather.wind_speed = extractDoubleValue(json_data, "wind_kph");
        weather.wind_dir = extractStringValue(json_data, "wind_dir");
        weather.pressure = extractDoubleValue(json_data, "pressure_mb");
        weather.uv_index = extractDoubleValue(json_data, "uv");
        weather.visibility = extractIntValue(json_data, "vis_km");
        weather.is_day = extractIntValue(json_data, "is_day") == 1;
        
        std::cout << "Condition: " << weather.condition << std::endl;
        std::cout << "Location: " << weather.location << std::endl;
        std::cout << "Temp: " << weather.temp_c << std::endl;
        
        if (!weather.condition.empty() && !weather.location.empty()) {
            data_loaded = true;
        }
    }
    
    void parseAstronomyData(const std::string& json_data) {
        weather.sunrise = formatTime(extractNestedStringValue(json_data, "astronomy", "sunrise"));
        weather.sunset = formatTime(extractNestedStringValue(json_data, "astronomy", "sunset"));
        
        std::cout << "Sunrise: " << weather.sunrise << std::endl;
        std::cout << "Sunset: " << weather.sunset << std::endl;
    }
    
    // Helper methods for nested JSON extraction
    std::string extractNestedStringValue(const std::string& json, const std::string& parent, const std::string& child) {
        std::string search_parent = "\"" + parent + "\"";
        size_t parent_pos = json.find(search_parent);
        if (parent_pos == std::string::npos) return "";
        
        size_t brace_pos = json.find("{", parent_pos);
        if (brace_pos == std::string::npos) return "";
        
        size_t end_brace_pos = json.find("}", brace_pos);
        if (end_brace_pos == std::string::npos) return "";
        
        std::string parent_content = json.substr(brace_pos, end_brace_pos - brace_pos);
        return SimpleJsonParser::extractStringValue(parent_content, child);
    }
    
    double extractDoubleValue(const std::string& json, const std::string& key) {
        return SimpleJsonParser::extractDoubleValue(json, key);
    }
    
    int extractIntValue(const std::string& json, const std::string& key) {
        return SimpleJsonParser::extractIntValue(json, key);
    }
    
    std::string extractStringValue(const std::string& json, const std::string& key) {
        return SimpleJsonParser::extractStringValue(json, key);
    }
    
    bool canRefresh() {
        time_t current_time = time(nullptr);
        return (current_time - last_refresh_time) >= MIN_REFRESH_INTERVAL;
    }
    
    void updateLocationAndWeather() {
        if (!canRefresh()) {
            std::cout << "Rate limited - please wait before refreshing" << std::endl;
            return;
        }
        
        std::cout << "Fetching fresh weather data..." << std::endl;
        
        // Always try to update location data on manual refresh
        std::string location_data = fetchLocationData();
        if (!location_data.empty()) {
            parseLocationData(location_data);
        }
        
        std::string weather_data = fetchWeatherData();
        if (!weather_data.empty()) {
            parseWeatherData(weather_data);
        }
        
        std::string astronomy_data = fetchAstronomyData();
        if (!astronomy_data.empty()) {
            parseAstronomyData(astronomy_data);
        }
        
        last_refresh_time = time(nullptr);
        std::cout << "Weather data updated successfully!" << std::endl;
    }

public:
    WeatherWidget() {
        weather.condition = "Loading...";
        weather.location = "Detecting location...";
        weather.temp_c = 0;
        weather.feels_like = 0;
        weather.humidity = 0;
        weather.wind_speed = 0;
        weather.pressure = 0;
        weather.uv_index = 0;
        weather.visibility = 0;
        weather.is_day = true;
        weather.sunrise = "--:--";
        weather.sunset = "--:--";
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
        g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), nullptr);

        gtk_widget_add_events(window, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);

        gtk_window_set_default_size(GTK_WINDOW(window), WIDGET_SIZE, WIDGET_SIZE);
        
        int x = SCREEN_WIDTH - WIDGET_SIZE - RIGHT_MARGIN;
        int y = TOP_MARGIN + 300;
        gtk_window_move(GTK_WINDOW(window), x, y);

        gtk_widget_show_all(window);

        updateLocationAndWeather();
        g_timeout_add(600000, (GSourceFunc)update_weather, this);

        gtk_main();
    }

    static void on_screen_changed(GtkWidget *widget, GdkScreen *old_screen, gpointer user_data) {
        GdkScreen *screen = gtk_widget_get_screen(widget);
        GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
        if (!visual) visual = gdk_screen_get_system_visual(screen);
        gtk_widget_set_visual(widget, visual);
    }

    static gboolean on_draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
        auto *self = static_cast<WeatherWidget*>(data);
        if (!self) return FALSE;

        GtkAllocation allocation;
        gtk_widget_get_allocation(widget, &allocation);
        int size = std::min(allocation.width, allocation.height);

        // Clear background
        cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
        cairo_paint(cr);
        cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

        cairo_set_antialias(cr, CAIRO_ANTIALIAS_SUBPIXEL);

        // Background with clean solid color (no gradient to avoid artifacts)
        self->drawRoundedRect(cr, 0, 0, size, size, CARD_RADIUS);
        cairo_set_source_rgba(cr, 0.08, 0.08, 0.09, 0.96);
        cairo_fill_preserve(cr);

        // Clean border
        cairo_set_source_rgba(cr, 0.25, 0.25, 0.25, 0.4);
        cairo_set_line_width(cr, 0.5);
        cairo_stroke(cr);

        if (!self->data_loaded) {
            PangoLayout *layout = pango_cairo_create_layout(cr);
            PangoFontDescription *desc = pango_font_description_new();
            pango_font_description_set_family(desc, "SF Pro Display");
            pango_font_description_set_weight(desc, PANGO_WEIGHT_NORMAL);
            pango_font_description_set_absolute_size(desc, 12 * PANGO_SCALE);
            pango_layout_set_font_description(layout, desc);

            pango_layout_set_text(layout, "Loading weather...", -1);
            
            int text_w, text_h;
            pango_layout_get_pixel_size(layout, &text_w, &text_h);

            cairo_set_source_rgba(cr, 0.9, 0.9, 0.9, 0.8);
            cairo_move_to(cr, (size - text_w) / 2, (size - text_h) / 2);
            pango_cairo_show_layout(cr, layout);

            g_object_unref(layout);
            pango_font_description_free(desc);
            return FALSE;
        }

        // Left side - Weather icon and main info
        self->drawWeatherIcon(cr, 16, 16, 40);

        PangoLayout *layout = pango_cairo_create_layout(cr);
        PangoFontDescription *desc = pango_font_description_new();
        
        // Location - top left below icon
        pango_font_description_set_family(desc, "SF Pro Display");
        pango_font_description_set_weight(desc, PANGO_WEIGHT_MEDIUM);
        pango_font_description_set_absolute_size(desc, 10 * PANGO_SCALE);
        pango_layout_set_font_description(layout, desc);

        pango_layout_set_text(layout, self->weather.location.c_str(), -1);
        cairo_set_source_rgba(cr, 0.9, 0.9, 0.9, 0.9);
        cairo_move_to(cr, 16, 62);
        pango_cairo_show_layout(cr, layout);

        // Temperature - large, left aligned
        pango_font_description_set_weight(desc, PANGO_WEIGHT_LIGHT);
        pango_font_description_set_absolute_size(desc, 26 * PANGO_SCALE);
        pango_layout_set_font_description(layout, desc);
        
        char temp_str[16];
        snprintf(temp_str, sizeof(temp_str), "%.1f°", self->weather.temp_c);
        pango_layout_set_text(layout, temp_str, -1);
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
        cairo_move_to(cr, 16, 80);
        pango_cairo_show_layout(cr, layout);

        // Condition - left aligned below temp
        pango_font_description_set_weight(desc, PANGO_WEIGHT_NORMAL);
        pango_font_description_set_absolute_size(desc, 10 * PANGO_SCALE);
        pango_layout_set_font_description(layout, desc);
        
        pango_layout_set_text(layout, self->weather.condition.c_str(), -1);
        cairo_set_source_rgba(cr, 0.85, 0.85, 0.85, 0.8);
        cairo_move_to(cr, 16, 115);
        pango_cairo_show_layout(cr, layout);

        // Right side info - cleaner typography
        pango_font_description_set_absolute_size(desc, 9 * PANGO_SCALE);
        pango_layout_set_font_description(layout, desc);
        cairo_set_source_rgba(cr, 0.85, 0.85, 0.85, 0.85);

        int right_x = size - 105; // Pushed slightly more left
        int y_start = 20;
        int line_height = 16;
        
        // Right column - better formatted with proper decimals
        char feels_str[20];
        snprintf(feels_str, sizeof(feels_str), "Feels like  %.1f°", self->weather.feels_like);
        pango_layout_set_text(layout, feels_str, -1);
        cairo_move_to(cr, right_x, y_start);
        pango_cairo_show_layout(cr, layout);

        char humidity_str[20];
        snprintf(humidity_str, sizeof(humidity_str), "Humidity  %d%%", self->weather.humidity);
        pango_layout_set_text(layout, humidity_str, -1);
        cairo_move_to(cr, right_x, y_start + line_height);
        pango_cairo_show_layout(cr, layout);

        char wind_str[20];
        snprintf(wind_str, sizeof(wind_str), "Wind  %.1f km/h", self->weather.wind_speed);
        pango_layout_set_text(layout, wind_str, -1);
        cairo_move_to(cr, right_x, y_start + 2 * line_height);
        pango_cairo_show_layout(cr, layout);

        char pressure_str[20];
        snprintf(pressure_str, sizeof(pressure_str), "Pressure  %.1f kPa", self->weather.pressure * 0.1);
        pango_layout_set_text(layout, pressure_str, -1);
        cairo_move_to(cr, right_x, y_start + 3 * line_height);
        pango_cairo_show_layout(cr, layout);

        char uv_str[20];
        snprintf(uv_str, sizeof(uv_str), "UV Index  %.1f", self->weather.uv_index);
        pango_layout_set_text(layout, uv_str, -1);
        cairo_move_to(cr, right_x, y_start + 4 * line_height);
        pango_cairo_show_layout(cr, layout);

        char vis_str[20];
        snprintf(vis_str, sizeof(vis_str), "Visibility  %d km", self->weather.visibility);
        pango_layout_set_text(layout, vis_str, -1);
        cairo_move_to(cr, right_x, y_start + 5 * line_height);
        pango_cairo_show_layout(cr, layout);

        // Sunrise/Sunset - bottom right
        char sunrise_str[20];
        snprintf(sunrise_str, sizeof(sunrise_str), "Sunrise  %s", self->weather.sunrise.c_str());
        pango_layout_set_text(layout, sunrise_str, -1);
        cairo_move_to(cr, right_x, y_start + 6 * line_height + 8);
        pango_cairo_show_layout(cr, layout);

        char sunset_str[20];
        snprintf(sunset_str, sizeof(sunset_str), "Sunset  %s", self->weather.sunset.c_str());
        pango_layout_set_text(layout, sunset_str, -1);
        cairo_move_to(cr, right_x, y_start + 7 * line_height + 8);
        pango_cairo_show_layout(cr, layout);

        // Draw refresh button (small clock on bottom left)
        self->drawRefreshButton(cr, 12, size - 28);

        g_object_unref(layout);
        pango_font_description_free(desc);

        return FALSE;
    }

    static gboolean on_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
        auto *self = static_cast<WeatherWidget*>(user_data);
        if (!self) return FALSE;
        
        if (event->button == 1) {
            // Check if refresh button was clicked
            if (self->isPointInRefreshButton((int)event->x, (int)event->y)) {
                if (self->canRefresh()) {
                    std::cout << "Manually refreshing weather data..." << std::endl;
                    self->updateLocationAndWeather();
                    gtk_widget_queue_draw(self->window);
                } else {
                    std::cout << "Please wait before refreshing (2 minute cooldown)" << std::endl;
                }
                return TRUE;
            }
            
            // Otherwise handle window dragging (but don't auto-refresh on drag)
            if (event->type == GDK_BUTTON_PRESS) {
                gtk_window_begin_move_drag(GTK_WINDOW(self->window),
                                         event->button,
                                         (int)event->x_root,
                                         (int)event->y_root,
                                         event->time);
            }
        }
        return TRUE;
    }

    static gboolean update_weather(gpointer data) {
        auto *self = static_cast<WeatherWidget*>(data);
        if (!self) return FALSE;
        
        self->updateLocationAndWeather();
        if (self->window) {
            gtk_widget_queue_draw(self->window);
        }
        return TRUE;
    }
};

int main(int argc, char** argv) {
    WeatherWidget widget;
    widget.run();
    return 0;
}
