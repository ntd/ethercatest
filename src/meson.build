ethercatest_soem_sources = files([
    'ethercatest-soem.c',
    'ethercatest.c',
    'ethercatest.h',
])
ethercatest_soem_deps = [
    soem_dep,
    thread_dep,
]
executable('ethercatest-soem',
           sources: ethercatest_soem_sources,
           dependencies: ethercatest_soem_deps,
           install: true)

ethercatest_igh_sources = files([
    'ethercatest-igh.c',
    'ethercatest.c',
    'ethercatest.h',
])
ethercatest_igh_deps = [
    igh_dep,
]
executable('ethercatest-igh',
           sources: ethercatest_igh_sources,
           dependencies: ethercatest_igh_deps,
           install: true)
