// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

#include "aud_dsp_sampler.h"

#include "FilePool.h"

#include "aud_sfz_node.hpp"
#include "aud_sfz_voice_budget.hpp"

namespace {

// ###########################################################################
// aud.sampler.sfz

const AudBusDescriptor kOutputs[] = {
    {sizeof(AudBusDescriptor), "out", "Output", AUD_BUS_MAIN, 2, 16, 2},
};

const AudEventPortDescriptor kInputs[] = {
    {sizeof(AudEventPortDescriptor), "midi", "MIDI",
     AUD_EVENT_PORT_MIDI | AUD_EVENT_PORT_CONTROL, 0},
};

const AudStringKeyDescriptor kKeys[] = {
    {sizeof(AudStringKeyDescriptor), AUD_SFZ_KEY_FILE, "sfz_file", "SFZ file"},
    {sizeof(AudStringKeyDescriptor), AUD_SFZ_KEY_VIRTUAL_PATH,
     "sfz_virtual_path", "Virtual path of an SFZ text"},
    {sizeof(AudStringKeyDescriptor), AUD_SFZ_KEY_TEXT, "sfz_text", "SFZ text"},
    {sizeof(AudStringKeyDescriptor), AUD_SFZ_KEY_SCALA_FILE, "scala_file",
     "Scala file"},
    {sizeof(AudStringKeyDescriptor), AUD_SFZ_KEY_SCALA_TEXT, "scala_text",
     "Scala text"},
};

const AudNodeVTable kVTable = {
    sizeof(AudNodeVTable), aud_sfz::create,    aud_sfz::destroy,
    aud_sfz::prepare,      aud_sfz::reset,     aud_sfz::setParam,
    aud_sfz::process,      aud_sfz::setString, nullptr,
    aud_sfz::getTail,      nullptr,            nullptr,
};

const AudNodeDescriptor kDescriptor = {
    sizeof(AudNodeDescriptor),
    AUD_ABI_VERSION_MAJOR,
    AUD_ABI_VERSION_MINOR,
    1,
    AUD_DSP_SAMPLER_SFZ_TYPE_ID,
    "SFZ sampler",
    "Audanika",
    AUD_NODE_CAP_VARIABLE_BLOCK | AUD_NODE_CAP_EVENTS | AUD_NODE_CAP_STRINGS |
        AUD_NODE_CAP_TAIL | AUD_NODE_CAP_TRANSPORT | AUD_NODE_CAP_MESSAGES,
    0,
    0,
    1,
    nullptr,
    kOutputs,
    1,
    0,
    kInputs,
    nullptr,
    AUD_SFZ_NUM_PARAMS,
    sizeof(kKeys) / sizeof(kKeys[0]),
    aud_sfz::kParams,
    kKeys,
    &kVTable,
};

}  // namespace

AUD_EXPORT int32_t aud_dsp_sampler_register(const AudHostApi* host) {
  if (host == nullptr || host->register_node_type == nullptr) {
    return AUD_ERROR_INVALID_ARGUMENT;
  }
  if (host->abi_major != AUD_ABI_VERSION_MAJOR) return AUD_ERROR_ABI_MAJOR;
  if (host->struct_size < sizeof(AudHostApi)) return AUD_ERROR_ABI_MINOR;
  return host->register_node_type(host->host, &kDescriptor);
}

AUD_EXPORT int64_t aud_dsp_sampler_store_bytes(void) {
  return sfz::SampleStore::get().bytes();
}

AUD_EXPORT int32_t aud_dsp_sampler_store_files(void) {
  return static_cast<int32_t>(sfz::SampleStore::get().size());
}

AUD_EXPORT void aud_dsp_sampler_set_voice_budget(int32_t voices) {
  aud_sfz::VoiceBudget::get().setBudget(voices);
}

AUD_EXPORT int32_t aud_dsp_sampler_voice_budget(void) {
  return aud_sfz::VoiceBudget::get().budget();
}

AUD_EXPORT void aud_dsp_sampler_set_voice_reserve(int32_t voices) {
  aud_sfz::VoiceBudget::get().setReserve(voices);
}

AUD_EXPORT int32_t aud_dsp_sampler_voice_reserve(void) {
  return aud_sfz::VoiceBudget::get().reserve();
}

AUD_EXPORT int32_t aud_dsp_sampler_voice_stats(AudSfzVoiceStats* out) {
  if (out == nullptr || out->struct_size < sizeof(AudSfzVoiceStats)) {
    return AUD_ERROR_INVALID_ARGUMENT;
  }
  const uint32_t size = out->struct_size;
  *out = aud_sfz::VoiceBudget::get().stats();
  out->struct_size = size;
  return AUD_OK;
}
