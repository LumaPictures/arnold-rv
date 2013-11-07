import excons
import excons.tools.arnold as arnold

env = excons.MakeBaseEnv()

static = int(ARGUMENTS.get("static", "0"))

SConscript("gnet/SConstruct")

targets = [
   {"name": "rvdriver",
    "type": "dynamicmodule",
    "defs": (["GCORE_STATIC", "GNET_STATIC"] if static else []),
    "incdirs": ["gnet/gcore/include", "gnet/include"],
    "srcs": ["driver/rvdriver.cpp"],
    "libs": ["gcore", "gnet"],
    "custom": [arnold.Require]
   }
]

excons.DeclareTargets(env, targets)
