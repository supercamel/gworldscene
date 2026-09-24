#ifndef GWORLD_SCENE_ATMOSPHERE_GLSL_PRIVATE_H
#define GWORLD_SCENE_ATMOSPHERE_GLSL_PRIVATE_H

// Shared by surface, sky and water shaders so their horizon/light agree.
// Single scattering with exponential density profiles, in kilometres.
// Rayleigh/Mie phase functions and Beer-Lambert extinction; sunlight optical
// depth uses a curved-atmosphere approximation instead of a nested ray march.
static const char *kAtmosphereGlsl = R"GLSL(
uniform bool atmosphere_enabled;
uniform float atmosphere_density;
uniform float atmosphere_haze;
uniform vec3 planet_center;
uniform mat3 atmosphere_from_scene;
const float ATM_RADIUS = 6371.0;
const float ATM_TOP = 6431.0;
const vec3 ATM_RAYLEIGH = vec3(0.005802, 0.013558, 0.033100);
const vec3 ATM_MIE = vec3(0.003996);
const float ATM_PI = 3.141592653589793;

vec2 atmosphere_sphere(vec3 p, vec3 ray, float radius) {
  float b = dot(p, ray);
  // The factored radial difference retains precision near the surface.
  float r = length(p);
  float c = (r - radius) * (r + radius);
  float discriminant = b * b - c;
  if (discriminant < 0.0) return vec2(-1.0);
  float root = sqrt(discriminant);
  return vec2(-b - root, -b + root);
}

vec3 atmosphere_position(vec3 scene) {
  return atmosphere_from_scene * (scene - planet_center);
}

vec3 atmosphere_sun_transmittance(vec3 p, vec3 sun, float density, float haze) {
  float r = length(p);
  float h = max(r - ATM_RADIUS, 0.0);
  float mu = dot(p / max(r, 1.0), sun);
  // The finite solar disc makes Earth's shadow continuous. A binary shadow
  // test creates visible bands as individual integration samples enter night.
  float horizon = -sqrt(max(0.0, 1.0 - pow(ATM_RADIUS / max(r, ATM_RADIUS), 2.0)));
  float sunlight = smoothstep(horizon - 0.0047, horizon + 0.0047, mu);
  vec2 heights = vec2(8.0, 1.2);
  vec2 denominator = sqrt(vec2(mu * mu) + 2.0 * heights / max(r, 1.0)) + mu;
  vec2 depth = exp(-h / heights) * 2.0 * heights / max(denominator, vec2(0.001));
  return sunlight * exp(-density * (ATM_RAYLEIGH * depth.x + ATM_MIE * haze * 1.1 * depth.y));
}

vec3 atmosphere_integrate(vec3 camera, vec3 direction, float limit_km, vec3 sun_direction,
                           out vec3 transmittance) {
  transmittance = vec3(1.0);
  vec3 origin = atmosphere_position(camera);
  // Cameras slightly below sea level still need a usable sky.
  float radius = length(origin);
  if (radius < ATM_RADIUS + 0.002) origin *= (ATM_RADIUS + 0.002) / max(radius, 1.0);
  vec3 ray = normalize(atmosphere_from_scene * direction);
  vec3 sun = normalize(atmosphere_from_scene * sun_direction);
  vec2 shell = atmosphere_sphere(origin, ray, ATM_TOP);
  float start = max(shell.x, 0.0);
  float end = min(shell.y, limit_km);
  float ground = atmosphere_sphere(origin, ray, ATM_RADIUS).x;
  if (ground > 0.0) end = min(end, ground);
  if (end <= start) return vec3(0.0);
  float density = max(atmosphere_density, 0.0);
  float haze = max(atmosphere_haze, 0.0);
  float mu = clamp(dot(ray, sun), -1.0, 1.0);
  float phase_r = 3.0 / (16.0 * ATM_PI) * (1.0 + mu * mu);
  const float g = 0.76;
  float phase_m = (1.0 - g * g) / (4.0 * ATM_PI * pow(max(1.0 + g * g - 2.0 * g * mu, 0.01), 1.5));
  vec3 scattering = vec3(0.0);
  // Concentrate samples at the low-altitude end of a surface ray. From
  // space this is the far end, while a limb ray needs the whole shell sampled.
  bool from_space = radius > ATM_TOP;
  int steps = from_space ? 16 : 8;
  for (int i = 0; i < 16; ++i) {
    if (i >= steps) break;
    float a = float(i) / float(steps), b = float(i + 1) / float(steps);
    float u0 = from_space ? (ground > 0.0 ? 1.0 - (1.0 - a) * (1.0 - a) : a) : a * a;
    float u1 = from_space ? (ground > 0.0 ? 1.0 - (1.0 - b) * (1.0 - b) : b) : b * b;
    float t0 = mix(start, end, u0), t1 = mix(start, end, u1);
    float step_km = t1 - t0;
    vec3 p = origin + ray * (t0 + t1) * 0.5;
    float height = max(length(p) - ATM_RADIUS, 0.0);
    vec2 local_density = exp(-height / vec2(8.0, 1.2)) * density;
    vec3 rayleigh = ATM_RAYLEIGH * local_density.x;
    vec3 mie = ATM_MIE * local_density.y * haze;
    vec3 extinction = rayleigh + mie * 1.1;
    vec3 step_transmittance = exp(-extinction * step_km);
    vec3 integral = (vec3(1.0) - step_transmittance) / max(extinction, vec3(0.000001));
    scattering += transmittance * integral * (rayleigh * phase_r + mie * phase_m) *
                  atmosphere_sun_transmittance(p, sun, density, haze) * 16.0;
    transmittance *= step_transmittance;
  }
  return scattering;
}

vec3 atmosphere_sky(vec3 camera, vec3 ray, vec3 sun) {
  vec3 transmittance;
  vec3 scattering = atmosphere_integrate(camera, ray, 1000000.0, sun, transmittance);
  // Small night-sky illumination rather than a blue daytime gradient in space.
  float sun_height = dot(normalize(atmosphere_position(camera)), normalize(atmosphere_from_scene * sun));
  float night = 1.0 - smoothstep(-0.18, 0.02, sun_height);
  float air = 1.0 - smoothstep(20.0, 100.0, length(atmosphere_position(camera)) - ATM_RADIUS);
  return scattering + vec3(0.0015, 0.0020, 0.0040) * night * air * min(atmosphere_density, 1.0) * transmittance;
}

vec3 atmosphere_surface(vec3 color, vec3 camera, vec3 point, vec3 sun) {
  vec3 delta = point - camera;
  float distance_m = length(delta);
  if (distance_m < 0.01) return color;
  vec3 transmittance;
  float distance_km = length(atmosphere_from_scene * delta);
  vec3 scattered = atmosphere_integrate(camera, delta / distance_m, distance_km, sun, transmittance);
  return color * transmittance + scattered;
}
)GLSL";
#endif
