#pragma once

#include "gpu/textures/formats.h"

namespace gpu {
// Host formats produced by the existing block decoders. Unknown means no
// lossless sampled-upload fallback is implemented for this source format.
TextureFormat DecodedSampledFormat(TextureFormat source);

template <typename Supports>
TextureFormat SelectSampledFormat(TextureFormat source, Supports&& supports) {
  if (source != TextureFormat::kUnknown && supports(source)) return source;
  const auto decoded = DecodedSampledFormat(source);
  return decoded != TextureFormat::kUnknown && supports(decoded) ? decoded : TextureFormat::kUnknown;
}

// Input is already detiled and endian-correct, as UploadSampled receives it.
// Decode every mip/face, retaining their order and cropping partial edge blocks.
// On failure output is empty; metadata, swizzle, layout and version stay owned
// by the caller. This does not modify or retain the compressed input.
bool DecodeSampledData(const DrawTexture& source, TextureFormat destination,
                       std::vector<std::vector<uint8_t>>& output, std::string& reason);
}  // namespace gpu
