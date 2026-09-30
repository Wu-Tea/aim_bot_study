#pragma once

#include <Windows.h>
#include <functional>
#include <stdexcept>
#include <string>
#include <thread>

namespace runtime_app {

// External process boundary. A blocking listener requests normal shutdown;
// no polling or window work is added to the controller tick.
class RuntimeStopSignal {
public:
    RuntimeStopSignal() {
        const auto name = L"Local\\cod_native_runtime_stop_" + std::to_wstring(GetCurrentProcessId());
        request_ = CreateEventW(nullptr, TRUE, FALSE, name.c_str());
        finished_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!request_ || !finished_) {
            if (request_) CloseHandle(request_);
            if (finished_) CloseHandle(finished_);
            throw std::runtime_error("cannot create runtime stop signal");
        }
    }
    class ListenerScope {
    public:
        explicit ListenerScope(RuntimeStopSignal& owner) : owner_(owner) {}
        ~ListenerScope() { owner_.finish(); }
        ListenerScope(const ListenerScope&) = delete;
        ListenerScope& operator=(const ListenerScope&) = delete;
    private:
        RuntimeStopSignal& owner_;
    };
    ListenerScope listen(std::function<void()> request_stop) {
        listener_ = std::thread([this, request_stop = std::move(request_stop)] {
            HANDLE events[] = {request_, finished_};
            if (WaitForMultipleObjects(2, events, FALSE, INFINITE) == WAIT_OBJECT_0)
                request_stop();
        });
        return ListenerScope(*this);
    }
    ~RuntimeStopSignal() {
        finish();
        CloseHandle(request_);
        CloseHandle(finished_);
    }
    RuntimeStopSignal(const RuntimeStopSignal&) = delete;
    RuntimeStopSignal& operator=(const RuntimeStopSignal&) = delete;
private:
    void finish() {
        SetEvent(finished_);
        if (listener_.joinable()) listener_.join();
    }
    HANDLE request_ = nullptr;
    HANDLE finished_ = nullptr;
    std::thread listener_;
};

} // namespace runtime_app
