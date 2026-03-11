// Zia Language Extension for VS Code
//
// Launches zia-server as an LSP language server for .zia files.
// Provides diagnostics, completions, hover, and document symbols.

const path = require("path");
const fs = require("fs");
const vscode = require("vscode");
const { LanguageClient } = require("vscode-languageclient/node");

let client;
let outputChannel;

/**
 * Search for the zia-server binary by walking up from a starting directory,
 * checking build/src/tools/zia-server/zia-server at each level.
 */
function findServerBinary(startDir) {
  let dir = startDir;
  const root = path.parse(dir).root;
  while (dir !== root) {
    const candidate = path.join(
      dir,
      "build",
      "src",
      "tools",
      "zia-server",
      "zia-server"
    );
    if (fs.existsSync(candidate)) {
      return candidate;
    }
    dir = path.dirname(dir);
  }
  return null;
}

async function activate(context) {
  outputChannel = vscode.window.createOutputChannel("Zia Language Server");
  context.subscriptions.push(outputChannel);

  const config = vscode.workspace.getConfiguration("zia");
  let serverPath = config.get("server.path", "");
  const serverArgs = config.get("server.args", ["--lsp"]);

  if (!serverPath) {
    // Strategy 1: walk up from each workspace folder
    const workspaceFolders = vscode.workspace.workspaceFolders;
    if (workspaceFolders) {
      for (const folder of workspaceFolders) {
        const found = findServerBinary(folder.uri.fsPath);
        if (found) {
          serverPath = found;
          break;
        }
      }
    }

    // Strategy 2: walk up from the active editor's file
    if (!serverPath && vscode.window.activeTextEditor) {
      const fileDir = path.dirname(
        vscode.window.activeTextEditor.document.uri.fsPath
      );
      serverPath = findServerBinary(fileDir) || "";
    }

    // Strategy 3: fall back to PATH
    if (!serverPath) {
      serverPath = "zia-server";
    }
  }

  // Verify the binary exists (unless it's a bare name for PATH lookup)
  if (serverPath !== "zia-server" && !fs.existsSync(serverPath)) {
    const msg = `Zia Language Server binary not found at: ${serverPath}. Set "zia.server.path" in settings.`;
    outputChannel.appendLine(msg);
    vscode.window.showWarningMessage(msg);
    return;
  }

  outputChannel.appendLine(`Starting zia-server: ${serverPath} ${serverArgs.join(" ")}`);

  const serverOptions = {
    command: serverPath,
    args: serverArgs,
    // Do not set transport — vscode-languageclient defaults to stdio.
    // Setting TransportKind.stdio explicitly causes "--stdio" to be appended
    // to args, which zia-server does not recognize.
  };

  const clientOptions = {
    documentSelector: [{ scheme: "file", language: "zia" }],
    outputChannel: outputChannel,
  };

  client = new LanguageClient(
    "zia",
    "Zia Language Server",
    serverOptions,
    clientOptions
  );

  try {
    await client.start();
    outputChannel.appendLine("Zia Language Server started successfully.");
  } catch (err) {
    outputChannel.appendLine(`Failed to start Zia Language Server: ${err}`);
    vscode.window.showErrorMessage(
      `Zia Language Server failed to start: ${err.message || err}`
    );
    client = null;
  }
}

async function deactivate() {
  if (client) {
    return client.stop();
  }
}

module.exports = { activate, deactivate };
