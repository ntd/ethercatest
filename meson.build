project('ethercatest', 'c',
        meson_version: '>=0.47.0',
        version: '0.0.1',
        license: 'GPL2')

cc       = meson.get_compiler('c')
soem_dep = cc.find_library('soem', required: get_option('soem'))
igh_dep  = cc.find_library('ethercat', required: get_option('igh'))

subdir('src')
