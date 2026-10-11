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
// store of the process, the pools as references into it, the streams as
// tasks of the engine and the trims of its budget; see FilePool.h.

#include "FilePool.h"
#include "AudioReader.h"
#include "Buffer.h"
#include "AudioBuffer.h"
#include "AudioSpan.h"
#include "Config.h"
#include "utility/Debug.h"
#include <absl/container/flat_hash_set.h>
#include <absl/strings/match.h>
#include <algorithm>
#include <chrono>
#include <system_error>

namespace {

int64_t nowNs() noexcept
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

void readBaseFile(sfz::AudioReader& reader, sfz::FileAudioBuffer& output, uint32_t numFrames)
{
    output.reset();
    output.resize(numFrames);

    const unsigned channels = reader.channels();

    if (channels == 1) {
        output.addChannel();
        output.clear();
        reader.readNextBlock(output.channelWriter(0), numFrames);
    } else if (channels == 2) {
        output.addChannel();
        output.addChannel();
        output.clear();
        sfz::Buffer<float> tempReadBuffer { 2 * numFrames };
        reader.readNextBlock(tempReadBuffer.data(), numFrames);
        sfz::readInterleaved(tempReadBuffer, output.getSpan(0), output.getSpan(1));
    }
}

sfz::FileAudioBuffer readFromFile(sfz::AudioReader& reader, uint32_t numFrames)
{
    sfz::FileAudioBuffer baseBuffer;
    readBaseFile(reader, baseBuffer, numFrames);
    return baseBuffer;
}

// Reads the whole file in chunks; `filledFrames` counts what can be read.
// The buffer is allocated before the first frame counts, so a reader that
// sees a count past the head reads frames that are there.
void streamFromFile(sfz::AudioReader& reader, sfz::FileAudioBuffer& output, std::atomic<size_t>* filledFrames)
{
    const auto numFrames = static_cast<size_t>(reader.frames());
    const auto numChannels = reader.channels();
    const auto chunkSize = static_cast<size_t>(sfz::config::fileChunkSize);

    output.reset();
    output.addChannels(reader.channels());
    output.resize(numFrames);
    output.clear();

    sfz::Buffer<float> fileBlock { chunkSize * numChannels };
    size_t inputFrameCounter { 0 };
    size_t outputFrameCounter { 0 };
    bool inputEof = false;

    while (!inputEof && inputFrameCounter < numFrames)
    {
        auto thisChunkSize = std::min(chunkSize, numFrames - inputFrameCounter);
        const auto numFramesRead = static_cast<size_t>(
            reader.readNextBlock(fileBlock.data(), thisChunkSize));
        if (numFramesRead == 0)
            break;

        if (numFramesRead < thisChunkSize) {
            inputEof = true;
            thisChunkSize = numFramesRead;
        }
        const auto outputChunkSize = thisChunkSize;

        for (size_t chanIdx = 0; chanIdx < numChannels; chanIdx++) {
            const auto outputChunk = output.getSpan(chanIdx).subspan(outputFrameCounter, outputChunkSize);
            for (size_t i = 0; i < thisChunkSize; ++i)
                outputChunk[i] = fileBlock[i * numChannels + chanIdx];
        }
        inputFrameCounter += thisChunkSize;
        outputFrameCounter += outputChunkSize;

        if (filledFrames != nullptr)
            filledFrames->fetch_add(outputChunkSize);
    }
}

absl::optional<sfz::FileInformation> getReaderInformation(sfz::AudioReader* reader) noexcept
{
    const unsigned channels = reader->channels();
    if (channels != 1 && channels != 2)
        return {};

    sfz::FileInformation returnedValue;
    returnedValue.end = static_cast<uint32_t>(reader->frames()) - 1;
    returnedValue.sampleRate = static_cast<double>(reader->sampleRate());
    returnedValue.numChannels = static_cast<int>(channels);

    // Check for instrument info
    sfz::InstrumentInfo instrumentInfo {};
    if (reader->getInstrumentInfo(instrumentInfo)) {
        returnedValue.rootKey = clamp<uint8_t>(instrumentInfo.basenote, 0, 127);
        if (reader->type() == sfz::AudioReaderType::Forward) {
            if (instrumentInfo.loop_count > 0) {
                returnedValue.hasLoop = true;
                returnedValue.loopStart = instrumentInfo.loops[0].start;
                returnedValue.loopEnd =
                    min(returnedValue.end, static_cast<int64_t>(instrumentInfo.loops[0].end - 1));
            }
        } else {
            // TODO loops ignored when reversed
            //   prehaps it can make use of SF_LOOP_BACKWARD?
        }
    }

    // Check for wavetable info
    sfz::WavetableInfo wt {};
    if (reader->getWavetableInfo(wt))
        returnedValue.wavetable = wt;

    return returnedValue;
}

// The key of a file in the store: its path and its direction, with its size
// and time of change, so that a file changed on disk is read again.
// The count of a file time may be a 128-bit integer (Android's libc++),
// which std::to_string does not take.
std::string keyOf(const fs::path& file, bool reverse)
{
    std::error_code sizeError;
    std::error_code timeError;
    const auto size = fs::file_size(file, sizeError);
    const auto time = fs::last_write_time(file, timeError);
    const unsigned long long bytes = sizeError ? 0 : static_cast<unsigned long long>(size);
    const long long ticks = timeError ? 0 : static_cast<long long>(time.time_since_epoch().count());
    return file.string() + (reverse ? "|r|" : "|f|") + std::to_string(bytes) + "|" +
        std::to_string(ticks);
}

size_t bufferBytes(const sfz::FileAudioBuffer& buffer) noexcept
{
    return buffer.getNumFrames() * buffer.getNumChannels() * sizeof(float);
}

} // namespace

namespace sfz {

size_t FileData::headBytes() const noexcept { return bufferBytes(preloadedData); }

size_t FileData::streamBytes() const noexcept { return bufferBytes(fileData); }

void FileDataHolder::reset()
{
    if (!data)
        return;

    // The time first, so that a trim that sees no reader sees when it left.
    data->lastUsedNs = nowNs();
    data->readerCount -= 1;
    data = nullptr;
}

// ############################################################################
// The store of the process

SampleStore& SampleStore::get()
{
    // Never destroyed: a task may still stream at exit.
    static SampleStore* store = new SampleStore();
    return *store;
}

absl::optional<FileInformation> SampleStore::information(const fs::path& file, bool reverse)
{
    const std::string key = keyOf(file, reverse);
    {
        std::lock_guard<std::mutex> lock { mutex_ };
        const auto it = information_.find(key);
        if (it != information_.end())
            return it->second;
    }
    std::error_code ec;
    if (!fs::exists(file, ec))
        return {};
    AudioReaderPtr reader = createAudioReader(file, reverse, &ec);
    if (ec || !reader)
        return {};
    auto info = getReaderInformation(reader.get());
    if (info) {
        std::lock_guard<std::mutex> lock { mutex_ };
        information_[key] = *info;
    }
    return info;
}

std::shared_ptr<FileData> SampleStore::acquire(const fs::path& file, bool reverse,
                                               uint32_t frames, const FileHost& host)
{
    const std::string key = keyOf(file, reverse);
    const auto serves = [frames](const FileData& data) {
        return data.fullyLoaded ||
            (frames > 0 && data.preloadedData.getNumFrames() >= frames);
    };
    {
        std::lock_guard<std::mutex> lock { mutex_ };
        const auto it = files_.find(key);
        if (it != files_.end() && serves(*it->second)) {
            it->second->lastUsedNs = nowNs();
            return it->second;
        }
    }

    // Read outside the lock: other loads go on meanwhile. Two loads that miss
    // together read the file twice; the second shares the first's result.
    std::error_code ec;
    if (!fs::exists(file, ec))
        return nullptr;
    AudioReaderPtr reader = createAudioReader(file, reverse, &ec);
    if (ec || !reader)
        return nullptr;
    auto info = getReaderInformation(reader.get());
    if (!info)
        return nullptr;
    const auto total = static_cast<uint32_t>(reader->frames());
    const uint32_t toLoad = (frames == 0) ? total : std::min(total, frames);

    auto data = std::make_shared<FileData>();
    data->information = *info;
    data->preloadedData = readFromFile(*reader, toLoad);
    data->fullyLoaded = toLoad >= total;
    data->path = file;
    data->reverse = reverse;
    data->lastUsedNs = nowNs();
    data->status = FileData::Status::Preloaded;

    {
        std::lock_guard<std::mutex> lock { mutex_ };
        information_[key] = *info;
        auto& slot = files_[key];
        if (slot && serves(*slot)) {
            slot->lastUsedNs = nowNs();
            return slot;
        }
        // A file loaded with a shorter head stays with its holders; the store
        // keeps it under a key of its own until a trim finds it unheld.
        if (slot)
            files_.emplace(key + "|" + std::to_string(static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(slot.get()))), slot);
        slot = data;
    }
    changed(host, static_cast<int64_t>(data->headBytes()));
    return data;
}

bool SampleStore::requestStream(FileData& data, const FileHost& host) noexcept
{
    if (data.fullyLoaded || data.unreadable.load())
        return true;
    auto expected = FileData::Status::Preloaded;
    if (!data.status.compare_exchange_strong(expected, FileData::Status::Queued))
        return true; // queued, streaming, done, or being collected
    if (host.post == nullptr) {
        data.status = FileData::Status::Preloaded;
        return false;
    }
    // The post publishes the host to the task.
    data.streamHost = host;
    if (!host.post(host.context, &SampleStore::streamTask, &data)) {
        data.status = FileData::Status::Preloaded;
        return false;
    }
    return true;
}

// [offline] Streams a queued file. The store keeps the file while it is
// queued or streams: a trim leaves those alone.
void SampleStore::streamTask(void* user)
{
    FileData& data = *static_cast<FileData*>(user);
    auto expected = FileData::Status::Queued;
    if (!data.status.compare_exchange_strong(expected, FileData::Status::Streaming))
        return;
    std::error_code ec;
    AudioReaderPtr reader = createAudioReader(data.path, data.reverse, &ec);
    if (ec || !reader) {
        data.unreadable = true;
        data.status = FileData::Status::Preloaded;
        return;
    }
    streamFromFile(*reader, data.fileData, &data.availableFrames);
    data.status = FileData::Status::Done;
    get().changed(data.streamHost, static_cast<int64_t>(data.streamBytes()));
}

void SampleStore::changed(const FileHost& host, int64_t delta) noexcept
{
    bytes_ += delta;
    if (host.bytesChanged != nullptr)
        host.bytesChanged(host.context, delta);
}

uint64_t SampleStore::trim(uint64_t bytes)
{
    std::lock_guard<std::mutex> lock { mutex_ };
    uint64_t freed = 0;

    // First the streamed data no voice reads, the least recently used first.
    std::vector<FileData*> streamed;
    for (auto& file : files_) {
        FileData& data = *file.second;
        if (data.status.load() == FileData::Status::Done && data.readerCount.load() == 0)
            streamed.push_back(&data);
    }
    std::sort(streamed.begin(), streamed.end(), [](const FileData* a, const FileData* b) {
        return a->lastUsedNs.load() < b->lastUsedNs.load();
    });
    for (FileData* data : streamed) {
        if (freed >= bytes)
            break;
        auto expected = FileData::Status::Done;
        if (!data->status.compare_exchange_strong(expected, FileData::Status::GarbageCollecting))
            continue;
        // A voice that started meanwhile keeps the data.
        if (data->readerCount.load() != 0) {
            data->status = FileData::Status::Done;
            continue;
        }
        const size_t size = data->streamBytes();
        FileAudioBuffer garbage = std::move(data->fileData);
        data->fileData = FileAudioBuffer {};
        data->availableFrames = 0;
        data->status = FileData::Status::Preloaded;
        freed += size;
    }

    // Then the files no pool holds, the least recently used first.
    if (freed < bytes) {
        std::vector<std::pair<int64_t, std::string>> unheld;
        for (auto& file : files_) {
            const FileData& data = *file.second;
            const auto status = data.status.load();
            if (file.second.use_count() == 1 && data.readerCount.load() == 0 &&
                status != FileData::Status::Queued && status != FileData::Status::Streaming)
                unheld.emplace_back(data.lastUsedNs.load(), file.first);
        }
        std::sort(unheld.begin(), unheld.end());
        for (const auto& entry : unheld) {
            if (freed >= bytes)
                break;
            const auto it = files_.find(entry.second);
            freed += it->second->headBytes() + it->second->streamBytes();
            files_.erase(it);
        }
    }

    bytes_ -= static_cast<int64_t>(freed);
    return freed;
}

size_t SampleStore::size()
{
    std::lock_guard<std::mutex> lock { mutex_ };
    return files_.size();
}

// ############################################################################
// The pool of a synth

FilePool::FilePool() = default;

FilePool::~FilePool()
{
    clear();
}

uint64_t FilePool::pinnedBytes() const
{
    absl::flat_hash_set<const FileData*> counted;
    uint64_t bytes = 0;
    for (const auto* files : { &preloadedFiles, &loadedFiles }) {
        for (const auto& file : *files) {
            const FileData* data = file.second.data.get();
            if (counted.insert(data).second)
                bytes += data->headBytes();
        }
    }
    return bytes;
}

bool FilePool::reportProgress(float progress)
{
    if (cancelled_)
        return false;
    if (observer_ && !observer_(progress))
        cancelled_ = true;
    return !cancelled_;
}

bool FilePool::checkSample(std::string& filename) const noexcept
{
    fs::path path { rootDirectory / filename };
    std::error_code ec;
    if (fs::exists(path, ec))
        return true;

#if defined(_WIN32)
    return false;
#else
    fs::path oldPath = std::move(path);
    path = oldPath.root_path();

    static const fs::path dot { "." };
    static const fs::path dotdot { ".." };

    for (const fs::path& part : oldPath.relative_path()) {
        if (part == dot || part == dotdot) {
            path /= part;
            continue;
        }

        if (fs::exists(path / part, ec)) {
            path /= part;
            continue;
        }

        auto it = path.empty() ? fs::directory_iterator { dot, ec } : fs::directory_iterator { path, ec };
        if (ec) {
            DBG("Error creating a directory iterator for " << filename << " (Error code: " << ec.message() << ")");
            return false;
        }

        auto searchPredicate = [&part](const fs::directory_entry &ent) -> bool {
#if !defined(GHC_USE_WCHAR_T)
            return absl::EqualsIgnoreCase(
                ent.path().filename().native(), part.native());
#else
            return absl::EqualsIgnoreCase(
                ent.path().filename().u8string(), part.u8string());
#endif
        };

        while (it != fs::directory_iterator {} && !searchPredicate(*it))
            it.increment(ec);

        if (it == fs::directory_iterator {}) {
            DBG("File not found, could not resolve " << filename);
            return false;
        }

        path /= it->path().filename();
    }

    const auto newPath = fs::relative(path, rootDirectory, ec);
    if (ec) {
        DBG("Error extracting the new relative path for " << filename << " (Error code: " << ec.message() << ")");
        return false;
    }
    DBG("Updating " << filename << " to " << newPath);
    filename = newPath.string();
    return true;
#endif
}

bool FilePool::checkSampleId(FileId& fileId) const noexcept
{
    if (loadedFiles.contains(fileId))
        return true;

    std::string filename = fileId.filename();
    bool result = checkSample(filename);
    if (result)
        fileId = FileId(std::move(filename), fileId.isReverse());
    return result;
}

absl::optional<FileInformation> FilePool::getFileInformation(const FileId& fileId) noexcept
{
    const auto loaded = loadedFiles.find(fileId);
    if (loaded != loadedFiles.end())
        return loaded->second.data->information;

    const auto preloaded = preloadedFiles.find(fileId);
    if (preloaded != preloadedFiles.end())
        return preloaded->second.data->information;

    return SampleStore::get().information(rootDirectory / fileId.filename(), fileId.isReverse());
}

bool FilePool::preloadFile(const FileId& fileId, uint32_t maxOffset) noexcept
{
    const auto loaded = loadedFiles.find(fileId);
    if (loaded != loadedFiles.end()) {
        loaded->second.preloadCallCount++;
        return true;
    }

    // 0 asks the store for the whole file.
    const uint32_t frames = loadInRam ? 0 : maxOffset + preloadSize;
    const auto existing = preloadedFiles.find(fileId);
    if (existing != preloadedFiles.end()) {
        const FileData& data = *existing->second.data;
        if (data.fullyLoaded || (frames > 0 && data.preloadedData.getNumFrames() >= frames)) {
            existing->second.preloadCallCount++;
            return true;
        }
    }

    auto data = SampleStore::get().acquire(
        rootDirectory / fileId.filename(), fileId.isReverse(), frames, host_);
    if (!data)
        return false;
    Reference& reference = preloadedFiles[fileId];
    if (reference.data)
        reference.data->lastUsedNs = nowNs();
    reference.data = std::move(data);
    reference.preloadCallCount++;
    return true;
}

FileDataHolder FilePool::loadFile(const FileId& fileId) noexcept
{
    const auto existing = loadedFiles.find(fileId);
    if (existing != loadedFiles.end()) {
        existing->second.preloadCallCount++;
        return { existing->second.data.get() };
    }

    auto data = SampleStore::get().acquire(
        rootDirectory / fileId.filename(), fileId.isReverse(), 0, host_);
    if (!data)
        return {};
    Reference& reference = loadedFiles[fileId];
    reference.data = std::move(data);
    reference.preloadCallCount++;
    return { reference.data.get() };
}

// Data embedded in the SFZ text belongs to this pool alone; the store does
// not share or count it.
FileDataHolder FilePool::loadFromRam(const FileId& fileId, const std::vector<char>& data) noexcept
{
    const auto loaded = loadedFiles.find(fileId);
    if (loaded != loadedFiles.end())
        return { loaded->second.data.get() };

    auto reader = createAudioReaderFromMemory(data.data(), data.size(), fileId.isReverse());
    auto fileInformation = getReaderInformation(reader.get());
    if (!fileInformation)
        return {};
    const auto frames = static_cast<uint32_t>(reader->frames());
    auto file = std::make_shared<FileData>();
    file->information = *fileInformation;
    file->preloadedData = readFromFile(*reader, frames);
    file->fullyLoaded = true;
    file->status = FileData::Status::Preloaded;
    Reference& reference = loadedFiles[fileId];
    reference.data = std::move(file);
    reference.preloadCallCount++;
    DBG("Added a file " << fileId.filename());
    return { reference.data.get() };
}

FileDataHolder FilePool::getFilePromise(const std::shared_ptr<FileId>& fileId) noexcept
{
    const auto loaded = loadedFiles.find(*fileId);
    if (loaded != loadedFiles.end())
        return { loaded->second.data.get() };

    const auto preloaded = preloadedFiles.find(*fileId);
    if (preloaded == preloadedFiles.end()) {
        DBG("[sfizz] File not found in the preloaded files: " << fileId->filename());
        return {};
    }

    FileData* data = preloaded->second.data.get();
    // The reader counts before the request, so that a trim sees it.
    FileDataHolder holder { data };
    if (!data->isComplete() && !SampleStore::get().requestStream(*data, host_))
        streamFailures_ += 1;
    return holder;
}

void FilePool::clear()
{
    const int64_t now = nowNs();
    for (auto& file : preloadedFiles)
        file.second.data->lastUsedNs = now;
    for (auto& file : loadedFiles)
        file.second.data->lastUsedNs = now;
    preloadedFiles.clear();
    loadedFiles.clear();
    cancelled_ = false;
}

void FilePool::resetPreloadCallCounts() noexcept
{
    for (auto& preloadedFile: preloadedFiles)
        preloadedFile.second.preloadCallCount = 0;

    for (auto& loadedFile: loadedFiles)
        loadedFile.second.preloadCallCount = 0;
}

void FilePool::removeUnusedPreloadedData() noexcept
{
    const int64_t now = nowNs();
    for (auto it = preloadedFiles.begin(), end = preloadedFiles.end(); it != end; ) {
        auto copyIt = it++;
        if (copyIt->second.preloadCallCount == 0) {
            DBG("[sfizz] Removing unused preloaded data: " << copyIt->first.filename());
            copyIt->second.data->lastUsedNs = now;
            preloadedFiles.erase(copyIt);
        }
    }

    for (auto it = loadedFiles.begin(), end = loadedFiles.end(); it != end; ) {
        auto copyIt = it++;
        if (copyIt->second.preloadCallCount == 0) {
            DBG("[sfizz] Removing unused loaded data: " << copyIt->first.filename());
            copyIt->second.data->lastUsedNs = now;
            loadedFiles.erase(copyIt);
        }
    }
}

} // namespace sfz
