#include "fakerinput_output.h"
#include <iostream>
#include <string>

// Explicit desktop stimulus, never registered as an unattended CTest.
// This uses the exact output backend linked into the relay, with no source
// driver and no physical capture. No button or wheel reports are generated.
int wmain(int argc, wchar_t** argv) {
    if (argc != 2 || std::wstring(argv[1]) != L"--send-motion") {
        std::cerr << "Usage: mouse_virtual_output_probe --send-motion\n";
        return 1;
    }
    virtual_mouse::FakerOutput output;
    if (!output.open()) { std::cerr << "Open failed: " << output.error() << '\n'; return 2; }
    const int pattern[4][2]{{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (int i = 0; i < 64; ++i) {
        virtual_mouse::Report report{};
        report.x = static_cast<short>(pattern[i % 4][0]);
        report.y = static_cast<short>(pattern[i % 4][1]);
        if (!output.send(report)) { std::cerr << "Write failed: " << output.error() << '\n'; return 3; }
        Sleep(8);
    }
    std::cout << "Submitted 64 motion reports; receiver evidence is required.\n";
    return 0;
}
