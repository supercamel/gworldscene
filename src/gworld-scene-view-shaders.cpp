#ifndef G_LOG_DOMAIN
#define G_LOG_DOMAIN "GWorldScene"
#endif

#include "gworld-scene-view-shaders-private.h"

#include <glib.h>

namespace {

GLuint
compile_shader(GLenum type, const char *source)
{
  GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, nullptr);
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
create_linked_program(const char *label, const char *vertex_source, const char *fragment_source)
{
  GLuint vertex = compile_shader(GL_VERTEX_SHADER, vertex_source);
  GLuint fragment = compile_shader(GL_FRAGMENT_SHADER, fragment_source);
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
#version 330 core
layout(location = 0) in vec3 position;
layout(location = 1) in vec2 detail_texcoord;
layout(location = 2) in vec2 mid_texcoord;
layout(location = 3) in vec2 base_texcoord;
layout(location = 4) in vec2 ultra_texcoord;
layout(location = 5) in vec3 normal;
layout(location = 6) in vec4 vertex_color;
layout(location = 7) in float material;
uniform mat4 mvp;
uniform mat4 light_mvp;
uniform bool ultra_atlas_valid;
uniform vec4 ultra_atlas_range;
uniform vec2 ultra_atlas_size;
uniform bool detail_atlas_valid;
uniform vec4 detail_atlas_range;
uniform vec2 detail_atlas_size;
uniform bool mid_atlas_valid;
uniform vec4 mid_atlas_range;
uniform vec2 mid_atlas_size;
uniform bool base_atlas_valid;
uniform vec4 base_atlas_range;
uniform vec2 base_atlas_size;
uniform bool globe_atlas_valid;
uniform vec4 globe_atlas_range;
uniform vec2 globe_atlas_size;
out vec2 v_detail_texcoord;
out vec2 v_mid_texcoord;
out vec2 v_base_texcoord;
out vec2 v_ultra_texcoord;
out vec3 v_normal;
out vec4 v_color;
out float v_material;
out float v_height;
out vec3 v_world_position;
out vec4 v_light_position;

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
    v_base_texcoord = base_atlas_valid ? atlas_uv_for_lat_lon(lat_lon, base_atlas_range, base_atlas_size) : vec2(-1.0, -1.0);
    v_ultra_texcoord = ultra_atlas_valid ? atlas_uv_for_lat_lon(lat_lon, ultra_atlas_range, ultra_atlas_size) : vec2(-1.0, -1.0);
  } else if (is_globe) {
    vec2 lat_lon = detail_texcoord;
    v_detail_texcoord = vec2(-1.0, -1.0);
    v_mid_texcoord = vec2(-1.0, -1.0);
    v_base_texcoord = globe_atlas_valid ? atlas_uv_for_lat_lon(lat_lon, globe_atlas_range, globe_atlas_size) : vec2(-1.0, -1.0);
    v_ultra_texcoord = vec2(-1.0, -1.0);
  } else {
    v_detail_texcoord = detail_texcoord;
    v_mid_texcoord = mid_texcoord;
    v_base_texcoord = base_texcoord;
    v_ultra_texcoord = ultra_texcoord;
  }
  v_normal = normal;
  v_color = vertex_color;
  v_material = material;
  v_height = position.y;
  v_world_position = position;
  v_light_position = light_mvp * world_position;
}
)GLSL";

  static const char *fragment_source = R"GLSL(
#version 330 core
in vec2 v_detail_texcoord;
in vec2 v_mid_texcoord;
in vec2 v_base_texcoord;
in vec2 v_ultra_texcoord;
in vec3 v_normal;
in vec4 v_color;
in float v_material;
in float v_height;
in vec3 v_world_position;
in vec4 v_light_position;
uniform sampler2D ultra_texture;
uniform sampler2D detail_texture;
uniform sampler2D mid_texture;
uniform sampler2D base_texture;
uniform sampler2D shadow_texture;
uniform sampler2D model_texture;
uniform bool has_ultra_texture;
uniform bool has_detail_texture;
uniform bool has_mid_texture;
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

vec3 lighting_normal() {
  vec3 normal = normalize(v_normal);
  if (v_material < 0.5 && terrain_normal_smoothing > 0.0) {
    vec3 local_up = vec3(0.0, 1.0, 0.0);
    float slope = 1.0 - clamp(abs(dot(normal, local_up)), 0.0, 1.0);
    float smoothing = clamp(terrain_normal_smoothing * (1.0 - slope * 0.28), 0.0, 1.0);
    normal = normalize(mix(normal, local_up, smoothing));
  }
  return normal;
}

float shadow_visibility(vec3 normal) {
  if (!has_shadow_texture || (v_material > 1.5 && v_material < 2.5))
    return 1.0;

  vec3 projected = v_light_position.xyz / v_light_position.w;
  projected = projected * 0.5 + 0.5;
  if (projected.z > 1.0 ||
      projected.x < 0.0 || projected.x > 1.0 ||
      projected.y < 0.0 || projected.y > 1.0)
    return 1.0;

  vec2 texel_size = 1.0 / vec2(textureSize(shadow_texture, 0));
  float bias = max(0.0012 * (1.0 - dot(normal, sun_direction)), 0.00035);
  float lit = 0.0;
  for (int y = -1; y <= 1; ++y) {
    for (int x = -1; x <= 1; ++x) {
      float depth = texture(shadow_texture, projected.xy + vec2(x, y) * texel_size).r;
      lit += projected.z - bias <= depth ? 1.0 : 0.0;
    }
  }

  return mix(0.42, 1.0, lit / 9.0);
}

float fog_amount() {
  if (!fog_enabled)
    return 0.0;

  float distance_to_camera = length(v_world_position - camera_position);
  float range = max(fog_end - fog_start, 1.0);
  float linear_fog = clamp((distance_to_camera - fog_start) / range, 0.0, 1.0);
  float density_fog = 1.0 - exp(-distance_to_camera * max(fog_density, 0.0));
  return clamp(max(linear_fog, density_fog), 0.0, 1.0);
}

void main() {
  vec3 normal = lighting_normal();
  float diffuse = clamp(dot(normal, normalize(sun_direction)), 0.0, 1.0);
  float visibility = shadow_visibility(normal);
  vec3 light = ambient_color * ambient_strength +
               direct_light_color * diffuse * sun_strength * visibility;
  vec3 low = vec3(0.26, 0.36, 0.24);
  vec3 high = vec3(0.76, 0.72, 0.62);
  vec3 terrain_tint = mix(low, high, clamp(v_height / 1800.0, 0.0, 1.0));
  bool in_detail = v_detail_texcoord.x >= 0.0 && v_detail_texcoord.x <= 1.0 && v_detail_texcoord.y >= 0.0 && v_detail_texcoord.y <= 1.0;
  bool in_mid = v_mid_texcoord.x >= 0.0 && v_mid_texcoord.x <= 1.0 && v_mid_texcoord.y >= 0.0 && v_mid_texcoord.y <= 1.0;
  bool in_base = v_base_texcoord.x >= 0.0 && v_base_texcoord.x <= 1.0 && v_base_texcoord.y >= 0.0 && v_base_texcoord.y <= 1.0;
  bool in_ultra = v_ultra_texcoord.x >= 0.0 && v_ultra_texcoord.x <= 1.0 && v_ultra_texcoord.y >= 0.0 && v_ultra_texcoord.y <= 1.0;
  vec4 base_texel = (has_base_texture && in_base) ? texture(base_texture, v_base_texcoord) : vec4(0.0);
  vec4 mid_texel = (has_mid_texture && in_mid) ? texture(mid_texture, v_mid_texcoord) : vec4(0.0);
  vec4 detail_texel = (has_detail_texture && in_detail) ? texture(detail_texture, v_detail_texcoord) : vec4(0.0);
  vec4 ultra_texel = (has_ultra_texture && in_ultra) ? texture(ultra_texture, v_ultra_texcoord) : vec4(0.0);
  vec4 texture_stack = detail_texel.a > 0.01 ? detail_texel : (mid_texel.a > 0.01 ? mid_texel : base_texel);
  float ultra_lateral_distance = length((v_world_position - ultra_texture_center).xz);
  float ultra_radius = max(ultra_texture_radius, 1.0);
  float ultra_fade_width = clamp(ultra_radius * 0.22, 80.0, 220.0);
  float ultra_blend = 1.0 - smoothstep(max(0.0, ultra_radius - ultra_fade_width),
                                      ultra_radius,
                                      ultra_lateral_distance);
  ultra_blend *= ultra_texel.a > 0.01 ? 1.0 : 0.0;
  vec4 texel = (has_ultra_texture && ultra_texture_radius > 1.0)
                 ? mix(texture_stack, ultra_texel, ultra_blend)
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
  vec3 lit_color = base * clamp(light, vec3(0.0), vec3(1.45));
  float alpha = (v_material > 0.5 && !is_globe) ? v_color.a : 1.0;
  color = vec4(mix(lit_color, fog_color, fog_amount()), alpha);
}
)GLSL";

  return create_linked_program("Scene", vertex_source, fragment_source);
}

GLuint
gworld_scene_view_create_shadow_program(void)
{
  static const char *vertex_source = R"GLSL(
#version 330 core
layout(location = 0) in vec3 position;
uniform mat4 light_mvp;
void main() {
  gl_Position = light_mvp * vec4(position, 1.0);
}
)GLSL";

  static const char *fragment_source = R"GLSL(
#version 330 core
void main() {
}
)GLSL";

  return create_linked_program("Shadow", vertex_source, fragment_source);
}

GLuint
gworld_scene_view_create_billboard_program(void)
{
  static const char *vertex_source = R"GLSL(
#version 330 core
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
#version 330 core
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
#version 330 core
layout(location = 0) in vec2 position;
layout(location = 1) in vec2 local_coord;
out vec2 v_local_coord;
void main() {
  v_local_coord = local_coord;
  gl_Position = vec4(position, 0.0, 1.0);
}
)GLSL";

  static const char *fragment_source = R"GLSL(
#version 330 core
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
  vec3 white_hot = vec3(1.0, 0.98, 0.86);
  vec3 rgb = mix(sun_color, white_hot, clamp(core + inner_glow * 0.35, 0.0, 1.0));
  color = vec4(rgb, alpha);
}
)GLSL";

  return create_linked_program("Sun", vertex_source, fragment_source);
}

GLuint
gworld_scene_view_create_sky_program(void)
{
  static const char *vertex_source = R"GLSL(
#version 330 core
layout(location = 0) in vec2 position;
out vec2 v_ndc;
void main() {
  v_ndc = position;
  gl_Position = vec4(position, 0.0, 1.0);
}
)GLSL";

  static const char *fragment_source = R"GLSL(
#version 330 core
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
  float up = ray.y;

  float sky_mix = smoothstep(-0.04, 0.82, up);
  vec3 day_sky = mix(day_horizon_color, day_zenith_color, sky_mix);
  vec3 twilight_sky = mix(twilight_color, day_sky, clamp(daylight, 0.0, 1.0));
  vec3 sky = mix(night_color, twilight_sky, clamp(daylight + twilight * 0.75, 0.0, 1.0));

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
  vec3 amber = vec3(1.00, 0.35, 0.13);
  vec3 rose = vec3(0.90, 0.18, 0.16);
  vec3 glow_color = mix(amber, rose, clamp(twilight * 0.65, 0.0, 1.0));
  sky = mix(sky, glow_color, clamp(glow, 0.0, 0.82));

  color = vec4(sky, 1.0);
}
)GLSL";

  return create_linked_program("Sky", vertex_source, fragment_source);
}
