#include "BuildingController.h"
#include "../Layer/VillageLayer.h"
#include "../View/BuildingSprite.h"
#include "../System/GridMap.h"

USING_NS_CC;

BuildingController* BuildingController::create() {
    BuildingController* controller = new BuildingController();
    if (controller && controller->init()) {
        controller->autorelease();
        return controller;
    }
    CC_SAFE_DELETE(controller);
    return nullptr;
}

bool BuildingController::init() {
    _villageLayer = nullptr;
    _selectedBuilding = nullptr;
    return true;
}

bool BuildingController::handleTouchBegan(const Vec2& layerPos) {
    _touchStartPos = layerPos;
    
    // 查找触摸位置的建筑
    _selectedBuilding = findBuildingAtPosition(layerPos);
    
    if (_selectedBuilding) {
        _buildingStartPos = _selectedBuilding->getPosition();
        // 设置建筑状态为MOVING
        if (_selectedBuilding->getBuildingData()) {
            _selectedBuilding->getBuildingData()->setState(Building::State::MOVING);
        }
        CCLOG("BuildingController: Selected building ID %d", 
              _selectedBuilding->getBuildingData()->getId());
        return true; // 捕获了触摸
    }
    
    return false; // 没有捕获触摸
}

void BuildingController::handleTouchMoved(const Vec2& layerPos) {
    if (_selectedBuilding) {
        updateBuildingPosition(_selectedBuilding, layerPos);
    }
}

void BuildingController::handleTouchEnded(const Vec2& layerPos) {
    if (_selectedBuilding) {
        snapBuildingToGrid(_selectedBuilding);
        
        // 设置建筑状态为已放置
        if (_selectedBuilding->getBuildingData()) {
            _selectedBuilding->getBuildingData()->setState(Building::State::PLACED);
        }
        
        _selectedBuilding = nullptr;
        CCLOG("BuildingController: Building placement completed");
    }
}

void BuildingController::addBuilding(Building* buildingData) {
    if (!buildingData || !_villageLayer) return;
    
    BuildingSprite* sprite = BuildingSprite::createWithBuilding(buildingData);
    _villageLayer->addChild(sprite);
    
    CCLOG("BuildingController: Added building ID %d at grid (%d,%d)", 
          buildingData->getId(), 
          buildingData->getGridX(), 
          buildingData->getGridY());
}

BuildingSprite* BuildingController::findBuildingAtPosition(const Vec2& layerPos) {
    if (!_villageLayer) return nullptr;
    
    // layerPos已经是Layer的局部坐标，不需要再转换
    for (auto& child : _villageLayer->getChildren()) {
        BuildingSprite* building = dynamic_cast<BuildingSprite*>(child);
        if (building && building->getBoundingBox().containsPoint(layerPos)) {
            return building;
        }
    }
    return nullptr;
}

void BuildingController::updateBuildingPosition(BuildingSprite* building, const Vec2& layerPos) {
    if (!building) return;
    
    Vec2 diff = layerPos - _touchStartPos;
    Vec2 newPos = _buildingStartPos + diff;
    building->setPosition(newPos);
}

void BuildingController::snapBuildingToGrid(BuildingSprite* building) {
    if (!building) return;
    
    // 使用GridMap系统进行坐标转换
    Vec2 currentPos = building->getPosition();
    Vec2 gridPos = GridMap::pixelToGrid(currentPos);
    
    // 检查放置合法性
    Building* buildingData = building->getBuildingData();
    if (buildingData && isValidPlacement(buildingData, (int)gridPos.x, (int)gridPos.y)) {
        // 合法放置：更新模型数据并对齐网格
        buildingData->setGridPosition((int)gridPos.x, (int)gridPos.y);
        Vec2 snapPos = GridMap::gridToPixel((int)gridPos.x, (int)gridPos.y);
        building->setPosition(snapPos);
        
        CCLOG("BuildingController: Building snapped to grid (%d,%d)", 
              (int)gridPos.x, (int)gridPos.y);
    } else {
        // 非法放置：恢复原位置
        building->setPosition(_buildingStartPos);
        if (buildingData) {
            Vec2 originalGrid = GridMap::pixelToGrid(_buildingStartPos);
            buildingData->setGridPosition((int)originalGrid.x, (int)originalGrid.y);
        }
        
        CCLOG("BuildingController: Invalid placement, building restored to original position");
    }
}

bool BuildingController::isValidPlacement(Building* building, int gridX, int gridY) {
    if (!building) return false;
    
    // 这里可以添加各种合法性检查：
    // 1. 边界检查
    if (gridX < 0 || gridY < 0 || 
        gridX >= GridMap::MAP_WIDTH || gridY >= GridMap::MAP_HEIGHT) {
        return false;
    }
    
    // 2. 冲突检查（与其他建筑重叠）
    // TODO: 需要遍历所有建筑检查位置冲突
    
    // 3. 特殊规则检查（比如某些建筑不能放在特定区域）
    // TODO: 根据建筑类型添加特殊规则
    
    return true; // 临时返回true，实际项目中需要完善检查逻辑
}