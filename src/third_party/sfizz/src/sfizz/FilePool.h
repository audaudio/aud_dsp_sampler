// SPDX-License-Identifier: BSD-2-Clause

// Copyright (c) 2019-2020, Paul Ferrand, Andrea Zanellato
// All rights reserved.

// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:

// 1. Redistributions of source code must retain the above copyright notice, this
//    list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.

// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
// ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// Modified by Audanika for aud_dsp_sampler (ticket 25 of aud_audio_pm): the
// sample data lives in one store per process that every synth shares; the
// pool of a synth holds references into it. The streaming reads run as tasks
// of the engine instead of the pool's own threads and thread pool, the store
// frees what nobody reads when the engine's sample cache budget asks for it,
// and nothing allocates or frees on the audio thread.

#pragma once
#include "Config.h"
#include "Defaults.h"
#include "AudioBuffer.h"
#include "AudioSpan.h"
#include "FileId.h"
#include "FileMetadata.h"
#include "SIMDHelpers.h"
#include "utility/LeakDetector.h"
#include "utility/MemoryHelpers.h"
#include <ghc/fs_std.hpp>
#include <absl/container/flat_hash_map.h>
#include <absl/types/optional.h>
#include <absl/strings/string_view.h>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace sfz {
using FileAudioBuffer = AudioBuffer<float, 2, config::defaultAlignment,
                                    sfz::config::excessFileFrames, sfz::config::excessFileFrames>;
using FileAudioBufferPtr = std::shared_ptr<FileAudioBuffer>;

struct FileInformation {
    int64_t end { Default::sampleEnd };
    int64_t maxOffset { 0 };
    int64_t loopStart { Default::loopStart };
    int64_t loopEnd { Default::loopEnd };
    bool hasLoop { false };
    double sampleRate { config::defaultSampleRate };
    int numChannels { 0 };
    int rootKey { 0 };
    absl::optional<WavetableInfo> wavetable;
};

/**
 * @brief What the pool of a synth needs from its engine: a worker thread for
 * the streaming reads and an account for the bytes of the store (ticket 25).
 * The aud_dsp_sampler node fills it over the host api of its engine, whose
 * context stays valid while a task it posted runs.
 */
struct FileHost {
    void* context { nullptr };
    /** [realtime] Runs `run(user)` on a worker thread; false when it cannot. */
    bool (*post)(void* context, void (*run)(void*), void* user) { nullptr };
    /** [any thread] The store holds `delta` bytes more or fewer. */
    void (*bytesChanged)(void* context, int64_t delta) { nullptr };
};

/**
 * @brief A sample file of the store: its information, its head - the frames
 * a voice plays at once - and, once a voice asked for it, the whole file
 * streamed by a task. Shared by every pool that loaded the same file.
 */
struct FileData
{
    enum class Status { Invalid, Preloaded, Queued, Streaming, Done, GarbageCollecting };
    FileData() = default;
    FileData(const FileData& other) = delete;
    FileData& operator=(const FileData& other) = delete;

    AudioSpan<const float> getData()
    {
        ASSERT(readerCount > 0);
        if (status != Status::GarbageCollecting && availableFrames > preloadedData.getNumFrames())
            return AudioSpan<const float>(fileData).first(availableFrames);
        else
            return AudioSpan<const float>(preloadedData);
    }

    /** Whether every frame of the file can be read, the head or the stream. */
    bool isComplete() const noexcept
    {
        return fullyLoaded || status.load() == Status::Done;
    }

    FileAudioBuffer preloadedData;
    FileInformation information;
    FileAudioBuffer fileData {};
    std::atomic<Status> status { Status::Invalid };
    bool fullyLoaded { false };
    std::atomic<size_t> availableFrames { 0 };
    std::atomic<int> readerCount { 0 };
    // The host time in nanoseconds of the last use: a reader that left, a
    // pool that let go. The trims free the oldest first.
    std::atomic<int64_t> lastUsedNs { 0 };
    // The stream could not open the file, e.g. it was deleted after the
    // load: no voice asks for it again, the voices play the head.
    std::atomic<bool> unreadable { false };

    // The store's own bookkeeping, set once at creation.
    fs::path path;
    bool reverse { false };
    // The host whose worker streams the file; written by the request that
    // queued the task, read by the task.
    FileHost streamHost {};

    size_t headBytes() const noexcept;
    size_t streamBytes() const noexcept;

    LEAK_DETECTOR(FileData);
};

class FileDataHolder {
public:
    FileDataHolder() = default;
    FileDataHolder(const FileDataHolder&) = delete;
    FileDataHolder& operator=(const FileDataHolder&) = delete;
    FileDataHolder(FileDataHolder&& other)
    {
        this->data = other.data;
        other.data = nullptr;
    }
    FileDataHolder& operator=(FileDataHolder&& other)
    {
        if (this != &other) {
            reset();
            this->data = other.data;
            other.data = nullptr;
        }
        return *this;
    }
    FileDataHolder(FileData* data) : data(data)
    {
        if (!data)
            return;

        data->readerCount += 1;
    }
    void reset();
    ~FileDataHolder()
    {
        ASSERT(!data || data->readerCount > 0);
        reset();
    }
    FileData& operator*() { return *data; }
    FileData* operator->() { return data; }
    explicit operator bool() const { return data != nullptr; }
private:
    FileData* data { nullptr };
    LEAK_DETECTOR(FileDataHolder);
};

/**
 * @brief The sample files of the process (ticket 25), shared by the pools of
 * every synth of every node and engine. A file is loaded once - the head, or
 * all of it - and stays while a pool holds it; streamed data stays while a
 * voice reads it. Beyond that the store keeps what it loaded until the
 * engine's budget asks for room: a trim frees the streamed data nobody reads
 * and the files no pool holds, the least recently used first.
 */
class SampleStore {
public:
    /** The store of the process; never destroyed. */
    static SampleStore& get();

    /** [offline] The information of a file, read once per process. */
    absl::optional<FileInformation> information(const fs::path& file, bool reverse);

    /**
     * @brief [offline] A file with at least `frames` frames in its head - the
     * whole file when `frames` reaches its length -, shared with every pool
     * that asked for the same. Loads on the calling thread; a file loaded with
     * a shorter head is loaded again for this request while its holders keep
     * theirs. NULL when the file cannot be read.
     */
    std::shared_ptr<FileData> acquire(const fs::path& file, bool reverse,
                                      uint32_t frames, const FileHost& host);

    /**
     * @brief [realtime] Asks for the whole file of a voice: queues its
     * streaming task on the worker of `host` unless it is complete, queued or
     * streaming already. False when the task could not be queued.
     */
    bool requestStream(FileData& data, const FileHost& host) noexcept;

    /**
     * @brief [offline] Frees streamed data no voice reads and files no pool
     * holds, the least recently used first, until `bytes` are freed or nothing
     * is left; returns the bytes freed.
     */
    uint64_t trim(uint64_t bytes);

    /** [any thread] The bytes the store holds. */
    int64_t bytes() const noexcept { return bytes_.load(); }

    /** [offline] The files of the store; for tests. */
    size_t size();

private:
    SampleStore() = default;
    static void streamTask(void* user);
    void changed(const FileHost& host, int64_t delta) noexcept;

    std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<FileData>> files_;
    std::unordered_map<std::string, FileInformation> information_;
    std::atomic<int64_t> bytes_ { 0 };
};

/**
 * @brief The files of one synth: references into the store, the head of every
 * sample its regions play and the whole files it loaded for wavetables and
 * silence checks. A voice asks for the whole file when it starts; the store's
 * task streams it while the voice plays the head.
 */
class FilePool {
public:
    FilePool();
    ~FilePool();

    void setRootDirectory(const fs::path& directory) noexcept { rootDirectory = directory; }
    size_t getNumPreloadedSamples() const noexcept { return preloadedFiles.size() + loadedFiles.size(); }

    /**
     * @brief [offline] The bytes the pool pins in the store: the heads and the
     * whole files it holds, each file once. A trim never frees them.
     */
    uint64_t pinnedBytes() const;

    /**
     * @brief The host of the synth (ticket 25): the worker of the streaming
     * reads and the account of the bytes. Without one the pool reads whole
     * files at once.
     */
    void setHost(const FileHost& host) noexcept { host_ = host; }

    /**
     * @brief Called with the progress of a load after every file; returning
     * false cancels the load (ticket 25).
     */
    void setLoadObserver(std::function<bool(float)> observer) { observer_ = std::move(observer); }

    /** Reports the progress of a load; false when it was cancelled. */
    bool reportProgress(float progress);

    /** Whether the observer cancelled the current load. */
    bool loadCancelled() const noexcept { return cancelled_; }

    absl::optional<FileInformation> getFileInformation(const FileId& fileId) noexcept;
    bool preloadFile(const FileId& fileId, uint32_t maxOffset) noexcept;
    FileDataHolder loadFile(const FileId& fileId) noexcept;
    FileDataHolder loadFromRam(const FileId& fileId, const std::vector<char>& data) noexcept;
    bool checkSample(std::string& filename) const noexcept;
    bool checkSampleId(FileId& fileId) const noexcept;
    void clear();
    void resetPreloadCallCounts() noexcept;
    void removeUnusedPreloadedData() noexcept;

    /**
     * @brief [realtime] A handle on a file for a voice; asks the store to
     * stream the whole file unless the head holds it.
     */
    FileDataHolder getFilePromise(const std::shared_ptr<FileId>& fileId) noexcept;
    void setPreloadSize(uint32_t preloadSize) noexcept { this->preloadSize = preloadSize; }
    uint32_t getPreloadSize() const noexcept { return preloadSize; }
    void emptyFileLoadingQueues() noexcept {}
    /**
     * @brief The streams of the store run as tasks of the engine; an offline
     * render waits for them between its blocks, so there is nothing to wait
     * for here.
     */
    void waitForBackgroundLoading() noexcept {}
    void setRamLoading(bool loadInRam) noexcept { this->loadInRam = loadInRam; }
    /** The engine's budget frees memory now; nothing collects on a timer. */
    void triggerGarbageCollection() noexcept {}

    /** [realtime] Asks the store again for the whole file of a voice. */
    void requestStream(FileData& data) noexcept
    {
        if (!SampleStore::get().requestStream(data, host_))
            streamFailures_ += 1;
    }

    /** [realtime] A voice outran the stream of its file in a block. */
    void countUnderflow() noexcept { underflows_ += 1; }

    /** [realtime] Streams that could not be queued; read and cleared by the node. */
    uint32_t takeStreamFailures() noexcept { return streamFailures_.exchange(0); }

    /** [realtime] Blocks of voices that outran their stream; read and cleared by the node. */
    uint32_t takeUnderflows() noexcept { return underflows_.exchange(0); }

private:
    struct Reference {
        std::shared_ptr<FileData> data;
        int preloadCallCount { 0 };
    };

    fs::path rootDirectory;
    bool loadInRam { config::loadInRam };
    uint32_t preloadSize { config::preloadSize };
    FileHost host_ {};
    std::function<bool(float)> observer_;
    bool cancelled_ { false };
    std::atomic<uint32_t> streamFailures_ { 0 };
    std::atomic<uint32_t> underflows_ { 0 };

    absl::flat_hash_map<FileId, Reference> preloadedFiles;
    absl::flat_hash_map<FileId, Reference> loadedFiles;
    LEAK_DETECTOR(FilePool);
};
}
