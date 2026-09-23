# Plan: Full XDG Standardization Across All MeshMon State Files

## Overview
Complete full XDG path standardization across all four `meshmon` runtime state files, strictly eliminating legacy path fallbacks (`~/.meshmon*`) across the entire codebase:

1. **`meshmon.cfg`**: Config file strictly located at `~/.config/meshmon/meshmon.cfg`. Remove hardcoded `database.path` from the configuration file.
2. **`meshmon.db`**: Database file strictly located at `~/.config/meshmon/meshmon.db`.
3. **`meshmon.calib`**: Calibration curves strictly located at `~/.config/meshmon/meshmon.calib`.
4. **`meshmon.sched`**: Chatbot cron schedules strictly located at `~/.config/meshmon/meshmon.sched`.

---

## User Review Required

> [!IMPORTANT]
> - All four subsystems (`meshmon.cxx`, `Calibration.cxx`, `ChatBot.cxx`, and `MeshMonShell.cxx`) will be unified to strictly use standard XDG paths (`$XDG_CONFIG_HOME/meshmon/...` or `~/.config/meshmon/...`).
> - All legacy fallbacks (`~/.meshmon`, `~/.meshmon.db`, `~/.meshmon.calib`, `~/.meshmon.sched`) are completely removed.
> - `~/.config/meshmon/meshmon.cfg`: `path` under `database` is removed.
> - Compilation will be performed natively on `fox` (`aarch64`) per project guidelines.
> - Service restart inside GNU `screen` on `fox` will be executed upon approval.

---

## Proposed Changes

### 1. Calibration (`Calibration.cxx`)

#### [MODIFY] [Calibration.cxx](file:///home/samurai/work/meshmon/Calibration.cxx)
- Update `resolveConfigPath(const string &path, const string &storedPath)`:
  Remove legacy `~/.meshmon.calib` checks and resolve strictly to XDG path.

```cpp
static string resolveConfigPath(const string &path, const string &storedPath)
{
    if (!path.empty()) {
        return path;
    }
    if (!storedPath.empty()) {
        return storedPath;
    }

    const char *xdg = getenv("XDG_CONFIG_HOME");
    if ((xdg != NULL) && (xdg[0] != '\0')) {
        return string(xdg) + "/meshmon/meshmon.calib";
    }

    const char *homedir = getenv("HOME");
    if ((homedir != NULL) && (homedir[0] != '\0')) {
        return string(homedir) + "/.config/meshmon/meshmon.calib";
    }

    struct passwd *pw = getpwuid(getuid());
    if ((pw != NULL) && (pw->pw_dir != NULL) && (pw->pw_dir[0] != '\0')) {
        return string(pw->pw_dir) + "/.config/meshmon/meshmon.calib";
    }

    return "/tmp/meshmon/meshmon.calib";
}
```

---

### 2. ChatBot Scheduler (`ChatBot.cxx`)

#### [MODIFY] [ChatBot.cxx](file:///home/samurai/work/meshmon/ChatBot.cxx)
- Update `resolveConfigPath(const string &path, const string &storedPath)`:
  Resolve strictly to standard XDG `meshmon.sched` without legacy fallbacks.

```cpp
static string resolveConfigPath(const string &path, const string &storedPath)
{
    if (!path.empty()) {
        return path;
    }
    if (!storedPath.empty()) {
        return storedPath;
    }

    const char *xdg = getenv("XDG_CONFIG_HOME");
    if ((xdg != NULL) && (xdg[0] != '\0')) {
        return string(xdg) + "/meshmon/meshmon.sched";
    }

    const char *homedir = getenv("HOME");
    if ((homedir != NULL) && (homedir[0] != '\0')) {
        return string(homedir) + "/.config/meshmon/meshmon.sched";
    }

    struct passwd *pw = getpwuid(getuid());
    if ((pw != NULL) && (pw->pw_dir != NULL) && (pw->pw_dir[0] != '\0')) {
        return string(pw->pw_dir) + "/.config/meshmon/meshmon.sched";
    }

    return "/tmp/meshmon/meshmon.sched";
}
```

---

### 3. Core Daemon (`meshmon.cxx`)

#### [MODIFY] [meshmon.cxx](file:///home/samurai/work/meshmon/meshmon.cxx)
- **`loadLibConfig()`**: Streamline to resolve strictly to `cfgDir + "/meshmon.cfg"`, removing legacy `~/.meshmon` check.
- **`getDefaultDbPath()`**: Streamline to return `cfgDir + "/meshmon.db"` directly, removing legacy `~/.meshmon.db` check.

```cpp
static void loadLibConfig(Config &cfg, string &path)
{
    int fd;

    if (path.empty()) {
        string cfgDir = getConfigDir();
        ensureConfigDirExists(cfgDir);
        path = cfgDir + "/meshmon.cfg";
    }
...
```

```cpp
static string getDefaultDbPath(void)
{
    string cfgDir = getConfigDir();
    ensureConfigDirExists(cfgDir);
    return cfgDir + "/meshmon.db";
}
```

---

### 4. Interactive Shell (`MeshMonShell.cxx`)

#### [MODIFY] [MeshMonShell.cxx](file:///home/samurai/work/meshmon/MeshMonShell.cxx)
- Update help text lines 942 and 1109 from `~/.meshmon.calib` to `~/.config/meshmon/meshmon.calib`.

---

### 5. Configuration File (`~/.config/meshmon/meshmon.cfg`)

#### [MODIFY] `~/.config/meshmon/meshmon.cfg`
- Remove `path` from the `database` section:
  ```libconfig
  database : 
  {
    enabled = true;
    retention_days = 30;
  };
  ```

---

## Verification Plan

### Automated & Compilation Tests
- **Native compilation on `fox` (`aarch64`)**:
  ```bash
  ssh -n fox "cd ~/work/meshmon && make -j$(nproc)"
  ```
- **Verify binary version and build date**:
  ```bash
  ssh -n fox "~/work/meshmon/build/aarch64/meshmon -v"
  ```
- **Verify all state paths**:
  Confirm clean resolution of `meshmon.cfg`, `meshmon.db`, `meshmon.calib`, and `meshmon.sched` under `~/.config/meshmon/`.

### Service Redeployment
- Cleanly launch `meshmon` inside the `meshmon` screen session on `fox`:
  ```bash
  ssh -n fox "screen -S meshmon -X stuff 'cd ~/work/meshmon && ./build/aarch64/meshmon\n'"
  ```
- Verify TCP connectivity to network shell (`16876`), web dashboard (`16880`), and AIMON gateway link (`builder:3885`).
