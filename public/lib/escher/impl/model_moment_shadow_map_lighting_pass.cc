// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/impl/model_moment_shadow_map_lighting_pass.h"

#include "lib/escher/impl/model_data.h"

namespace {

constexpr char kFragmentShaderSourceCode[] = R"GLSL(
#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec4 shadowPos;

layout(set = 0, binding = 0) uniform PerModel {
  vec2 frag_coord_to_uv_multiplier;
  float time;
  vec3 ambient_light_intensity;
  vec3 direct_light_intensity;
  vec2 shadow_map_uv_multiplier;
};

layout(set = 0, binding = 1) uniform sampler2D shadow_map_tex;

layout(set = 1, binding = 0) uniform PerObject {
  mat4 camera_transform;
  mat4 light_transform;
  vec4 color;
};

layout(set = 1, binding = 1) uniform sampler2D material_tex;

layout(location = 0) out vec4 outColor;

// Solve x for Bx = z, where B is symmetric and positive semi-definite.
// https://en.wikipedia.org/wiki/Cholesky_decomposition
// https://en.wikipedia.org/wiki/Triangular_matrix
vec3 solveLinear(mat3 B, vec3 z) {
  // Compute the lower triangular matrix L.
  float D1 = B[0][0];
  float invD1 = 1. / B[0][0];
  float L21 = B[1][0] * invD1;
  float L31 = B[2][0] * invD1;
  float D2 = B[1][1] - L21 * L21 * D1;
  float invD2 = 1. / D2;
  float L32 = (B[2][1] - L31 * L21 * D1) * invD2;
  float D3 = B[2][2] - L31 * L31 * D1 - L32 * L32 * D2;
  float invD3 = 1. / D3;
  // Solve (D * L.T * x) with forward substitution.
  vec3 y;
  y[0] = z[0];
  y[1] = z[1] - L21 * y[0];
  y[2] = z[2] - L31 * y[0] - L32 * y[1];
  // Scale y to get (L.T * x).
  y *= vec3(invD1, invD2, invD3);
  // Solve x with backward substitution.
  vec3 x;
  x[2] = y[2];
  x[1] = y[1] - L32 * x[2];
  x[0] = y[0] - L21 * x[1] - L31 * x[2];
  return x;
}

float computeVisibility(vec4 moments, float fragLightDist) {
  const float kMomentBias = 3e-6;
  const float kDepthBias = 1e-8;
  vec4 b = mix(moments, vec4(0., .63, 0, .63), kMomentBias);
  mat3 B = mat3(
      1.0, b.x, b.y,
      b.x, b.y, b.z,
      b.y, b.z, b.w);
  float zf = fragLightDist - kDepthBias;
  vec3 z = vec3(1., zf, zf * zf);
  vec3 c = solveLinear(B, z);
  float sqrtDelta = sqrt(max(0., c.y * c.y - 4. * c.z * c.x));
  float d1 = (-c.y - sqrtDelta) / (2. * c.z);
  float d2 = (-c.y + sqrtDelta) / (2. * c.z);
  if (d2 < d1) {
    float tmp = d1;
    d1 = d2;
    d2 = tmp;
  }
  if (zf <= d1) {
    return 1.;
  } else if (zf <= d2) {
    return 1. - (zf * d2 - b.x * (zf + d2) + b.y) / ((d2 - d1) * (zf - d1));
  } else {
    return (d1 * d2 - b.x * (d1 + d2) + b.y) / ((zf - d1) * (zf - d2));
  }
}

void main() {
  vec4 shadowUV = shadowPos / shadowPos.w;
  float fragLightDist = shadowUV.z;
  vec4 shadowMapSample = texture(shadow_map_tex, shadowUV.xy);
  float visibility = computeVisibility(shadowMapSample, fragLightDist);
  visibility = clamp(visibility, 0., 1.);
  vec3 light = ambient_light_intensity + visibility * direct_light_intensity;
  outColor = vec4(light, 1.) * color * texture(material_tex, inUV);
}
)GLSL";

}  // namespace

namespace escher {
namespace impl {

ModelMomentShadowMapLightingPass::ModelMomentShadowMapLightingPass(
    ResourceRecycler* recycler,
    ModelDataPtr model_data,
    vk::Format color_format,
    vk::Format depth_format,
    uint32_t sample_count)
    : ModelShadowMapLightingPass(
        recycler,
        std::move(model_data),
        color_format,
        depth_format,
        sample_count) {}

std::string ModelMomentShadowMapLightingPass::GetFragmentShaderSourceCode(
    const ModelPipelineSpec& spec) {
  return kFragmentShaderSourceCode;
}

}  // namespace impl
}  // namespace escher
