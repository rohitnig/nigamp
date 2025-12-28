#include "hotkey_handler.hpp"
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <thread>
#include <atomic>
#include <iostream>
#include <chrono>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <sys/select.h>

namespace nigamp {

struct LinuxHotkeyHandler::Impl {
    Display* display = nullptr;
    Window window = 0;
    HotkeyCallback callback;
    std::thread message_thread;
    std::thread console_input_thread;
    std::atomic<bool> should_stop{false};
    struct termios original_termios;
    bool terminal_configured = false;
    bool x11_available = false;
    
    static constexpr int HOTKEY_NEXT = 1;
    static constexpr int HOTKEY_PREV = 2;
    static constexpr int HOTKEY_PAUSE = 3;
    static constexpr int HOTKEY_VOLUME_UP = 4;
    static constexpr int HOTKEY_VOLUME_DOWN = 5;
    static constexpr int HOTKEY_QUIT = 6;
    
    bool create_display() {
        display = XOpenDisplay(nullptr);
        if (!display) {
            return false;
        }
        return true;
    }
    
    bool create_window() {
        int screen = DefaultScreen(display);
        Window root = RootWindow(display, screen);
        
        // Create an invisible window for receiving events
        XSetWindowAttributes attrs;
        attrs.override_redirect = True;
        attrs.event_mask = KeyPressMask;
        
        window = XCreateWindow(
            display, root,
            -1, -1, 1, 1, 0,  // x, y, width, height, border
            CopyFromParent,   // depth
            InputOnly,        // class
            CopyFromParent,   // visual
            CWOverrideRedirect | CWEventMask,
            &attrs
        );
        
        if (!window) {
            return false;
        }
        
        // Map the window (make it exist)
        XMapWindow(display, window);
        XFlush(display);
        
        return true;
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
                    
                    if (event.type == KeyPress) {
                        KeySym keysym = XLookupKeysym(&event.xkey, 0);
                        unsigned int state = event.xkey.state;
                        
                        // Check for Ctrl+Alt combinations
                        bool ctrl = (state & ControlMask) != 0;
                        bool alt = (state & Mod1Mask) != 0;
                        
                        if (ctrl && alt) {
                            handle_x11_hotkey(keysym);
                        }
                    }
                }
            }
        }
    }
    
    void handle_x11_hotkey(KeySym keysym) {
        if (!callback) return;
        
        switch (keysym) {
            case XK_n:
            case XK_N:
                callback(HotkeyAction::NEXT_TRACK);
                break;
            case XK_p:
            case XK_P:
                callback(HotkeyAction::PREVIOUS_TRACK);
                break;
            case XK_r:
            case XK_R:
                callback(HotkeyAction::PAUSE_RESUME);
                break;
            case XK_plus:
            case XK_equal:
                callback(HotkeyAction::VOLUME_UP);
                break;
            case XK_minus:
            case XK_underscore:
                callback(HotkeyAction::VOLUME_DOWN);
                break;
            case XK_Escape:
                callback(HotkeyAction::QUIT);
                break;
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
        if (m_impl->create_window()) {
            m_impl->x11_available = true;
        } else {
            std::cerr << "Warning: Could not create X11 window, falling back to terminal input\n";
            XCloseDisplay(m_impl->display);
            m_impl->display = nullptr;
        }
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
    
    if (m_impl->window) {
        XDestroyWindow(m_impl->display, m_impl->window);
    }
    if (m_impl->display) {
        XCloseDisplay(m_impl->display);
    }
    
    m_impl->restore_terminal();
}

void LinuxHotkeyHandler::set_callback(HotkeyCallback callback) {
    m_impl->callback = callback;
}

bool LinuxHotkeyHandler::register_hotkeys() {
    if (!m_impl->x11_available || !m_impl->display || !m_impl->window) {
        std::cout << "X11 not available - using terminal input only\n";
        std::cout << "Linux Hotkeys (terminal input):\n";
        std::cout << "  N/n - Next track\n";
        std::cout << "  P/p - Previous track\n";
        std::cout << "  Space/R/r - Pause/Resume\n";
        std::cout << "  +/- - Volume up/down\n";
        std::cout << "  Q/q/ESC - Quit\n";
        return true;
    }
    
    // Get keycodes for the keys we want
    KeyCode n_key = XKeysymToKeycode(m_impl->display, XK_n);
    KeyCode p_key = XKeysymToKeycode(m_impl->display, XK_p);
    KeyCode r_key = XKeysymToKeycode(m_impl->display, XK_r);
    KeyCode plus_key = XKeysymToKeycode(m_impl->display, XK_plus);
    KeyCode minus_key = XKeysymToKeycode(m_impl->display, XK_minus);
    KeyCode escape_key = XKeysymToKeycode(m_impl->display, XK_Escape);
    
    unsigned int mod_mask = ControlMask | Mod1Mask; // Ctrl+Alt
    
    bool success = true;
    
    // Register global hotkeys
    if (XGrabKey(m_impl->display, n_key, mod_mask, m_impl->window, False, GrabModeAsync, GrabModeAsync) != Success) {
        std::cerr << "Failed to register Ctrl+Alt+N\n";
        success = false;
    }
    if (XGrabKey(m_impl->display, p_key, mod_mask, m_impl->window, False, GrabModeAsync, GrabModeAsync) != Success) {
        std::cerr << "Failed to register Ctrl+Alt+P\n";
        success = false;
    }
    if (XGrabKey(m_impl->display, r_key, mod_mask, m_impl->window, False, GrabModeAsync, GrabModeAsync) != Success) {
        std::cerr << "Failed to register Ctrl+Alt+R\n";
        success = false;
    }
    if (XGrabKey(m_impl->display, plus_key, mod_mask, m_impl->window, False, GrabModeAsync, GrabModeAsync) != Success) {
        std::cerr << "Failed to register Ctrl+Alt+Plus\n";
        success = false;
    }
    if (XGrabKey(m_impl->display, minus_key, mod_mask, m_impl->window, False, GrabModeAsync, GrabModeAsync) != Success) {
        std::cerr << "Failed to register Ctrl+Alt+Minus\n";
        success = false;
    }
    if (XGrabKey(m_impl->display, escape_key, mod_mask, m_impl->window, False, GrabModeAsync, GrabModeAsync) != Success) {
        std::cerr << "Failed to register Ctrl+Alt+Escape\n";
        success = false;
    }
    
    XFlush(m_impl->display);
    
    if (success) {
        std::cout << "Global hotkeys registered successfully!\n";
        std::cout << "  Ctrl+Alt+N - Next track\n";
        std::cout << "  Ctrl+Alt+P - Previous track\n";
        std::cout << "  Ctrl+Alt+R - Pause/Resume\n";
        std::cout << "  Ctrl+Alt+Plus - Volume up\n";
        std::cout << "  Ctrl+Alt+Minus - Volume down\n";
        std::cout << "  Ctrl+Alt+Escape - Quit\n";
    } else {
        std::cerr << "Some hotkeys failed to register. They may be in use by another application.\n";
    }
    
    return success;
}

void LinuxHotkeyHandler::unregister_hotkeys() {
    if (!m_impl->display || !m_impl->window) {
        return;
    }
    
    KeyCode n_key = XKeysymToKeycode(m_impl->display, XK_n);
    KeyCode p_key = XKeysymToKeycode(m_impl->display, XK_p);
    KeyCode r_key = XKeysymToKeycode(m_impl->display, XK_r);
    KeyCode plus_key = XKeysymToKeycode(m_impl->display, XK_plus);
    KeyCode minus_key = XKeysymToKeycode(m_impl->display, XK_minus);
    KeyCode escape_key = XKeysymToKeycode(m_impl->display, XK_Escape);
    
    unsigned int mod_mask = ControlMask | Mod1Mask;
    
    XUngrabKey(m_impl->display, n_key, mod_mask, m_impl->window);
    XUngrabKey(m_impl->display, p_key, mod_mask, m_impl->window);
    XUngrabKey(m_impl->display, r_key, mod_mask, m_impl->window);
    XUngrabKey(m_impl->display, plus_key, mod_mask, m_impl->window);
    XUngrabKey(m_impl->display, minus_key, mod_mask, m_impl->window);
    XUngrabKey(m_impl->display, escape_key, mod_mask, m_impl->window);
    
    XFlush(m_impl->display);
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
