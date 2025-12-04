#include "VillageLayer.h"
#include "../proj.win32/Constants.h"
#include <iostream>
USING_NS_CC;

// 静态常量定义
const float VillageLayer::MAX_SCALE = 3.0f;  // 最大放大倍率
const float VillageLayer::ZOOM_SPEED = 0.1f; // 缩放的速率

bool VillageLayer::init() {
  if (!Layer::init()) {
    return false;
  }

  // 1. 创建地图精灵
  _mapSprite = createMapSprite();
  if (!_mapSprite) {
    return false;
  }
  this->addChild(_mapSprite);

  // 2. 初始化位置
  initializeBasicProperties();

  // 3. 设置事件处理
  setupEventHandling();

  return true;
}

#pragma region 初始化方法
void VillageLayer::calculateMinScale() {
  auto visibleSize = Director::getInstance()->getVisibleSize();
  auto mapSize = this->getContentSize();
  
  // 计算使地图完全覆盖屏幕所需的最小缩放值
  // 取宽度和高度缩放比的最大值，确保地图的两个方向都能覆盖屏幕
  float scaleX = visibleSize.width / mapSize.width;
  float scaleY = visibleSize.height / mapSize.height;
  
  _minScale = std::max(scaleX, scaleY);
  
  CCLOG("Screen: %.0fx%.0f, Map: %.0fx%.0f", 
        visibleSize.width, visibleSize.height,
        mapSize.width, mapSize.height);
  CCLOG("Scale ratios - X: %.3f, Y: %.3f", scaleX, scaleY);
  CCLOG("Calculated MIN_SCALE: %.3f (地图始终覆盖屏幕)", _minScale);
}

void VillageLayer::initializeBasicProperties() {
	auto mapSize = _mapSprite->getContentSize();
  this->setContentSize(mapSize);
  this->setAnchorPoint(Vec2::ANCHOR_MIDDLE);
  _mapSprite->setPosition(0, 0);
  Vec2 initialPos = Director::getInstance()->getVisibleSize() / 2;
  //initialPos = clampMapPosition(initialPos);
  this->setPosition(initialPos);

  calculateMinScale();
  CCLOG("VillageLayer initialized:");
  CCLOG("  Scale: %.3f (range: %.3f - %.3f)", _currentScale, _minScale, MAX_SCALE);
  CCLOG("  Layer position: (%.2f, %.2f)", this->getPosition().x, this->getPosition().y);
}

void VillageLayer::setupEventHandling() {
  setupTouchHandling(); // 拖动
  setupMouseHandling(); // 缩放
}
#pragma endregion

#pragma region 辅助方法
Sprite* VillageLayer::createMapSprite() {
  auto mapSprite = Sprite::create("Scene/LinedVillageScene.jpg");
  if (!mapSprite) {
    CCLOG("Error: Failed to load map image");
    return nullptr;
  }
  
  mapSprite->setAnchorPoint(Vec2::ANCHOR_MIDDLE);
  return mapSprite;
}

Vec2 VillageLayer::calculateCenterPosition() {
  return Director::getInstance()->getVisibleSize() / 2.0f;
}
#pragma endregion

#pragma region 触摸事件处理（拖动）
void VillageLayer::setupTouchHandling() {
  auto listener = EventListenerTouchOneByOne::create();
  listener->setSwallowTouches(true);

  listener->onTouchBegan = CC_CALLBACK_2(VillageLayer::onTouchBegan, this);
  listener->onTouchMoved = CC_CALLBACK_2(VillageLayer::onTouchMoved, this);
  listener->onTouchEnded = CC_CALLBACK_2(VillageLayer::onTouchEnded, this);

  _eventDispatcher->addEventListenerWithSceneGraphPriority(listener, this);
}

bool VillageLayer::onTouchBegan(Touch* touch, Event* event) {
  storeTouchStartState(touch);
  return true;
}

void VillageLayer::onTouchMoved(Touch* touch, Event* event) {
  handleMapDragging(touch);
}

void VillageLayer::onTouchEnded(Touch* touch, Event* event) {
  // 拖动结束
}

void VillageLayer::storeTouchStartState(Touch* touch) {
  _touchStartPos = touch->getLocation();
  _layerStartPos = this->getPosition();
	CCLOG("Touch began at (%.2f, %.2f), Layer start pos (%.2f, %.2f)",
        _touchStartPos.x, _touchStartPos.y,
        _layerStartPos.x, _layerStartPos.y);
}

void VillageLayer::handleMapDragging(Touch* touch) {
  Vec2 currentTouchPos = touch->getLocation();
  Vec2 delta = currentTouchPos - _touchStartPos;
  Vec2 newPos = _layerStartPos + delta;

  newPos = clampMapPosition(newPos);
  this->setPosition(newPos);
}

Vec2 VillageLayer::clampMapPosition(const Vec2& position) {
  auto visibleSize = Director::getInstance()->getVisibleSize();
  auto layerSize = this->getContentSize() * this->getScale();

  float halfLayerWidth = layerSize.width / 2.0f;
  float halfLayerHeight = layerSize.height / 2.0f;
  
  float clampedX, clampedY;

  // ==================== X 轴约束 ====================
  // 由于 minScale 保证地图始终覆盖屏幕，这里的逻辑简化
  // 地图宽度总是 >= 屏幕宽度
  float maxX = halfLayerWidth;
  float minX = visibleSize.width - halfLayerWidth;
  clampedX = clampf(position.x, minX, maxX);

  // ==================== Y 轴约束 ====================
  // 地图高度总是 >= 屏幕高度
  float maxY = halfLayerHeight;
  float minY = visibleSize.height - halfLayerHeight;
  clampedY = clampf(position.y, minY, maxY);

  return Vec2(clampedX, clampedY);
}

void VillageLayer::drawAnchorMovementRange() {
  auto visibleSize = Director::getInstance()->getVisibleSize();
  auto layerSize = this->getContentSize() * this->getScale();

  float halfLayerWidth = layerSize.width / 2.0f;
  float halfLayerHeight = layerSize.height / 2.0f;

  float maxX = halfLayerWidth;
  float minX = visibleSize.width - halfLayerWidth;
  float maxY = halfLayerHeight;
  float minY = visibleSize.height - halfLayerHeight;

  // 绘制活动范围矩形
  auto drawNode = DrawNode::create();
  this->getParent()->addChild(drawNode, 999);

  Vec2 rect[5] = {
    Vec2(minX, minY),
    Vec2(maxX, minY),
    Vec2(maxX, maxY),
    Vec2(minX, maxY),
    Vec2(minX, minY)
  };

  drawNode->drawPoly(rect, 5, false, Color4F(1.0f, 0.0f, 0.0f, 1.0f));
  drawNode->drawSolidCircle(this->getPosition(), 5.0f, 0, 20,
                            Color4F(0.0f, 1.0f, 0.0f, 1.0f));

	CCLOG("Current Layer Position: (%.2f, %.2f)", this->getPosition().x, this->getPosition().y);
  CCLOG("Anchor range: [%.1f, %.1f] x [%.1f, %.1f]", minX, maxX, minY, maxY);
  CCLOG("Range size: %.1f x %.1f", maxX - minX, maxY - minY);
}

float VillageLayer::calculateNewScale(float scrollDelta) {
  // 基于滚轮增量的线性缩放：向上滚动放大，向下滚动缩小
  float newScale = _currentScale + scrollDelta * ZOOM_SPEED;

  // 使用动态计算的 _minScale（确保地图始终覆盖屏幕）与 MAX_SCALE 进行范围限制
  newScale = clampf(newScale, _minScale, MAX_SCALE);

  return newScale;
}
#pragma endregion

#pragma region 鼠标事件处理（缩放）
void VillageLayer::setupMouseHandling() {
  auto mouseListener = EventListenerMouse::create();
  mouseListener->onMouseScroll = CC_CALLBACK_1(VillageLayer::onMouseScroll, this);
  _eventDispatcher->addEventListenerWithSceneGraphPriority(mouseListener, this);
}

void VillageLayer::onMouseScroll(Event* event) {
  EventMouse* mouseEvent = static_cast<EventMouse*>(event);
  float scrollY = mouseEvent->getScrollY();

  float newScale = calculateNewScale(scrollY);
  if (std::abs(newScale - _currentScale) < 0.01f) {
    return;
  }

  // 简化：始终以锚点（Layer中心）为中心缩放
  applyZoomAtAnchor(newScale);
}

void VillageLayer::applyZoomAtAnchor(float newScale) {
  // 记录旧缩放
  const float oldScale = _currentScale;

  // 直接设置新缩放（锚点在中心，缩放围绕锚点进行）
  this->setScale(newScale);
  _currentScale = newScale;

  // 缩放后应用边界约束（保证地图不露白）
  Vec2 newPos = clampMapPosition(this->getPosition());
  this->setPosition(newPos);

  CCLOG("Zoom(center anchor): %.3f -> %.3f (range: %.3f - %.3f)",
        oldScale, newScale, _minScale, MAX_SCALE);

  #ifdef COCOS2D_DEBUG
  drawAnchorMovementRange();
  #endif
}
#pragma endregion