if soem_dep.found()
    ethercatest_soem_sources = files([
        'ethercatest-soem.c',
        'ethercatest.c',
        'ethercatest.h',
    ])
    ethercatest_soem_deps = [
        soem_dep,
        dependency('threads')
    ]
    executable('ethercatest-soem',
               sources: ethercatest_soem_sources,
               dependencies: ethercatest_soem_deps,
               install: true)
endif

if igh_dep.found()
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
endif
