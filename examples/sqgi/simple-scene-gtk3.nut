#!/usr/bin/env sqgi

function script_dir() {
  local src = getstackinfos(1).src
  for (local i = src.len() - 1; i >= 0; i = i - 1) {
    if (src[i] == 47)
      return src.slice(0, i)
  }
  return "."
}

::GWORLD_SCENE_SQGI_GTK_MAJOR <- 3
dofile(script_dir() + "/simple-scene.nut")
