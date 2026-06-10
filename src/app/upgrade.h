#pragma once

enum class DefaultPreferencesMode
{
    Legacy,
    Current
};

void handleChangedDefaults(DefaultPreferencesMode mode);
bool upgrade();
void setCurrentMigrationVersion();
