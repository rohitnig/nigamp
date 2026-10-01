#include "hotkey_handler.hpp"
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <thread>
#include <atomic>
#include <iostream>
#include <chrono>
#include <cstdlib>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <sys/select.h>

namespace nigamp {

namespace {

struct KeyBinding {
    KeySym keysym;
    HotkeyAction action;
    const char* label;
    bool also_with_shift;  // e.g. "Plus" is Shift+= on most layouts
};

// All global hotkeys are Ctrl+Alt+<key>. Keypad +/- are deliberately absent: X servers
// reserve Ctrl+Alt+KP_Add/KP_Subtract for video-mode switching (XF86Next/Prev_VMode).
const KeyBinding kBindings[] = {
    {XK_n,           HotkeyAction::NEXT_TRACK,     "Ctrl+Alt+N",        false},
    {XK_p,           HotkeyAction::PREVIOUS_TRACK, "Ctrl+Alt+P",        false},
    {XK_r,           HotkeyAction::PAUSE_RESUME,   "Ctrl+Alt+R",        false},
    {XK_equal,       HotkeyAction::VOLUME_UP,      "Ctrl+Alt+Plus",     true},
    {XK_minus,       HotkeyAction::VOLUME_DOWN,    "Ctrl+Alt+Minus",    false},
    {XK_Escape,      HotkeyAction::QUIT,           "Ctrl+Alt+Escape",   false},
};

constexpr unsigned int kHotkeyMods = ControlMask | Mod1Mask;

// XGrabKey reports failures (e.g. BadAccess when another client owns the combo)
// asynchronously through the error handler, not through its return value.
std::atomic<bool> g_grab_failed{false};

int grab_error_handler(Display*, XErrorEvent* error) {
    if (error->request_code == 33 /* X_GrabKey */) {
        g_grab_failed = true;
    }
    return 0;
}

}  // namespace

struct LinuxHotkeyHandler::Impl {
    Display* display = nullptr;
    // Grabs go on the root window so they fire regardless of which window has focus
    Window root = 0;
    unsigned int numlock_mask = 0;
    bool hotkeys_grabbed = false;
    HotkeyCallback callback;
    std::thread message_thread;
    std::thread console_input_thread;
    std::atomic<bool> should_stop{false};
    struct termios original_termios;
    bool terminal_configured = false;
    bool x11_available = false;

    bool create_display() {
        display = XOpenDisplay(nullptr);
        if (!display) {
            return false;
        }
        root = DefaultRootWindow(display);
        numlock_mask = find_numlock_mask();
        return true;
    }

    // NumLock is usually Mod2, but the modifier map is the only reliable source
    unsigned int find_numlock_mask() {
        unsigned int mask = 0;
        const KeyCode numlock = XKeysymToKeycode(display, XK_Num_Lock);
        XModifierKeymap* modmap = XGetModifierMapping(display);
        if (modmap && numlock) {
            for (int mod = 0; mod < 8; ++mod) {
                for (int k = 0; k < modmap->max_keypermod; ++k) {
                    if (modmap->modifiermap[mod * modmap->max_keypermod + k] == numlock) {
                        mask = (1u << mod);
                    }
                }
            }
        }
        if (modmap) {
            XFreeModifiermap(modmap);
        }
        return mask;
    }

    // A grab only matches the exact modifier state, so register every combination
    // of the lock modifiers; otherwise NumLock or CapsLock being on breaks hotkeys.
    template <typename Fn>
    void for_each_modifier_variant(const KeyBinding& binding, Fn&& fn) {
        const unsigned int locks[] = {0, LockMask, numlock_mask, LockMask | numlock_mask};
        for (unsigned int lock : locks) {
            fn(kHotkeyMods | lock);
            if (binding.also_with_shift) {
                fn(kHotkeyMods | ShiftMask | lock);
            }
        }
    }

    bool configure_terminal() {
        if (tcgetattr(STDIN_FILENO, &original_termios) != 0) {
            return false;
        }

        struct termios new_termios = original_termios;
        // Disable canonical mode and echo
        new_termios.c_lflag &= ~(ICANON | ECHO);
        new_termios.c_cc[VMIN] = 0;  // Non-blocking read
        new_termios.c_cc[VTIME] = 0;

        if (tcsetattr(STDIN_FILENO, TCSANOW, &new_termios) != 0) {
            return false;
        }

        // Set stdin to non-blocking
        int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
        fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

        terminal_configured = true;
        return true;
    }

    void restore_terminal() {
        if (terminal_configured) {
            tcsetattr(STDIN_FILENO, TCSANOW, &original_termios);
            int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
            fcntl(STDIN_FILENO, F_SETFL, flags & ~O_NONBLOCK);
            terminal_configured = false;
        }
    }

    void message_loop() {
        XEvent event;
        while (!should_stop) {
            // Check for X11 events with a timeout
            fd_set readfds;
            FD_ZERO(&readfds);
            int x11_fd = ConnectionNumber(display);
            FD_SET(x11_fd, &readfds);

            struct timeval timeout;
            timeout.tv_sec = 0;
            timeout.tv_usec = 100000; // 100ms

            int result = select(x11_fd + 1, &readfds, nullptr, nullptr, &timeout);

            if (result > 0 && FD_ISSET(x11_fd, &readfds)) {
                while (XPending(display) > 0) {
                    XNextEvent(display, &event);

                    // Only grabbed combinations reach us, so the key alone identifies the action
                    if (event.type == KeyPress) {
                        handle_x11_hotkey(XLookupKeysym(&event.xkey, 0));
                    }
                }
            }
        }
    }

    void handle_x11_hotkey(KeySym keysym) {
        if (!callback) return;

        for (const auto& binding : kBindings) {
            if (binding.keysym == keysym) {
                callback(binding.action);
                return;
            }
        }
    }

    void console_input_loop() {
        while (!should_stop) {
            char c;
            if (read(STDIN_FILENO, &c, 1) > 0) {
                handle_input(c);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    void handle_input(char c) {
        if (!callback) return;

        // Map keys to actions (local hotkeys when terminal has focus)
        switch (c) {
            case 'n': case 'N': callback(HotkeyAction::NEXT_TRACK); break;
            case 'p': case 'P': callback(HotkeyAction::PREVIOUS_TRACK); break;
            case ' ': case 'r': case 'R': callback(HotkeyAction::PAUSE_RESUME); break;
            case '+': case '=': callback(HotkeyAction::VOLUME_UP); break;
            case '-': case '_': callback(HotkeyAction::VOLUME_DOWN); break;
            case 'q': case 'Q': case 27: callback(HotkeyAction::QUIT); break;
        }
    }
};

LinuxHotkeyHandler::LinuxHotkeyHandler() : m_impl(std::make_unique<Impl>()) {}

LinuxHotkeyHandler::~LinuxHotkeyHandler() {
    shutdown();
}

bool LinuxHotkeyHandler::initialize() {
    // Try X11 first for global hotkeys
    if (m_impl->create_display()) {
        m_impl->x11_available = true;
    } else {
        std::cerr << "Warning: Could not open X11 display, falling back to terminal input\n";
        std::cerr << "Make sure DISPLAY environment variable is set (e.g., export DISPLAY=:0)\n";
    }

    // Always configure terminal for local hotkeys
    if (!m_impl->configure_terminal()) {
        std::cerr << "Warning: Could not configure terminal for hotkey input\n";
    }

    return true;
}

void LinuxHotkeyHandler::shutdown() {
    unregister_hotkeys();

    m_impl->should_stop = true;
    if (m_impl->message_thread.joinable()) {
        m_impl->message_thread.join();
    }
    if (m_impl->console_input_thread.joinable()) {
        m_impl->console_input_thread.join();
    }

    // Null the handle so a second shutdown() (e.g. from the destructor) is a no-op
    if (m_impl->display) {
        XCloseDisplay(m_impl->display);
        m_impl->display = nullptr;
        m_impl->root = 0;
    }
    m_impl->x11_available = false;

    m_impl->restore_terminal();
}

void LinuxHotkeyHandler::set_callback(HotkeyCallback callback) {
    m_impl->callback = callback;
}

bool LinuxHotkeyHandler::register_hotkeys() {
    if (!m_impl->x11_available || !m_impl->display) {
        std::cout << "X11 not available - using terminal input only\n";
        std::cout << "Linux Hotkeys (terminal input):\n";
        std::cout << "  N/n - Next track\n";
        std::cout << "  P/p - Previous track\n";
        std::cout << "  Space/R/r - Pause/Resume\n";
        std::cout << "  +/- - Volume up/down\n";
        std::cout << "  Q/q/ESC - Quit\n";
        return true;
    }

    Display* display = m_impl->display;
    XErrorHandler previous_handler = XSetErrorHandler(grab_error_handler);

    bool success = true;
    for (const auto& binding : kBindings) {
        const KeyCode keycode = XKeysymToKeycode(display, binding.keysym);
        if (keycode == 0) {
            continue;  // Key not present in the current keyboard layout
        }

        g_grab_failed = false;
        m_impl->for_each_modifier_variant(binding, [&](unsigned int mods) {
            XGrabKey(display, keycode, mods, m_impl->root, False, GrabModeAsync, GrabModeAsync);
        });
        XSync(display, False);  // Flush so any BadAccess error is delivered now

        if (g_grab_failed) {
            std::cerr << "Failed to register " << binding.label << " (already in use by another application)\n";
            success = false;
        }
    }

    XSetErrorHandler(previous_handler);
    m_impl->hotkeys_grabbed = true;

    if (std::getenv("WAYLAND_DISPLAY")) {
        std::cout << "Note: Wayland session detected. Ctrl+Alt hotkeys only fire while an X11 app has focus;\n"
                  << "      use the media keys or the system media controls (MPRIS) instead.\n";
    }

    return success;
}

void LinuxHotkeyHandler::unregister_hotkeys() {
    if (!m_impl->display || !m_impl->hotkeys_grabbed) {
        return;
    }

    for (const auto& binding : kBindings) {
        const KeyCode keycode = XKeysymToKeycode(m_impl->display, binding.keysym);
        if (keycode == 0) {
            continue;
        }
        m_impl->for_each_modifier_variant(binding, [&](unsigned int mods) {
            XUngrabKey(m_impl->display, keycode, mods, m_impl->root);
        });
    }

    XFlush(m_impl->display);
    m_impl->hotkeys_grabbed = false;
}

void LinuxHotkeyHandler::process_messages() {
    if (m_impl->x11_available && m_impl->display && !m_impl->message_thread.joinable()) {
        m_impl->should_stop = false;
        m_impl->message_thread = std::thread(&Impl::message_loop, m_impl.get());
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    if (!m_impl->console_input_thread.joinable()) {
        m_impl->console_input_thread = std::thread(&Impl::console_input_loop, m_impl.get());
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

std::unique_ptr<IHotkeyHandler> create_hotkey_handler() {
    return std::make_unique<LinuxHotkeyHandler>();
}

}
