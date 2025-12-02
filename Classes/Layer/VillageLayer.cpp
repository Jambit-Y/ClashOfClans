#include "VillageLayer.h"
#include "../Model/Building.h"
#include "../View/BuildingSprite.h"
#include "../System/GridMap.h"
#include "../Controller/BuildingController.h"

USING_NS_CC;

// Static constants definition
const float VillageLayer::MIN_SCALE = 0.5f;
const float VillageLayer::MAX_SCALE = 2.0f;
const float VillageLayer::ZOOM_SPEED = 0.1f;

bool VillageLayer::init() {
    if (!Layer::init()) {
        return false;
    }

    // 1. Initialize basic properties
    initializeBasicProperties();
    
    // 2. Create and setup controller
    createBuildingController();
    
    // 3. Setup event handling
    setupEventHandling();
    
    // 4. Create map background
    createMapBackground();
    
    // 5. Setup initial layout
    setupInitialLayout();
    
    // 6. Create test buildings (for development)
    createTestBuildings();

    return true;
}

VillageLayer::~VillageLayer() {
    // 正确的Controller生命周期管理
    CC_SAFE_RELEASE(_buildingController);
}

#pragma region Initialization Methods

void VillageLayer::initializeBasicProperties() {
    _currentScale = 1.0f;
    _mapDragActive = false;
    this->setScale(_currentScale);
}

void VillageLayer::createBuildingController() {
    _buildingController = BuildingController::create();
    _buildingController->retain(); // 手动管理生命周期
    _buildingController->setVillageLayer(this);
    // 不再addChild！Controller不应该在场景图中
}

void VillageLayer::setupEventHandling() {
    setupTouchHandling();
    setupMouseHandling();
}

void VillageLayer::createMapBackground() {
    auto mapSprite = createMapSprite();
    this->addChild(mapSprite);
    
    // Set layer content size to match map size
    this->setContentSize(mapSprite->getContentSize());
    this->setAnchorPoint(Vec2::ANCHOR_MIDDLE);
}

void VillageLayer::setupInitialLayout() {
    auto visibleSize = Director::getInstance()->getVisibleSize();
    auto mapSize = this->getContentSize();
    
    // Center the map initially
    float initialX = visibleSize.width / 2 - mapSize.width / 2;
    float initialY = visibleSize.height / 2 - mapSize.height / 2;
    this->setPosition(initialX, initialY);
}

void VillageLayer::createTestBuildings() {
    // 只创建加农炮进行测试
    static const struct {
        int id;
        int gridX, gridY;
        int type;
    } testBuildings[] = {
        {2, 10, 8, (int)Building::Type::CANNON}  // 只保留加农炮
    };
    
    for (const auto& buildingConfig : testBuildings) {
        Building* building = createBuildingByType(
            buildingConfig.type, 
            buildingConfig.id, 
            buildingConfig.gridX, 
            buildingConfig.gridY
        );
        _buildingController->addBuilding(building);
    }
}

#pragma endregion

#pragma region Helper Methods

Sprite* VillageLayer::createMapSprite() {
    auto mapSprite = Sprite::create("Scene/VillageScene.jpg");
    if (!mapSprite) {
        // Create placeholder if image not found
        mapSprite = Sprite::create();
        mapSprite->setTextureRect(Rect(0, 0, MAP_DEFAULT_WIDTH, MAP_DEFAULT_HEIGHT));
        mapSprite->setColor(Color3B(50, 50, 50));
    }
    
    mapSprite->setAnchorPoint(Vec2::ANCHOR_MIDDLE);
    mapSprite->setPosition(this->getContentSize() / 2);
    return mapSprite;
}

Building* VillageLayer::createBuildingByType(int buildingType, int id, int gridX, int gridY) {
    Building::Type type = static_cast<Building::Type>(buildingType);
    
    switch (type) {
        case Building::Type::TOWN_HALL:
            return Building::createTownHall(id, gridX, gridY);
        case Building::Type::CANNON:
            return Building::createCannon(id, gridX, gridY);
        case Building::Type::GOLD_MINE:
            return Building::createGoldMine(id, gridX, gridY);
        case Building::Type::BARRACKS:
            return Building::createBarracks(id, gridX, gridY);
        default:
            return nullptr;
    }
}

#pragma endregion

#pragma region Touch Event Handling

void VillageLayer::setupTouchHandling() {
    auto listener = EventListenerTouchOneByOne::create();
    listener->setSwallowTouches(true);

    listener->onTouchBegan = CC_CALLBACK_2(VillageLayer::onTouchBegan, this);
    listener->onTouchMoved = CC_CALLBACK_2(VillageLayer::onTouchMoved, this);
    listener->onTouchEnded = CC_CALLBACK_2(VillageLayer::onTouchEnded, this);

    _eventDispatcher->addEventListenerWithSceneGraphPriority(listener, this);
}

bool VillageLayer::onTouchBegan(Touch* touch, Event* event) {
    // 关键修复：坐标系转换 + 互斥逻辑
    Vec2 layerPos = this->convertToNodeSpace(touch->getLocation());
    
    // 1. 先问Controller：你捕获到建筑了吗？
    bool isBuildingSelected = _buildingController->handleTouchBegan(layerPos);
    
    if (isBuildingSelected) {
        // 建筑被选中，禁用地图拖拽
        _mapDragActive = false;
        CCLOG("VillageLayer: Building selected, map drag disabled");
        return true;
    }
    
    // 2. 没选中建筑，启用地图拖拽模式
    _mapDragActive = true;
    storeTouchStartState(touch);
    CCLOG("VillageLayer: Map drag mode activated");
    return true;
}

void VillageLayer::onTouchMoved(Touch* touch, Event* event) {
    Vec2 layerPos = this->convertToNodeSpace(touch->getLocation());
    
    // 基于Began阶段的决策进行分流
    if (_buildingController->getSelectedBuilding()) {
        // Controller处理建筑拖拽（使用转换后的坐标）
        _buildingController->handleTouchMoved(layerPos);
    } else if (_mapDragActive) {
        // Layer处理地图拖拽（使用世界坐标）
        handleMapDragging(touch);
    }
}

void VillageLayer::onTouchEnded(Touch* touch, Event* event) {
    Vec2 layerPos = this->convertToNodeSpace(touch->getLocation());
    
    // 结束建筑拖拽
    _buildingController->handleTouchEnded(layerPos);
    
    // 重置地图拖拽状态
    _mapDragActive = false;
}

void VillageLayer::storeTouchStartState(Touch* touch) {
    _touchStartPos = touch->getLocation(); // 世界坐标，用于地图拖拽计算
    _layerStartPos = this->getPosition();
}

void VillageLayer::handleMapDragging(Touch* touch) {
    // 地图拖拽使用世界坐标计算
    Vec2 diff = touch->getLocation() - _touchStartPos;
    Vec2 newPos = _layerStartPos + diff;
    
    // Apply map boundary constraints
    newPos = clampMapPosition(newPos);
    this->setPosition(newPos);
}

Vec2 VillageLayer::clampMapPosition(const Vec2& position) {
    auto visibleSize = Director::getInstance()->getVisibleSize();
    auto mapSize = this->getContentSize() * this->getScale(); // 考虑缩放

    float minX = visibleSize.width - mapSize.width;
    float maxX = 0;
    float minY = visibleSize.height - mapSize.height;
    float maxY = 0;

    return Vec2(
        clampf(position.x, minX, maxX),
        clampf(position.y, minY, maxY)
    );
}

#pragma endregion

#pragma region Mouse Event Handling

void VillageLayer::setupMouseHandling() {
    auto mouseListener = EventListenerMouse::create();
    mouseListener->onMouseScroll = CC_CALLBACK_1(VillageLayer::onMouseScroll, this);
    _eventDispatcher->addEventListenerWithSceneGraphPriority(mouseListener, this);
}

void VillageLayer::onMouseScroll(Event* event) {
    EventMouse* mouseEvent = static_cast<EventMouse*>(event);
    float scrollY = mouseEvent->getScrollY();

    float newScale = calculateNewScale(scrollY);
    if (newScale == _currentScale) return;

    Vec2 mousePos = getAdjustedMousePosition(mouseEvent);
    applyZoomAroundPoint(mousePos, newScale);
}

float VillageLayer::calculateNewScale(float scrollDelta) {
    float newScale = _currentScale - scrollDelta * ZOOM_SPEED;
    return clampf(newScale, MIN_SCALE, MAX_SCALE);
}

Vec2 VillageLayer::getAdjustedMousePosition(EventMouse* mouseEvent) {
    Vec2 mousePosInView = mouseEvent->getLocationInView();
    auto winSize = Director::getInstance()->getWinSize();
    
    // Convert from top-left origin to bottom-left origin
    mousePosInView.y = winSize.height - mousePosInView.y;
    return mousePosInView;
}

void VillageLayer::applyZoomAroundPoint(const Vec2& zoomPoint, float newScale) {
    // Calculate zoom transformation
    Vec2 nodeSpaceBefore = this->convertToNodeSpaceAR(zoomPoint);
    
    // Apply new scale
    this->setScale(newScale);
    
    // Adjust position to keep zoom point fixed
    Vec2 nodeSpaceAfter = nodeSpaceBefore * (newScale / _currentScale);
    Vec2 worldDelta = nodeSpaceAfter - nodeSpaceBefore;
    Vec2 newPos = this->getPosition() - worldDelta;
    
    // Apply constraints and update
    newPos = clampMapPositionForScale(newPos, newScale);
    this->setPosition(newPos);
    
    _currentScale = newScale;
}

Vec2 VillageLayer::clampMapPositionForScale(const Vec2& position, float scale) {
    auto visibleSize = Director::getInstance()->getVisibleSize();
    auto mapSize = this->getContentSize() * scale;

    float minX = visibleSize.width - mapSize.width;
    float maxX = 0;
    float minY = visibleSize.height - mapSize.height;
    float maxY = 0;

    return Vec2(
        clampf(position.x, minX, maxX),
        clampf(position.y, minY, maxY)
    );
}

#pragma endregion