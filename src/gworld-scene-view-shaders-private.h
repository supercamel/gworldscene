#ifndef GWORLD_SCENE_VIEW_SHADERS_PRIVATE_H
#define GWORLD_SCENE_VIEW_SHADERS_PRIVATE_H

#include <epoxy/gl.h>

GLuint gworld_scene_view_create_program(void);
GLuint gworld_scene_view_create_shadow_program(void);
GLuint gworld_scene_view_create_billboard_program(void);
GLuint gworld_scene_view_create_sun_program(void);
GLuint gworld_scene_view_create_sky_program(void);

#endif /* GWORLD_SCENE_VIEW_SHADERS_PRIVATE_H */
