// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// The entry point of aud_dsp_sampler (ticket 5, S0-mobile): registers the
// SFZ sampler node on sfizz with an engine through the C ABI of
// aud_audio_core. Ticket S9 adds the loaders, presets and the message API.

#ifndef AUD_DSP_SAMPLER_H
#define AUD_DSP_SAMPLER_H

#include <stdint.h>

#include "aud_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

// The type id of the sampler node. Parameter 0 is the volume in dB.
#define AUD_DSP_SAMPLER_SFZ_TYPE_ID "aud.sampler.sfz"

// The string settings of the sampler node (AudNodeVTable.set_string). They
// run on the control thread and must not run while the node renders:
// sfizz loads only while no realtime call is in flight, so load first and
// put the node into the chain afterwards.
enum {
  // Loads an SFZ file; the value is its path.
  AUD_SFZ_SAMPLER_KEY_FILE = 0,
  // Sets the virtual path that resolves the sample paths of a text load.
  AUD_SFZ_SAMPLER_KEY_VIRTUAL_PATH = 1,
  // Loads SFZ text relative to the virtual path.
  AUD_SFZ_SAMPLER_KEY_TEXT = 2,
};

// [control] Registers the node types with the host api of an engine
// (`const AudHostApi*`); AUD_OK or the first error code.
AUD_EXPORT int32_t aud_dsp_sampler_register(const void* host_api);

#ifdef __cplusplus
}
#endif

#endif  // AUD_DSP_SAMPLER_H
