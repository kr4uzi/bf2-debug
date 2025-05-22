import * as vscode from 'vscode';
import * as child_process from 'child_process';
import * as path from 'path';

export function activate(context: vscode.ExtensionContext) {
	vscode.debug.onDidReceiveDebugSessionCustomEvent(event => {
		if (event.event == "bf2py" && event.body.type == "modpath") {
			// replace or add the mods/mod/python folder
			const folders = vscode.workspace.workspaceFolders;

			const [base, mod]: string[] = event.body.data.toLowerCase().split(";");
			let updated = false;
			if (!folders?.find(f => f.uri.fsPath.toLowerCase().startsWith(base))) {
				vscode.workspace.updateWorkspaceFolders(0, undefined, {
					uri: vscode.Uri.joinPath(vscode.Uri.file(base), "python"),
					name: "bf2/python"
				}, {
					uri: vscode.Uri.joinPath(vscode.Uri.file(base), "admin"),
					name: "admin"
				});

				updated = true;
			}

			const modUri = vscode.Uri.joinPath(vscode.Uri.file(base), mod);
			const modIndex = folders?.findIndex(f => f.uri == modUri) || -1;
			updated = updated || modIndex != -1;
			vscode.workspace.updateWorkspaceFolders(modIndex != -1 ? modIndex : 0, modIndex != -1 ? 1 : 0, {
				uri: modUri,
				name: mod
			});
		}
	});

	vscode.debug.registerDebugAdapterDescriptorFactory('bf2py', new BF2Py());
}

export function deactivate() {
	// nothing to do
}

class BF2Py implements vscode.DebugAdapterDescriptorFactory
{
	bf2Path: string = "";

	getBF2PathFromRegistry(): string {
		const paths = {
			"HKLM\\SOFTWARE\\EA GAMES\\Battlefield 2 Server": "GAMEDIR",
			"HKLM\\SOFTWARE\\Electronic Arts\\EA Games\\Battlefield 2": "InstallDir"
		};

		for (const [path, key] of Object.entries(paths)) {
			try {
				const out = child_process.execSync(`reg query "${path}" /reg:32 /v "${key}"`, { stdio: ['ignore', 'pipe', 'ignore'] });
				const matches = out.toString().match(/REG_SZ\s+(.+)$/m);
				if (matches) {
					return matches[1];
				}
			} catch (e) {

			}
		}

		return "";
	}

	async resolveBF2Path() {
		let bf2Path = '';
		if (process.platform == 'win32') {
			bf2Path = this.getBF2PathFromRegistry() || "C:\\Program Files (x86)\\EA Games\\Battlefield 2 Server";
		} else {
			bf2Path = "/home/bf2server";
		}

		this.bf2Path = await vscode.window.showInputBox({
			placeHolder: "Please enter the Battlefield 2 (Server) directory",
			value: bf2Path
		}) || "";
		return this.bf2Path;
	}

	createDebugAdapterDescriptor(session: vscode.DebugSession): vscode.ProviderResult<vscode.DebugAdapterDescriptor> {
		const dapPort = 19021;
		session.configuration.dapPort ??= dapPort;

		const request = session.configuration.request;
		if (request == "attach") {
			return new vscode.DebugAdapterServer(session.configuration.dapPort);
		} else if (request == "launch") {
			return this.resolveBF2Path().then((bf2Path) => {
				const subprocess = child_process.spawnSync(path.join("..", "debug-launcher"), session.configuration.bf2args);
				if (subprocess.pid) {
					return new vscode.DebugAdapterServer(session.configuration.dapPort);
				} else {
					vscode.window.showErrorMessage("Failed to launch debug-launcher: " + request);
				}
			});
		}
			
		vscode.window.showErrorMessage("Invalid bf2py request: " + request);
		return null;
	}
}
