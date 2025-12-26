#include "hotkey_handler.hpp"
#include <thread>
#include <atomic>
#include <iostream>
#include <chrono>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>

namespace nigamp {

struct LinuxHotkeyHandler::Impl {
    HotkeyCallback callback;
    std::thread input_thread;
    std::atomic<bool> should_stop{false};
    struct termios original_termios;
    bool terminal_configured = false;
    
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
    
    void input_loop() {
        while (!should_stop) {
            char c;
            if (read(STDIN_FILENO, &c, 1) > 0) {
                handle_input(c);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
    
    void handle_input(char c) {
        if (!callback) {
            return;
        }
        
        // Map keys to actions
        // Note: For Linux, we use simpler key combinations since global hotkeys
        // require X11 integration which is more complex
        switch (c) {
            case 'n':
            case 'N':
                callback(HotkeyAction::NEXT_TRACK);
                break;
            case 'p':
            case 'P':
                callback(HotkeyAction::PREVIOUS_TRACK);
                break;
            case ' ':
            case 'r':
            case 'R':
                callback(HotkeyAction::PAUSE_RESUME);
                break;
            case '+':
            case '=':
                callback(HotkeyAction::VOLUME_UP);
                break;
            case '-':
            case '_':
                callback(HotkeyAction::VOLUME_DOWN);
                break;
            case 'q':
            case 'Q':
            case 27: // ESC
                callback(HotkeyAction::QUIT);
                break;
        }
    }
};

LinuxHotkeyHandler::LinuxHotkeyHandler() : m_impl(std::make_unique<Impl>()) {}

LinuxHotkeyHandler::~LinuxHotkeyHandler() {
    shutdown();
}

bool LinuxHotkeyHandler::initialize() {
    if (!m_impl->configure_terminal()) {
        std::cerr << "Warning: Could not configure terminal for hotkey input\n";
        std::cerr << "Hotkeys may not work properly. Make sure you're running in a terminal.\n";
        return false;
    }
    return true;
}

void LinuxHotkeyHandler::shutdown() {
    m_impl->should_stop = true;
    if (m_impl->input_thread.joinable()) {
        m_impl->input_thread.join();
    }
    m_impl->restore_terminal();
}

void LinuxHotkeyHandler::set_callback(HotkeyCallback callback) {
    m_impl->callback = callback;
}

bool LinuxHotkeyHandler::register_hotkeys() {
    // On Linux, we don't register global hotkeys like Windows
    // Instead, we use terminal input
    std::cout << "Linux Hotkeys (terminal input):\n";
    std::cout << "  N/n - Next track\n";
    std::cout << "  P/p - Previous track\n";
    std::cout << "  Space/R/r - Pause/Resume\n";
    std::cout << "  +/- - Volume up/down\n";
    std::cout << "  Q/q/ESC - Quit\n";
    return true;
}

void LinuxHotkeyHandler::unregister_hotkeys() {
    // Nothing to unregister on Linux
}

void LinuxHotkeyHandler::process_messages() {
    if (!m_impl->input_thread.joinable()) {
        m_impl->should_stop = false;
        m_impl->input_thread = std::thread(&Impl::input_loop, m_impl.get());
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

std::unique_ptr<IHotkeyHandler> create_hotkey_handler() {
    return std::make_unique<LinuxHotkeyHandler>();
}

}

