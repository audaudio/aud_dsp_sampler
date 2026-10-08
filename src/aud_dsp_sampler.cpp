// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

#include "aud_dsp_sampler.h"

#include <algorithm>
#include <cstdint>
#include <new>
#include <string>

#include "sfizz.h"

namespace {

// ###########################################################################
// aud.sampler.sfz

struct SamplerNode {
  sfizz_synth_t* synth = nullptr;
  std::string virtualPath = "/aud_dsp_sampler/virtual.sfz";
  uint32_t channels = 2;
};

const AudParamDescriptor kSamplerParams[] = {
    {sizeof(AudParamDescriptor), "volume", "Volume", "dB", -60.0f, 6.0f, 0.0f},
};

int midiValue(float value) {
  return std::min(127, std::max(0, static_cast<int>(value * 127.0f + 0.5f)));
}

void* samplerCreate(const AudNodeDescriptor*, const AudHostApi* host) {
  auto* node = new (std::nothrow) SamplerNode();
  if (node == nullptr) return nullptr;
  node->synth = sfizz_create_synth();
  if (node->synth == nullptr) {
    if (host != nullptr && host->log != nullptr) {
      host->log(host->host, AUD_LOG_ERROR, "aud.sampler.sfz: sfizz not created");
    }
    delete node;
    return nullptr;
  }
  return node;
}

void samplerDestroy(void* instance) {
  auto* node = static_cast<SamplerNode*>(instance);
  sfizz_free(node->synth);
  delete node;
}

int32_t samplerPrepare(void* instance, double sampleRate, uint32_t maxFrames,
                       uint32_t channels) {
  auto* node = static_cast<SamplerNode*>(instance);
  if (channels == 0 || channels % 2 != 0) return AUD_ERROR_INVALID_ARGUMENT;
  node->channels = channels;
  sfizz_set_sample_rate(node->synth, static_cast<float>(sampleRate));
  sfizz_set_samples_per_block(node->synth, static_cast<int>(maxFrames));
  return AUD_OK;
}

void samplerReset(void* instance) {
  sfizz_all_sound_off(static_cast<SamplerNode*>(instance)->synth);
}

void samplerSetParam(void* instance, uint32_t index, float value) {
  if (index == 0) sfizz_set_volume(static_cast<SamplerNode*>(instance)->synth, value);
}

void samplerEvent(void* instance, const AudEvent* event) {
  sfizz_synth_t* synth = static_cast<SamplerNode*>(instance)->synth;
  const int delay = static_cast<int>(event->sample_offset);
  const int number = static_cast<int>(event->number);
  switch (event->type) {
    case AUD_EVENT_NOTE_ON:
      sfizz_send_note_on(synth, delay, number, std::max(1, midiValue(event->value)));
      break;
    case AUD_EVENT_NOTE_OFF:
      sfizz_send_note_off(synth, delay, number, 0);
      break;
    case AUD_EVENT_CONTROL:
      sfizz_send_cc(synth, delay, number, midiValue(event->value));
      break;
    default:
      break;
  }
}

void samplerProcess(void* instance, const AudProcessContext* context) {
  auto* node = static_cast<SamplerNode*>(instance);
  sfizz_render_block(node->synth, const_cast<float**>(context->outputs),
                     static_cast<int>(node->channels),
                     static_cast<int>(context->frames));
}

int32_t samplerSetString(void* instance, uint32_t key, const char* value) {
  auto* node = static_cast<SamplerNode*>(instance);
  switch (key) {
    case AUD_SFZ_SAMPLER_KEY_FILE:
      return sfizz_load_file(node->synth, value) ? AUD_OK : AUD_ERROR_FAILED;
    case AUD_SFZ_SAMPLER_KEY_VIRTUAL_PATH:
      node->virtualPath = value;
      return AUD_OK;
    case AUD_SFZ_SAMPLER_KEY_TEXT:
      return sfizz_load_string(node->synth, node->virtualPath.c_str(), value)
                 ? AUD_OK
                 : AUD_ERROR_FAILED;
    default:
      return AUD_ERROR_INVALID_ARGUMENT;
  }
}

const AudNodeVTable kSamplerVTable = {
    sizeof(AudNodeVTable), samplerCreate,   samplerDestroy, samplerPrepare,
    samplerReset,          samplerSetParam, samplerEvent,   samplerProcess,
    samplerSetString,
};

const AudNodeDescriptor kSamplerDescriptor = {
    sizeof(AudNodeDescriptor),
    AUD_ABI_VERSION_MAJOR,
    AUD_ABI_VERSION_MINOR,
    AUD_DSP_SAMPLER_SFZ_TYPE_ID,
    "SFZ sampler (sfizz)",
    AUD_NODE_CAP_VARIABLE_BLOCK | AUD_NODE_CAP_EVENTS | AUD_NODE_CAP_STRINGS,
    0,
    1,
    1,
    kSamplerParams,
    &kSamplerVTable,
};

}  // namespace

AUD_EXPORT int32_t aud_dsp_sampler_register(const void* host_api) {
  const auto* host = static_cast<const AudHostApi*>(host_api);
  if (host == nullptr || host->struct_size < sizeof(AudHostApi) ||
      host->register_node_type == nullptr) {
    return AUD_ERROR_INVALID_ARGUMENT;
  }
  if (host->abi_major != AUD_ABI_VERSION_MAJOR) return AUD_ERROR_ABI_MAJOR;
  return host->register_node_type(host->host, &kSamplerDescriptor);
}
