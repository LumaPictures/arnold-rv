import socket

def command(cmd, python=False, host="localhost", port=4700, verbose=False):
   if cmd:
      client = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
      client.connect((host, port))
      if cmd[-1] not in ["\n", ";"]:
         cmd += ";"
      if python:
         cmd = "python(\"" + cmd.replace("\\", "\\\\").replace("\"", "\\\"") + "\");"
      if verbose:
         print("sendmaya.command [%s]\n%s" % ("python" if python else "mel", cmd))
      client.send(cmd + "\n")
      client.close()
      return True
   else:
      return False

if __name__ == "__main__":
   import sys

   host = "localhost"
   port = 4700
   verbose = False
   python = False
   cmd = ""
   readcmd = False

   i = 1
   n = len(sys.argv)
   while i < n:
      if not readcmd:
         if sys.argv[i] in ["-h", "--host"]:
            i += 1
            if i >= n:
               sys.stderr.write("-h/--host flag requires a value\n")
               sys.exit(1)
            host = sys.argv[i]
         elif sys.argv[i] in ["-p", "--port"]:
            i += 1
            if i >= n:
               sys.stderr.write("-p/--port flag requires a value\n")
               sys.exit(1)
            try:
               port = int(sys.argv[i])
            except:
               sys.stderr.write("-p/--port expects an integer value\n")
               sys.exit(1)
         elif sys.argv[i] in ["-v", "--verbose"]:
            verbose = True
         elif sys.argv[i] in ["-py", "--python"]:
            python = True
         elif sys.argv[i] == "--":
            readcmd = True
         else:
            sys.stderr.write("Invalid argument \"%s\"\n" % sys.argv[i])
            sys.exit(1)
      else:
         if cmd:
            cmd += " "
         cmd += sys.argv[i]
      i += 1

   try:
      command(cmd, python=python, host=host, port=port, verbose=verbose)
      sys.exit(0)
   except Exception, e:
      sys.stderr.write("sendmaya.command failed (%s)\n" % e)      
      sys.exit(1)
