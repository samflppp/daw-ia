#include "NativeWindow.h"

#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace daw::app::native
{

#if JUCE_WINDOWS

namespace
{

// The pointer's place as Windows hands it over: physical pixels, packed.
[[nodiscard]] LPARAM packed(juce::Point<int> screen)
{
    const auto physical =
        juce::Desktop::getInstance().getDisplays().logicalToPhysical(screen.toFloat()).roundToInt();
    return MAKELPARAM(static_cast<WORD>(static_cast<short>(physical.x)),
                      static_cast<WORD>(static_cast<short>(physical.y)));
}

} // namespace

juce::String hitTest(juce::ComponentPeer& peer, juce::Point<int> screen)
{
    const auto hwnd = static_cast<HWND>(peer.getNativeHandle());
    switch (SendMessageW(hwnd, WM_NCHITTEST, 0, packed(screen)))
    {
    case HTCAPTION:
        return "caption";
    case HTMINBUTTON:
        return "minimise";
    case HTMAXBUTTON:
        return "maximise";
    case HTCLOSE:
        return "close";
    case HTCLIENT:
        return "client";
    case HTLEFT:
    case HTRIGHT:
    case HTTOP:
    case HTBOTTOM:
    case HTTOPLEFT:
    case HTTOPRIGHT:
    case HTBOTTOMLEFT:
    case HTBOTTOMRIGHT:
        return "edge";
    default:
        return "other";
    }
}

bool doubleClickCaption(juce::ComponentPeer& peer, juce::Point<int> screen)
{
    const auto hwnd = static_cast<HWND>(peer.getNativeHandle());
    SendMessageW(hwnd, WM_NCLBUTTONDBLCLK, HTCAPTION, packed(screen));
    return true;
}

juce::String windowState(juce::ComponentPeer& peer)
{
    const auto hwnd = static_cast<HWND>(peer.getNativeHandle());
    WINDOWPLACEMENT placement{};
    placement.length = sizeof(placement);
    GetWindowPlacement(hwnd, &placement);
    const auto shown = IsWindowVisible(hwnd) != 0;
    if (placement.showCmd == SW_SHOWMINIMIZED || IsIconic(hwnd) != 0)
        return juce::String::fromUTF8("réduite");
    return shown ? juce::String::fromUTF8("affichée") : juce::String::fromUTF8("cachée");
}

juce::String foreground()
{
    const auto hwnd = GetForegroundWindow();
    if (hwnd == nullptr)
        return juce::String::fromUTF8(
            "aucune fenêtre au premier plan (écran verrouillé ou bureau sécurisé ?)");
    wchar_t title[256]{};
    GetWindowTextW(hwnd, title, 256);
    DWORD process = 0;
    GetWindowThreadProcessId(hwnd, &process);
    const auto ours = process == GetCurrentProcessId();
    return juce::String::fromUTF8("« ") + juce::String{title} + juce::String::fromUTF8(" »") +
           (ours ? juce::String::fromUTF8(", de ce processus")
                 : juce::String::fromUTF8(", d'un autre processus (") +
                       juce::String{static_cast<int>(process)} + ")");
}

#else

juce::String hitTest(juce::ComponentPeer&, juce::Point<int>)
{
    return "unsupported";
}

bool doubleClickCaption(juce::ComponentPeer&, juce::Point<int>)
{
    return false;
}

juce::String windowState(juce::ComponentPeer&)
{
    return "unsupported";
}

juce::String foreground()
{
    return "unsupported";
}

#endif

} // namespace daw::app::native
