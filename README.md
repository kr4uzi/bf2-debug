# bf2-debug
This project enables proper debugging for the embedded python used in Battlefield 2.

# bf2 debugging
1. double-click launcher.exe (launches bf2_w32ded.exe detected from local > server (registry) > client (registry))
2. Launch VS-Code: right-click <bf2-path>\python > "Open with Code"
3. open "Run and Debug" (ctrl+shift+d) > Add Configuration... > Remote Attach > localhost (default) > 5678 (default)

# parameters
The launcher accepts the following parameters:
 - -bf2path=<path/to/bf2>
 - -inject=<path/to/dll>

The launcher has the following hierarchy when searching for the "bf2_w32ded.exe":
1. -bf2path parameter
2. current directory
3. Battlefield 2 Server installation (registry)
4. Battlefield 2 Client installation (registry)

In addition to searching for the BF2 path, the launcher will query the python version from dice_py.dll and will then inject a dll using the following name schema:\
bf2py{PY_VERSION}-debug.dll\
A explicit -inject of a dll that has this name-schema will disable this behavior.

# development
Use ./configure to initialize this project. It searches for the Battlefield 2 directory using the registry and the default installation paths and then extracts the python version of the dice-py.dll.\
The heads for the detected python version will be automatically downloaded (vanilla bf2 uses python v. 2.3.4).\
Note: You can to this process manually by editing the bf2.props file and installing the headers manually to python-x.x.x/

# testing
Use the debug-test executable to launch a bf2 (startup) simulation.
Here you can debug the bf2py-debug.dll which is not possible after it is injected into the bf2 process.

# TODOs
- Exception debugging: the bf2py-debugger is based on python's bdb which didn't support "Break on every exception" or "Break on unhandled exception".\
So currently exception breakpoints aren't yet fully supported
- Hot reloading: reload(<module>) exists, and you could use this within the debugger

# FAQ
**Q**: How to add +mapList and other options to the startup?\
**A**: Add them to debug-test.exe or launch.exe. All command line arguments will be forwarded to the bf2 executable

**Q**: Is the debugger compatible with my custom compiled dice-py.dll to run python 2.x.x?\
**A**: I've tested a custom built (see my other repository) with the latest python2 (2.7.18) and a dll for this version is contained in the release binaries.
