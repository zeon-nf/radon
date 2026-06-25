```text
          _______  _______  ______   _______  _       
         (  ____ )(  ___  )(  __  \ (  ___  )( (    /|
         | (    )|| (   ) || (  \  )| (   ) ||  \  ( |
         | (____)|| (___) || |   ) || |   | ||   \ | |
         |     __)|  ___  || |   | || |   | || (\ \) |
         | (\ (   | (   ) || |   ) || |   | || | \   |
         | ) \ \__| )   ( || (__/  )| (___) || )  \  |
         |/   \__/|/     \|(______/ (_______)|/    )_)
                                                      
```
# Radon: Snapshot Manager Graphical Frontend

Radon is a lightweight GTK4 and Libadwaita desktop application developed by **Zeon** that serves as a graphical frontend for the `nsm` (Network Snapshot Manager / System Snapshot Utility) command-line tool. It simplifies system snapshot administration on Linux systems by providing a clean user interface to create, view, roll back, and delete manual root snapshots.

## Key Capabilities

* **Manual Snapshot Creation:** Safely trigger root-level manual system snapshots with custom names directly from the UI.
* **Snapshot Lifecycle Management:** Instantly roll back to previous stable points or delete stale manual snapshots.
* **Libadwaita Integration:** Adheres to modern GNOME design patterns with automatic layout adjustment, system toasts, and native password dialogs.
* **Security Context Handling:** Prompts for administrator access natively to execute backend system operations securely through `sudo`.

---

## Technical Specifications & Architecture

Radon is designed to wrap around an existing system execution binary (`/usr/bin/nsm`). It interfaces through standard process isolation mechanisms:

```
+---------------+      IPC (popen)      +-------------------+
|  Radon (GUI)  |  ------------------>  | sudo -S /usr/bin/nsm |
+---------------+                       +-------------------+
        ^                                         |
        |                                         v
        +----------- Reads Output ----------------+

```

### Constraints

* **Scope:** Radon specifically processes and handles **Manual Snapshots** under the `[Root]` context block returned by `nsm`.
* **Reserved Names:** Names beginning with `apt` or matching `nsmd` are explicitly prohibited during creation to maintain system reliability and prevent overlaps with background automation.

---

## Installation & Compiling

### 1. Install System Dependencies

Ensure your target machine contains the development packages for GTK4 and Libadwaita. Run the following compilation prerequisite commands:

```bash
sudo napt sync
sudo napt install libgtk-4-dev libadwaita-1-dev gcc make

```

### 2. Build from Source

Compile the source codebase utilizing standard `gcc` arguments to link the required libraries:

```bash
gcc $(pkg-config --cflags gtk4 libadwaita-1) -o radon main.c $(pkg-config --libs gtk4 libadwaita-1)

```

### 3. Deploying Assets

To ensure system menus display application branding correctly, relocate the app asset icon to either a local user path or system folder:

```bash
# Option A: Local Deployment
mkdir -p ~/.local/share/icons/
cp radon.png ~/.local/share/icons/

# Option B: Global Deployment
sudo cp radon.png /usr/share/radon.png

```

---

## Licensing

This software is distributed under the terms of the **TALv1 (The Arvor Linux)** License Agreement.

```
Copyright (c) 2026 Zeon. All rights reserved.
Distributed under the terms of the TALv1 License.

```
