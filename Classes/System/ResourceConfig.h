#ifndef __RESOURCE_CONFIG_H__
#define __RESOURCE_CONFIG_H__

#include <string>
#include "../Model/Building.h"

class ResourceConfig {
public:
    // 根据建筑类型获取图片路径
    static std::string getBuildingImagePath(Building::Type buildingType);
    
private:
    // 只保留加农炮
    static const std::string CANNON_IMAGE;
};

#endif // __RESOURCE_CONFIG_H__