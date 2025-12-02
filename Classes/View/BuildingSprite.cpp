#include "BuildingSprite.h"
#include "../System/GridMap.h"
#include "../System/ResourceConfig.h"

USING_NS_CC;

BuildingSprite* BuildingSprite::createWithBuilding(Building* building) {
  BuildingSprite* sprite = new (std::nothrow) BuildingSprite();
  if (sprite && sprite->initWithBuilding(building)) {
    sprite->autorelease();
    return sprite;
  }
  CC_SAFE_DELETE(sprite);
  return nullptr;
}

bool BuildingSprite::initWithBuilding(Building* building) {
  if (!building) {
    return false;
  }

  _buildingData = building;

  // 获取图片路径
  std::string imagePath = ResourceConfig::getBuildingImagePath(_buildingData->getType());
  
  if (!imagePath.empty()) {
    // 尝试加载图片
    if (!Sprite::initWithFile(imagePath)) {
      // 如果加载失败，使用占位符
      if (!Sprite::init()) {
        return false;
      }
      this->setTextureRect(Rect(0, 0, 60, 60));
      this->setColor(Color3B(100, 200, 100)); // 绿色占位
    }
  } else {
    // 没有图片路径，使用占位符
    if (!Sprite::init()) {
      return false;
    }
    this->setTextureRect(Rect(0, 0, 60, 60));
    this->setColor(Color3B(100, 200, 100)); // 绿色占位
  }

  // 设置锚点为中心
  this->setAnchorPoint(Vec2::ANCHOR_MIDDLE);

  // 根据网格数据设置初始位置
  updatePositionFromGrid();

  return true;
}

void BuildingSprite::updatePositionFromGrid() {
  if (!_buildingData) return;

  // 使用 GridMap 将网格坐标转换为屏幕坐标
  Vec2 screenPos = GridMap::gridToPixel(
    _buildingData->getGridX(),
    _buildingData->getGridY()
  );

  this->setPosition(screenPos);
}