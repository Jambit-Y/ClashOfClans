#include "BattleTroopLayer.h"
#include "../Manager/AnimationManager.h"
#include "../Util/GridMapUtils.h"

USING_NS_CC;

BattleTroopLayer* BattleTroopLayer::create() {
    auto layer = new (std::nothrow) BattleTroopLayer();
    if (layer && layer->init()) {
        layer->autorelease();
        return layer;
    }
    CC_SAFE_DELETE(layer);
    return nullptr;
}

bool BattleTroopLayer::init() {
    if (!Layer::init()) {
        return false;
    }
    
    this->setAnchorPoint(Vec2::ZERO);
    this->setPosition(Vec2::ZERO);
    
    CCLOG("BattleTroopLayer: Initialized");
    
    return true;
}

// ========== 单位生成 ==========

BattleUnitSprite* BattleTroopLayer::spawnUnit(const std::string& unitType, int gridX, int gridY) {
    // 边界检查
    if (gridX < 0 || gridX >= GRID_WIDTH || gridY < 0 || gridY >= GRID_HEIGHT) {
        CCLOG("BattleTroopLayer: Invalid grid position (%d, %d)", gridX, gridY);
        return nullptr;
    }
    
    // 创建单位
    auto unit = BattleUnitSprite::create(unitType);
    if (!unit) {
        CCLOG("BattleTroopLayer: Failed to create unit '%s'", unitType.c_str());
        return nullptr;
    }
    
    // 设置位置
    unit->teleportToGrid(gridX, gridY);
    unit->playIdleAnimation();
    
    // 添加到层级
    // 【修改】添加到父节点 (MapLayer) 以便与建筑进行统一 Z 序排序
    auto mapLayer = this->getParent();
    if (mapLayer) {
        mapLayer->addChild(unit);
    } else {
        // Fallback (防崩): 如果还没加到 MapLayer，就加到自己身上
        this->addChild(unit); 
    }
    _units.push_back(unit);
    
    CCLOG("BattleTroopLayer: Spawned %s at grid(%d, %d)", unitType.c_str(), gridX, gridY);
    return unit;
}

void BattleTroopLayer::spawnUnitsGrid(const std::string& unitType, int spacing) {
    int count = 0;
    
    for (int gridY = 0; gridY < GRID_HEIGHT; gridY += spacing) {
        for (int gridX = 0; gridX < GRID_WIDTH; gridX += spacing) {
            if (spawnUnit(unitType, gridX, gridY)) {
                count++;
            }
        }
    }
    
    CCLOG("BattleTroopLayer: Spawned %d units in grid pattern", count);
}

void BattleTroopLayer::removeAllUnits() {
    for (auto unit : _units) {
        this->removeChild(unit);
    }
    _units.clear();
    CCLOG("BattleTroopLayer: Removed all units");
}

// ========== 单位移除 ==========

void BattleTroopLayer::removeUnit(BattleUnitSprite* unit) {
    if (!unit) {
        CCLOG("BattleTroopLayer::removeUnit - ERROR: unit is nullptr!");
        return;
    }
    
    // 【修复】在删除前保存兵种信息，避免访问野指针
    std::string unitType = unit->getUnitType();
    float posX = unit->getPositionX();
    float posY = unit->getPositionY();
    
    CCLOG("BattleTroopLayer::removeUnit - START: Removing unit %s at position (%.1f, %.1f)", 
          unitType.c_str(), posX, posY);
    
    // 从列表中移除
    auto it = std::find(_units.begin(), _units.end(), unit);
    if (it != _units.end()) {
        _units.erase(it);
        CCLOG("BattleTroopLayer::removeUnit - Unit removed from _units vector (size now: %zu)", _units.size());
    } else {
        CCLOG("BattleTroopLayer::removeUnit - WARNING: Unit not found in _units vector!");
    }
    
    // 从显示树移除
    auto mapLayer = this->getParent();
    CCLOG("BattleTroopLayer::removeUnit - MapLayer: %p", mapLayer);
    
    if (mapLayer) {
        Node* unitParent = unit->getParent();
        CCLOG("BattleTroopLayer::removeUnit - Unit's parent: %p (MapLayer: %p)", unitParent, mapLayer);
        
        if (unitParent == mapLayer) {
            CCLOG("BattleTroopLayer::removeUnit - Removing from MapLayer...");
            mapLayer->removeChild(unit);
            CCLOG("BattleTroopLayer::removeUnit - Successfully removed from MapLayer");
        } else {
            CCLOG("BattleTroopLayer::removeUnit - Unit parent mismatch, removing from TroopLayer...");
            this->removeChild(unit);
            CCLOG("BattleTroopLayer::removeUnit - Successfully removed from TroopLayer");
        }
    } else {
        CCLOG("BattleTroopLayer::removeUnit - No MapLayer, removing from TroopLayer...");
        this->removeChild(unit);
        CCLOG("BattleTroopLayer::removeUnit - Successfully removed from TroopLayer");
    }
    
    CCLOG("BattleTroopLayer::removeUnit - COMPLETE: Unit %s removed successfully", unitType.c_str());
}


// ========== 墓碑系统 ==========

void BattleTroopLayer::spawnTombstone(const Vec2& position) {
    CCLOG("===== TOMBSTONE DEBUG START =====");
    CCLOG("BattleTroopLayer::spawnTombstone - Creating tombstone at (%.1f, %.1f)", position.x, position.y);
    
    // 创建一个灰色方块代表墓碑
    auto tombstone = DrawNode::create();
    if (!tombstone) {
        CCLOG("BattleTroopLayer::spawnTombstone - ERROR: Failed to create DrawNode!");
        return;
    }
    
    // 【修复】使用更明显的大方块和颜色
    Vec2 vertices[] = {
        Vec2(-20, 0),    // 左下
        Vec2(20, 0),     // 右下
        Vec2(20, 40),    // 右上
        Vec2(-20, 40)    // 左上
    };
    
    // 【修复】使用红色而不是灰色，更容易发现
    Color4F fillColor(1.0f, 0.0f, 0.0f, 1.0f);  // 鲜红色
    Color4F borderColor(0.0f, 0.0f, 0.0f, 1.0f); // 黑色边框
    
    tombstone->drawSolidPoly(vertices, 4, fillColor);
    tombstone->drawPoly(vertices, 4, true, borderColor);
    
    // 设置位置
    tombstone->setPosition(position);
    
    // 【修复】确保可见性
    tombstone->setVisible(true);
    tombstone->setOpacity(255);
    
    // 添加到地图层
    auto mapLayer = this->getParent();
    CCLOG("BattleTroopLayer::spawnTombstone - MapLayer: %p", mapLayer);
    
    if (mapLayer) {
        CCLOG("BattleTroopLayer::spawnTombstone - Adding to MapLayer with Z-order 999...");
        mapLayer->addChild(tombstone, 999); // 【修复】Z-Order设为999，显示在最顶层
        CCLOG("BattleTroopLayer::spawnTombstone - Tombstone added, checking parent...");
        CCLOG("BattleTroopLayer::spawnTombstone - Tombstone parent: %p", tombstone->getParent());
        CCLOG("BattleTroopLayer::spawnTombstone - Tombstone position: (%.1f, %.1f)", 
              tombstone->getPositionX(), tombstone->getPositionY());
        CCLOG("BattleTroopLayer::spawnTombstone - Tombstone visible: %s", tombstone->isVisible() ? "YES" : "NO");
        CCLOG("BattleTroopLayer::spawnTombstone - Tombstone opacity: %d", tombstone->getOpacity());
    } else {
        CCLOG("BattleTroopLayer::spawnTombstone - WARNING: No MapLayer, adding to TroopLayer...");
        this->addChild(tombstone, 999);
    }
    
    // 保存到列表
    _tombstones.push_back(tombstone);
    
    CCLOG("BattleTroopLayer::spawnTombstone - COMPLETE: Tombstone #%zu created", _tombstones.size());
    CCLOG("===== TOMBSTONE DEBUG END =====");
}

void BattleTroopLayer::clearAllTombstones() {
    auto mapLayer = this->getParent();
    
    for (auto tombstone : _tombstones) {
        if (mapLayer && tombstone->getParent() == mapLayer) {
            mapLayer->removeChild(tombstone);
        } else {
            this->removeChild(tombstone);
        }
    }
    
    _tombstones.clear();
    CCLOG("BattleTroopLayer: Cleared all tombstones");
}
