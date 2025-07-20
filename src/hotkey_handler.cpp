#include "hotkey_handler.hpp"
#include <windows.h>
#include <thread>
#include <atomic>
#include <iostream>

namespace nigamp {

struct WindowsHotkeyHandler::Impl {
    HWND window_handle = nullptr;
    HotkeyCallback callback;
    std::thread message_thread;
    std::atomic<bool> should_stop{false};
    
    static constexpr int HOTKEY_NEXT = 1;
    static constexpr int HOTKEY_PREV = 2;
    static constexpr int HOTKEY_PAUSE = 3;
    static constexpr int HOTKEY_VOLUME_UP = 4;
    static constexpr int HOTKEY_VOLUME_DOWN = 5;
    static constexpr int HOTKEY_QUIT = 6;
    
    bool create_window() {
        WNDCLASSEX wc = {};
        wc.cbSize = sizeof(WNDCLASSEX);
        wc.lpfnWndProc = window_proc;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.lpszClassName = "NigampHotkeyWindow";
        wc.lpfnWndProc = [](HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) -> LRESULT {
            if (msg == WM_HOTKEY) {
                WindowsHotkeyHandler::Impl* impl = 
                    reinterpret_cast<WindowsHotkeyHandler::Impl*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
                if (impl && impl->callback) {
                    impl->handle_hotkey(static_cast<int>(wparam));
                }
                return 0;
            }
            return DefWindowProc(hwnd, msg, wparam, lparam);
        };
        
        RegisterClassEx(&wc);
        
        window_handle = CreateWindowEx(
            0, "NigampHotkeyWindow", "Nigamp Hotkeys",
            0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandle(nullptr), nullptr);
            
        if (window_handle) {
            SetWindowLongPtr(window_handle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        }
        
        return window_handle != nullptr;
    }
    
    static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
        if (msg == WM_HOTKEY) {
            WindowsHotkeyHandler::Impl* impl = 
                reinterpret_cast<WindowsHotkeyHandler::Impl*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
            if (impl && impl->callback) {
                impl->handle_hotkey(static_cast<int>(wparam));
            }
            return 0;
        }
        return DefWindowProc(hwnd, msg, wparam, lparam);
    }
    
    void handle_hotkey(int hotkey_id) {
        switch (hotkey_id) {
            case HOTKEY_NEXT:
                callback(HotkeyAction::NEXT_TRACK);
                break;
            case HOTKEY_PREV:
                callback(HotkeyAction::PREVIOUS_TRACK);
                break;
            case HOTKEY_PAUSE:
                callback(HotkeyAction::PAUSE_RESUME);
                break;
            case HOTKEY_VOLUME_UP:
                callback(HotkeyAction::VOLUME_UP);
                break;
            case HOTKEY_VOLUME_DOWN:
                callback(HotkeyAction::VOLUME_DOWN);
                break;
            case HOTKEY_QUIT:
                callback(HotkeyAction::QUIT);
                break;
        }
    }
    
    void message_loop() {
        MSG msg;
        while (!should_stop) {
            BOOL result = PeekMessage(&msg, window_handle, 0, 0, PM_REMOVE);
            if (result > 0) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
    }
};

WindowsHotkeyHandler::WindowsHotkeyHandler() : m_impl(std::make_unique<Impl>()) {}

WindowsHotkeyHandler::~WindowsHotkeyHandler() {
    shutdown();
}

bool WindowsHotkeyHandler::initialize() {
    return m_impl->create_window();
}

void WindowsHotkeyHandler::shutdown() {
    unregister_hotkeys();
    
    m_impl->should_stop = true;
    if (m_impl->message_thread.joinable()) {
        m_impl->message_thread.join();
    }
    
    if (m_impl->window_handle) {
        DestroyWindow(m_impl->window_handle);
        m_impl->window_handle = nullptr;
    }
}

void WindowsHotkeyHandler::set_callback(HotkeyCallback callback) {
    m_impl->callback = callback;
}

bool WindowsHotkeyHandler::register_hotkeys() {
    if (!m_impl->window_handle) {
        return false;
    }
    
    bool success = true;
    
    if (!RegisterHotKey(m_impl->window_handle, m_impl->HOTKEY_NEXT, 
                       MOD_CONTROL | MOD_ALT, 'N')) {
        std::cerr << "Failed to register Ctrl+Alt+N hotkey (error: " << GetLastError() << ")\n";
        success = false;
    }
    
    if (!RegisterHotKey(m_impl->window_handle, m_impl->HOTKEY_PREV, 
                       MOD_CONTROL | MOD_ALT, 'P')) {
        std::cerr << "Failed to register Ctrl+Alt+P hotkey (error: " << GetLastError() << ")\n";
        success = false;
    }
    
    if (!RegisterHotKey(m_impl->window_handle, m_impl->HOTKEY_PAUSE, 
                       MOD_CONTROL | MOD_ALT, 'R')) {  // Changed to R for Resume
        std::cerr << "Failed to register Ctrl+Alt+R hotkey (error: " << GetLastError() << ")\n";
        success = false;
    }
    
    if (!RegisterHotKey(m_impl->window_handle, m_impl->HOTKEY_VOLUME_UP, 
                       MOD_CONTROL | MOD_ALT, VK_OEM_PLUS)) {  // Changed to Plus key
        std::cerr << "Failed to register Ctrl+Alt+Plus hotkey (error: " << GetLastError() << ")\n";
        success = false;
    }
    
    if (!RegisterHotKey(m_impl->window_handle, m_impl->HOTKEY_VOLUME_DOWN, 
                       MOD_CONTROL | MOD_ALT, VK_OEM_MINUS)) {  // Changed to Minus key
        std::cerr << "Failed to register Ctrl+Alt+Minus hotkey (error: " << GetLastError() << ")\n";
        success = false;
    }
    
    if (!RegisterHotKey(m_impl->window_handle, m_impl->HOTKEY_QUIT, 
                       MOD_CONTROL | MOD_ALT, VK_ESCAPE)) {
        std::cerr << "Failed to register Ctrl+Alt+Escape hotkey (error: " << GetLastError() << ")\n";
        success = false;
    }
    
    return success;
}

void WindowsHotkeyHandler::unregister_hotkeys() {
    if (!m_impl->window_handle) {
        return;
    }
    
    UnregisterHotKey(m_impl->window_handle, m_impl->HOTKEY_NEXT);
    UnregisterHotKey(m_impl->window_handle, m_impl->HOTKEY_PREV);
    UnregisterHotKey(m_impl->window_handle, m_impl->HOTKEY_PAUSE);
    UnregisterHotKey(m_impl->window_handle, m_impl->HOTKEY_VOLUME_UP);
    UnregisterHotKey(m_impl->window_handle, m_impl->HOTKEY_VOLUME_DOWN);
    UnregisterHotKey(m_impl->window_handle, m_impl->HOTKEY_QUIT);
}

void WindowsHotkeyHandler::process_messages() {
    if (!m_impl->message_thread.joinable()) {
        m_impl->should_stop = false;
        m_impl->message_thread = std::thread(&Impl::message_loop, m_impl.get());
    }
}

std::unique_ptr<IHotkeyHandler> create_hotkey_handler() {
    return std::make_unique<WindowsHotkeyHandler>();
}

}