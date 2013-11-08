import excons
import excons.tools.arnold as arnold
import excons.tools.threads as threads

env = excons.MakeBaseEnv()

static = int(ARGUMENTS.get("static", "0"))

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
   }
]

excons.DeclareTargets(env, targets)
