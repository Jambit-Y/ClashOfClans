#ifndef __BUILDING_CONTROLLER_H__
#define __BUILDING_CONTROLLER_H__

#include "cocos2d.h"
#include "../Model/Building.h"

// 前向声明
class BuildingSprite;
class VillageLayer;

class BuildingController : public cocos2d::Ref {
public:
    static BuildingController* create();
    
    bool init();
    
    // 设置关联的Layer（View管理者）
    void setVillageLayer(VillageLayer* layer) { _villageLayer = layer; }
    
    // 核心交互方法 - 返回bool表示是否捕获了触摸
    bool handleTouchBegan(const cocos2d::Vec2& layerPos);
    void handleTouchMoved(const cocos2d::Vec2& layerPos);
    void handleTouchEnded(const cocos2d::Vec2& layerPos);
    
    // 建筑管理
    void addBuilding(Building* buildingData);
    void removeBuilding(int buildingId);
    
    // 选中建筑管理
    BuildingSprite* getSelectedBuilding() { return _selectedBuilding; }
    
private:
    VillageLayer* _villageLayer;
    BuildingSprite* _selectedBuilding;
    cocos2d::Vec2 _touchStartPos;
    cocos2d::Vec2 _buildingStartPos;
    
    // 辅助方法
    BuildingSprite* findBuildingAtPosition(const cocos2d::Vec2& layerPos);
    void updateBuildingPosition(BuildingSprite* building, const cocos2d::Vec2& layerPos);
    void snapBuildingToGrid(BuildingSprite* building);
    bool isValidPlacement(Building* building, int gridX, int gridY);
};

#endif // __BUILDING_CONTROLLER_H__