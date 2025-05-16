import * as vscode from 'vscode';
import { promisified as regedit } from 'regedit';
import { spawn } from 'child_process';

export function activate(context: vscode.ExtensionContext) {

	context.subscriptions.push(vscode.commands.registerCommand('extension.bf2py-debug.getBF2Directory', async config => {
		let bf2Dir = "C:\\Program Files (x86)\\EA Games\\Battlefield 2 Server";
		if (process.platform === 'win32') {
			const paths = {
				'HKEY_LOCAL_MACHINE\\SOFTWARE\\EA GAMES\\Battlefield 2 Server': 'GAMEDIR',
				'HKEY_LOCAL_MACHINE\\SOFTWARE\\Electronic Arts\\EA Games\\Battlefield 2': 'InstallDir'
			};

			const values = await regedit.arch.list32(Object.keys(paths));
			for (const [key, item] of Object.entries(values)) {
				if (item.exists) {
					bf2Dir = item.values[paths[key]].value as string;
					break;
				}
			}
		} else {
			bf2Dir = "/home/bf2server"
		}

		return vscode.window.showInputBox({
			placeHolder: "Please enter the Battlefield 2 (Server) directory",
			value: bf2Dir
		});
	}));

	context.subscriptions.push(vscode.debug.registerDebugConfigurationProvider('mock', new BF2PyConfigurationProvider()));
	context.subscriptions.push(vscode.debug.registerDebugAdapterDescriptorFactory('bf2py', new BF2DebugAdapterServerDescriptorFactory()));
}

export function deactivate() {
	// nothing to do
}

class BF2PyConfigurationProvider implements vscode.DebugConfigurationProvider {
	async resolveDebugConfiguration(folder: vscode.WorkspaceFolder | undefined, config: vscode.DebugConfiguration, token?: vscode.CancellationToken): vscode.ProviderResult<vscode.DebugConfiguration> {
		
		if (config.request == "launch" && !config.bf2dir) {
			await vscode.window.showInformationMessage("Cannot Launch BF2");
			return;
		} else if (config.request == "attach" && !config.debugServer) {
			await vscode.window.showInformationMessage("Cannot Attach to BF2");
			return; 
		}

		return config;
	}
}

class BF2DebugAdapterServerDescriptorFactory implements vscode.DebugAdapterDescriptorFactory {
	createDebugAdapterDescriptor(session: vscode.DebugSession, executable: vscode.DebugAdapterExecutable | undefined): vscode.ProviderResult<vscode.DebugAdapterDescriptor> {
		if (session.configuration.request == "attach") {
			return new vscode.DebugAdapterServer(session.configuration.debugPort);
		}
		else if (session.configuration.request == "launch") {
			const subprocess = spawn('with_dll', ['bf2_w32ded', ...session.configuration.bf2args, '+debugPython'], {
				cwd: session.configuration.bf2dir,
				detached: true,
				stdio: 'ignore',
			});
		}

		return new vscode.DebugAdapterServer(4711);
	}
}