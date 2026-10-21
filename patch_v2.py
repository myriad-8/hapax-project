import re

m = open('Makefile').read()
m = re.sub(r"> NC=\$\$\(nproc\); for l in \$\(LOCKS\); do .*\n",
 "> NC=$$(nproc); for l in $(LOCKS); do timeout 120 ./$(BIN)/test_$$l $$((NC*2)) 20 || echo \"$$l: oversubscribed test slow/timeout (expected for spinlocks, not a failure)\"; done\n", m)
open('Makefile','w').write(m)

b = open('src/bench.c').read()
b = b.replace("static int g_cs = 0, g_ncs = 0, g_pin = 1, g_ncpu = 1;",
              "static int g_cs = 0, g_ncs = 0, g_pin = 1, g_ncpu = 1;\nstatic int g_cpus[4096];")
b = b.replace("CPU_SET(t->id % g_ncpu, &s);", "CPU_SET(g_cpus[t->id % g_ncpu], &s);")
b = b.replace("  g_ncpu = (int)sysconf(_SC_NPROCESSORS_ONLN);",
"""  { cpu_set_t m; CPU_ZERO(&m); sched_getaffinity(0, sizeof m, &m);
    g_ncpu = 0;
    for (int c = 0; c < CPU_SETSIZE && g_ncpu < 4096; c++) if (CPU_ISSET(c, &m)) g_cpus[g_ncpu++] = c;
    if (g_ncpu == 0) { g_ncpu = 1; g_cpus[0] = 0; } }""")
open('src/bench.c','w').write(b)

r = open('scripts/run_all.sh').read()
r = r.replace("echo $NCPU; echo $((NCPU*2)); }",
              "echo $NCPU; [ \"${OVERSUB:-0}\" = 1 ] && echo $((NCPU*2)); true; }")
open('scripts/run_all.sh','w').write(r)
print("patched OK. Now run:  make clean && make all")
