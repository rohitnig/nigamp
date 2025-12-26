#pragma once

#include <functional>
#include <memory>

namespace nigamp {

enum class HotkeyAction {
    NEXT_TRACK,
    PREVIOUS_TRACK,
    PAUSE_RESUME,
    VOLUME_UP,
    VOLUME_DOWN,
    QUIT
};

using HotkeyCallback = std::function<void(HotkeyAction)>;

class IHotkeyHandler {
public:
    virtual ~IHotkeyHandler() = default;
    virtual bool initialize() = 0;
    virtual void shutdown() = 0;
    virtual void set_callback(HotkeyCallback callback) = 0;
    virtual bool register_hotkeys() = 0;
    virtual void unregister_hotkeys() = 0;
    virtual void process_messages() = 0;
};

class WindowsHotkeyHandler : public IHotkeyHandler {
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;

public:
    WindowsHotkeyHandler();
    ~WindowsHotkeyHandler() override;

    bool initialize() override;
    void shutdown() override;
    void set_callback(HotkeyCallback callback) override;
    bool register_hotkeys() override;
    void unregister_hotkeys() override;
    void process_messages() override;
};

class LinuxHotkeyHandler : public IHotkeyHandler {
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;

public:
    LinuxHotkeyHandler();
    ~LinuxHotkeyHandler() override;

    bool initialize() override;
    void shutdown() override;
    void set_callback(HotkeyCallback callback) override;
    bool register_hotkeys() override;
    void unregister_hotkeys() override;
    void process_messages() override;
};

std::unique_ptr<IHotkeyHandler> create_hotkey_handler();

}