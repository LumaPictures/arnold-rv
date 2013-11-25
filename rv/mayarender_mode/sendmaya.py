import socket
import sys

def command(cmd, host, port, python, readSize, verbose):
   if cmd:
      try:
         client = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
         client.connect((host, port))
         if cmd[-1] not in ["\n", ";"]:
            cmd += ";"
         if python:
            cmd = "python(\"" + cmd.replace("\\", "\\\\").replace("\"", "\\\"") + "\");"
         if verbose:
            print("sendmaya.command [%s]\n%s" % ("python" if python else "mel", cmd))
         client.send(cmd + "\n")
         if readSize > 0:
            rv = client.recv(readSize)
         else:
            rv = ""
         client.close()
         return rv
      except Exception, e:
         sys.stderr.write("sendmaya.command failed (%s)\n" % e)      
         return ""
   else:
      return ""

if __name__ == "__main__":
   host = "localhost"
   port = 4700
   verbose = False
   python = False
   readSize = 0
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
         elif sys.argv[i] in ["-r", "--read"]:
            i += 1
            if i >= n:
               sys.stderr.write("-r/--read flag requires a value\n")
               sys.exit(1)
            try:
               readSize = int(sys.argv[i])
            except:
               sys.stderr.write("-r/--read expects an integer value\n")
               sys.exit(1)
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

   rv = command(cmd, host, port, python, readSize, verbose)
   if readSize > 0:
      sys.stdout.write(rv)

   sys.exit(0)
