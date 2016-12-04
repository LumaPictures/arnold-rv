import os
import sys
import glob
import subprocess

initsubs = False

try:
   import excons
   if len(glob.glob("gcore/*")) == 0:
      initsubs = True
   if len(glob.glob("gnet/*")) == 0:
      initsubs = True
except:
   initsubs = True

if initsubs:
   subprocess.Popen("git submodule init", shell=True).communicate()
   subprocess.Popen("git submodule update", shell=True).communicate()
   
   import excons

import excons.tools.arnold as arnold
import excons.tools.threads as threads
import excons.tools.boost as boost

env = excons.MakeBaseEnv()

static = (excons.GetArgument("static", 1, int) != 0)

# to force static build (or not) in gnet
excons.SetArgument("static", 1 if static else 0)
SConscript("gcore/SConstruct")

excons.SetArgument("with-gcore-inc", os.path.abspath("gcore/include"))
SConscript("gnet/SConstruct")

customs = [arnold.Require]
if static:
   customs.append(threads.Require)

if sys.platform != "win32":
   def NoUnused(env):
      env.Append(CPPFLAGS=" -Wno-unused-parameter")
   
   customs.append(NoUnused)


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
    "custom": [arnold.Require, boost.Require(libs=["thread-mt", "date_time-mt", "system-mt"])]
   }
]

excons.DeclareTargets(env, targets)

Default("rvdriver")
