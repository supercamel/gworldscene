#ifndef G_LOG_DOMAIN
#define G_LOG_DOMAIN "GWorldScene"
#endif

#include "gworld-scene-view-shaders-private.h"
#include "gworld-scene-atmosphere-glsl-private.h"

#include <glib.h>

namespace {

GLuint
compile_shader(GLenum type, const char *source, const char *helpers = "")
{
  GLuint shader = glCreateShader(type);
  const char *preamble = epoxy_is_desktop_gl()
    ? "#version 330 core\n"
    : "#version 300 es\nprecision highp float;\nprecision highp int;\nprecision highp sampler2D;\nprecision highp sampler2DArray;\n";
  static const char *color_functions = R"GLSL(
vec3 srgb_to_linear(vec3 value) {
  return mix(value / 12.92, pow((value + 0.055) / 1.055, vec3(2.4)),
             greaterThan(value, vec3(0.04045)));
}
vec3 linear_to_srgb(vec3 value) {
  value = max(value, vec3(0.0));
  return mix(value * 12.92, 1.055 * pow(value, vec3(1.0 / 2.4)) - 0.055,
             greaterThan(value, vec3(0.0031308)));
}
)GLSL";
  const char *sources[] = {preamble, color_functions, helpers, source};
  glShaderSource(shader, G_N_ELEMENTS(sources), sources, nullptr);
  glCompileShader(shader);

  GLint ok = GL_FALSE;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (ok != GL_TRUE) {
    char log[1024] = {0};
    glGetShaderInfoLog(shader, sizeof log, nullptr, log);
    g_warning("Shader compile failed: %s", log);
    glDeleteShader(shader);
    return 0;
  }

  return shader;
}

GLuint
create_linked_program(const char *label, const char *vertex_source, const char *fragment_source,
                      const char *fragment_helpers = "")
{
  GLuint vertex = compile_shader(GL_VERTEX_SHADER, vertex_source);
  GLuint fragment = compile_shader(GL_FRAGMENT_SHADER, fragment_source, fragment_helpers);
  if (vertex == 0 || fragment == 0) {
    if (vertex)
      glDeleteShader(vertex);
    if (fragment)
      glDeleteShader(fragment);
    return 0;
  }

  GLuint program = glCreateProgram();
  glAttachShader(program, vertex);
  glAttachShader(program, fragment);
  glLinkProgram(program);
  glDeleteShader(vertex);
  glDeleteShader(fragment);

  GLint ok = GL_FALSE;
  glGetProgramiv(program, GL_LINK_STATUS, &ok);
  if (ok != GL_TRUE) {
    char log[1024] = {0};
    glGetProgramInfoLog(program, sizeof log, nullptr, log);
    g_warning("%s program link failed: %s", label, log);
    glDeleteProgram(program);
    return 0;
  }

  return program;
}

} // namespace

GLuint
gworld_scene_view_create_program(void)
{
  static const char *vertex_source = R"GLSL(
layout(location = 0) in vec3 position;
layout(location = 1) in vec2 detail_texcoord;
layout(location = 2) in vec2 mid_texcoord;
layout(location = 3) in vec2 base_texcoord;
layout(location = 4) in vec2 ultra_texcoord;
layout(location = 5) in vec3 normal;
layout(location = 6) in vec4 vertex_color;
layout(location = 7) in float material;
layout(location = 8) in vec2 surface;
uniform mat4 mvp;
uniform bool ultra_atlas_valid;
uniform vec4 ultra_atlas_range;
uniform vec2 ultra_atlas_size;
uniform bool detail_atlas_valid;
uniform vec4 detail_atlas_range;
uniform vec2 detail_atlas_size;
uniform bool mid_atlas_valid;
uniform vec4 mid_atlas_range;
uniform vec2 mid_atlas_size;
uniform bool far_atlas_valid;
uniform vec4 far_atlas_range;
uniform vec2 far_atlas_size;
uniform bool base_atlas_valid;
uniform vec4 base_atlas_range;
uniform vec2 base_atlas_size;
uniform bool globe_atlas_valid;
uniform vec4 globe_atlas_range;
uniform vec2 globe_atlas_size;
uniform bool water_near_valid;
uniform vec4 water_near_range;
uniform vec2 water_near_size;
out vec2 v_water_near;
uniform bool water_mid_valid;
uniform vec4 water_mid_range;
uniform vec2 water_mid_size;
out vec2 v_water_mid;
uniform bool water_far_valid;
uniform vec4 water_far_range;
uniform vec2 water_far_size;
out vec2 v_water_far;
out vec2 v_detail_texcoord;
out vec2 v_mid_texcoord;
out vec2 v_far_texcoord;
out vec2 v_base_texcoord;
out vec2 v_ultra_texcoord;
out vec3 v_normal;
out vec4 v_color;
out float v_material;
out vec2 v_surface;
out float v_height;
out vec3 v_world_position;

const float MERCATOR_MAX_LATITUDE = 85.05112878;
const float PI = 3.14159265358979323846;

vec2 atlas_uv_for_lat_lon(vec2 lat_lon, vec4 range, vec2 size) {
  if (size.x <= 0.0 || size.y <= 0.0)
    return vec2(-1.0, -1.0);

  float n = exp2(range.x);
  float latitude = clamp(lat_lon.x, -MERCATOR_MAX_LATITUDE, MERCATOR_MAX_LATITUDE);
  float lat_rad = radians(latitude);
  float tile_x = (lat_lon.y + 180.0) / 360.0 * n;
  float tile_y = (1.0 - log(tan(PI * 0.25 + lat_rad * 0.5)) / PI) * 0.5 * n;
  return vec2((tile_x - range.y) / size.x,
              (tile_y - range.z) / size.y);
}

void main() {
  vec4 world_position = vec4(position, 1.0);
  gl_Position = mvp * world_position;
  bool is_terrain = material < 0.5;
  bool is_globe = material > 1.5 && material < 2.5;
  if (is_terrain) {
    vec2 lat_lon = detail_texcoord;
    v_detail_texcoord = detail_atlas_valid ? atlas_uv_for_lat_lon(lat_lon, detail_atlas_range, detail_atlas_size) : vec2(-1.0, -1.0);
    v_mid_texcoord = mid_atlas_valid ? atlas_uv_for_lat_lon(lat_lon, mid_atlas_range, mid_atlas_size) : vec2(-1.0, -1.0);
    v_far_texcoord = far_atlas_valid ? atlas_uv_for_lat_lon(lat_lon, far_atlas_range, far_atlas_size) : vec2(-1.0, -1.0);
    v_base_texcoord = base_atlas_valid ? atlas_uv_for_lat_lon(lat_lon, base_atlas_range, base_atlas_size) : vec2(-1.0, -1.0);
    v_ultra_texcoord = ultra_atlas_valid ? atlas_uv_for_lat_lon(lat_lon, ultra_atlas_range, ultra_atlas_size) : vec2(-1.0, -1.0);
  } else if (is_globe) {
    vec2 lat_lon = detail_texcoord;
    v_detail_texcoord = vec2(-1.0, -1.0);
    v_mid_texcoord = vec2(-1.0, -1.0);
    v_far_texcoord = vec2(-1.0, -1.0);
    v_base_texcoord = globe_atlas_valid ? atlas_uv_for_lat_lon(lat_lon, globe_atlas_range, globe_atlas_size) : vec2(-1.0, -1.0);
    v_ultra_texcoord = vec2(-1.0, -1.0);
  } else {
    v_detail_texcoord = detail_texcoord;
    v_mid_texcoord = mid_texcoord;
    v_far_texcoord = vec2(-1.0, -1.0);
    v_base_texcoord = base_texcoord;
    v_ultra_texcoord = ultra_texcoord;
  }
  v_water_near = water_near_valid && (is_terrain || is_globe)
    ? atlas_uv_for_lat_lon(detail_texcoord, water_near_range, water_near_size) : vec2(-1.0);
  v_water_mid = water_mid_valid && (is_terrain || is_globe)
    ? atlas_uv_for_lat_lon(detail_texcoord, water_mid_range, water_mid_size) : vec2(-1.0);
  v_water_far = water_far_valid && (is_terrain || is_globe)
    ? atlas_uv_for_lat_lon(detail_texcoord, water_far_range, water_far_size) : vec2(-1.0);
  v_normal = normal;
  v_color = vec4(srgb_to_linear(vertex_color.rgb), vertex_color.a);
  v_material = material;
  v_surface = surface;
  v_height = position.y;
  v_world_position = position;
}
)GLSL";

  static const char *fragment_source = R"GLSL(
uniform bool water_near_valid;
uniform vec4 water_near_range;
uniform vec2 water_near_size;
in vec2 v_water_near;
uniform sampler2D water_near_texture;
uniform bool water_mid_valid;
uniform vec4 water_mid_range;
uniform vec2 water_mid_size;
in vec2 v_water_mid;
uniform sampler2D water_mid_texture;
uniform bool water_far_valid;
uniform vec4 water_far_range;
uniform vec2 water_far_size;
in vec2 v_water_far;
uniform sampler2D water_far_texture;
uniform bool water_enabled;
uniform float water_wave_strength;
uniform vec3 water_wave_phase;
uniform vec2 water_wave_origin;
uniform float water_tile_meters;
in vec2 v_detail_texcoord;
in vec2 v_mid_texcoord;
in vec2 v_far_texcoord;
in vec2 v_base_texcoord;
in vec2 v_ultra_texcoord;
in vec3 v_normal;
in vec4 v_color;
in float v_material;
in vec2 v_surface;
in float v_height;
in vec3 v_world_position;
uniform bool ultra_atlas_valid;
uniform vec4 ultra_atlas_range;
uniform vec2 ultra_atlas_size;
uniform bool detail_atlas_valid;
uniform vec4 detail_atlas_range;
uniform vec2 detail_atlas_size;
uniform bool mid_atlas_valid;
uniform vec4 mid_atlas_range;
uniform vec2 mid_atlas_size;
uniform bool far_atlas_valid;
uniform vec4 far_atlas_range;
uniform vec2 far_atlas_size;
uniform bool base_atlas_valid;
uniform vec4 base_atlas_range;
uniform vec2 base_atlas_size;
uniform bool globe_atlas_valid;
uniform vec4 globe_atlas_range;
uniform vec2 globe_atlas_size;
uniform sampler2D ultra_texture;
uniform sampler2D detail_texture;
uniform sampler2D mid_texture;
uniform sampler2D far_texture;
uniform sampler2D base_texture;
uniform sampler2DArray shadow_texture;
uniform mat4 shadow_matrices[3];
uniform vec3 shadow_splits;
uniform vec3 shadow_texel_meters;
uniform vec3 camera_forward;
uniform sampler2D model_texture;
uniform bool has_ultra_texture;
uniform bool has_detail_texture;
uniform bool has_mid_texture;
uniform bool has_far_texture;
uniform bool has_base_texture;
uniform bool has_shadow_texture;
uniform bool has_model_texture;
uniform vec3 sun_direction;
uniform vec3 ambient_color;
uniform vec3 direct_light_color;
uniform float ambient_strength;
uniform float sun_strength;
uniform vec3 camera_position;
uniform vec3 ultra_texture_center;
uniform float ultra_texture_radius;
uniform bool fog_enabled;
uniform vec3 fog_color;
uniform float fog_start;
uniform float fog_end;
uniform float fog_density;
uniform float terrain_normal_smoothing;
out vec4 color;

// Wrap after interpolation: wrapping individual vertices would interpolate
// through the atlas across the longitude discontinuity on the far side of Earth.
vec2 wrap_atlas_uv(vec2 uv, vec4 range, vec2 size) {
  float tiles = exp2(range.x);
  if (size.x > 0.0) {
    float period = tiles / size.x;
    uv.x -= floor((uv.x - 0.5) / period + 0.5) * period;
  }
  return uv;
}

// Blend through the outer part of each distance band's coverage. An absent
// finer tile keeps the coarser image; an absent coarser tile keeps all available
// fine detail rather than fading it into an untextured surface.
vec4 blend_imagery(vec4 coarse, vec4 fine, vec2 uv, vec2 atlas_size) {
  if (fine.a <= 0.01) return coarse;
  if (coarse.a <= 0.01) return fine;
  vec2 edge_tiles = min(uv, vec2(1.0) - uv) * atlas_size;
  float weight = smoothstep(0.0, 0.75, min(edge_tiles.x, edge_tiles.y)) * fine.a;
  return vec4(mix(coarse.rgb, fine.rgb, weight), mix(coarse.a, 1.0, weight));
}

// Gradients come from continuous coordinates, before longitude wrapping.
// Filtering premultiplied linear RGB avoids dark borders at missing tiles.
vec4 sample_imagery(sampler2D source, vec2 uv, vec2 continuous_uv, bool valid) {
  vec4 texel = textureGrad(source, uv, dFdx(continuous_uv), dFdy(continuous_uv));
  if (!valid || texel.a <= 0.0001) return vec4(0.0);
  return vec4(texel.rgb / texel.a, texel.a);
}

vec3 lighting_normal() {
  vec3 normal = normalize(v_normal);
  vec3 face = cross(dFdx(v_world_position), dFdy(v_world_position));
  if (v_material < 0.5 && dot(face, face) > 0.00000001) {
    face = normalize(face);
    if (dot(face, normal) < 0.0) face = -face;
    normal = normalize(mix(face, normal, clamp(terrain_normal_smoothing, 0.0, 1.0)));
  }
  return normal;
}

float cascade_visibility(int cascade, vec3 normal) {
  float slope = 1.0 - max(dot(normal, normalize(sun_direction)), 0.0);
  vec3 offset = normal * shadow_texel_meters[cascade] * (0.35 + slope * 1.5);
  vec4 light = shadow_matrices[cascade] * vec4(v_world_position + offset, 1.0);
  vec3 projected = light.xyz / light.w * 0.5 + 0.5;
  if (any(lessThan(projected, vec3(0.0))) || any(greaterThan(projected, vec3(1.0)))) return 1.0;
  vec2 size = vec2(textureSize(shadow_texture, 0).xy);
  vec2 texel = 1.0 / size;
  float lit = 0.0, weights = 0.0;
  for (int y = -2; y <= 2; ++y) {
    for (int x = -2; x <= 2; ++x) {
      vec2 uv = projected.xy + vec2(x, y) * texel;
      float weight = float((3 - abs(x)) * (3 - abs(y)));
      float depth = (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))))
        ? 1.0 : texture(shadow_texture, vec3(uv, float(cascade))).r;
      lit += (projected.z - 0.000005 <= depth ? 1.0 : 0.0) * weight;
      weights += weight;
    }
  }
  return lit / weights;
}

float shadow_visibility(vec3 normal) {
  if (!has_shadow_texture || (v_material > 1.5 && v_material < 2.5)) return 1.0;
  float depth = max(dot(v_world_position - camera_position, camera_forward), 0.0);
  int cascade = depth < shadow_splits.x ? 0 : (depth < shadow_splits.y ? 1 : 2);
  if (depth >= shadow_splits.z) return 1.0;
  float current = cascade_visibility(cascade, normal);
  float blend = smoothstep(shadow_splits[cascade] * 0.85, shadow_splits[cascade], depth);
  if (blend > 0.0) {
    float next = cascade < 2 ? cascade_visibility(cascade + 1, normal) : 1.0;
    current = mix(current, next, blend);
  }
  return current;
}

// GGX/Cook-Torrance direct light and a hemispherical environment approximation.
// Roughness is perceptual; the lower bound keeps tiny sun highlights stable.
vec3 material_lighting(vec3 base, vec3 normal, float visibility, vec2 surface, float reflectance, float variance) {
  vec3 view_delta = camera_position - v_world_position;
  vec3 view = view_delta / max(length(view_delta), 0.0001);
  vec3 sun = normalize(sun_direction);
  vec3 half_delta = view + sun;
  vec3 half_vector = half_delta / max(length(half_delta), 0.0001);
  float nv = clamp(dot(normal, view), 0.0001, 1.0);
  float nl = max(dot(normal, sun), 0.0);
  float nh = max(dot(normal, half_vector), 0.0);
  float vh = clamp(dot(view, half_vector), 0.0, 1.0);
  float roughness = clamp(surface.x, 0.08, 1.0);
  float metallic = clamp(surface.y, 0.0, 1.0);
  // Normal variation below a pixel broadens highlights instead of sparkling.
  float a2 = clamp(pow(roughness, 4.0) + variance * 0.25, 0.00004, 1.0);
  float denominator = nh * nh * (a2 - 1.0) + 1.0;
  float distribution = a2 / max(3.14159265 * denominator * denominator, 0.000001);
  float gv = nl * sqrt(nv * nv * (1.0 - a2) + a2);
  float gl = nv * sqrt(nl * nl * (1.0 - a2) + a2);
  float geometry = 0.5 / max(gv + gl, 0.0001);
  vec3 f0 = mix(vec3(reflectance), base, metallic);
  vec3 fresnel = f0 + (vec3(1.0) - f0) * pow(1.0 - vh, 5.0);
  vec3 diffuse = base * (vec3(1.0) - fresnel) * (1.0 - metallic);
  vec3 direct = (diffuse + distribution * geometry * fresnel * 3.14159265) *
                srgb_to_linear(direct_light_color) * sun_strength * nl * visibility;
  vec3 up = atmosphere_enabled ? normalize(v_world_position - planet_center) : vec3(0, 1, 0);
  float sky_weight = dot(normal, up) * 0.5 + 0.5;
  vec3 ambient = srgb_to_linear(ambient_color) * ambient_strength * mix(0.38, 1.0, sky_weight);
  vec3 reflected = reflect(-view, normal);
  float reflection_sky = clamp(dot(reflected, up) * 0.5 + 0.5, 0.0, 1.0);
  vec3 environment = mix(srgb_to_linear(vec3(0.24, 0.22, 0.19)),
                          srgb_to_linear(ambient_color), reflection_sky) * ambient_strength;
  vec3 environment_fresnel = f0 + (max(vec3(1.0 - roughness), f0) - f0) * pow(1.0 - nv, 5.0);
  return direct + base * ambient * (1.0 - metallic) +
         environment * environment_fresnel * (1.0 - roughness * 0.45);
}

vec2 water_mask(sampler2D source, vec2 uv, vec2 continuous_uv, bool valid) {
  vec2 value = textureGrad(source, uv, dFdx(continuous_uv), dFdy(continuous_uv)).rg;
  bool inside = all(greaterThanEqual(uv, vec2(0.0))) && all(lessThanEqual(uv, vec2(1.0)));
  return valid && inside ? value : vec2(0.0);
}

float water_layer(float coarse, vec2 mask, vec2 uv, vec2 size) {
  vec2 edge = min(uv, vec2(1.0) - uv) * size;
  float blend = smoothstep(0.0, 0.5, min(edge.x, edge.y)) * mask.g;
  return mix(coarse, mask.r / max(mask.g, 0.0001), blend);
}

float water_coverage() {
  vec2 near_uv = wrap_atlas_uv(v_water_near, water_near_range, water_near_size);
  vec2 near_mask = water_mask(water_near_texture, near_uv, v_water_near, water_near_valid);
  vec2 mid_uv = wrap_atlas_uv(v_water_mid, water_mid_range, water_mid_size);
  vec2 mid_mask = water_mask(water_mid_texture, mid_uv, v_water_mid, water_mid_valid);
  vec2 far_uv = wrap_atlas_uv(v_water_far, water_far_range, water_far_size);
  vec2 far_mask = water_mask(water_far_texture, far_uv, v_water_far, water_far_valid);
  float coverage = water_layer(0.0, far_mask, far_uv, water_far_size);
  coverage = water_layer(coverage, mid_mask, mid_uv, water_mid_size);
  return clamp(water_layer(coverage, near_mask, near_uv, water_near_size), 0.0, 1.0);
}

vec3 water_lighting(vec3 base, float visibility, float footprint) {
  vec3 up = normalize(v_world_position - planet_center);
  vec3 axis = abs(up.x) > 0.9 ? vec3(0, 0, 1) : vec3(1, 0, 0);
  vec3 tangent = normalize(axis - up * dot(axis, up));
  vec3 bitangent = normalize(cross(tangent, up));
  vec2 position = v_water_near * water_near_size * water_tile_meters + water_wave_origin;
  position *= 6.28318530718 / (40075016.68557849 / 9784.0);
  vec2 gradient = vec2(0.0);
  gradient += vec2(0.9, 0.3) * cos(dot(position, vec2(171, 63)) + water_wave_phase.x);
  gradient += vec2(0.4, -0.7) * cos(dot(position, vec2(89, -149)) + water_wave_phase.y);
  gradient += vec2(0.2, 0.15) * cos(dot(position, vec2(367, 277)) + water_wave_phase.z);
  float distance_m = length(camera_position - v_world_position);
  float wave_fade = 1.0 - smoothstep(3000.0, 20000.0, distance_m);
  // Filter waves whose period projects below a pixel instead of sparkling.
  wave_fade *= 1.0 - smoothstep(0.5, 3.0, footprint);
  vec3 normal = normalize(up - (tangent * gradient.x + bitangent * gradient.y) *
                                water_wave_strength * 0.18 * wave_fade);
  vec3 delta = camera_position - v_world_position;
  vec3 view = delta / max(length(delta), 0.0001);
  float nv = clamp(dot(normal, view), 0.0, 1.0);
  float fresnel = 0.02 + 0.98 * pow(1.0 - nv, 5.0);
  vec3 reflection = reflect(-view, normal);
  vec3 sky = atmosphere_enabled ? atmosphere_sky(v_world_position + up * 2.0, reflection, sun_direction) :
    srgb_to_linear(mix(vec3(0.55, 0.69, 0.82), vec3(0.22, 0.42, 0.72), max(dot(reflection, up), 0.0))) * ambient_strength;
  vec3 tint = mix(base, srgb_to_linear(vec3(0.025, 0.13, 0.17)), 0.25);
  vec3 surface = material_lighting(tint, normal, visibility, vec2(0.16, 0.0), 0.02, min(footprint * footprint, 1.0) * 0.01);
  return mix(surface, sky, fresnel);
}

float fog_amount() {
  if (!fog_enabled)
    return 0.0;

  float distance_to_camera = length(v_world_position - camera_position);
  float range = max(fog_end - fog_start, 1.0);
  float linear_fog = clamp((distance_to_camera - fog_start) / range, 0.0, 1.0);
  float density_fog = 1.0 - exp(-distance_to_camera * max(fog_density, 0.0));
  return atmosphere_enabled ? linear_fog : clamp(max(linear_fog, density_fog), 0.0, 1.0);
}

void main() {
  vec3 normal = lighting_normal();
  float diffuse = clamp(dot(normal, normalize(sun_direction)), 0.0, 1.0);
  float visibility = shadow_visibility(normal);
  vec3 light = srgb_to_linear(ambient_color) * ambient_strength +
               srgb_to_linear(direct_light_color) * diffuse * sun_strength * visibility;
  vec3 low = srgb_to_linear(vec3(0.26, 0.36, 0.24));
  vec3 high = srgb_to_linear(vec3(0.76, 0.72, 0.62));
  vec3 terrain_tint = mix(low, high, clamp(v_height / 1800.0, 0.0, 1.0));
  vec2 detail_uv = v_detail_texcoord;
  vec2 mid_uv = v_mid_texcoord;
  vec2 far_uv = v_far_texcoord;
  vec2 base_uv = v_base_texcoord;
  vec2 ultra_uv = v_ultra_texcoord;
  if (v_material < 0.5) {
    if (detail_atlas_valid) detail_uv = wrap_atlas_uv(detail_uv, detail_atlas_range, detail_atlas_size);
    if (mid_atlas_valid) mid_uv = wrap_atlas_uv(mid_uv, mid_atlas_range, mid_atlas_size);
    if (far_atlas_valid) far_uv = wrap_atlas_uv(far_uv, far_atlas_range, far_atlas_size);
    if (base_atlas_valid) base_uv = wrap_atlas_uv(base_uv, base_atlas_range, base_atlas_size);
    if (ultra_atlas_valid) ultra_uv = wrap_atlas_uv(ultra_uv, ultra_atlas_range, ultra_atlas_size);
  } else if (v_material > 1.5 && v_material < 2.5 && globe_atlas_valid) {
    base_uv = wrap_atlas_uv(base_uv, globe_atlas_range, globe_atlas_size);
  }
  bool in_detail = detail_uv.x >= 0.0 && detail_uv.x <= 1.0 && detail_uv.y >= 0.0 && detail_uv.y <= 1.0;
  bool in_far = far_uv.x >= 0.0 && far_uv.x <= 1.0 && far_uv.y >= 0.0 && far_uv.y <= 1.0;
  bool in_mid = mid_uv.x >= 0.0 && mid_uv.x <= 1.0 && mid_uv.y >= 0.0 && mid_uv.y <= 1.0;
  bool in_base = base_uv.x >= 0.0 && base_uv.x <= 1.0 && base_uv.y >= 0.0 && base_uv.y <= 1.0;
  bool in_ultra = ultra_uv.x >= 0.0 && ultra_uv.x <= 1.0 && ultra_uv.y >= 0.0 && ultra_uv.y <= 1.0;
  vec4 base_texel = sample_imagery(base_texture, base_uv, v_base_texcoord, has_base_texture && in_base);
  vec4 far_texel = sample_imagery(far_texture, far_uv, v_far_texcoord, has_far_texture && in_far);
  vec4 mid_texel = sample_imagery(mid_texture, mid_uv, v_mid_texcoord, has_mid_texture && in_mid);
  vec4 detail_texel = sample_imagery(detail_texture, detail_uv, v_detail_texcoord, has_detail_texture && in_detail);
  vec4 ultra_texel = sample_imagery(ultra_texture, ultra_uv, v_ultra_texcoord, has_ultra_texture && in_ultra);
  vec4 texture_stack = blend_imagery(base_texel, far_texel, far_uv, far_atlas_size);
  texture_stack = blend_imagery(texture_stack, mid_texel, mid_uv, mid_atlas_size);
  texture_stack = blend_imagery(texture_stack, detail_texel, detail_uv, detail_atlas_size);
  float ultra_lateral_distance = length((v_world_position - ultra_texture_center).xz);
  float ultra_radius = max(ultra_texture_radius, 1.0);
  float ultra_fade_width = clamp(ultra_radius * 0.22, 80.0, 220.0);
  float ultra_blend = 1.0 - smoothstep(max(0.0, ultra_radius - ultra_fade_width),
                                      ultra_radius,
                                      ultra_lateral_distance);
  ultra_blend *= ultra_texel.a > 0.01 ? 1.0 : 0.0;
  vec4 texel = (has_ultra_texture && ultra_texture_radius > 1.0)
                 ? (texture_stack.a <= 0.01 && ultra_texel.a > 0.01 ? ultra_texel :
                    mix(texture_stack, ultra_texel, ultra_blend * ultra_texel.a))
                 : texture_stack;
  vec3 terrain_base = mix(terrain_tint, texel.rgb, texel.a * 0.88);
  vec3 globe_base = texel.a > 0.01 ? texel.rgb : v_color.rgb;
  bool is_globe = v_material > 1.5 && v_material < 2.5;
  bool is_textured_model = v_material > 2.5;
  vec4 model_texel = (has_model_texture && is_textured_model) ? texture(model_texture, v_detail_texcoord) : vec4(0.0);
  vec3 object_base = (is_textured_model && model_texel.a > 0.01)
                       ? mix(v_color.rgb, model_texel.rgb * v_color.rgb, model_texel.a)
                       : v_color.rgb;
  vec3 base = is_globe ? globe_base : (v_material > 0.5 ? object_base : terrain_base);
  float normal_variance = dot(dFdx(normal), dFdx(normal)) + dot(dFdy(normal), dFdy(normal));
  vec3 lit_color = (v_material > 0.5 && !is_globe)
    ? material_lighting(base, normal, visibility, v_surface, 0.04, normal_variance) : base * max(light, vec3(0.0));
  if (water_enabled) {
    // Derivatives precede the spatially varying coastline branch.
    float coverage = water_coverage();
    vec2 wave_position = v_water_near * water_near_size * water_tile_meters * (6.28318530718 / (40075016.68557849 / 9784.0));
    float footprint = max(length(dFdx(wave_position)), length(dFdy(wave_position))) * 367.0;
    if ((v_material < 0.5 || is_globe) && coverage > 0.001)
      lit_color = mix(lit_color, water_lighting(base, visibility, footprint), coverage);
  }
  float alpha = (v_material > 0.5 && !is_globe) ? v_color.a : 1.0;
  if (atmosphere_enabled)
    lit_color = atmosphere_surface(lit_color, camera_position, v_world_position, sun_direction);
  float fog = atmosphere_enabled && is_globe ? 0.0 : fog_amount();
  color = vec4(mix(lit_color, srgb_to_linear(fog_color), fog), alpha);
}
)GLSL";

  const GLuint program = create_linked_program("Scene", vertex_source, fragment_source, kAtmosphereGlsl);
  if (program) {
    // Different sampler types must use distinct units even while shadows are off.
    GLint previous = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &previous);
    glUseProgram(program);
    glUniform1i(glGetUniformLocation(program, "shadow_texture"), 3);
    glUseProgram(static_cast<GLuint>(previous));
  }
  return program;
}

GLuint
gworld_scene_view_create_shadow_program(void)
{
  static const char *vertex_source = R"GLSL(
layout(location = 0) in vec3 position;
layout(location = 1) in vec2 texcoord;
layout(location = 6) in vec4 vertex_color;
layout(location = 7) in float material;
uniform mat4 light_mvp;
out vec2 v_uv;
out float v_alpha;
out float v_material;
void main() {
  gl_Position = light_mvp * vec4(position, 1.0);
  v_uv = texcoord;
  v_alpha = vertex_color.a;
  v_material = material;
}
)GLSL";

  static const char *fragment_source = R"GLSL(
in vec2 v_uv;
in float v_alpha;
in float v_material;
uniform sampler2D model_texture;
uniform bool has_model_texture;
void main() {
  if (v_alpha < 0.5) discard;
  if (has_model_texture && v_material > 2.5 && texture(model_texture, v_uv).a < 0.5) discard;
}
)GLSL";

  return create_linked_program("Shadow", vertex_source, fragment_source);
}

GLuint
gworld_scene_view_create_billboard_program(void)
{
  static const char *vertex_source = R"GLSL(
layout(location = 0) in vec3 position;
layout(location = 1) in vec2 texcoord;
uniform mat4 mvp;
out vec2 v_texcoord;
void main() {
  gl_Position = mvp * vec4(position, 1.0);
  v_texcoord = texcoord;
}
)GLSL";

  static const char *fragment_source = R"GLSL(
in vec2 v_texcoord;
uniform sampler2D billboard_texture;
uniform float opacity;
out vec4 color;
void main() {
  vec4 texel = texture(billboard_texture, v_texcoord);
  texel.a *= opacity;
  if (texel.a < 0.01)
    discard;
  color = texel;
}
)GLSL";

  return create_linked_program("Billboard", vertex_source, fragment_source);
}

GLuint
gworld_scene_view_create_sun_program(void)
{
  static const char *vertex_source = R"GLSL(
layout(location = 0) in vec2 position;
layout(location = 1) in vec2 local_coord;
out vec2 v_local_coord;
void main() {
  v_local_coord = local_coord;
  gl_Position = vec4(position, 0.0, 1.0);
}
)GLSL";

  static const char *fragment_source = R"GLSL(
in vec2 v_local_coord;
uniform vec3 sun_color;
uniform float intensity;
uniform float core_ratio;
out vec4 color;
void main() {
  float distance_from_center = length(v_local_coord);
  if (distance_from_center > 1.0)
    discard;

  float core = 1.0 - smoothstep(core_ratio * 0.78, core_ratio, distance_from_center);
  float inner_glow = 1.0 - smoothstep(core_ratio, min(core_ratio * 2.4, 1.0), distance_from_center);
  float halo = pow(clamp(1.0 - distance_from_center, 0.0, 1.0), 2.2);
  float alpha = max(core, max(inner_glow * 0.42, halo * 0.28)) * intensity;
  vec3 white_hot = srgb_to_linear(vec3(1.0, 0.98, 0.86));
  vec3 rgb = mix(srgb_to_linear(sun_color), white_hot, clamp(core + inner_glow * 0.35, 0.0, 1.0));
  color = vec4(rgb, alpha);
}
)GLSL";

  return create_linked_program("Sun", vertex_source, fragment_source);
}

GLuint
gworld_scene_view_create_sky_program(void)
{
  static const char *vertex_source = R"GLSL(
layout(location = 0) in vec2 position;
out vec2 v_ndc;
void main() {
  v_ndc = position;
  gl_Position = vec4(position, 0.0, 1.0);
}
)GLSL";

  static const char *fragment_source = R"GLSL(
in vec2 v_ndc;
uniform mat4 inverse_mvp;
uniform vec3 camera_position;
uniform vec3 sun_direction;
uniform vec3 day_horizon_color;
uniform vec3 day_zenith_color;
uniform vec3 twilight_color;
uniform vec3 night_color;
uniform float daylight;
uniform float twilight;
uniform float horizon_glow_strength;
out vec4 color;

void main() {
  vec4 far_world = inverse_mvp * vec4(v_ndc, 1.0, 1.0);
  far_world /= far_world.w;
  vec3 ray = normalize(far_world.xyz - camera_position);
  if (atmosphere_enabled) {
    color = vec4(atmosphere_sky(camera_position, ray, sun_direction), 1.0);
    return;
  }
  float up = ray.y;

  float sky_mix = smoothstep(-0.04, 0.82, up);
  vec3 day_sky = mix(srgb_to_linear(day_horizon_color), srgb_to_linear(day_zenith_color), sky_mix);
  vec3 twilight_sky = mix(srgb_to_linear(twilight_color), day_sky, clamp(daylight, 0.0, 1.0));
  vec3 sky = mix(srgb_to_linear(night_color), twilight_sky, clamp(daylight + twilight * 0.75, 0.0, 1.0));

  vec2 ray_horizontal = ray.xz;
  vec2 sun_horizontal = sun_direction.xz;
  float ray_len = length(ray_horizontal);
  float sun_len = length(sun_horizontal);
  float azimuth_alignment = 0.0;
  if (ray_len > 0.0001 && sun_len > 0.0001)
    azimuth_alignment = clamp(dot(ray_horizontal / ray_len, sun_horizontal / sun_len), 0.0, 1.0);

  float horizon_band = 1.0 - smoothstep(0.00, 0.20, abs(up));
  float broad_lobe = pow(azimuth_alignment, 5.5);
  float core_lobe = pow(azimuth_alignment, 22.0);
  float glow = horizon_band * (broad_lobe * 0.45 + core_lobe * 0.75) * horizon_glow_strength;
  vec3 amber = srgb_to_linear(vec3(1.00, 0.35, 0.13));
  vec3 rose = srgb_to_linear(vec3(0.90, 0.18, 0.16));
  vec3 glow_color = mix(amber, rose, clamp(twilight * 0.65, 0.0, 1.0));
  sky = mix(sky, glow_color, clamp(glow, 0.0, 0.82));

  color = vec4(sky, 1.0);
}
)GLSL";

  return create_linked_program("Sky", vertex_source, fragment_source, kAtmosphereGlsl);
}

GLuint
gworld_scene_view_create_present_program(void)
{
  static const char *vertex_source = R"GLSL(
out vec2 uv;
void main() {
  uv = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
  gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}
)GLSL";
  static const char *fragment_source = R"GLSL(
in vec2 uv;
uniform sampler2D scene_texture;
uniform bool encode_srgb;
out vec4 color;
void main() {
  vec3 linear = max(texture(scene_texture, uv).rgb, vec3(0.0));
  // A neutral shoulder preserves midtones and hue while retaining highlights
  // above display white in the floating-point render target.
  float peak = max(linear.r, max(linear.g, linear.b));
  if (peak > 0.8) {
    float mapped = 0.8 + 0.2 * (1.0 - exp(-(peak - 0.8) / 0.2));
    linear *= mapped / peak;
  }
  color = vec4(encode_srgb ? linear_to_srgb(linear) : linear, 1.0);
}
)GLSL";
  return create_linked_program("Presentation", vertex_source, fragment_source);
}
