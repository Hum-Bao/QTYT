#pragma once

#include "utils/StringUtils.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#else
#include <dlfcn.h>
#endif

#include <string>
#include <string_view>

namespace lightyt {

class DynamicLibrary {
public:
#ifdef _WIN32
    using HandleType = HMODULE;
#else
    using HandleType = void *;
#endif

    DynamicLibrary() = default;
    ~DynamicLibrary() = default;

    DynamicLibrary(const DynamicLibrary &) = delete;
    DynamicLibrary &operator=(const DynamicLibrary &) = delete;
    DynamicLibrary(DynamicLibrary &&other) noexcept : m_handle(other.m_handle) { other.m_handle = nullptr; }
    DynamicLibrary &operator=(DynamicLibrary &&other) noexcept {
        if (this != &other) {
            unload();
            m_handle = other.m_handle;
            other.m_handle = nullptr;
        }
        return *this;
    }

    bool load(std::string_view name) {
        unload();
#ifdef _WIN32
        std::wstring wName = utils::toWString(name);
        m_handle = LoadLibraryW(wName.c_str());
        if (m_handle == nullptr) {
            m_handle = LoadLibraryA(std::string(name).c_str());
        }
#else
        m_handle = dlopen(std::string(name).c_str(), RTLD_LAZY);
#endif
        return m_handle != nullptr;
    }

    void unload() noexcept {
        if (m_handle != nullptr) {
#ifdef _WIN32
            FreeLibrary(m_handle);
#else
            dlclose(m_handle);
#endif
            m_handle = nullptr;
        }
    }

    [[nodiscard]] bool isLoaded() const noexcept { return m_handle != nullptr; }

    template<typename FuncT>
    [[nodiscard]] FuncT getSymbol(const char *symbolName) const noexcept {
        if (m_handle == nullptr)
            return nullptr;
#ifdef _WIN32
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
        return reinterpret_cast<FuncT>(GetProcAddress(m_handle, symbolName));
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#else
        return reinterpret_cast<FuncT>(dlsym(m_handle, symbolName));
#endif
    }

private:
    HandleType m_handle = nullptr;
};

}  // namespace lightyt
