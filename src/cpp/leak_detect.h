#ifndef __LEAK_DETECT_H__
#define __LEAK_DETECT_H__

#include "data_struct.h"


LeakingNodes* searchObjectsForLeaks(LeakCheckData* data);
void findLeaksInTaggedObjects();
void tagAllMapsAndCollections();
void selfCheck();

#endif
