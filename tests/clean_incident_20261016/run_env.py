"""Launch tools with case-insensitive Windows environment keys normalized."""
import os
import subprocess
import sys

args = sys.argv[1:]
command = subprocess.list2cmdline(args[:-1]) + ' ' + args[-1] if args[0].lower() == 'cmd.exe' else args
raise SystemExit(subprocess.call(command, env={k.upper(): v for k, v in os.environ.items()}))
