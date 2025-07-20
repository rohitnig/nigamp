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
    std::thread console_input_thread;
    std::atomic<bool> should_stop{false};
    
    static constexpr int HOTKEY_NEXT = 1;
    static constexpr int HOTKEY_PREV = 2;
    static constexpr int HOTKEY_PAUSE = 3;
    static constexpr int HOTKEY_VOLUME_UP = 4;
    static constexpr int HOTKEY_VOLUME_DOWN = 5;
    static constexpr int HOTKEY_QUIT = 6;
    
    bool create_window() {
        // Create message-only window for global hotkeys
        WNDCLASSEX wc = {};
        wc.cbSize = sizeof(WNDCLASSEX);
        wc.lpfnWndProc = window_proc;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.lpszClassName = "NigampHotkeyWindow";
        
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
        // Log all messages for debugging (can be disabled later)
        if (msg == WM_HOTKEY) {
            std::cout << "[HOTKEY DEBUG] WM_HOTKEY message received!" << std::endl;
            std::cout << "[HOTKEY DEBUG] Hotkey ID: " << static_cast<int>(wparam) << std::endl;
            std::cout << "[HOTKEY DEBUG] Modifiers: " << LOWORD(lparam) << ", VirtualKey: " << HIWORD(lparam) << std::endl;
            
            WindowsHotkeyHandler::Impl* impl = 
                reinterpret_cast<WindowsHotkeyHandler::Impl*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
            
            if (!impl) {
                std::cout << "[HOTKEY DEBUG] ERROR: No impl pointer found!" << std::endl;
                return 0;
            }
            
            if (!impl->callback) {
                std::cout << "[HOTKEY DEBUG] ERROR: No callback set!" << std::endl;
                return 0;
            }
            
            std::cout << "[HOTKEY DEBUG] Calling handle_hotkey..." << std::endl;
            impl->handle_hotkey(static_cast<int>(wparam));
            std::cout << "[HOTKEY DEBUG] handle_hotkey completed" << std::endl;
            return 0;
        }
        return DefWindowProc(hwnd, msg, wparam, lparam);
    }
    
    
    void handle_hotkey(int hotkey_id) {
        std::cout << "[HOTKEY DEBUG] handle_hotkey called with ID: " << hotkey_id << std::endl;
        
        switch (hotkey_id) {
            case HOTKEY_NEXT:
                std::cout << "[HOTKEY DEBUG] Triggering NEXT_TRACK action" << std::endl;
                callback(HotkeyAction::NEXT_TRACK);
                break;
            case HOTKEY_PREV:
                std::cout << "[HOTKEY DEBUG] Triggering PREVIOUS_TRACK action" << std::endl;
                callback(HotkeyAction::PREVIOUS_TRACK);
                break;
            case HOTKEY_PAUSE:
                std::cout << "[HOTKEY DEBUG] Triggering PAUSE_RESUME action" << std::endl;
                callback(HotkeyAction::PAUSE_RESUME);
                break;
            case HOTKEY_VOLUME_UP:
                std::cout << "[HOTKEY DEBUG] Triggering VOLUME_UP action" << std::endl;
                callback(HotkeyAction::VOLUME_UP);
                break;
            case HOTKEY_VOLUME_DOWN:
                std::cout << "[HOTKEY DEBUG] Triggering VOLUME_DOWN action" << std::endl;
                callback(HotkeyAction::VOLUME_DOWN);
                break;
            case HOTKEY_QUIT:
                std::cout << "[HOTKEY DEBUG] Triggering QUIT action" << std::endl;
                callback(HotkeyAction::QUIT);
                break;
            default:
                std::cout << "[HOTKEY DEBUG] ERROR: Unknown hotkey ID: " << hotkey_id << std::endl;
                break;
        }
        
        std::cout << "[HOTKEY DEBUG] handle_hotkey finished" << std::endl;
    }
    
    void message_loop() {
        std::cout << "[HOTKEY DEBUG] Message loop thread started" << std::endl;
        MSG msg;
        int heartbeat_counter = 0;
        
        while (!should_stop) {
            BOOL result = PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE);
            if (result > 0) {
                std::cout << "[HOTKEY DEBUG] Message received: " << msg.message << std::endl;
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                
                // Heartbeat every 10 seconds
                heartbeat_counter++;
                if (heartbeat_counter >= 1000) { // 10ms * 1000 = 10 seconds
                    std::cout << "[HOTKEY DEBUG] Message loop heartbeat (waiting for hotkeys...)" << std::endl;
                    heartbeat_counter = 0;
                }
            }
        }
        
        std::cout << "[HOTKEY DEBUG] Message loop thread stopped" << std::endl;
    }
    
    void console_input_loop() {
        std::cout << "[LOCAL HOTKEY DEBUG] Console input thread started" << std::endl;
        
        while (!should_stop) {
            // Check if Ctrl is pressed
            if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
                // Check for local hotkey combinations
                if (GetAsyncKeyState('N') & 0x8000) {
                    std::cout << "[LOCAL HOTKEY DEBUG] Triggering NEXT_TRACK (Ctrl+N)" << std::endl;
                    if (callback) callback(HotkeyAction::NEXT_TRACK);
                    std::this_thread::sleep_for(std::chrono::milliseconds(200)); // Prevent rapid repeats
                }
                else if (GetAsyncKeyState('P') & 0x8000) {
                    std::cout << "[LOCAL HOTKEY DEBUG] Triggering PREVIOUS_TRACK (Ctrl+P)" << std::endl;
                    if (callback) callback(HotkeyAction::PREVIOUS_TRACK);
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                }
                else if (GetAsyncKeyState('R') & 0x8000) {
                    std::cout << "[LOCAL HOTKEY DEBUG] Triggering PAUSE_RESUME (Ctrl+R)" << std::endl;
                    if (callback) callback(HotkeyAction::PAUSE_RESUME);
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                }
                else if (GetAsyncKeyState(VK_OEM_PLUS) & 0x8000) {
                    std::cout << "[LOCAL HOTKEY DEBUG] Triggering VOLUME_UP (Ctrl+Plus)" << std::endl;
                    if (callback) callback(HotkeyAction::VOLUME_UP);
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                }
                else if (GetAsyncKeyState(VK_OEM_MINUS) & 0x8000) {
                    std::cout << "[LOCAL HOTKEY DEBUG] Triggering VOLUME_DOWN (Ctrl+Minus)" << std::endl;
                    if (callback) callback(HotkeyAction::VOLUME_DOWN);
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                }
                else if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) {
                    std::cout << "[LOCAL HOTKEY DEBUG] Triggering QUIT (Ctrl+Escape)" << std::endl;
                    if (callback) callback(HotkeyAction::QUIT);
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                }
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        
        std::cout << "[LOCAL HOTKEY DEBUG] Console input thread stopped" << std::endl;
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
    if (m_impl->console_input_thread.joinable()) {
        m_impl->console_input_thread.join();
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
    std::cout << "[HOTKEY DEBUG] Starting hotkey registration..." << std::endl;
    
    if (!m_impl->window_handle) {
        std::cerr << "Cannot register hotkeys: Window handle not created\n";
        return false;
    }
    
    std::cout << "[HOTKEY DEBUG] Window handle valid: " << m_impl->window_handle << std::endl;
    bool success = true;
    
    auto register_hotkey_with_error = [&](int id, UINT modifiers, UINT vk, const char* name) {
        std::cout << "[HOTKEY DEBUG] Registering " << name << " (ID:" << id << ", Mod:" << modifiers << ", VK:" << vk << ")" << std::endl;
        
        if (!RegisterHotKey(m_impl->window_handle, id, modifiers, vk)) {
            DWORD error = GetLastError();
            std::cerr << "Failed to register " << name << " hotkey (error: " << error;
            
            switch (error) {
                case 1409: // ERROR_HOTKEY_ALREADY_REGISTERED
                    std::cerr << " - hotkey already in use by another application";
                    break;
                case 87: // ERROR_INVALID_PARAMETER
                    std::cerr << " - invalid hotkey combination";
                    break;
                case 1400: // ERROR_INVALID_WINDOW_HANDLE
                    std::cerr << " - invalid window handle";
                    break;
                default:
                    std::cerr << " - unknown error";
                    break;
            }
            std::cerr << ")\n";
            success = false;
        } else {
            std::cout << "[HOTKEY DEBUG] Successfully registered " << name << std::endl;
        }
    };
    
    register_hotkey_with_error(m_impl->HOTKEY_NEXT, MOD_CONTROL | MOD_ALT, 'N', "Ctrl+Alt+N");
    register_hotkey_with_error(m_impl->HOTKEY_PREV, MOD_CONTROL | MOD_ALT, 'P', "Ctrl+Alt+P");
    register_hotkey_with_error(m_impl->HOTKEY_PAUSE, MOD_CONTROL | MOD_ALT, 'R', "Ctrl+Alt+R");
    register_hotkey_with_error(m_impl->HOTKEY_VOLUME_UP, MOD_CONTROL | MOD_ALT, VK_OEM_PLUS, "Ctrl+Alt+Plus");
    register_hotkey_with_error(m_impl->HOTKEY_VOLUME_DOWN, MOD_CONTROL | MOD_ALT, VK_OEM_MINUS, "Ctrl+Alt+Minus");
    register_hotkey_with_error(m_impl->HOTKEY_QUIT, MOD_CONTROL | MOD_ALT, VK_ESCAPE, "Ctrl+Alt+Escape");
    
    if (!success) {
        std::cerr << "Note: Some hotkeys failed to register. Try closing other applications\n";
        std::cerr << "or running as administrator to resolve conflicts.\n";
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
    std::cout << "[HOTKEY DEBUG] process_messages() called" << std::endl;
    
    if (!m_impl->message_thread.joinable()) {
        std::cout << "[HOTKEY DEBUG] Starting message thread..." << std::endl;
        m_impl->should_stop = false;
        m_impl->message_thread = std::thread(&Impl::message_loop, m_impl.get());
        
        // Give the thread a moment to start
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        std::cout << "[HOTKEY DEBUG] Message thread should be running now" << std::endl;
    } else {
        std::cout << "[HOTKEY DEBUG] Message thread already running" << std::endl;
    }
    
    if (!m_impl->console_input_thread.joinable()) {
        std::cout << "[LOCAL HOTKEY DEBUG] Starting console input thread..." << std::endl;
        m_impl->console_input_thread = std::thread(&Impl::console_input_loop, m_impl.get());
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        std::cout << "[LOCAL HOTKEY DEBUG] Console input thread should be running now" << std::endl;
    } else {
        std::cout << "[LOCAL HOTKEY DEBUG] Console input thread already running" << std::endl;
    }
}

std::unique_ptr<IHotkeyHandler> create_hotkey_handler() {
    return std::make_unique<WindowsHotkeyHandler>();
}

}