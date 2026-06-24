#ifndef GWORLD_SCENE_VIEW_PRIVATE_H
#define GWORLD_SCENE_VIEW_PRIVATE_H

#include "gworld-scene-view.h"

struct GWorldSceneViewState;

class GWorldSceneViewBackend {
public:
  explicit GWorldSceneViewBackend(GWorldSceneView *owner);
  ~GWorldSceneViewBackend();

  GWorldSceneViewBackend(const GWorldSceneViewBackend &) = delete;
  GWorldSceneViewBackend &operator=(const GWorldSceneViewBackend &) = delete;

  GWorldSceneView *owner() const;
  GWorldSceneViewState &state();
  const GWorldSceneViewState &state() const;

  void schedule_scene_requests(guint delay_ms);
  void ensure_scene_requests_scheduled(guint delay_ms);

private:
  GWorldSceneView *owner_ = nullptr;
  GWorldSceneViewState *state_ = nullptr;
};

#endif /* GWORLD_SCENE_VIEW_PRIVATE_H */
