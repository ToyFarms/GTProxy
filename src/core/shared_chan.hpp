#pragma once

#include <unordered_map>
#include <windows.h>
#include <string>
#include <atomic>
#include <cassert>
#include <vector>
#include <stdexcept>
#include <iostream>

struct ChannelHeader {
    std::atomic<uint32_t> head;
    std::atomic<uint32_t> tail;
    uint32_t capacity;    // size of data buffer in bytes
    // followed immediately by `capacity` bytes of data[]
};

class SharedChannel {
public:
    SharedChannel(const std::string& name, uint32_t capacity, bool create)
     : _name(name), _capacity(capacity)
    {
        std::string shmName = "Channel_" + name + "_SHM";   // drop Global
        std::string evDataName = "Channel_" + name + "_DATA";
        std::string evSpaceName = "Channel_" + name + "_SPACE";
        SIZE_T totalSize = sizeof(ChannelHeader) + capacity;

        // 1) Create or open the file mapping
        _hMap = CreateFileMappingA(
            INVALID_HANDLE_VALUE,    // page‐file backed
            nullptr,                 // default security
            PAGE_READWRITE,
            0,                        // high  DWORD of max size
            (DWORD)totalSize,        // low   DWORD of max size
            shmName.c_str()
        );
        if (!_hMap) {
            DWORD err = GetLastError();
            throw std::runtime_error("CreateFileMapping failed: " + std::to_string(err));
        }
        bool alreadyExisted = (GetLastError() == ERROR_ALREADY_EXISTS);

        // 2) Map it into our address space
        _hdr = reinterpret_cast<ChannelHeader*>(
            MapViewOfFile(_hMap,
                          FILE_MAP_ALL_ACCESS,
                          0, 0,
                          totalSize)
        );
        if (!_hdr) {
            DWORD err = GetLastError();
            CloseHandle(_hMap);
            throw std::runtime_error("MapViewOfFile failed: " + std::to_string(err));
        }

        // 3) Initialize header _only_ if we really just created it
        if (create && !alreadyExisted) {
            _hdr->head = 0;
            _hdr->tail = 0;
            _hdr->capacity = capacity;
        }

        _buf = reinterpret_cast<uint8_t*>(_hdr + 1);

        // 4) Create/open events in the same (local) namespace
        _hEvData  = CreateEventA(nullptr, FALSE, FALSE, evDataName.c_str());
        if (!_hEvData) {
            DWORD err = GetLastError();
            UnmapViewOfFile(_hdr);
            CloseHandle(_hMap);
            throw std::runtime_error("CreateEvent(DATA) failed: " + std::to_string(err));
        }
        _hEvSpace = CreateEventA(nullptr, FALSE, FALSE, evSpaceName.c_str());
        if (!_hEvSpace) {
            DWORD err = GetLastError();
            CloseHandle(_hEvData);
            UnmapViewOfFile(_hdr);
            CloseHandle(_hMap);
            throw std::runtime_error("CreateEvent(SPACE) failed: " + std::to_string(err));
        }
    }

    ~SharedChannel() {
        UnmapViewOfFile(_hdr);
        CloseHandle(_hMap);
        CloseHandle(_hEvData);
        CloseHandle(_hEvSpace);
    }

    // // Single‐producer send:
    // bool send(const uint8_t* data, uint32_t len, DWORD timeout_ms = INFINITE) {
    //     assert(len < _capacity);
    //     uint32_t cap = _hdr->capacity;
    //     while (true) {
    //         uint32_t head = _hdr->head.load(std::memory_order_acquire);
    //         uint32_t tail = _hdr->tail.load(std::memory_order_acquire);
    //         uint32_t used = (tail + cap - head) % cap;
    //         uint32_t free = cap - used - 1;    // leave one byte empty
    //         if (free >= len) {
    //             uint32_t idx = tail % cap;
    //             if (idx + len <= cap) {
    //                 memcpy(_buf + idx, data, len);
    //             } else {
    //                 // wrap
    //                 uint32_t n0 = cap - idx;
    //                 memcpy(_buf + idx, data, n0);
    //                 memcpy(_buf, data + n0, len - n0);
    //             }
    //             _hdr->tail.fetch_add(len, std::memory_order_release);
    //             SetEvent(_hEvData);
    //             return true;
    //         }
    //         // wait for space
    //         if (WaitForSingleObject(_hEvSpace, timeout_ms) != WAIT_OBJECT_0)
    //             return false;
    //     }
    // }

    bool send(const uint8_t* data, uint32_t len) {
        assert(len < _capacity);
        uint32_t cap  = _hdr->capacity;
        // load current pointers
        uint32_t head = _hdr->head.load(std::memory_order_acquire);
        uint32_t tail = _hdr->tail.load(std::memory_order_acquire);

        // compute used and free
        uint32_t used = (tail + cap - head) % cap;
        uint32_t free = cap - used;

        // if not enough room, advance head to drop oldest bytes
        if (free < len) {
            // drop exactly enough bytes
            uint32_t drop = len - free;
            head = (head + drop) % cap;
            _hdr->head.store(head, std::memory_order_release);
            // now free == cap - ((tail-head)%cap) >= len
        }

        // write at tail
        uint32_t idx = tail % cap;
        if (idx + len <= cap) {
            memcpy(_buf + idx, data, len);
        } else {
            uint32_t n0 = cap - idx;
            memcpy(_buf + idx,       data,   n0);
            memcpy(_buf,             data+n0, len - n0);
        }
        // advance tail
        tail = (tail + len) % cap;
        _hdr->tail.store(tail, std::memory_order_release);

        // signal that data is available
        SetEvent(_hEvData);
        return true;
    }

    bool send(const std::byte* data, uint32_t len) {
        return send(reinterpret_cast<const uint8_t*>(data), len);
    }

    // Single‐consumer receive (blocking up to timeout):
    bool recv(uint8_t* outBuf, uint32_t maxLen, uint32_t& outLen, DWORD timeout_ms = INFINITE) {
        // wait until there's data
        if (WaitForSingleObject(_hEvData, timeout_ms) != WAIT_OBJECT_0)
            return false;

        uint32_t head = _hdr->head.load(std::memory_order_acquire);
        uint32_t tail = _hdr->tail.load(std::memory_order_acquire);
        uint32_t cap  = _hdr->capacity;
        uint32_t used = (tail + cap - head) % cap;
        outLen = std::min(used, maxLen);
        uint32_t idx = head % cap;
        if (idx + outLen <= cap) {
            memcpy(outBuf, _buf + idx, outLen);
        } else {
            uint32_t n0 = cap - idx;
            memcpy(outBuf, _buf + idx, n0);
            memcpy(outBuf + n0, _buf, outLen - n0);
        }
        _hdr->head.fetch_add(outLen, std::memory_order_release);
        SetEvent(_hEvSpace);
        return true;
    }

    bool recv(std::byte* outBuf, uint32_t maxLen, uint32_t& outLen, DWORD timeout_ms = INFINITE) {
        return recv(reinterpret_cast<uint8_t*>(outBuf), maxLen, outLen, timeout_ms);
    }

    HANDLE dataEvent()  const { return _hEvData; }
    HANDLE spaceEvent() const { return _hEvSpace; }

private:
    std::string _name;
    uint32_t    _capacity;
    HANDLE      _hMap, _hEvData, _hEvSpace;
    ChannelHeader* _hdr;
    uint8_t*    _buf;
};


class ChannelManager {
public:
    // create or open
    void add_channel(const std::string& name, uint32_t capacity, bool create = false) {
        auto* ch = new SharedChannel(name, capacity, create);
        channels_[name] = ch;
        events_.push_back(ch->dataEvent());
        names_.push_back(name);
    }

    // send to a named channel (uint8_t version)
    bool send_to(const std::string& name,
                 const std::vector<uint8_t>& data)
    {
        auto it = channels_.find(name);
        if (it == channels_.end()) return false;
        return it->second->send(data.data(),
                                static_cast<uint32_t>(data.size()));
    }

    // send to a named channel (std::byte version)
    bool send_to(const std::string& name,
                 const std::vector<std::byte>& data)
    {
        auto it = channels_.find(name);
        if (it == channels_.end()) return false;
        return it->second->send(
            reinterpret_cast<const uint8_t*>(data.data()),
            static_cast<uint32_t>(data.size())
        );
    }

    bool send_to(const std::string& name)
    {
        // just forward with an empty uint8_t vector
        return send_to(name, std::vector<uint8_t>{});
    }

    // wait for any channel and recv (uint8_t version)
    bool recv_any(std::string&       out_name,
                  std::vector<uint8_t>& out_data,
                  uint32_t            max_len   = 4096,
                  DWORD               timeout_ms= INFINITE)
    {
        if (events_.empty()) return false;

        DWORD idx = WaitForMultipleObjects(
            static_cast<DWORD>(events_.size()),
            events_.data(),
            FALSE,
            timeout_ms
        );
        if (idx < WAIT_OBJECT_0 ||
            idx >= WAIT_OBJECT_0 + events_.size())
            return false;

        size_t ch_i = idx - WAIT_OBJECT_0;
        out_name = names_[ch_i];
        out_data.resize(max_len);

        uint32_t recv_len;
        if (!channels_[out_name]->recv(
                out_data.data(), max_len, recv_len, 0))
            return false;

        out_data.resize(recv_len);
        return true;
    }

    // wait for any channel and recv (std::byte version)
    bool recv_any(std::string&           out_name,
                  std::vector<std::byte>& out_data,
                  uint32_t                max_len    = 4096,
                  DWORD                   timeout_ms = INFINITE)
    {
        // first call the uint8_t version into a temp buffer
        std::vector<uint8_t> tmp;
        if (!recv_any(out_name, tmp, max_len, timeout_ms))
            return false;

        // reinterpret_cast the data into std::byte
        out_data.resize(tmp.size());
        std::memcpy(
            out_data.data(),
            tmp.data(),
            tmp.size() * sizeof(std::byte)
        );
        return true;
    }

    ~ChannelManager() {
        for (auto& p : channels_) delete p.second;
    }

private:
    std::unordered_map<std::string, SharedChannel*> channels_;
    std::vector<HANDLE>       events_;
    std::vector<std::string>  names_;
};
