#include "RawKeyboard.h"

#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace daw::app
{

#if JUCE_WINDOWS

namespace
{

constexpr const wchar_t* windowClass = L"DawIaRawKeyboard";

// Whether the window in front is one of this process's: the main window, a
// page, a plugin's editor.
bool inFront()
{
    const auto front = GetForegroundWindow();
    if (front == nullptr)
        return false;
    DWORD process = 0;
    GetWindowThreadProcessId(front, &process);
    return process == GetCurrentProcessId();
}

LRESULT CALLBACK receive(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_INPUT)
    {
        auto* keys =
            reinterpret_cast<domain::live::TypingKeyboard*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        RAWINPUT input{};
        UINT size = sizeof(input);
        if (keys != nullptr &&
            GetRawInputData(
                reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, &input, &size, sizeof(RAWINPUTHEADER)) !=
                static_cast<UINT>(-1) &&
            input.header.dwType == RIM_TYPEKEYBOARD)
        {
            const auto& key = input.data.keyboard;
            // 0xFF: a key Windows made up (the second half of Pause, an
            // overrun); 0: no scan code at all.
            if (key.VKey != 0xFF && key.MakeCode != 0)
            {
                const auto seconds = domain::live::now();
                const bool down = (key.Flags & RI_KEY_BREAK) == 0;
                const bool extended = (key.Flags & RI_KEY_E0) != 0;
                if (down)
                    keys->setForeground(inFront());
                static_cast<void>(keys->key(static_cast<int>(key.MakeCode), extended, down, seconds));
            }
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }
    if (message == WM_CLOSE)
    {
        DestroyWindow(window);
        return 0;
    }
    if (message == WM_DESTROY)
    {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace

RawKeyboard::RawKeyboard(domain::live::TypingKeyboard& keys)
    : juce::Thread("DAW IA clavier")
    , keys_(keys)
{
    // Above the message thread, below the audio: a key is a few
    // microseconds of work, and must not wait behind a repaint.
    startThread(juce::Thread::Priority::high);

    // The window is made on the thread itself; wait for it, briefly.
    for (int tries = 0; tries < 100 && window_.load() == nullptr && isThreadRunning(); ++tries)
        juce::Thread::sleep(5);
}

RawKeyboard::~RawKeyboard()
{
    signalThreadShouldExit();
    if (auto* window = window_.load(); window != nullptr)
        PostMessageW(static_cast<HWND>(window), WM_CLOSE, 0, 0);
    stopThread(2000);
}

void RawKeyboard::run()
{
    WNDCLASSEXW type{};
    type.cbSize = sizeof(type);
    type.lpfnWndProc = receive;
    type.hInstance = GetModuleHandleW(nullptr);
    type.lpszClassName = windowClass;
    RegisterClassExW(&type); // a second time fails harmlessly: the class exists

    const auto window =
        CreateWindowExW(0, windowClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, type.hInstance, nullptr);
    if (window == nullptr)
    {
        juce::Logger::writeToLog("jeu: la fenêtre du clavier ne s'ouvre pas, erreur " +
                                 juce::String(static_cast<int>(GetLastError())));
        return;
    }
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&keys_));

    // Usage page 1 (generic desktop), usage 6: the keyboards.
    RAWINPUTDEVICE device{};
    device.usUsagePage = 0x01;
    device.usUsage = 0x06;
    device.dwFlags = RIDEV_INPUTSINK;
    device.hwndTarget = window;
    if (RegisterRawInputDevices(&device, 1, sizeof(device)) == FALSE)
    {
        juce::Logger::writeToLog("jeu: Raw Input refusé, erreur " +
                                 juce::String(static_cast<int>(GetLastError())));
        DestroyWindow(window);
        return;
    }

    registered_.store(true, std::memory_order_release);
    window_.store(window);
    juce::Logger::writeToLog("jeu: le clavier de l'ordinateur est lu par Raw Input");

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    device.dwFlags = RIDEV_REMOVE;
    device.hwndTarget = nullptr;
    RegisterRawInputDevices(&device, 1, sizeof(device));
    registered_.store(false, std::memory_order_release);
    window_.store(nullptr);
}

#else

RawKeyboard::RawKeyboard(domain::live::TypingKeyboard& keys)
    : juce::Thread("DAW IA clavier")
    , keys_(keys)
{
    // Linux is not a product target (CLAUDE.md §2): nothing reads the keys.
    juce::ignoreUnused(keys_);
}

RawKeyboard::~RawKeyboard() = default;

void RawKeyboard::run() {}

#endif

} // namespace daw::app
