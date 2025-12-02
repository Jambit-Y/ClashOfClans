#ifndef __VILLAGELAYER_H__
#define __VILLAGELAYER_H__

#include "cocos2d.h"

// 前向声明
class BuildingSprite;
class BuildingController;
class Building;

class VillageLayer : public cocos2d::Layer {
public:
    virtual bool init();
    virtual ~VillageLayer(); // 添加析构函数
    CREATE_FUNC(VillageLayer);

private:
    // 常量定义
    static const int MAP_DEFAULT_WIDTH = 2000;
    static const int MAP_DEFAULT_HEIGHT = 2000;
    static const float MIN_SCALE;
    static const float MAX_SCALE;
    static const float ZOOM_SPEED;

    // 成员变量
    BuildingController* _buildingController;
    cocos2d::Vec2 _touchStartPos;
    cocos2d::Vec2 _layerStartPos;
    float _currentScale;
    bool _mapDragActive; // 标记当前是否在拖拽地图模式

    // 初始化方法
    void initializeBasicProperties();
    void createBuildingController();
    void setupEventHandling();
    void createMapBackground();
    void setupInitialLayout();
    void createTestBuildings();

    // 帮助方法
    cocos2d::Sprite* createMapSprite();
    Building* createBuildingByType(int buildingType, int id, int gridX, int gridY);

    // 触摸事件处理
    void setupTouchHandling();
    virtual bool onTouchBegan(cocos2d::Touch* touch, cocos2d::Event* event);
    virtual void onTouchMoved(cocos2d::Touch* touch, cocos2d::Event* event);
    virtual void onTouchEnded(cocos2d::Touch* touch, cocos2d::Event* event);
    
    void storeTouchStartState(cocos2d::Touch* touch);
    void handleMapDragging(cocos2d::Touch* touch);
    cocos2d::Vec2 clampMapPosition(const cocos2d::Vec2& position);

    // 鼠标事件处理
    void setupMouseHandling();
    void onMouseScroll(cocos2d::Event* event);
    
    float calculateNewScale(float scrollDelta);
    cocos2d::Vec2 getAdjustedMousePosition(cocos2d::EventMouse* mouseEvent);
    void applyZoomAroundPoint(const cocos2d::Vec2& zoomPoint, float newScale);
    cocos2d::Vec2 clampMapPositionForScale(const cocos2d::Vec2& position, float scale);
};

#endif // __VILLAGELAYER_H__