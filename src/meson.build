ethercatest_sources = files([
    'ethercatest.c',
])

ethercatest_deps = [
    glib_dep,
    soem_dep,
    thread_dep,
]

executable('ethercatest',
           sources: ethercatest_sources,
           dependencies: ethercatest_deps,
           install: true)
