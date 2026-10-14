// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// Tests the decoders of the sfizz fork
// (external/st_audiofile/src/st_audiofile_libs.c) on several threads at
// once: what they share between files - dr_flac what it knows of the CPU,
// stb_vorbis its CRC table - is built once per process (ticket 28).

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include "st_audiofile.h"

#include "aud_sampler_fixture.hpp"

using namespace aud_sampler_test;

namespace {

// The bytes of a 32-bit integer, the least significant first.
std::string littleEndian(uint32_t value) {
  std::string bytes;
  for (int i = 0; i < 4; ++i) bytes += static_cast<char>((value >> (8 * i)) & 0xff);
  return bytes;
}

// An Ogg page of one packet shorter than 255 bytes: the capture pattern,
// version 0, the flags, the granule position 0, the serial number, the
// number of the page, a CRC that the decoders do not check while they open
// a file, and one segment.
std::string oggPage(uint8_t flags, uint32_t number, const std::string& packet) {
  return std::string("OggS", 4) + '\0' + static_cast<char>(flags) + std::string(8, '\0') +
         littleEndian(1) + littleEndian(number) + std::string(4, '\0') + '\x01' +
         static_cast<char>(packet.size()) + packet;
}

// The headers of an Ogg Vorbis stream whose setup header is broken: no
// decoder opens it, but stb_vorbis builds its CRC table before it reads the
// setup, and dr_flac looks at the CPU before it looks at the file.
std::string vorbisHeaders() {
  constexpr uint8_t kFirstPage = 0x02;
  const std::string vorbis("vorbis", 6);
  // Version 0, one channel at 48 kHz, no bitrates, blocks of 256 and 2048
  // frames, the framing bit.
  const std::string identification = '\x01' + vorbis + littleEndian(0) + '\x01' +
                                     littleEndian(48000) + std::string(12, '\0') + '\xb8' +
                                     '\x01';
  // No vendor, no comments, the framing bit.
  const std::string comment = '\x03' + vorbis + littleEndian(0) + littleEndian(0) + '\x01';
  return oggPage(kFirstPage, 0, identification) + oggPage(0, 1, comment) +
         oggPage(0, 2, std::string(1, '\0'));
}

}  // namespace

// ############################################################################
// Files opened on several threads at once

AUD_TEST(sfizz_st_audiofile_opens_files_on_several_threads_at_once) {
  // The loads and the streams of the engine's workers open files at the
  // same time. st_open_file tries one decoder after the other: a WAV file
  // opens with the first, the Vorbis headers pass dr_flac and reach
  // stb_vorbis, and open with none. Both decoders wrote what they share on
  // every file they opened, while other threads read it.
  constexpr int kThreads = 4;
  constexpr int kOpens = 25;
  TempFolder folder;
  writeWav(folder.file("sine.wav"), sine(375, 0.1));
  writeText(folder.file("headers.ogg"), vorbisHeaders());
  const std::string wav = folder.file("sine.wav");
  const std::string ogg = folder.file("headers.ogg");
  std::atomic<bool> start{false};
  std::vector<int> opened(kThreads, 0);
  std::vector<int> refused(kThreads, 0);
  std::vector<std::thread> threads;
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&, t] {
      while (!start.load()) std::this_thread::yield();
      for (int i = 0; i < kOpens; ++i) {
        if (st_audio_file* file = st_open_file(wav.c_str())) {
          if (st_get_type(file) == st_audio_file_wav && st_get_frame_count(file) == 4800) {
            opened[t] += 1;
          }
          st_close(file);
        }
        st_audio_file* headers = st_open_file(ogg.c_str());
        if (headers == nullptr) {
          refused[t] += 1;
        } else {
          st_close(headers);
        }
      }
    });
  }
  start.store(true);
  for (std::thread& thread : threads) thread.join();
  AUD_CHECK(opened == std::vector<int>(kThreads, kOpens));
  AUD_CHECK(refused == std::vector<int>(kThreads, kOpens));
}
