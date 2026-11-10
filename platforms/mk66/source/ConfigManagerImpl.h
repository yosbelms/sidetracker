#ifndef SOURCE_CONFIGMANAGERIMPL_H_
#define SOURCE_CONFIGMANAGERIMPL_H_

#include "ConfigManager.h"

class ConfigManagerImpl : public ConfigManager
{
  public:
    virtual bool readData(int address, void* data, int size) override;
    virtual bool writeConfig(void* data, int size) override;

    void init();
};

#endif /* SOURCE_CONFIGMANAGERIMPL_H_ */
