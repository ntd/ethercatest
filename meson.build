project('ethercatest', 'c',
        version: '0.0.1',
        license: 'GPL2')

cc         = meson.get_compiler('c')
soem_dep   = cc.find_library('soem')
glib_dep   = dependency('glib-2.0')
thread_dep = dependency('threads')
igh_dep    = cc.find_library('ethercat')

subdir('src')
