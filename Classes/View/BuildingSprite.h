#ifndef __BUILDING_SPRITE_H__
#define __BUILDING_SPRITE_H__

#include "cocos2d.h"
#include "../Model/Building.h" // 引用 Model

class BuildingSprite : public cocos2d::Sprite {
public:
  // 静态创建方法
  static BuildingSprite* createWithBuilding(Building* building);

  // 初始化
  bool initWithBuilding(Building* building);

  // 获取绑定的建筑模型
  Building* getBuildingData() { return _buildingData; }

  // 根据网格模型，更新 Sprite 显示在屏幕上的位置
  void updatePositionFromGrid();

private:
  Building* _buildingData; // 绑定的建筑数据
};

#endif // __BUILDING_SPRITE_H__