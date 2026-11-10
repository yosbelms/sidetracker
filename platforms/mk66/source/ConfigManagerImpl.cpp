#include "ConfigManagerImpl.h"
#include "EEPROM.h"
#include "config.h"

void ConfigManagerImpl::init()
{
    EEPROM.begin();
}

bool ConfigManagerImpl::readData(int address, void* data, int size)
{
    EEPROM.readData(address + CONFIG_EEPROM_ADRESS, data, size);
    return true;
}

bool ConfigManagerImpl::writeConfig(void* data, int size)
{
    EEPROM.writeData(CONFIG_EEPROM_ADRESS, data, size);
    return true;
}
