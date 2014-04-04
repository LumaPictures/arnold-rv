import os
import subprocess

try:
   import excons
except:
   print("Initializing submodule...")
   p = subprocess.Popen("git submodule init", shell=True)
   p.communicate()
   p = subprocess.Popen("git submodule update", shell=True)
   p.communicate()
   import excons

import excons.tools.arnold as arnold
import excons.tools.threads as threads
import excons.tools.boost as boost

env = excons.MakeBaseEnv()

static = (int(ARGUMENTS.get("static", "1")) != 0)

# to force static build (or not) in gnet
ARGUMENTS["static"] = ("1" if static else "0")
SConscript("gcore/SConstruct")

ARGUMENTS["with-gcore-inc"] = os.path.abspath("gcore/include")
SConscript("gnet/SConstruct")

customs = [arnold.Require]
if static:
   customs.append(threads.Require)

targets = [
   {"name": "rvdriver",
    "type": "dynamicmodule",
    "ext": arnold.PluginExt(),
    "defs": (["GCORE_STATIC", "GNET_STATIC"] if static else []),
    "incdirs": ["gcore/include", "gnet/include"],
    "srcs": ["driver/rvdriver.cpp"],
    "libs": ["gcore", "gnet"],
    "custom": customs
   },
   {"name": "driver_rv",
    "type": "dynamicmodule",
    "ext": arnold.PluginExt(),
    "srcs": ["driver/driver_rv.cpp"],
    "custom": [arnold.Require, boost.Require(libs=["thread-mt", "date_time-mt", "system-mt"], static=static)]
   }
]

excons.DeclareTargets(env, targets)

Default("rvdriver")
