ethercatest_soem_sources = files([
    'ethercatest-soem.c',
])
ethercatest_soem_deps = [
    glib_dep,
    soem_dep,
    thread_dep,
]

executable('ethercatest-soem',
           sources: ethercatest_soem_sources,
           dependencies: ethercatest_soem_deps,
           install: true)
