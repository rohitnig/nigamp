#include "player_gui.hpp"

#include <FL/Fl.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_RGB_Image.H>
#include <FL/fl_draw.H>
#include <FL/platform.H>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace nigamp {

namespace {

using SteadyClock = std::chrono::steady_clock;

const Fl_Color COLOR_BG = fl_rgb_color(0x15, 0x16, 0x1B);
const Fl_Color COLOR_TEXT = fl_rgb_color(0xEC, 0xEC, 0xF1);
const Fl_Color COLOR_MUTED = fl_rgb_color(0x8A, 0x8D, 0x98);
const Fl_Color COLOR_TRACK = fl_rgb_color(0x2C, 0x2E, 0x37);
const Fl_Color COLOR_HOVER = fl_rgb_color(0x24, 0x26, 0x2E);
const Fl_Color COLOR_ACCENT = fl_rgb_color(0xF5, 0xA5, 0x24);
const Fl_Color COLOR_ACCENT_HOT = fl_rgb_color(0xFF, 0xBF, 0x50);

constexpr int WINDOW_W = 440;
constexpr int WINDOW_H = 116;
constexpr int MIN_W = 340;
constexpr int COMPACT_H = 34;
constexpr int MIN_COMPACT_W = 260;
constexpr int PAD = 18;
constexpr double VOLUME_STEP_EPSILON = 0.005;
constexpr auto PROGRESS_REDRAW_INTERVAL = std::chrono::milliseconds(250);
constexpr auto VOLUME_FLASH_DURATION = std::chrono::milliseconds(1500);

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
    bool contains(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

// Everything the window shows; written by any thread, copied out under the mutex to draw
struct ViewState {
    std::string title;
    PlaybackStatus status = PlaybackStatus::STOPPED;
    double volume = 0.8;
    double played = 0.0;
    double total = 0.0;
    SteadyClock::time_point position_time = SteadyClock::now();
    bool preview = false;
};

std::string format_time(double seconds) {
    const int total = std::max(0, static_cast<int>(seconds));
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%d:%02d", total / 60, total % 60);
    return buffer;
}

// Trims UTF-8 text to max_width pixels in the current font, ending with an ellipsis
std::string fit_text(const std::string& text, int max_width) {
    if (fl_width(text.c_str()) <= max_width) {
        return text;
    }
    const std::string ellipsis = "\xE2\x80\xA6";
    std::string out = text;
    while (!out.empty()) {
        out.pop_back();
        while (!out.empty() && (static_cast<unsigned char>(out.back()) & 0xC0) == 0x80) {
            out.pop_back();
        }
        if (!out.empty() && (static_cast<unsigned char>(out.back()) & 0xC0) == 0xC0) {
            out.pop_back();  // drop a lead byte whose continuation bytes were removed
        }
        if (fl_width((out + ellipsis).c_str()) <= max_width) {
            break;
        }
    }
    return out + ellipsis;
}

void fill_circle(int cx, int cy, int r) {
    // A zero-size pie leaves FLTK's Cairo context in an error state, after which every
    // later drawing call in the window is silently ignored
    if (r <= 0) return;
    fl_pie(cx - r, cy - r, 2 * r, 2 * r, 0, 360);
}

// Horizontal bar with round caps
void fill_pill(int x, int y, int w, int h) {
    if (w <= 0) return;
    w = std::max(w, h);  // a fill shorter than the bar's thickness shows as a round dot
    const int r = h / 2;
    fl_rectf(x + r, y, w - 2 * r, h);
    fill_circle(x + r, y + r, r);
    fill_circle(x + w - r, y + r, r);
}

void draw_play_icon(int cx, int cy, int s) {
    fl_polygon(cx - s / 2 + 1, cy - s * 3 / 5, cx - s / 2 + 1, cy + s * 3 / 5, cx + s * 3 / 5 + 1, cy);
}

void draw_pause_icon(int cx, int cy, int s) {
    const int bar = std::max(2, s / 3);
    fl_rectf(cx - bar - 2, cy - s / 2, bar, s);
    fl_rectf(cx + 2, cy - s / 2, bar, s);
}

// Skip icon: a bar plus a triangle, pointing right (next) or left (previous)
void draw_skip_icon(int cx, int cy, int s, bool forward) {
    const int half = s / 2;
    const int dir = forward ? 1 : -1;
    const int tip = cx + dir * half;
    fl_polygon(cx - dir * half, cy - half, cx - dir * half, cy + half, tip, cy);
    const int bar_x = forward ? tip : tip - 2;
    fl_rectf(bar_x, cy - half, 2, s);
}

void draw_speaker_icon(int x, int cy, double volume) {
    fl_rectf(x, cy - 3, 4, 6);
    fl_polygon(x + 3, cy - 3, x + 9, cy - 7, x + 9, cy + 7, x + 3, cy + 3);
    fl_line_style(FL_SOLID | FL_CAP_ROUND, 2);
    if (volume <= 0.0) {
        fl_line(x + 12, cy - 3, x + 17, cy + 3);
        fl_line(x + 12, cy + 3, x + 17, cy - 3);
    } else {
        fl_arc(x + 5, cy - 5, 10, 10, -50, 50);
        if (volume > 0.5) {
            fl_arc(x + 4, cy - 9, 18, 18, -50, 50);
        }
    }
    fl_line_style(0);
}

// Window/dock icon: an amber disc with a dark play triangle, 4x supersampled for smooth edges
Fl_RGB_Image make_app_icon(std::vector<unsigned char>& pixels) {
    constexpr int SIZE = 64;
    constexpr int SAMPLES = 4;
    const double c = SIZE / 2.0;
    const double radius = SIZE / 2.0 - 1;
    auto in_triangle = [](double x, double y) {
        // Play triangle: left edge at x=24, tip at (46, 32), half-height 13
        if (x < 24 || x > 46) return false;
        const double half = 13.0 * (46 - x) / 22.0;
        return y > 32 - half && y < 32 + half;
    };
    pixels.assign(SIZE * SIZE * 4, 0);
    for (int py = 0; py < SIZE; ++py) {
        for (int px = 0; px < SIZE; ++px) {
            int disc = 0, glyph = 0;
            for (int sy = 0; sy < SAMPLES; ++sy) {
                for (int sx = 0; sx < SAMPLES; ++sx) {
                    const double x = px + (sx + 0.5) / SAMPLES;
                    const double y = py + (sy + 0.5) / SAMPLES;
                    if ((x - c) * (x - c) + (y - c) * (y - c) <= radius * radius) {
                        ++disc;
                        glyph += in_triangle(x, y) ? 1 : 0;
                    }
                }
            }
            const double total = SAMPLES * SAMPLES;
            const double g = disc ? static_cast<double>(glyph) / disc : 0.0;
            unsigned char* out = &pixels[(py * SIZE + px) * 4];
            out[0] = static_cast<unsigned char>(0xF5 * (1 - g) + 0x15 * g);
            out[1] = static_cast<unsigned char>(0xA5 * (1 - g) + 0x16 * g);
            out[2] = static_cast<unsigned char>(0x24 * (1 - g) + 0x1B * g);
            out[3] = static_cast<unsigned char>(255 * disc / total);
        }
    }
    return Fl_RGB_Image(pixels.data(), SIZE, SIZE, 4);
}

// Thumbtack: head, collar, and needle
void draw_pin_icon(int cx, int cy) {
    fl_rectf(cx - 3, cy - 7, 6, 6);
    fl_rectf(cx - 6, cy - 1, 12, 2);
    fl_rectf(cx - 1, cy + 1, 2, 7);
}

// Whether the window system lets us keep the window above others. GNOME's Wayland
// compositor has no protocol for it, which is why the GUI prefers XWayland.
bool can_stay_on_top() {
#if defined(_WIN32)
    return true;
#elif defined(FLTK_USE_X11)
    return fl_x11_display() != nullptr;
#else
    return false;
#endif
}

void set_stay_on_top(Fl_Window* window, bool on_top) {
#if defined(_WIN32)
    SetWindowPos(fl_win32_xid(window), on_top ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
#elif defined(FLTK_USE_X11)
    Display* display = fl_x11_display();
    if (!display) {
        return;
    }
    // EWMH: ask the window manager to add/remove _NET_WM_STATE_ABOVE
    XEvent event{};
    event.xclient.type = ClientMessage;
    event.xclient.window = fl_x11_xid(window);
    event.xclient.message_type = XInternAtom(display, "_NET_WM_STATE", False);
    event.xclient.format = 32;
    event.xclient.data.l[0] = on_top ? 1 : 0;  // _NET_WM_STATE_ADD / _NET_WM_STATE_REMOVE
    event.xclient.data.l[1] = static_cast<long>(XInternAtom(display, "_NET_WM_STATE_ABOVE", False));
    event.xclient.data.l[3] = 1;  // source indication: normal application
    XSendEvent(display, DefaultRootWindow(display), False,
               SubstructureRedirectMask | SubstructureNotifyMask, &event);
    XFlush(display);
#else
    (void)window;
    (void)on_top;
#endif
}

// Window mode, placement and stay-on-top, remembered between runs
struct GuiSettings {
    bool compact = false;
    bool on_top = false;  // stay-on-top in the full view (the compact bar is always on top)
    bool has_position = false;
    int x = 0;
    int y = 0;
    int w = WINDOW_W;
};

std::filesystem::path settings_path() {
#if defined(_WIN32)
    const char* base = std::getenv("APPDATA");
    return base ? std::filesystem::path(base) / "nigamp" / "gui.conf" : std::filesystem::path();
#else
    if (const char* config = std::getenv("XDG_CONFIG_HOME"); config && *config) {
        return std::filesystem::path(config) / "nigamp" / "gui.conf";
    }
    const char* home = std::getenv("HOME");
    return home ? std::filesystem::path(home) / ".config" / "nigamp" / "gui.conf" : std::filesystem::path();
#endif
}

GuiSettings load_settings() {
    GuiSettings settings;
    const auto path = settings_path();
    if (path.empty()) {
        return settings;
    }
    std::ifstream in(path);
    std::string key;
    int value = 0;
    bool have_x = false;
    bool have_y = false;
    while (in >> key >> value) {
        if (key == "compact") {
            settings.compact = value != 0;
        } else if (key == "on_top") {
            settings.on_top = value != 0;
        } else if (key == "x") {
            settings.x = value;
            have_x = true;
        } else if (key == "y") {
            settings.y = value;
            have_y = true;
        } else if (key == "width") {
            settings.w = std::max(value, MIN_COMPACT_W);
        }
    }
    settings.has_position = have_x && have_y;
    return settings;
}

void save_settings(const GuiSettings& settings) {
    const auto path = settings_path();
    if (path.empty()) {
        return;
    }
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream out(path);
    out << "compact " << settings.compact << "\n"
        << "on_top " << settings.on_top << "\n"
        << "x " << settings.x << "\n"
        << "y " << settings.y << "\n"
        << "width " << settings.w << "\n";
}

// Guards against restoring onto a monitor that is no longer connected
bool point_on_any_screen(int x, int y) {
    for (int i = 0; i < Fl::screen_count(); ++i) {
        int sx = 0, sy = 0, sw = 0, sh = 0;
        Fl::screen_xywh(sx, sy, sw, sh, i);
        if (x >= sx && x < sx + sw && y >= sy && y < sy + sh) {
            return true;
        }
    }
    return false;
}

// Two layouts: the full mini-player, and a borderless always-on-top bar (double-click
// empty space or press C to switch). Dragging empty space moves the window.
class MiniPlayerWindow : public Fl_Double_Window {
public:
    MiniPlayerWindow(std::mutex& mutex, const ViewState& state)
        : Fl_Double_Window(WINDOW_W, WINDOW_H, "nigamp"), m_mutex(mutex), m_state(state) {
        color(COLOR_BG);
        size_range(MIN_W, WINDOW_H, 0, WINDOW_H);
        end();
    }

    HotkeyCallback on_command;
    VolumeCallback on_volume;

    bool compact() const { return m_compact; }
    bool full_on_top() const { return m_full_on_top; }
    void set_full_on_top(bool on_top) { m_full_on_top = on_top; }

    void set_compact(bool compact) {
        m_compact = compact;
        if (compact) {
            border(0);
            size_range(MIN_COMPACT_W, COMPACT_H, 0, COMPACT_H);
            resize(x(), y(), w(), COMPACT_H);
        } else {
            border(1);
            size_range(MIN_W, WINDOW_H, 0, WINDOW_H);
            resize(x(), y(), std::max(w(), MIN_W), WINDOW_H);
        }
        apply_on_top();
        set_hover(Hit::NONE);
        redraw();
    }

    // (Re)sends the stay-on-top state; also called shortly after the window is first mapped
    void apply_on_top() {
        if (shown()) {
            set_stay_on_top(this, m_compact || m_full_on_top);
        }
    }

    // True while something on screen changes without a state update (the volume readout)
    bool animating() const { return SteadyClock::now() < m_volume_flash_until; }

    void draw() override {
        ViewState s;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            s = m_state;
        }
        if (m_dragging_volume) {
            s.volume = m_drag_volume;
        }
        if (m_last_volume >= 0 && std::abs(s.volume - m_last_volume) > 0.001) {
            m_volume_flash_until = SteadyClock::now() + VOLUME_FLASH_DURATION;
        }
        m_last_volume = s.volume;

        // Between position updates, extrapolate while playing so progress moves smoothly
        double played = s.played;
        if (s.status == PlaybackStatus::PLAYING) {
            played += std::chrono::duration<double>(SteadyClock::now() - s.position_time).count();
        }
        played = std::clamp(played, 0.0, std::max(0.0, s.total));
        const double fraction = s.total > 0 ? played / s.total : 0.0;

        layout();
        fl_color(COLOR_BG);
        fl_rectf(0, 0, w(), h());
        if (m_compact) {
            draw_compact(s, played, fraction);
        } else {
            draw_full(s, played, fraction);
        }
    }

    int handle(int event) override {
        switch (event) {
            case FL_ENTER:
            case FL_MOVE:
                set_hover(hit_test(Fl::event_x(), Fl::event_y()));
                return 1;
            case FL_LEAVE:
                set_hover(Hit::NONE);
                return 1;
            case FL_PUSH: {
                const Hit hit = hit_test(Fl::event_x(), Fl::event_y());
                if (hit == Hit::NONE) {
                    if (Fl::event_clicks()) {
                        Fl::event_clicks(0);
                        set_compact(!m_compact);
                    } else {
                        m_moving = true;
                        m_move_dx = Fl::event_x_root() - x();
                        m_move_dy = Fl::event_y_root() - y();
                    }
                } else if (hit == Hit::VOLUME) {
                    m_dragging_volume = true;
                    drag_volume_to(Fl::event_x());
                } else if (hit == Hit::PIN) {
                    toggle_on_top();
                } else if (hit == Hit::PREV) {
                    send(HotkeyAction::PREVIOUS_TRACK);
                } else if (hit == Hit::PLAY) {
                    send(HotkeyAction::PAUSE_RESUME);
                } else if (hit == Hit::NEXT) {
                    send(HotkeyAction::NEXT_TRACK);
                }
                return 1;
            }
            case FL_DRAG:
                if (m_moving) {
                    position(Fl::event_x_root() - m_move_dx, Fl::event_y_root() - m_move_dy);
                } else if (m_dragging_volume) {
                    drag_volume_to(Fl::event_x());
                }
                return 1;
            case FL_RELEASE:
                m_moving = false;
                if (m_dragging_volume) {
                    m_dragging_volume = false;
                    set_hover(hit_test(Fl::event_x(), Fl::event_y()));
                    redraw();
                }
                return 1;
            case FL_MOUSEWHEEL:
                if (Fl::event_dy() != 0) {
                    send(Fl::event_dy() < 0 ? HotkeyAction::VOLUME_UP : HotkeyAction::VOLUME_DOWN);
                }
                return 1;
            case FL_KEYBOARD:
            case FL_SHORTCUT:  // the window has no focusable children, so keys arrive as shortcuts
                return handle_key();
            case FL_FOCUS:
            case FL_UNFOCUS:
                return 1;
        }
        return Fl_Double_Window::handle(event);
    }

private:
    enum class Hit { NONE, PREV, PLAY, NEXT, VOLUME, PIN };

    std::mutex& m_mutex;
    const ViewState& m_state;

    Rect m_progress, m_prev, m_play, m_next, m_volume_slider, m_pin;
    bool m_compact = false;
    bool m_full_on_top = false;
    Hit m_hover = Hit::NONE;
    bool m_dragging_volume = false;
    double m_drag_volume = 0.0;
    bool m_moving = false;
    int m_move_dx = 0;
    int m_move_dy = 0;
    double m_last_volume = -1.0;
    SteadyClock::time_point m_volume_flash_until{};

    static int center_x(const Rect& r) { return r.x + r.w / 2; }
    static int center_y(const Rect& r) { return r.y + r.h / 2; }

    void draw_full(const ViewState& s, double played, double fraction) {
        // Title row, with the pin and a status pill on the right
        const char* pill = nullptr;
        Fl_Color pill_color = COLOR_MUTED;
        if (s.status == PlaybackStatus::PAUSED) {
            pill = "PAUSED";
        } else if (s.preview) {
            pill = "PREVIEW";
            pill_color = COLOR_ACCENT;
        }
        int title_right = w() - PAD;
        if (m_pin.w > 0) {
            draw_button_hover(m_pin, Hit::PIN);
            fl_color(m_full_on_top ? COLOR_ACCENT : (m_hover == Hit::PIN ? COLOR_TEXT : COLOR_MUTED));
            draw_pin_icon(center_x(m_pin), center_y(m_pin));
            title_right = m_pin.x - 6;
        }
        if (pill) {
            fl_font(FL_HELVETICA_BOLD, 10);
            const int pw = static_cast<int>(fl_width(pill)) + 14;
            const int px = title_right - pw;
            fl_color(COLOR_TRACK);
            fill_pill(px, 18, pw, 18);
            fl_color(pill_color);
            fl_draw(pill, px + 7, 31);
            title_right = px - 10;
        }
        fl_font(FL_HELVETICA_BOLD, 15);
        fl_color(COLOR_TEXT);
        fl_draw(fit_text(display_title(s), title_right - PAD).c_str(), PAD, 32);

        fl_color(COLOR_TRACK);
        fill_pill(m_progress.x, m_progress.y, m_progress.w, m_progress.h);
        fl_color(COLOR_ACCENT);
        fill_pill(m_progress.x, m_progress.y, static_cast<int>(m_progress.w * fraction), m_progress.h);

        fl_font(FL_HELVETICA, 11);
        fl_color(COLOR_MUTED);
        fl_draw(format_time(played).c_str(), PAD, 66);
        const std::string total = s.total > 0 ? format_time(s.total) : std::string("--:--");
        fl_draw(total.c_str(), w() - PAD - static_cast<int>(fl_width(total.c_str())), 66);

        draw_transport(s, 10, 12);

        // Volume
        const bool volume_hot = m_hover == Hit::VOLUME || m_dragging_volume;
        fl_color(volume_hot ? COLOR_TEXT : COLOR_MUTED);
        draw_speaker_icon(m_volume_slider.x - 30, center_y(m_volume_slider), s.volume);

        const int bar_y = center_y(m_volume_slider) - 2;
        fl_color(COLOR_TRACK);
        fill_pill(m_volume_slider.x, bar_y, m_volume_slider.w, 4);
        const int filled = static_cast<int>(m_volume_slider.w * s.volume);
        fl_color(volume_hot ? COLOR_ACCENT : COLOR_TEXT);
        fill_pill(m_volume_slider.x, bar_y, filled, 4);
        if (volume_hot) {
            fl_color(COLOR_TEXT);
            fill_circle(m_volume_slider.x + filled, bar_y + 2, 6);
        }

        fl_font(FL_HELVETICA, 11);
        fl_color(COLOR_MUTED);
        fl_draw(volume_text(s.volume).c_str(), w() - PAD - static_cast<int>(fl_width("100%")),
                center_y(m_volume_slider) + 4);
    }

    void draw_compact(const ViewState& s, double played, double fraction) {
        // Hairline border: the bar has no window frame or shadow to separate it from what's behind
        fl_color(COLOR_TRACK);
        fl_rect(0, 0, w(), h());

        draw_transport(s, 8, 10);

        // Right: time, or the volume for a moment after it changes
        std::string right;
        if (animating()) {
            right = "Vol " + volume_text(s.volume);
        } else {
            right = format_time(played) + " / " + (s.total > 0 ? format_time(s.total) : std::string("--:--"));
        }
        fl_font(FL_HELVETICA, 11);
        const int right_w = static_cast<int>(fl_width(right.c_str()));
        fl_color(COLOR_MUTED);
        fl_draw(right.c_str(), w() - 10 - right_w, 21);

        const int title_x = m_next.x + m_next.w + 8;
        const int title_w = w() - 10 - right_w - 12 - title_x;
        if (title_w > 20) {
            fl_font(FL_HELVETICA_BOLD, 12);
            fl_color(s.status == PlaybackStatus::PAUSED ? COLOR_MUTED : COLOR_TEXT);
            fl_draw(fit_text(display_title(s), title_w).c_str(), title_x, 21);
        }

        // Progress as a thin line along the bottom edge
        fl_color(COLOR_TRACK);
        fl_rectf(0, h() - 2, w(), 2);
        const int filled = static_cast<int>(w() * fraction);
        if (filled > 0) {
            fl_color(COLOR_ACCENT);
            fl_rectf(0, h() - 2, filled, 2);
        }
    }

    // Previous / play-pause / next, sized for the current layout
    void draw_transport(const ViewState& s, int skip_size, int play_size) {
        draw_button_hover(m_prev, Hit::PREV);
        fl_color(m_hover == Hit::PREV ? COLOR_TEXT : COLOR_MUTED);
        draw_skip_icon(center_x(m_prev), center_y(m_prev), skip_size, false);

        fl_color(m_hover == Hit::PLAY ? COLOR_ACCENT_HOT : COLOR_ACCENT);
        fill_circle(center_x(m_play), center_y(m_play), m_play.w / 2);
        fl_color(COLOR_BG);
        if (s.status == PlaybackStatus::PLAYING) {
            draw_pause_icon(center_x(m_play), center_y(m_play), play_size);
        } else {
            draw_play_icon(center_x(m_play), center_y(m_play), play_size);
        }

        draw_button_hover(m_next, Hit::NEXT);
        fl_color(m_hover == Hit::NEXT ? COLOR_TEXT : COLOR_MUTED);
        draw_skip_icon(center_x(m_next), center_y(m_next), skip_size, true);
    }

    static std::string display_title(const ViewState& s) {
        return s.title.empty() ? std::string("Nothing playing") : s.title;
    }

    static std::string volume_text(double volume) {
        char percent[8];
        std::snprintf(percent, sizeof(percent), "%d%%", static_cast<int>(volume * 100 + 0.5));
        return percent;
    }

    void layout() {
        if (m_compact) {
            m_prev = {4, 5, 24, 24};
            m_play = {30, 4, 26, 26};
            m_next = {58, 5, 24, 24};
            m_progress = m_volume_slider = m_pin = Rect{};
            return;
        }
        m_progress = {PAD, 44, w() - 2 * PAD, 5};
        const int controls_y = 90;
        m_prev = {PAD - 6, controls_y - 16, 32, 32};
        m_play = {m_prev.x + m_prev.w + 6, controls_y - 18, 36, 36};
        m_next = {m_play.x + m_play.w + 6, controls_y - 16, 32, 32};
        const int slider_w = std::clamp(w() / 4, 70, 140);
        // The slider's hit area is taller than the 4px bar it draws
        m_volume_slider = {w() - PAD - 40 - slider_w, controls_y - 10, slider_w, 20};
        m_pin = can_stay_on_top() ? Rect{w() - PAD - 22, 14, 26, 26} : Rect{};
    }

    void draw_button_hover(const Rect& r, Hit which) {
        if (m_hover == which) {
            fl_color(COLOR_HOVER);
            fill_circle(center_x(r), center_y(r), r.w / 2);
        }
    }

    Hit hit_test(int x, int y) {
        layout();
        if (m_pin.contains(x, y)) return Hit::PIN;
        if (m_prev.contains(x, y)) return Hit::PREV;
        if (m_play.contains(x, y)) return Hit::PLAY;
        if (m_next.contains(x, y)) return Hit::NEXT;
        if (m_volume_slider.w > 0) {
            // Include the speaker icon and a little slack past the ends of the slider
            const Rect volume_area{m_volume_slider.x - 34, m_volume_slider.y, m_volume_slider.w + 42,
                                   m_volume_slider.h};
            if (volume_area.contains(x, y)) return Hit::VOLUME;
        }
        return Hit::NONE;
    }

    void set_hover(Hit hit) {
        if (hit != m_hover) {
            m_hover = hit;
            redraw();
        }
        // Empty space moves the window, which is the only way to move the borderless bar
        cursor(hit != Hit::NONE ? FL_CURSOR_HAND : (m_compact ? FL_CURSOR_MOVE : FL_CURSOR_DEFAULT));
    }

    void drag_volume_to(int x) {
        const double volume = std::clamp(static_cast<double>(x - m_volume_slider.x) / m_volume_slider.w, 0.0, 1.0);
        if (std::abs(volume - m_drag_volume) < VOLUME_STEP_EPSILON && m_dragging_volume) {
            return;
        }
        m_drag_volume = volume;
        redraw();
        if (on_volume) {
            on_volume(volume);
        }
    }

    void toggle_on_top() {
        m_full_on_top = !m_full_on_top;
        apply_on_top();
        redraw();
    }

    void send(HotkeyAction action) {
        if (on_command) {
            on_command(action);
        }
    }

    int handle_key() {
        const int key = Fl::event_key();
        const char* text = Fl::event_text();
        const char c = text && text[0] ? text[0] : '\0';
        if (key == ' ' || c == 'r' || c == 'R' || c == 'k') {
            send(HotkeyAction::PAUSE_RESUME);
        } else if (key == FL_Right || c == 'n' || c == 'N') {
            send(HotkeyAction::NEXT_TRACK);
        } else if (key == FL_Left || c == 'p' || c == 'P') {
            send(HotkeyAction::PREVIOUS_TRACK);
        } else if (key == FL_Up || c == '+' || c == '=') {
            send(HotkeyAction::VOLUME_UP);
        } else if (key == FL_Down || c == '-') {
            send(HotkeyAction::VOLUME_DOWN);
        } else if (c == 'c' || c == 'C') {
            set_compact(!m_compact);
        } else if ((c == 't' || c == 'T') && !m_compact && can_stay_on_top()) {
            toggle_on_top();
        } else if (c == 'q' || c == 'Q') {
            send(HotkeyAction::QUIT);
        } else {
            return 0;  // let Escape reach the window callback
        }
        return 1;
    }
};

class FltkPlayerGui : public IPlayerGui {
public:
    ~FltkPlayerGui() override { shutdown(); }

    bool initialize(HotkeyCallback callback) override {
#if !defined(_WIN32)
        // FLTK exits the process if it cannot open a display, so check first
        if (!std::getenv("WAYLAND_DISPLAY") && !std::getenv("DISPLAY")) {
            return false;
        }
        // Prefer XWayland over native Wayland: GNOME gives Wayland clients no way to stay
        // on top, and its server-side decorations make the window behave like any other.
        // FLTK_BACKEND=wayland in the environment still selects native Wayland.
        if (std::getenv("DISPLAY")) {
            setenv("FLTK_BACKEND", "x11", 0);
        }
#endif
        m_window = new MiniPlayerWindow(m_mutex, m_state);
        m_window->on_command = callback;
        m_window->on_volume = m_volume_callback;
        m_window->xclass("nigamp");
        std::vector<unsigned char> icon_pixels;
        const Fl_RGB_Image icon = make_app_icon(icon_pixels);
        m_window->icon(&icon);  // copied by FLTK
        // Closing the window (or pressing Escape) quits the player
        m_window->callback([](Fl_Widget*, void* data) {
            auto* window = static_cast<MiniPlayerWindow*>(data);
            if (window->on_command) {
                window->on_command(HotkeyAction::QUIT);
            }
        }, m_window);

        const GuiSettings settings = load_settings();
        m_window->set_full_on_top(settings.on_top);
        if (settings.compact) {
            m_window->set_compact(true);
        }
        m_window->size(std::max(settings.w, settings.compact ? MIN_COMPACT_W : MIN_W), m_window->h());
        if (settings.has_position && point_on_any_screen(settings.x + 20, settings.y + 10)) {
            m_window->position(settings.x, settings.y);
        }
        m_window->show();
        // The window manager ignores stay-on-top requests until it has mapped the window
        Fl::add_timeout(0.3, apply_on_top_after_map, m_window);
        return true;
    }

    void shutdown() override {
        if (m_window) {
            Fl::remove_timeout(apply_on_top_after_map, m_window);
            save_settings({m_window->compact(), m_window->full_on_top(), true, m_window->x(), m_window->y(),
                           m_window->w()});
            m_window->hide();
            delete m_window;
            m_window = nullptr;
            Fl::check();
        }
    }

    void set_track(const TrackInfo& track) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_state.title = track.title;
        m_state.played = 0.0;
        m_state.total = track.duration_seconds;
        m_state.position_time = SteadyClock::now();
        m_dirty = true;
        m_title_changed = true;
    }

    void set_playback_status(PlaybackStatus status) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (status != m_state.status) {
            // Restart extrapolation from now so a resume doesn't jump ahead by the paused time
            m_state.position_time = SteadyClock::now();
        }
        m_state.status = status;
        m_dirty = true;
    }

    void set_volume(double volume) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_state.volume = std::clamp(volume, 0.0, 1.0);
        m_dirty = true;
    }

    void set_position(double played_seconds, double total_seconds) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_state.played = played_seconds;
        m_state.total = total_seconds;
        m_state.position_time = SteadyClock::now();
        m_dirty = true;
    }

    void set_preview_mode(bool preview) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_state.preview = preview;
        m_dirty = true;
    }

    void set_volume_callback(VolumeCallback callback) override {
        m_volume_callback = std::move(callback);
        if (m_window) {
            m_window->on_volume = m_volume_callback;
        }
    }

    void pump(double timeout_seconds) override {
        if (!m_window) {
            std::this_thread::sleep_for(std::chrono::duration<double>(timeout_seconds));
            return;
        }
        Fl::wait(timeout_seconds);
        if (!m_window) {
            return;  // a command handled during wait() shut the GUI down
        }

        bool redraw_needed = false;
        bool playing = false;
        std::string window_title;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            redraw_needed = m_dirty;
            m_dirty = false;
            playing = m_state.status == PlaybackStatus::PLAYING;
            if (m_title_changed) {
                m_title_changed = false;
                window_title = m_state.title.empty() ? "nigamp" : m_state.title + " - nigamp";
            }
        }
        if (!window_title.empty()) {
            m_window->copy_label(window_title.c_str());
        }
        // While playing, the progress bar advances between position updates. Throttle that:
        // every redraw produces X events that wake Fl::wait(), so redrawing on each pump spins.
        const auto now = SteadyClock::now();
        const bool animating = m_window->animating();
        // One more redraw when an animation ends, so e.g. the volume readout clears while paused
        redraw_needed = redraw_needed || (m_was_animating && !animating);
        m_was_animating = animating;
        if (redraw_needed || ((playing || animating) && now - m_last_redraw >= PROGRESS_REDRAW_INTERVAL)) {
            m_window->redraw();
            m_last_redraw = now;
        }
    }

private:
    static void apply_on_top_after_map(void* window) {
        static_cast<MiniPlayerWindow*>(window)->apply_on_top();
    }

    std::mutex m_mutex;
    ViewState m_state;
    bool m_dirty = true;
    bool m_title_changed = false;
    MiniPlayerWindow* m_window = nullptr;
    VolumeCallback m_volume_callback;
    SteadyClock::time_point m_last_redraw{};
    bool m_was_animating = false;
};

}

std::unique_ptr<IPlayerGui> create_player_gui() {
    return std::make_unique<FltkPlayerGui>();
}

}
