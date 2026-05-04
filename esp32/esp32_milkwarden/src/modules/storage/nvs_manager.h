#pragma once

// Loads settings from NVS (Preferences) into calibration vars in loadcell/heartbeat modules.
void loadSettings();

// Persists current calibration vars to NVS.
void saveSettings();

// Optional: reads /config.ini from LittleFS and overrides NVS values.
// Falls back silently if filesystem is not mounted or file is missing.
void loadConfigFile();
