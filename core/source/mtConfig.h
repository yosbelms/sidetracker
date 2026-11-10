#ifndef SOURCE_MTCONFIG_H_
#define SOURCE_MTCONFIG_H_

#include <stdint.h>

class Platform;

void initConfig(Platform* platform);
void updateConfig(bool force);

void forceSaveConfig();
void saveConfig();
void saveConfigAsap();
void readConfig();

uint8_t getPerformanceMix();
void setPerformanceMix(uint8_t mix);
void loadPerformanceMix();

void resetConfig();
void checkConfig();
void recoverConfigFromOldVerison();

#endif /* SOURCE_MTCONFIG_H_ */
