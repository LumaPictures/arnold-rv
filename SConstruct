import excons
import excons.tools.arnold as arnold
import excons.tools.threads as threads
import excons.tools.boost as boost

env = excons.MakeBaseEnv()

static = (int(ARGUMENTS.get("static", "0")) != 0)

SConscript("gnet/SConstruct")

customs = [arnold.Require]
if static:
   customs.append(threads.Require)

targets = [
   {"name": "rvdriver",
    "type": "dynamicmodule",
    "ext": arnold.PluginExt(),
    "defs": (["GCORE_STATIC", "GNET_STATIC"] if static else []),
    "incdirs": ["gnet/gcore/include", "gnet/include"],
    "srcs": ["driver/rvdriver.cpp"],
    "libs": ["gcore", "gnet"],
    "custom": customs
   },
   {"name": "driver_rv",
    "type": "dynamicmodule",
    "ext": arnold.PluginExt(),
    "srcs": ["driver/org/rvdriver.cpp"],
    "custom": [arnold.Require, boost.Require(libs=["thread-mt", "date_time-mt", "system-mt"], static=static)]
   }
]

excons.DeclareTargets(env, targets)

Default("rvdriver")
