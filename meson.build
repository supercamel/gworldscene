project(
  'gworldscene',
  ['c', 'cpp', 'vala'],
  version: '0.4.0',
  default_options: [
    'buildtype=debugoptimized',
    'c_std=c11',
    'cpp_std=c++17',
    'warning_level=2',
  ],
)

gnome = import('gnome')
pkg = import('pkgconfig')

subdir('src')
subdir('examples')
subdir('tests')
