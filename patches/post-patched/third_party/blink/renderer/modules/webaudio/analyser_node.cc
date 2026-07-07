/*
 * Copyright (C) 2010, Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1.  Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 */

#include "third_party/blink/renderer/modules/webaudio/analyser_node.h"

#include "third_party/blink/renderer/bindings/modules/v8/v8_analyser_options.h"
#include "third_party/blink/renderer/modules/webaudio/analyser_handler.h"
#include "third_party/blink/renderer/modules/webaudio/audio_graph_tracer.h"
#include "third_party/blink/renderer/modules/webaudio/base_audio_context.h"
#include "third_party/blink/renderer/platform/fingerprint/fingerprint_state.h"

namespace blink {

AnalyserNode::AnalyserNode(BaseAudioContext& context) : AudioNode(context) {
  SetHandler(AnalyserHandler::Create(*this, context.sampleRate()));
}

AnalyserNode* AnalyserNode::Create(BaseAudioContext& context,
                                   ExceptionState& exception_state) {
  DCHECK(IsMainThread());

  return MakeGarbageCollected<AnalyserNode>(context);
}

AnalyserNode* AnalyserNode::Create(BaseAudioContext* context,
                                   const AnalyserOptions* options,
                                   ExceptionState& exception_state) {
  DCHECK(IsMainThread());

  AnalyserNode* node = Create(*context, exception_state);

  if (!node) {
    return nullptr;
  }

  node->HandleChannelOptions(options, exception_state);

  node->setFftSize(options->fftSize(), exception_state);
  node->setSmoothingTimeConstant(options->smoothingTimeConstant(),
                                 exception_state);

  // minDecibels and maxDecibels have default values.  Set both of the values
  // at once.
  node->SetMinMaxDecibels(options->minDecibels(), options->maxDecibels(),
                          exception_state);

  return node;
}

AnalyserHandler& AnalyserNode::GetAnalyserHandler() const {
  return static_cast<AnalyserHandler&>(Handler());
}

unsigned AnalyserNode::fftSize() const {
  return GetAnalyserHandler().FftSize();
}

void AnalyserNode::setFftSize(unsigned size, ExceptionState& exception_state) {
  return GetAnalyserHandler().SetFftSize(size, exception_state);
}

unsigned AnalyserNode::frequencyBinCount() const {
  return GetAnalyserHandler().FrequencyBinCount();
}

void AnalyserNode::setMinDecibels(double min, ExceptionState& exception_state) {
  GetAnalyserHandler().SetMinDecibels(min, exception_state);
}

double AnalyserNode::minDecibels() const {
  return GetAnalyserHandler().MinDecibels();
}

void AnalyserNode::setMaxDecibels(double max, ExceptionState& exception_state) {
  GetAnalyserHandler().SetMaxDecibels(max, exception_state);
}

void AnalyserNode::SetMinMaxDecibels(double min,
                                     double max,
                                     ExceptionState& exception_state) {
  GetAnalyserHandler().SetMinMaxDecibels(min, max, exception_state);
}

double AnalyserNode::maxDecibels() const {
  return GetAnalyserHandler().MaxDecibels();
}

void AnalyserNode::setSmoothingTimeConstant(double smoothing_time,
                                            ExceptionState& exception_state) {
  GetAnalyserHandler().SetSmoothingTimeConstant(smoothing_time,
                                                exception_state);
}

double AnalyserNode::smoothingTimeConstant() const {
  return GetAnalyserHandler().SmoothingTimeConstant();
}

// Chronium: applies seeded sub-perceptible noise to the user-supplied
// AnalyserNode output buffer. The buffer is written by the analyser
// handler then perturbed here, so every call sees a perturbed value but
// the perturbation is deterministic per (profile, channel, index).
namespace {

void NoiseFloatBuffer(DOMFloat32Array* array, const char* channel_name) {
  if (!array || !FingerprintState::IsActive()) {
    return;
  }
  float* data = array->Data();
  const unsigned len = array->length();
  for (unsigned i = 0; i < len; ++i) {
    const uint32_t hash = FingerprintState::HashAt(channel_name, 0u, i);
    const float signed_unit =
        (static_cast<float>(hash) * (1.0f / 2147483648.0f)) - 1.0f;
    // Frequency data is in dB (typically -100..0). A 1e-4 perturbation
    // shifts the hash without crossing rounding/quantization boundaries
    // a fingerprinter would expect to be stable across runs of the same
    // profile.
    UNSAFE_TODO(data[i] += signed_unit * 1e-4f);
  }
}

void NoiseByteBuffer(DOMUint8Array* array, const char* channel_name) {
  if (!array || !FingerprintState::IsActive()) {
    return;
  }
  uint8_t* data = array->Data();
  const unsigned len = array->length();
  for (unsigned i = 0; i < len; ++i) {
    const uint32_t hash = FingerprintState::HashAt(channel_name, 0u, i);
    // Flip LSB of ~1/256 entries; keeps the buffer visually identical for
    // any reasonable consumer but changes the fingerprint hash.
    if ((hash & 0xFFu) == 0u) {
      UNSAFE_TODO(data[i] ^= 1u);
    }
  }
}

}  // namespace

void AnalyserNode::getFloatFrequencyData(NotShared<DOMFloat32Array> array) {
  GetAnalyserHandler().GetFloatFrequencyData(array.Get(),
                                             context()->currentTime());
  NoiseFloatBuffer(array.Get(), "audio-freq-f");
}

void AnalyserNode::getByteFrequencyData(NotShared<DOMUint8Array> array) {
  GetAnalyserHandler().GetByteFrequencyData(array.Get(),
                                            context()->currentTime());
  NoiseByteBuffer(array.Get(), "audio-freq-b");
}

void AnalyserNode::getFloatTimeDomainData(NotShared<DOMFloat32Array> array) {
  GetAnalyserHandler().GetFloatTimeDomainData(array.Get());
  NoiseFloatBuffer(array.Get(), "audio-time-f");
}

void AnalyserNode::getByteTimeDomainData(NotShared<DOMUint8Array> array) {
  GetAnalyserHandler().GetByteTimeDomainData(array.Get());
  NoiseByteBuffer(array.Get(), "audio-time-b");
}

void AnalyserNode::ReportDidCreate() {
  GraphTracer().DidCreateAudioNode(this);
}

void AnalyserNode::ReportWillBeDestroyed() {
  GraphTracer().WillDestroyAudioNode(this);
}

}  // namespace blink
