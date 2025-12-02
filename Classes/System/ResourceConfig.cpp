#include "ResourceConfig.h"

// 使用你确认有效的加农炮路径
const std::string ResourceConfig::CANNON_IMAGE = "../Resources/buildings/defence architecture/cannon/Cannon1.webp";

std::string ResourceConfig::getBuildingImagePath(Building::Type buildingType) {
    if (buildingType == Building::Type::CANNON) {
        return CANNON_IMAGE;
    }
    
    // 其他建筑类型返回空字符串（使用占位符）
    return "";
}