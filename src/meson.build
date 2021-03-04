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

ethercatest_igh_sources = files([
    'ethercatest-igh.c',
])
ethercatest_igh_deps = [
    glib_dep,
    igh_dep,
]
executable('ethercatest-igh',
           sources: ethercatest_igh_sources,
           dependencies: ethercatest_igh_deps,
           install: true)
