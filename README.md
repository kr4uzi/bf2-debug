# bf2-debug
This project enables proper debugging for the embedded python used in Battlefield 2.

# installation
Copy the following files and folders to your Battlefield 2 directory:
- bf2py-debugger.dll
- debug-test.exe
- vscode-ext/
- launch-vscode.bat

Use `debug-test.exe -inject=bf2py-debugger.dll -mode=bf2 +pyDebugStopOnEntry=1` to launch bf2.
Then open the VSCode extension using `launch-vscode.bat` to open the debugger GUI.

The `+pyDebugStopOnEntry=1` option will make the Battlefield 2 startup wait until a debugger has attached.

# development
Use ./configure to initialize this project. It searches for the Battlefield 2 directory using the registry and the default installation paths and then extracts the python version of the dice-py.dll.

The project ships with header files for Python 2.3.4 (default for bf2), but if a different version is detected, the header files are automatically downloaded.

You can to this process manually by editing the bf2.props file and installing the headers manually to python-x.x.x/

When debugging or running the bf2-debug.sln, you will be asked how to run the bf2py-debugger:
- dry: Simulate the bf2 startup sequence and mock a host-module
- bf2: Launch the bf2 dedicated server

# TODOs
- Exception debugging: the bf2py-debugger is based on python's bdb which didn't support "Break on every exception" or "Break on unhandled exception". So currently exception breakpoints aren't yet fully supported

# FAQ
Q: How to add +mapList and other options to the startup?
A: Add them to debug-test.exe. Anything that is not -inject= or -mode= will be forwarded to the bf2 executable

Q: Is the debugger compatible with my custom compiled dice-py.dll to run python 2.7.x?
A: Maybe, I dont know. I've tested it with the default python version (2.3.4), but ./configure will automatically initialize the project properly. It might fail to compile, but this is caused by API changes introduced by newer python versions. Your welcomed to add `#if PY_VERSION >`-guards to the source code and submit a merge request.

Q: Im getting a dice_py.dll not found error
A: Please follow the installation process - the run-files need to be in the same directory as the Battlefield 2 executable