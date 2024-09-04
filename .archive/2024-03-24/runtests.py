#!/usr/bin/python3
import os
import sys
import pathlib
import subprocess

# - https://stackoverflow.com/a/3430395
SCRIPTDIR = pathlib.Path(__file__).parent.resolve()
TESTDIR   = os.path.join(SCRIPTDIR, "tests/")
LULUEXE   = os.path.join(SCRIPTDIR, "bin/lulu")


# Determine if is file, not dir, and is a lua script
def isluascript(fpath: str, fname: str):
    # Resolve to complete and absolute version of path + relative name
    fname = os.path.join(fpath, fname)
    return os.path.isfile(fname) and fname.find(".lua", 0, -4)


# Get a list of our lulu/lua test scripts.
# - https://stackoverflow.com/a/3207973
def gettests(fpath: str):
    return [item for item in os.listdir(fpath) if isluascript(fpath, item)]


def invalidtest(i: int, limit: int, usage: str):
    if i < 0 or i > limit:
        print(f"Out of range {usage} index {i}: must be {0} to {limit}")
        return True
    return False


# '*args' are basically positional variadic argument automatically packed into
# a tuple for you.
# - https://stackoverflow.com/a/64680708
def capture(*args: str):
    # To redirect both stdout and stderr to the same file stream, use the
    # respective named arguments and supply 'subprocess.PIPE'.
    # - https://docs.python.org/3/library/subprocess.html#subprocess.run
    proc = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    # If we have errors, we MIGHT have stuff already in stdout. 
    if proc.stderr != b"":
        data = b"".join([proc.stdout, proc.stderr])
    else:
        data = proc.stdout

    # Since subprocess.run() returns raw byte strings, we convert it to a normal
    # Python UTF-8 string.
    # - https://stackoverflow.com/a/606199
    return data.decode().strip('\n')


# Call a system process.
# - https://stackoverflow.com/a/89243
#
# Use bufsize of 1 to force process to not buffer the output.
# - https://stackoverflow.com/a/6414278
#
# We explicitly need to 'buffer' our output manually so that when piped into
# another process like 'less' the output will be consistent.
def runtest(script: str, testno: int):
    print(f"TEST-{testno:02}: {script:16}")
    script = os.path.join(TESTDIR, script)
    print(f"Contents of '{script}':")
    print(capture("cat", "--number", "--", script))
    print(f"Output of '{script}':")
    print(capture(LULUEXE, script))
    print()
    

def printscripts(scripts: list[str]):
    for i  in range(len(scripts)):
        print(f"{i:-2}: {scripts[i]}")
    

def main(argc: int, argv: list[str]):
    # https://stackoverflow.com/a/14032557
    scripts = sorted(gettests(TESTDIR), reverse=False)
    limit   = len(scripts) - 1
    if argc != 2 and argc != 3:
        print(f"Usage: {argv[0]} <start> [stop]")
        print("Options:")
        printscripts(scripts)
        sys.exit(2)
    elif argc == 2 and isinstance(argv[1], str):
        # Run all or scripts or a particular script
        if argv[1] == "all":
            for i in range(0, len(scripts)):
                runtest(scripts[i], i)
        elif argv[1] == "help":
            printscripts(scripts);
        else:
            if argv[1].find(".lua") == -1:
                argv[1] += ".lua"
            runtest(argv[1], 0)
        sys.exit(0)
    
    start = int(argv[1])
    stop  = (int(argv[2]) if argc == 3 else start)
    if invalidtest(start, limit, "starting"):
        sys.exit(1)
    elif invalidtest(stop, limit, "ending"):
        sys.exit(1)
    else:
        # Add 1 so this acts more like an inclusive end
        for i in range(start, stop + 1):
            runtest(scripts[i], i)
    sys.exit(0)


if __name__ == "__main__":
    main(len(sys.argv), sys.argv)
