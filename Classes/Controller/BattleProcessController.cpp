#include "BattleProcessController.h"
#include "../Layer/BattleTroopLayer.h"
#include "../Manager/VillageDataManager.h"
#include "../Model/BuildingConfig.h"
#include "../Util/GridMapUtils.h"
#include "../Util/FindPathUtil.h"
#include "2d/CCParticleExamples.h"
#include <cmath>
#include <set>
#include "../Sprite/BuildingSprite.h"
#include "../Component/DefenseBuildingAnimation.h"

USING_NS_CC;

BattleProcessController* BattleProcessController::_instance = nullptr;

// ==========================================
// 静态辅助函数
// ==========================================

static float calculatePathLength(const std::vector<Vec2>& path) {
    if (path.size() < 2) return 0.0f;
    float totalDist = 0.0f;
    for (size_t i = 0; i < path.size() - 1; ++i) {
        totalDist += path[i].distance(path[i + 1]);
    }
    return totalDist;
}

static int getDamageByUnitType(UnitTypeID typeID) {
    switch (typeID) {
        case UnitTypeID::BARBARIAN:
            return TroopConfig::getInstance()->getTroopById(1001).damagePerSecond;
        case UnitTypeID::ARCHER:
            return TroopConfig::getInstance()->getTroopById(1002).damagePerSecond;
        case UnitTypeID::GOBLIN:
            return TroopConfig::getInstance()->getTroopById(1003).damagePerSecond;
        case UnitTypeID::GIANT:
            return TroopConfig::getInstance()->getTroopById(1004).damagePerSecond;
        case UnitTypeID::WALL_BREAKER:
            return TroopConfig::getInstance()->getTroopById(1005).damagePerSecond;
        case UnitTypeID::BALLOON:
            return TroopConfig::getInstance()->getTroopById(1006).damagePerSecond;
        default:
            return TroopConfig::getInstance()->getTroopById(1001).damagePerSecond;
    }
}

static int getAttackRangeByUnitType(UnitTypeID typeID) {
    switch (typeID) {
        case UnitTypeID::ARCHER:
            return 3;
        case UnitTypeID::BARBARIAN:
        case UnitTypeID::GOBLIN:
        case UnitTypeID::GIANT:
        case UnitTypeID::BALLOON:  // 气球兵也需要 1 格攻击范围，否则无法正常攻击
            return 1;
        case UnitTypeID::WALL_BREAKER:
            return 0;
        default:
            return 1;
    }
}

// ==========================================
// 生命周期管理
// ==========================================

BattleProcessController* BattleProcessController::getInstance() {
    if (!_instance) {
        _instance = new BattleProcessController();
    }
    return _instance;
}

void BattleProcessController::destroyInstance() {
    if (_instance) {
        delete _instance;
        _instance = nullptr;
    }
}

// ==========================================
// 战斗状态重置
// ==========================================

void BattleProcessController::resetBattleState() {
    auto dataManager = VillageDataManager::getInstance();
    auto& buildings = const_cast<std::vector<BuildingInstance>&>(dataManager->getAllBuildings());

    int restoredCount = 0;
    for (auto& building : buildings) {
        auto config = BuildingConfig::getInstance()->getConfig(building.type);
        int maxHP = (config && config->hitPoints > 0) ? config->hitPoints : 100;

        if (building.currentHP != maxHP || building.isDestroyed) {
            building.currentHP = maxHP;
            building.isDestroyed = false;
            restoredCount++;
        }

        // 清除防御建筑的锁定目标
        building.lockedTarget = nullptr;

        // ✅ 重置攻击冷却
        building.attackCooldown = 0.0f;
    }

    // ✅ 清理陷阱触发状态
    _triggeredTraps.clear();
    _trapTimers.clear();

    dataManager->saveToFile("village.json");
}
// ==========================================
// 核心攻击逻辑（提取公共代码）
// ==========================================

void BattleProcessController::executeAttack(
    BattleUnitSprite* unit,
    BattleTroopLayer* troopLayer,
    int targetID,
    bool isForcedTarget,
    const std::function<void()>& onTargetDestroyed,
    const std::function<void()>& onContinueAttack
) {
    auto dm = VillageDataManager::getInstance();
    BuildingInstance* liveTarget = dm->getBuildingById(targetID);

    // 防止重复处理
    if (unit->isChangingTarget()) {
        return;
    }

    // 目标已摧毁
    if (!liveTarget || liveTarget->isDestroyed || liveTarget->currentHP <= 0) {
        unit->setChangingTarget(true);
        onTargetDestroyed();
        
        unit->runAction(Sequence::create(
            DelayTime::create(0.1f),
            CallFunc::create([unit]() {
                unit->setChangingTarget(false);
            }),
            nullptr
        ));
        return;
    }

    // ========== ✅ 炸弹兵自爆特判 ==========
    if (unit->getUnitTypeID() == UnitTypeID::WALL_BREAKER) {
        CCLOG("BattleProcessController: Wall Breaker executing suicide attack");

        // 执行自爆攻击
        performWallBreakerSuicideAttack(unit, liveTarget, troopLayer, [onTargetDestroyed]() {
            // 炸弹兵死后，不继续攻击，直接调用目标摧毁回调
            // 注意：这里不管目标是否被摧毁，炸弹兵都已经死了
            if (onTargetDestroyed) {
                onTargetDestroyed();
            }
        });

        // 立即返回，不执行后续的普通攻击逻辑
        return;
    }

    // 计算伤害
    int dps = getDamageByUnitType(unit->getUnitTypeID());
    
    // 二次检查
    if (liveTarget->isDestroyed || liveTarget->currentHP <= 0) {
        onTargetDestroyed();
        return;
    }

    liveTarget->currentHP -= dps;

    // 目标被摧毁
    if (liveTarget->currentHP <= 0) {
        liveTarget->isDestroyed = true;
        liveTarget->currentHP = 0;
        
        FindPathUtil::getInstance()->updatePathfindingMap();
        
        // 【新增】发送建筑摧毁事件
        Director::getInstance()->getEventDispatcher()->dispatchCustomEvent(
            "EVENT_BUILDING_DESTROYED", 
            static_cast<void*>(liveTarget)
        );

        // 更新摧毁进度
        updateDestructionProgress();

        onTargetDestroyed();
    }
    else {
        onContinueAttack();
    }
}

// ==========================================
// 砍城墙时检查是否有更好的路径
// ==========================================

bool BattleProcessController::shouldAbandonWallForBetterPath(BattleUnitSprite* unit, int currentWallID) {
    Vec2 unitPos = unit->getPosition();
    Vec2 unitGridPos = GridMapUtils::pixelToGrid(unitPos);
    
    CCLOG("--- shouldAbandonWallForBetterPath DEBUG ---");
    CCLOG("  Unit at pixel(%.1f, %.1f), grid(%.1f, %.1f)", 
          unitPos.x, unitPos.y, unitGridPos.x, unitGridPos.y);
    CCLOG("  Current wall ID: %d", currentWallID);
    
    const BuildingInstance* bestTarget = nullptr;
    if (unit->getUnitTypeID() == UnitTypeID::GOBLIN) {
        bestTarget = findTargetWithResourcePriority(unitPos, unit->getUnitTypeID());
    } else if (unit->getUnitTypeID() == UnitTypeID::GIANT || unit->getUnitTypeID() == UnitTypeID::BALLOON) {
        bestTarget = findTargetWithDefensePriority(unitPos, unit->getUnitTypeID());
    } else {
        bestTarget = findTargetWithResourcePriority(unitPos, unit->getUnitTypeID());
    }
    
    if (!bestTarget) {
        CCLOG("  No best target found, keep attacking wall");
        return false;
    }
    
    CCLOG("  Best target: ID=%d, Type=%d at grid(%d, %d)",
          bestTarget->id, bestTarget->type, bestTarget->gridX, bestTarget->gridY);
    
    Vec2 targetCenter = GridMapUtils::gridToPixelCenter(bestTarget->gridX, bestTarget->gridY);
    
    auto pathfinder = FindPathUtil::getInstance();
    int attackRange = getAttackRangeByUnitType(unit->getUnitTypeID());
    std::vector<Vec2> pathAround = pathfinder->findPathToAttackBuilding(unitPos, *bestTarget, attackRange);
    
    if (pathAround.empty()) {
        CCLOG("  No path around found, keep attacking wall");
        return false;
    }
    
    float pathLength = calculatePathLength(pathAround);
    float directDist = unitPos.distance(targetCenter);
    float detourCost = pathLength - directDist;
    
    CCLOG("  Path analysis:");
    CCLOG("    Path length: %.1f", pathLength);
    CCLOG("    Direct distance: %.1f", directDist);
    CCLOG("    Detour cost: %.1f (threshold: %.1f)", detourCost, PIXEL_DETOUR_THRESHOLD);
    CCLOG("    Path/Direct ratio: %.2f (max allowed: 2.0)", pathLength / directDist);
    
    if (detourCost <= PIXEL_DETOUR_THRESHOLD && pathLength <= directDist * 2.0f) {
        CCLOG("  ✓ ABANDON WALL - better path found!");
        return true;
    }
    
    CCLOG("  ✗ Keep attacking wall - no better path");
    CCLOG("--- END shouldAbandonWallForBetterPath ---");
    return false;
}



// ==========================================
// 核心 AI 逻辑入口
// ==========================================

void BattleProcessController::startUnitAI(BattleUnitSprite* unit, BattleTroopLayer* troopLayer) {
    if (!unit) {
        return;
    }

    Vec2 unitPos = unit->getPosition();
    Vec2 unitGridPos = GridMapUtils::pixelToGrid(unitPos);
    
    CCLOG("========== START UNIT AI DEBUG ==========");
    CCLOG("Unit: %s at pixel(%.1f, %.1f), grid(%.1f, %.1f)",
          unit->getUnitType().c_str(), unitPos.x, unitPos.y, unitGridPos.x, unitGridPos.y);
    
    const BuildingInstance* target = nullptr;
    
    // ========== 炸弹人特殊处理：只攻击城墙 ==========
    if (unit->getUnitTypeID() == UnitTypeID::WALL_BREAKER) {
        target = findNearestWall(unitPos);
        if (!target) {
            CCLOG("Wall Breaker: No walls found, standing idle");
            unit->playIdleAnimation();
            return;  // 没有城墙则原地待机
        }
    }
    // ============================================
    else if (unit->getUnitTypeID() == UnitTypeID::GOBLIN) {
        target = findTargetWithResourcePriority(unitPos, unit->getUnitTypeID());
    } else if (unit->getUnitTypeID() == UnitTypeID::GIANT || unit->getUnitTypeID() == UnitTypeID::BALLOON) {
        target = findTargetWithDefensePriority(unitPos, unit->getUnitTypeID());
    } else {
        target = findTargetWithResourcePriority(unitPos, unit->getUnitTypeID());
    }

    if (!target) {
        CCLOG("No target found, playing idle animation");
        unit->playIdleAnimation();
        return;
    }

    CCLOG("Target selected: ID=%d, Type=%d at grid(%d, %d)",
          target->id, target->type, target->gridX, target->gridY);

    // 【新增】发送目标锁定事件，通知显示 Beacon
    EventCustom event("EVENT_UNIT_TARGET_LOCKED");
    event.setUserData(reinterpret_cast<void*>(static_cast<intptr_t>(target->id)));
    Director::getInstance()->getEventDispatcher()->dispatchEvent(&event);

    auto pathfinder = FindPathUtil::getInstance();
    Vec2 targetCenter = GridMapUtils::gridToPixelCenter(target->gridX, target->gridY);

    int attackRange = getAttackRangeByUnitType(unit->getUnitTypeID());
    CCLOG("Attack range: %d grids", attackRange);
    
    //气球兵是飞行单位，直接飞向目标建筑边缘，无视城墙和地面障碍物
    if (unit->getUnitTypeID() == UnitTypeID::BALLOON) {
        // 计算飞到建筑边缘的攻击位置（而不是飞到中心）
        auto config = BuildingConfig::getInstance()->getConfig(target->type);
        int buildingWidth = config ? config->gridWidth : 2;
        int buildingHeight = config ? config->gridHeight : 2;
        
        // 计算建筑中心
        float buildingCenterX = target->gridX + buildingWidth / 2.0f;
        float buildingCenterY = target->gridY + buildingHeight / 2.0f;
        Vec2 buildingCenter = GridMapUtils::gridToPixelCenter(
            static_cast<int>(buildingCenterX), 
            static_cast<int>(buildingCenterY)
        );
        
        // 计算从气球当前位置到建筑中心的方向
        Vec2 direction = buildingCenter - unitPos;
        direction.normalize();
        
        // 计算建筑边缘的攻击点（中心位置减去攻击范围距离）
        float attackDistancePixels = (attackRange + buildingWidth / 2.0f) * 32.0f;  // 假设每格 32 像素
        Vec2 attackPosition = buildingCenter - direction * attackDistancePixels;
        
        // 如果攻击位置比当前位置更远，就直接飞到建筑中心
        if (unitPos.distance(attackPosition) > unitPos.distance(buildingCenter)) {
            attackPosition = buildingCenter;
        }
        
        std::vector<Vec2> directPath = { attackPosition };
        unit->followPath(directPath, 100.0f, [this, unit, troopLayer]() {
            startCombatLoop(unit, troopLayer);
        });
        return;
    }
    
    std::vector<Vec2> pathAround = pathfinder->findPathToAttackBuilding(unitPos, *target, attackRange);
    float distAround = calculatePathLength(pathAround);
    float distDirect = unitPos.distance(targetCenter);

    CCLOG("Path finding result: pathAround.size()=%zu, distAround=%.1f, distDirect=%.1f",
          pathAround.size(), distAround, distDirect);

    if (!pathAround.empty()) {
        float detourCost = distAround - distDirect;
        
        CCLOG("Detour cost: %.1f (threshold=%.1f)", detourCost, PIXEL_DETOUR_THRESHOLD);
        
        if (detourCost <= PIXEL_DETOUR_THRESHOLD && distAround <= distDirect * 2.0f) {
            CCLOG("✓ Using path around (detour acceptable)");
            unit->followPath(pathAround, 100.0f, [this, unit, troopLayer]() {
                startCombatLoop(unit, troopLayer);
            });
            return;
        } else {
            CCLOG("✗ Path around too long, will check for wall to break");
        }
    } else {
        CCLOG("No valid path around found, will check for wall to break");
    }

    const BuildingInstance* wallToBreak = getFirstWallInLine(unitPos, targetCenter);

    if (wallToBreak && unit->getUnitTypeID() != UnitTypeID::BALLOON) {
        CCLOG("Wall to break found: ID=%d at grid(%d, %d)", 
              wallToBreak->id, wallToBreak->gridX, wallToBreak->gridY);
        
        std::vector<Vec2> pathToWall = pathfinder->findPathToAttackBuilding(unitPos, *wallToBreak, attackRange);
        CCLOG("Path to wall: size=%zu", pathToWall.size());

        if (pathToWall.empty()) {
            CCLOG("No path to wall, starting forced combat directly");
            startCombatLoopWithForcedTarget(unit, troopLayer, wallToBreak);
        }
        else {
            CCLOG("Following path to wall");
            unit->followPath(pathToWall, 100.0f, [this, unit, troopLayer, wallToBreak]() {
                startCombatLoopWithForcedTarget(unit, troopLayer, wallToBreak);
            });
        }
    }
    else {
        CCLOG("⚠️ NO WALL FOUND in line! Going direct to target (may pass through walls!)");
        CCLOG("  Unit pos: (%.1f, %.1f)", unitPos.x, unitPos.y);
        CCLOG("  Target center: (%.1f, %.1f)", targetCenter.x, targetCenter.y);
        
        std::vector<Vec2> directPath = { targetCenter };
        unit->followPath(directPath, 100.0f, [this, unit, troopLayer]() {
            startCombatLoop(unit, troopLayer);
        });
    }
    
    CCLOG("========== END UNIT AI DEBUG ==========\n");
}

// ==========================================
// 辅助方法
// ==========================================

const BuildingInstance* BattleProcessController::getFirstWallInLine(const Vec2& startPixel, const Vec2& endPixel) {
    auto dataManager = VillageDataManager::getInstance();
    auto pathfinder = FindPathUtil::getInstance();

    CCLOG("--- getFirstWallInLine DEBUG ---");
    CCLOG("  Start pixel: (%.1f, %.1f)", startPixel.x, startPixel.y);
    CCLOG("  End pixel: (%.1f, %.1f)", endPixel.x, endPixel.y);
    
    Vec2 startGridF = GridMapUtils::pixelToGrid(startPixel);
    Vec2 endGridF = GridMapUtils::pixelToGrid(endPixel);
    CCLOG("  Start grid: (%.1f, %.1f)", startGridF.x, startGridF.y);
    CCLOG("  End grid: (%.1f, %.1f)", endGridF.x, endGridF.y);

    // 方法1: 使用寻路器找到穿墙路径
    std::vector<Vec2> throughPath = pathfinder->findPathIgnoringWalls(startPixel, endPixel);
    CCLOG("  findPathIgnoringWalls returned %zu points", throughPath.size());
    
    if (!throughPath.empty()) {
        for (size_t i = 0; i < throughPath.size(); ++i) {
            const auto& worldPt = throughPath[i];
            Vec2 gridF = GridMapUtils::pixelToGrid(worldPt);
            int gx = static_cast<int>(std::floor(gridF.x + 0.5f));
            int gy = static_cast<int>(std::floor(gridF.y + 0.5f));

            BuildingInstance* b = dataManager->getBuildingAtGrid(gx, gy);
            if (b) {
                CCLOG("    Path point %zu: grid(%d, %d) has building ID=%d, type=%d, destroyed=%s",
                      i, gx, gy, b->id, b->type, b->isDestroyed ? "true" : "false");
                      
                if (b->type == 303 && !b->isDestroyed && b->currentHP > 0) {
                    CCLOG("  ✓ Found wall at grid(%d, %d)!", gx, gy);
                    return b;
                }
            }
        }
    }

    CCLOG("  No wall found via pathfinder, trying enhanced line scan...");
    
    // 方法2: 改进的线性扫描 - 使用更多采样点
    Vec2 startGrid = GridMapUtils::pixelToGrid(startPixel);
    Vec2 endGrid = GridMapUtils::pixelToGrid(endPixel);
    Vec2 diff = endGrid - startGrid;
    
    // ✅ 修复：使用更细的步长，确保每个格子都被检测到
    float maxDiff = std::max(std::abs(diff.x), std::abs(diff.y));
    int steps = static_cast<int>(std::ceil(maxDiff)) * 2;  // 乘以2确保更细的采样
    if (steps < 1) steps = 1;
    
    CCLOG("  Enhanced line scan: %d steps from grid(%.1f, %.1f) to grid(%.1f, %.1f)",
          steps, startGrid.x, startGrid.y, endGrid.x, endGrid.y);
    
    Vec2 direction = diff / static_cast<float>(steps);
    Vec2 current = startGrid;
    
    // 用于避免重复检测同一格子
    std::set<std::pair<int, int>> checkedGrids;
    
    for (int i = 0; i <= steps; ++i) {
        int gx = static_cast<int>(std::floor(current.x));
        int gy = static_cast<int>(std::floor(current.y));
        
        // 检查是否已经检测过这个格子
        auto gridKey = std::make_pair(gx, gy);
        if (checkedGrids.find(gridKey) == checkedGrids.end()) {
            checkedGrids.insert(gridKey);

            BuildingInstance* b = dataManager->getBuildingAtGrid(gx, gy);
            if (b) {
                CCLOG("    Step %d: grid(%d, %d) has building ID=%d, type=%d",
                      i, gx, gy, b->id, b->type);
                      
                if (b->type == 303 && !b->isDestroyed && b->currentHP > 0) {
                    CCLOG("  ✓ Found wall at grid(%d, %d) via line scan!", gx, gy);
                    return b;
                }
            }
        }
        current += direction;
    }
    
    // 方法3: 遍历所有城墙，检查是否在路径上
    CCLOG("  Line scan failed, checking all walls for intersection...");
    const auto& buildings = dataManager->getAllBuildings();
    
    for (const auto& building : buildings) {
        if (building.type != 303) continue;
        if (building.isDestroyed || building.currentHP <= 0) continue;
        
        // 检查城墙是否在起点和终点之间
        int wallX = building.gridX;
        int wallY = building.gridY;
        
        // 计算城墙是否在路径的包围盒内
        int minX = static_cast<int>(std::min(startGrid.x, endGrid.x)) - 1;
        int maxX = static_cast<int>(std::max(startGrid.x, endGrid.x)) + 1;
        int minY = static_cast<int>(std::min(startGrid.y, endGrid.y)) - 1;
        int maxY = static_cast<int>(std::max(startGrid.y, endGrid.y)) + 1;
        
        if (wallX >= minX && wallX <= maxX && wallY >= minY && wallY <= maxY) {
            // 检查城墙是否真的在路径上（使用点到线段距离）
            Vec2 wallPos(wallX + 0.5f, wallY + 0.5f);
            Vec2 lineDir = endGrid - startGrid;
            float lineLen = lineDir.length();
            
            if (lineLen > 0.01f) {
                lineDir.normalize();
                Vec2 toWall = wallPos - startGrid;
                float proj = toWall.dot(lineDir);
                
                // 检查投影点是否在线段范围内
                if (proj >= 0 && proj <= lineLen) {
                    Vec2 projPoint = startGrid + lineDir * proj;
                    float dist = wallPos.distance(projPoint);
                    
                    // 如果距离小于1格，认为城墙在路径上
                    if (dist < 1.5f) {
                        CCLOG("  ✓ Found wall ID=%d at grid(%d, %d) via intersection check! (dist=%.2f)",
                              building.id, wallX, wallY, dist);
                        return &building;
                    }
                }
            }
        }
    }

    CCLOG("  ✗ NO WALL FOUND in line between unit and target!");
    CCLOG("--- END getFirstWallInLine ---");
    return nullptr;
}

static bool isResourceBuilding(int buildingType) {
    return buildingType == 1 || buildingType == 202 || buildingType == 203 || 
           buildingType == 204 || buildingType == 205;
}

static bool isDefenseBuilding(int buildingType) {
    return buildingType == 301 || buildingType == 302;
}

const BuildingInstance* BattleProcessController::findTargetWithResourcePriority(const Vec2& unitWorldPos, UnitTypeID unitType) {
    auto dataManager = VillageDataManager::getInstance();
    const auto& buildings = dataManager->getAllBuildings();

    if (buildings.empty()) return nullptr;

    const BuildingInstance* bestBuilding = nullptr;
    const BuildingInstance* fallbackBuilding = nullptr;
    float minDistanceSq = FLT_MAX;
    float fallbackMinDistanceSq = FLT_MAX;

    for (const auto& building : buildings) {
        if (building.isDestroyed || building.currentHP <= 0) continue;
        if (building.state == BuildingInstance::State::PLACING) continue;
        
        // 跳过城墙（303）和陷阱（4xx）
        if (building.type == 303) continue;
        if (building.type >= 400 && building.type < 500) continue;

        auto config = BuildingConfig::getInstance()->getConfig(building.type);
        if (!config) continue;

        // ✅ 修复：使用 getBuildingCenterPixel 获取建筑的真实中心位置
        Vec2 bPos = GridMapUtils::getBuildingCenterPixel(
            building.gridX, building.gridY, 
            config->gridWidth, config->gridHeight
        );
        float distSq = unitWorldPos.distanceSquared(bPos);

        if (unitType == UnitTypeID::GOBLIN) {
            if (isResourceBuilding(building.type)) {
                if (distSq < minDistanceSq) {
                    minDistanceSq = distSq;
                    bestBuilding = &building;
                }
            } else {
                if (distSq < fallbackMinDistanceSq) {
                    fallbackMinDistanceSq = distSq;
                    fallbackBuilding = &building;
                }
            }
        } else {
            if (distSq < minDistanceSq) {
                minDistanceSq = distSq;
                bestBuilding = &building;
            }
        }
    }

    if (unitType == UnitTypeID::GOBLIN && !bestBuilding && fallbackBuilding) {
        return fallbackBuilding;
    }

    return bestBuilding;
}

const BuildingInstance* BattleProcessController::findTargetWithDefensePriority(const Vec2& unitWorldPos, UnitTypeID unitType) {
    auto dataManager = VillageDataManager::getInstance();
    const auto& buildings = dataManager->getAllBuildings();

    if (buildings.empty()) return nullptr;

    const BuildingInstance* bestBuilding = nullptr;
    const BuildingInstance* fallbackBuilding = nullptr;
    float minDistanceSq = FLT_MAX;
    float fallbackMinDistanceSq = FLT_MAX;

    for (const auto& building : buildings) {
        if (building.isDestroyed || building.currentHP <= 0) continue;
        if (building.state == BuildingInstance::State::PLACING) continue;
        
        // 跳过城墙（303）和陷阱（4xx）
        if (building.type == 303) continue;
        if (building.type >= 400 && building.type < 500) continue;
        
        // 气球兵额外跳过城墙（已在上面处理，这里保留代码完整性）
        if (unitType == UnitTypeID::BALLOON && building.type == 303) continue;

        auto config = BuildingConfig::getInstance()->getConfig(building.type);
        if (!config) continue;

        // ✅ 修复：使用 getBuildingCenterPixel 获取建筑的真实中心位置
        Vec2 bPos = GridMapUtils::getBuildingCenterPixel(
            building.gridX, building.gridY, 
            config->gridWidth, config->gridHeight
        );
        float distSq = unitWorldPos.distanceSquared(bPos);

        if (unitType == UnitTypeID::GIANT || unitType == UnitTypeID::BALLOON) {
            if (isDefenseBuilding(building.type)) {
                if (distSq < minDistanceSq) {
                    minDistanceSq = distSq;
                    bestBuilding = &building;
                }
            } else {
                if (building.type != 303) {
                    if (distSq < fallbackMinDistanceSq) {
                        fallbackMinDistanceSq = distSq;
                        fallbackBuilding = &building;
                    }
                }
            }
        } else {
            if (distSq < minDistanceSq) {
                minDistanceSq = distSq;
                bestBuilding = &building;
            }
        }
    }

    if ((unitType == UnitTypeID::GIANT || unitType == UnitTypeID::BALLOON) && !bestBuilding && fallbackBuilding) {
        return fallbackBuilding;
    }

    return bestBuilding;
}

const BuildingInstance* BattleProcessController::findNearestBuilding(const Vec2& unitWorldPos, UnitTypeID unitType) {
    // ========== 炸弹人特殊处理：只返回城墙 ==========
    if (unitType == UnitTypeID::WALL_BREAKER) {
        return findNearestWall(unitWorldPos);
    }
    // ================================================
    
    if (unitType == UnitTypeID::GOBLIN) {
        return findTargetWithResourcePriority(unitWorldPos, unitType);
    } else if (unitType == UnitTypeID::GIANT || unitType == UnitTypeID::BALLOON) {
        return findTargetWithDefensePriority(unitWorldPos, unitType);
    } else {
        return findTargetWithResourcePriority(unitWorldPos, unitType);
    }
}

// ==========================================
// 炸弹兵专用：查找最近城墙
// ==========================================

const BuildingInstance* BattleProcessController::findNearestWall(const Vec2& unitWorldPos) {
    auto dataManager = VillageDataManager::getInstance();
    const auto& buildings = dataManager->getAllBuildings();
    
    const BuildingInstance* nearestWall = nullptr;
    float minDistanceSq = FLT_MAX;
    
    for (const auto& building : buildings) {
        // 只查找城墙（type=303）
        if (building.type != 303) continue;
        if (building.isDestroyed || building.currentHP <= 0) continue;
        if (building.state == BuildingInstance::State::PLACING) continue;
        
        Vec2 bPos = GridMapUtils::gridToPixelCenter(building.gridX, building.gridY);
        float distSq = unitWorldPos.distanceSquared(bPos);
        
        if (distSq < minDistanceSq) {
            minDistanceSq = distSq;
            nearestWall = &building;
        }
    }
    
    return nearestWall;
}

// ==========================================
// 建筑防御系统 - 查找攻击范围内的兵种
// ==========================================

BattleUnitSprite* BattleProcessController::findNearestUnitInRange(
    const BuildingInstance& building, 
    float attackRangeGrids,
    BattleTroopLayer* troopLayer) {
    
    if (!troopLayer) return nullptr;
    
    auto config = BuildingConfig::getInstance()->getConfig(building.type);
    if (!config) return nullptr;
    
    int centerX = building.gridX + config->gridWidth / 2;
    int centerY = building.gridY + config->gridHeight / 2;
    
    auto allUnits = troopLayer->getAllUnits();
    if (allUnits.empty()) return nullptr;
    
    BattleUnitSprite* nearestUnit = nullptr;
    int minGridDistance = INT_MAX;
    int attackRangeInt = static_cast<int>(attackRangeGrids);
    
    for (auto unit : allUnits) {
        if (!unit) continue;
        
        // 气球兵是飞行单位，只有箭塔（302）能攻击它
        // 加农炮（301）只能攻击地面单位
        if (unit->getUnitTypeID() == UnitTypeID::BALLOON && building.type != 302) {
            continue;  // 跳过气球兵
        }
        
        Vec2 unitGridPos = unit->getGridPosition();
        int unitGridX = static_cast<int>(unitGridPos.x);
        int unitGridY = static_cast<int>(unitGridPos.y);
        
        int gridDistance = std::max(
            std::abs(unitGridX - centerX),
            std::abs(unitGridY - centerY)
        );
        
        if (gridDistance <= attackRangeInt) {
            if (gridDistance < minGridDistance) {
                minGridDistance = gridDistance;
                nearestUnit = unit;
            }
        }
    }
    
    if (nearestUnit) {
        nearestUnit->setTargetedByBuilding(true);
    }
    
    return nearestUnit;
}

std::vector<BattleUnitSprite*> BattleProcessController::getAllUnitsInRange(
    const BuildingInstance& building, 
    float attackRangeGrids,
    BattleTroopLayer* troopLayer) {
    
    std::vector<BattleUnitSprite*> unitsInRange;
    
    if (!troopLayer) return unitsInRange;
    
    auto config = BuildingConfig::getInstance()->getConfig(building.type);
    if (!config) return unitsInRange;
    
    int centerX = building.gridX + config->gridWidth / 2;
    int centerY = building.gridY + config->gridHeight / 2;
    
    auto allUnits = troopLayer->getAllUnits();
    int attackRangeInt = static_cast<int>(attackRangeGrids);
    
    for (auto unit : allUnits) {
        if (!unit) continue;
        
        Vec2 unitGridPos = unit->getGridPosition();
        int unitGridX = static_cast<int>(unitGridPos.x);
        int unitGridY = static_cast<int>(unitGridPos.y);
        
        int gridDistance = std::max(
            std::abs(unitGridX - centerX),
            std::abs(unitGridY - centerY)
        );
        
        if (gridDistance <= attackRangeInt) {
            unitsInRange.push_back(unit);
        }
    }
    
    return unitsInRange;
}

// ==========================================
// 建筑防御自动更新系统
// ==========================================

void BattleProcessController::updateBuildingDefense(BattleTroopLayer* troopLayer) {
    if (!troopLayer) return;

    auto dataManager = VillageDataManager::getInstance();
    auto& buildings = const_cast<std::vector<BuildingInstance>&>(dataManager->getAllBuildings());

    std::set<BattleUnitSprite*> targetedUnitsThisFrame;
    float deltaTime = Director::getInstance()->getDeltaTime();

    for (auto& building : buildings) {
        // 跳过非防御建筑
        if (building.isDestroyed || building.currentHP <= 0) continue;
        if (building.state == BuildingInstance::State::PLACING) continue;
        if (building.type != 301 && building.type != 302) continue;

        auto config = BuildingConfig::getInstance()->getConfig(building.type);
        if (!config) continue;

        float attackRange = config->attackRange;
        float attackSpeed = config->attackSpeed;

        BattleUnitSprite* currentTarget = static_cast<BattleUnitSprite*>(building.lockedTarget);

        // ========== 目标有效性检查 ==========
        bool targetValid = false;

        if (currentTarget) {
            auto allUnits = troopLayer->getAllUnits();

            // 检查1: 目标是否还存活
            for (auto unit : allUnits) {
                if (unit == currentTarget && !unit->isDead()) {
                    targetValid = true;
                    break;
                }
            }

            // 检查2: 是否还在范围内
            if (targetValid) {
                int centerX = building.gridX + config->gridWidth / 2;
                int centerY = building.gridY + config->gridHeight / 2;

                Vec2 unitGridPos = currentTarget->getGridPosition();
                int gridDistance = std::max(
                    std::abs((int)unitGridPos.x - centerX),
                    std::abs((int)unitGridPos.y - centerY)
                );

                if (gridDistance > static_cast<int>(attackRange)) {
                    targetValid = false;
                }
            }

            // 目标无效，清除锁定
            if (!targetValid) {
                building.lockedTarget = nullptr;
                currentTarget = nullptr;
            }
        }

        // ========== 寻找新目标 ==========
        if (!currentTarget) {
            BattleUnitSprite* newTarget = findNearestUnitInRange(building, attackRange, troopLayer);
            if (newTarget && !newTarget->isDead()) {
                building.lockedTarget = static_cast<void*>(newTarget);
                currentTarget = newTarget;
                building.attackCooldown = 0.0f;
            }
        }

        // ========== 攻击逻辑 ==========
        if (currentTarget) {
            targetedUnitsThisFrame.insert(currentTarget);

            building.attackCooldown -= deltaTime;

            if (building.attackCooldown <= 0.0f) {
                int damagePerShot = static_cast<int>(config->damagePerSecond * attackSpeed);
                currentTarget->takeDamage(damagePerShot);

                // ✅✅✅ 关键修复：转换到 MapLayer 坐标系
                auto mapLayer = troopLayer->getParent();
                if (mapLayer) {
                    std::string spriteName = "Building_" + std::to_string(building.id);
                    auto buildingSprite = dynamic_cast<BuildingSprite*>(mapLayer->getChildByName(spriteName));

                    if (buildingSprite) {
                        auto defenseAnim = dynamic_cast<DefenseBuildingAnimation*>(
                            buildingSprite->getChildByName("DefenseAnim")
                            );

                        if (defenseAnim) {
                            // ✅ 核心修复：转换到 MapLayer 坐标系（而非世界坐标）
                            // 1. 获取兵种在 TroopLayer 中的位置
                            Vec2 unitPosInTroopLayer = currentTarget->getPosition();

                            // 2. 转换到 MapLayer 坐标系
                            Vec2 targetPosInMapLayer = troopLayer->convertToNodeSpace(
                                troopLayer->getParent()->convertToWorldSpace(unitPosInTroopLayer)
                            );

                            // 更简洁的写法（推荐）
                            // Vec2 targetPosInMapLayer = mapLayer->convertToNodeSpace(
                            //     troopLayer->convertToWorldSpace(currentTarget->getPosition())
                            // );

                            CCLOG("BattleProcessController: Aiming at target - Unit pos in TroopLayer: (%.1f, %.1f), Target pos in MapLayer: (%.1f, %.1f)",
                                  unitPosInTroopLayer.x, unitPosInTroopLayer.y,
                                  targetPosInMapLayer.x, targetPosInMapLayer.y);

                            defenseAnim->playAttackAnimation(targetPosInMapLayer);
                        }
                    }
                }

                building.attackCooldown = attackSpeed;

                // ✅ 修复：目标死亡，立即清除锁定并停止所有动作
                if (currentTarget->isDead()) {
                    // 1. 立即清除建筑的锁定引用
                    building.lockedTarget = nullptr;
                    targetedUnitsThisFrame.erase(currentTarget);

                    // 2. 恢复兵种颜色
                    currentTarget->setTargetedByBuilding(false);

                    // 3. 停止兵种所有正在进行的动作（包括攻击动画）
                    currentTarget->stopAllActions();

                    // 4. 播放死亡动画
                    currentTarget->playDeathAnimation([troopLayer, currentTarget]() {
                        troopLayer->removeUnit(currentTarget);
                    });

                    CCLOG("BattleProcessController: Unit killed, stopped all actions and playing death animation");
                }
            }
        }
    }

    // ========== 更新兵种锁定状态 ==========
    auto allUnits = troopLayer->getAllUnits();
    for (auto unit : allUnits) {
        if (!unit || unit->isDead()) continue;

        bool shouldBeTargeted = (targetedUnitsThisFrame.find(unit) != targetedUnitsThisFrame.end());
        if (unit->isTargetedByBuilding() != shouldBeTargeted) {
            unit->setTargetedByBuilding(shouldBeTargeted);
        }
    }
}

// ==========================================
// 战斗循环逻辑
// ==========================================

void BattleProcessController::startCombatLoop(BattleUnitSprite* unit, BattleTroopLayer* troopLayer) {
    if (!unit || !troopLayer) return;

    Vec2 unitPos = unit->getPosition();
    auto dm = VillageDataManager::getInstance();
    const BuildingInstance* target = findNearestBuilding(unitPos, unit->getUnitTypeID());

    if (!target) {
        unit->playIdleAnimation();
        return;
    }

    BuildingInstance* mutableTarget = dm->getBuildingById(target->id);
    if (!mutableTarget || mutableTarget->isDestroyed || mutableTarget->currentHP <= 0) {
        startUnitAI(unit, troopLayer);
        return;
    }

    auto config = BuildingConfig::getInstance()->getConfig(mutableTarget->type);
    if (!config) {
        startUnitAI(unit, troopLayer);
        return;
    }

    Vec2 unitGridPos = GridMapUtils::pixelToGrid(unitPos);
    int unitGridX = static_cast<int>(std::floor(unitGridPos.x));
    int unitGridY = static_cast<int>(std::floor(unitGridPos.y));

    int bX = mutableTarget->gridX;
    int bY = mutableTarget->gridY;
    int bW = config->gridWidth;
    int bH = config->gridHeight;

    CCLOG("--- startCombatLoop DEBUG ---");
    CCLOG("  Unit %s at grid(%d, %d)", unit->getUnitType().c_str(), unitGridX, unitGridY);
    CCLOG("  Target ID=%d Type=%d at grid(%d, %d), size(%d x %d)",
          mutableTarget->id, mutableTarget->type, bX, bY, bW, bH);

    // ========== 距离计算逻辑 ==========
    int gridDistX = 0;
    int gridDistY = 0;

    if (unitGridX < bX) {
        gridDistX = bX - unitGridX;
    } else if (unitGridX >= bX + bW) {
        gridDistX = unitGridX - (bX + bW - 1);
    }

    if (unitGridY < bY) {
        gridDistY = bY - unitGridY;
    } else if (unitGridY >= bY + bH) {
        gridDistY = unitGridY - (bY + bH - 1);
    }

    int gridDistance = std::max(gridDistX, gridDistY);
    int attackRangeGrid = getAttackRangeByUnitType(unit->getUnitTypeID());

    CCLOG("  Distance: X=%d, Y=%d, Max=%d, AttackRange=%d",
          gridDistX, gridDistY, gridDistance, attackRangeGrid);

    // 气球兵特殊判定
    if (unit->getUnitTypeID() == UnitTypeID::BALLOON) {
        Vec2 buildingCenter = GridMapUtils::gridToPixelCenter(
            bX + bW / 2,
            bY + bH / 2
        );
        float pixelDistance = unitPos.distance(buildingCenter);
        float maxAttackDistance = (std::max(bW, bH) + 1) * 32.0f;

        if (pixelDistance > maxAttackDistance) {
            CCLOG("  Balloon too far (%.1f > %.1f), restarting AI", pixelDistance, maxAttackDistance);
            startUnitAI(unit, troopLayer);
            return;
        }
    } else if (gridDistance > attackRangeGrid) {
        CCLOG("  Out of range (%d > %d), restarting AI", gridDistance, attackRangeGrid);
        startUnitAI(unit, troopLayer);
        return;
    }

    CCLOG("  ✓ In range, attacking target!");
    CCLOG("--- END startCombatLoop ---");

    // 攻击逻辑
    Vec2 buildingPos = GridMapUtils::gridToPixelCenter(mutableTarget->gridX, mutableTarget->gridY);
    int targetID = mutableTarget->id;

    unit->attackTowardPosition(buildingPos, [this, unit, troopLayer, targetID]() {
        executeAttack(unit, troopLayer, targetID, false,
                      [this, unit, troopLayer]() {
            startUnitAI(unit, troopLayer);
        },
                      [this, unit, troopLayer]() {
            startCombatLoop(unit, troopLayer);
        }
        );
    });
}

void BattleProcessController::startCombatLoopWithForcedTarget(BattleUnitSprite* unit, BattleTroopLayer* troopLayer, const BuildingInstance* forcedTarget) {
    if (!unit || !troopLayer || !forcedTarget) return;

    Vec2 unitPos = unit->getPosition();
    auto dm = VillageDataManager::getInstance();
    int targetID = forcedTarget->id;

    BuildingInstance* liveTarget = dm->getBuildingById(targetID);
    if (!liveTarget || liveTarget->isDestroyed || liveTarget->currentHP <= 0) {
        startUnitAI(unit, troopLayer);
        return;
    }

    // ✅ 持续检查：如果正在攻击城墙，随时检查是否有更好的路径
    if (liveTarget->type == 303 && shouldAbandonWallForBetterPath(unit, targetID)) {
        CCLOG("BattleProcessController: Found better path! Abandoning wall attack.");
        startUnitAI(unit, troopLayer);
        return;
    }

    auto config = BuildingConfig::getInstance()->getConfig(liveTarget->type);
    if (!config) {
        startUnitAI(unit, troopLayer);
        return;
    }

    Vec2 unitGridPos = GridMapUtils::pixelToGrid(unitPos);
    int unitGridX = static_cast<int>(std::floor(unitGridPos.x));
    int unitGridY = static_cast<int>(std::floor(unitGridPos.y));

    int bX = liveTarget->gridX;
    int bY = liveTarget->gridY;
    int bW = config->gridWidth;
    int bH = config->gridHeight;

    int gridDistX = 0;
    int gridDistY = 0;

    if (unitGridX < bX) {
        gridDistX = bX - unitGridX;
    } else if (unitGridX >= bX + bW) {
        gridDistX = unitGridX - (bX + bW) + 1;
    }

    if (unitGridY < bY) {
        gridDistY = bY - unitGridY;
    } else if (unitGridY >= bY + bH) {
        gridDistY = unitGridY - (bY + bH) + 1;
    }

    int gridDistance = std::max(gridDistX, gridDistY);
    int attackRangeGrid = getAttackRangeByUnitType(unit->getUnitTypeID());
    
    if (gridDistance > attackRangeGrid) {
        
        auto pathfinder = FindPathUtil::getInstance();
        std::vector<Vec2> pathToTarget = pathfinder->findPathToAttackBuilding(unitPos, *liveTarget, attackRangeGrid);
        
        if (!pathToTarget.empty()) {
            unit->followPath(pathToTarget, 100.0f, [this, unit, troopLayer, forcedTarget]() {
                startCombatLoopWithForcedTarget(unit, troopLayer, forcedTarget);
            });
        } else {
            Vec2 targetPos = GridMapUtils::gridToPixelCenter(liveTarget->gridX, liveTarget->gridY);
            std::vector<Vec2> directPath = { targetPos };
            unit->followPath(directPath, 100.0f, [this, unit, troopLayer, forcedTarget]() {
                startCombatLoopWithForcedTarget(unit, troopLayer, forcedTarget);
            });
        }
        return;
    }
    
    Vec2 targetPos = GridMapUtils::gridToPixelCenter(liveTarget->gridX, liveTarget->gridY);
    
    unit->attackTowardPosition(targetPos, [this, unit, troopLayer, targetID]() {
        executeAttack(unit, troopLayer, targetID, true,
            [this, unit, troopLayer]() {
                // 目标被摧毁，重新寻找目标
                startUnitAI(unit, troopLayer);
            },
            [this, unit, troopLayer, targetID]() {
                auto dm = VillageDataManager::getInstance();
                auto t = dm->getBuildingById(targetID);
                if (t && !t->isDestroyed && t->currentHP > 0) {
                    // ✅ 每次攻击后都检查是否有更好的路径
                    if (t->type == 303 && shouldAbandonWallForBetterPath(unit, targetID)) {
                        CCLOG("BattleProcessController: Better path found after attack! Switching target.");
                        startUnitAI(unit, troopLayer);
                    } else {
                        startCombatLoopWithForcedTarget(unit, troopLayer, t);
                    }
                } else {
                    startUnitAI(unit, troopLayer);
                }
            }
        );
    });
}

// ==========================================
// 炸弹兵自爆攻击逻辑
// ==========================================
void BattleProcessController::performWallBreakerSuicideAttack(
    BattleUnitSprite* unit,
    BuildingInstance* target,
    BattleTroopLayer* troopLayer,
    const std::function<void()>& onComplete
) {
    if (!unit || !target || !troopLayer) {
        if (onComplete) onComplete();
        return;
    }

    CCLOG("BattleProcessController: Wall Breaker suicide attack on building %d", target->id);

    // 1. 获取炸弹兵伤害值
    int damage = getDamageByUnitType(unit->getUnitTypeID());
    
    // ========== 对城墙造成10倍伤害 ==========
    if (target->type == 303) {  // 城墙
        damage *= 10;
        CCLOG("BattleProcessController: Wall Breaker deals 10x damage to wall!");
    }

    // 2. 对目标建筑造成伤害
    target->currentHP -= damage;

    CCLOG("BattleProcessController: Target HP: %d (damage: %d)", target->currentHP, damage);

    // 3. 检查目标是否被摧毁
    if (target->currentHP <= 0) {
        target->isDestroyed = true;
        target->currentHP = 0;

        // 更新寻路地图
        FindPathUtil::getInstance()->updatePathfindingMap();

        // 发送建筑摧毁事件
        Director::getInstance()->getEventDispatcher()->dispatchCustomEvent(
            "EVENT_BUILDING_DESTROYED",
            static_cast<void*>(target)
        );

        // 新增：更新摧毁进度
        updateDestructionProgress();
        CCLOG("BattleProcessController: Target destroyed!");
    }

    // 4. 播放爆炸特效
    auto explosion = ParticleExplosion::create();
    explosion->setPosition(unit->getPosition());
    explosion->setDuration(0.2f);
    explosion->setScale(0.3f);
    explosion->setAutoRemoveOnFinish(true);
    troopLayer->getParent()->addChild(explosion, 1000);

    // 5. 屏幕震动（可选）
    auto camera = Camera::getDefaultCamera();
    auto shake = Sequence::create(
        MoveBy::create(0.05f, Vec3(5, 0, 0)),
        MoveBy::create(0.05f, Vec3(-10, 0, 0)),
        MoveBy::create(0.05f, Vec3(5, 0, 0)),
        nullptr
    );
    camera->runAction(shake);

    // 6. 炸弹兵自杀
    unit->takeDamage(9999);

    // ✅ 恢复正常颜色（移除红色锁定状态）
    unit->setColor(Color3B::WHITE);

    // 7. 播放死亡动画并移除
    unit->playDeathAnimation([troopLayer, unit, onComplete]() {
        CCLOG("BattleProcessController: Wall Breaker death animation completed");

        // ✅ 在移除前生成墓碑
        Vec2 tombstonePos = unit->getPosition();
        UnitTypeID unitType = unit->getUnitTypeID();

        // 移除兵种
        troopLayer->removeUnit(unit);

        // 生成墓碑（会自动淡出）
        troopLayer->spawnTombstone(tombstonePos, unitType);

        if (onComplete) {
            onComplete();
        }
    });
}

// ==========================================
// 🆕 摧毁进度追踪系统实现
// ==========================================

void BattleProcessController::initDestructionTracking() {
    // 重置所有追踪变量
    _totalBuildingHP = 0;
    _currentStars = 0;
    _townHallDestroyed = false;
    _star50Awarded = false;
    _star100Awarded = false;

    // 计算总血量
    _totalBuildingHP = calculateTotalBuildingHP();

    CCLOG("========================================");
    CCLOG("BattleProcessController: Destruction tracking initialized");
    CCLOG("Total Building HP: %d (excluding walls and traps)", _totalBuildingHP);
    CCLOG("========================================");
}

int BattleProcessController::calculateTotalBuildingHP() {
    auto dataManager = VillageDataManager::getInstance();
    const auto& buildings = dataManager->getAllBuildings();

    int totalHP = 0;
    int buildingCount = 0;

    for (const auto& building : buildings) {
        // 跳过城墙（type == 303）
        if (building.type == 303) continue;

        // 跳过陷阱（type >= 400 && type < 500）
        if (building.type >= 400 && building.type < 500) continue;

        // 跳过未建造完成的建筑
        if (building.state != BuildingInstance::State::BUILT) continue;

        // 获取建筑配置
        auto config = BuildingConfig::getInstance()->getConfig(building.type);
        if (!config) continue;

        // 累加血量
        int maxHP = config->hitPoints;
        if (maxHP > 0) {
            totalHP += maxHP;
            buildingCount++;

            CCLOG("  [%d] Type=%d, HP=%d", building.id, building.type, maxHP);
        }
    }

    CCLOG("BattleProcessController: Total %d buildings tracked, Total HP=%d",
          buildingCount, totalHP);

    return totalHP;
}

void BattleProcessController::updateDestructionProgress() {
    // 如果总血量为0，说明没有初始化或没有建筑
    if (_totalBuildingHP <= 0) {
        CCLOG("BattleProcessController: Total HP is 0, skipping progress update");
        return;
    }

    auto dataManager = VillageDataManager::getInstance();
    const auto& buildings = dataManager->getAllBuildings();

    // 计算当前剩余血量
    int currentTotalHP = 0;
    bool townHallDestroyed = false;

    for (const auto& building : buildings) {
        // 使用与 calculateTotalBuildingHP 相同的过滤逻辑
        if (building.type == 303) continue;
        if (building.type >= 400 && building.type < 500) continue;
        if (building.state != BuildingInstance::State::BUILT) continue;

        // 检查大本营状态（type == 1）
        if (building.type == 1 && building.isDestroyed) {
            townHallDestroyed = true;
        }

        // 累加当前血量
        if (!building.isDestroyed && building.currentHP > 0) {
            currentTotalHP += building.currentHP;
        }
    }

    // 计算摧毁进度
    float progress = ((_totalBuildingHP - currentTotalHP) / (float)_totalBuildingHP) * 100.0f;

    // 限制在 0-100 范围内
    if (progress < 0.0f) progress = 0.0f;
    if (progress > 100.0f) progress = 100.0f;

    CCLOG("========================================");
    CCLOG("BattleProcessController: Progress Updated");
    CCLOG("  Total HP: %d", _totalBuildingHP);
    CCLOG("  Current HP: %d", currentTotalHP);
    CCLOG("  Destroyed HP: %d", _totalBuildingHP - currentTotalHP);
    CCLOG("  Progress: %.1f%%", progress);
    CCLOG("  Town Hall Destroyed: %s", townHallDestroyed ? "YES" : "NO");
    CCLOG("========================================");

    // ✅ 关键修复：先检查星级条件（此时 _townHallDestroyed 还是旧值）
    checkStarConditions(progress, townHallDestroyed);

    // ✅ 然后再更新大本营状态（放在检查之后）
    _townHallDestroyed = townHallDestroyed;

    // 发送进度更新事件
    DestructionProgressEventData eventData;
    eventData.progress = progress;
    eventData.stars = _currentStars;

    EventCustom event("EVENT_DESTRUCTION_PROGRESS_UPDATED");
    event.setUserData(&eventData);
    Director::getInstance()->getEventDispatcher()->dispatchEvent(&event);
}

void BattleProcessController::checkStarConditions(float progress, bool townHallDestroyed) {
    int oldStars = _currentStars;

    // ✅ 修复：三个条件独立累加，每个条件各加1颗星
    int newStars = 0;

    // ========== 第1颗星：摧毁进度 >= 50% ==========
    if (progress >= 50.0f) {
        newStars++;
        
        if (!_star50Awarded) {
            _star50Awarded = true;
            
            CCLOG("⭐⭐⭐ STAR AWARDED! ⭐⭐⭐");
            CCLOG("  Reason: 50%% Destruction");
            CCLOG("  Progress: %.1f%%", progress);

            // 发送星星获得事件
            StarAwardedEventData starData;
            starData.starIndex = 0;  // 第1颗星（索引0）
            starData.reason = "50%";

            EventCustom event("EVENT_STAR_AWARDED");
            event.setUserData(&starData);
            Director::getInstance()->getEventDispatcher()->dispatchEvent(&event);
        }
    }

    // ========== 第2颗星：大本营被摧毁 ==========
    if (townHallDestroyed) {
        newStars++;
        
        if (!_townHallDestroyed) {
            CCLOG("⭐⭐⭐ STAR AWARDED! ⭐⭐⭐");
            CCLOG("  Reason: Town Hall Destroyed");

            // 发送星星获得事件
            StarAwardedEventData starData;
            starData.starIndex = 1;  // 第2颗星（索引1）
            starData.reason = "townhall";

            EventCustom event("EVENT_STAR_AWARDED");
            event.setUserData(&starData);
            Director::getInstance()->getEventDispatcher()->dispatchEvent(&event);
        }
    }

    // ========== 第3颗星：摧毁进度 == 100% ==========
    if (progress >= 99.9f) {  // 使用 99.9 避免浮点误差
        newStars++;
        
        if (!_star100Awarded) {
            _star100Awarded = true;

            CCLOG("⭐⭐⭐ STAR AWARDED! ⭐⭐⭐");
            CCLOG("  Reason: 100%% Destruction");
            CCLOG("  Progress: %.1f%%", progress);

            // 发送星星获得事件
            StarAwardedEventData starData;
            starData.starIndex = 2;  // 第3颗星（索引2）
            starData.reason = "100%";

            EventCustom event("EVENT_STAR_AWARDED");
            event.setUserData(&starData);
            Director::getInstance()->getEventDispatcher()->dispatchEvent(&event);
        }
    }

    // 更新星数
    _currentStars = newStars;

    // 如果星数发生变化，输出日志
    if (_currentStars != oldStars) {
        CCLOG("BattleProcessController: Stars updated: %d -> %d", oldStars, _currentStars);
        CCLOG("  - 50%% progress: %s", progress >= 50.0f ? "YES" : "NO");
        CCLOG("  - Town Hall destroyed: %s", townHallDestroyed ? "YES" : "NO");
        CCLOG("  - 100%% progress: %s", progress >= 99.9f ? "YES" : "NO");
    }
}

float BattleProcessController::getDestructionProgress() {
    if (_totalBuildingHP <= 0) return 0.0f;

    auto dataManager = VillageDataManager::getInstance();
    const auto& buildings = dataManager->getAllBuildings();

    int currentTotalHP = 0;

    for (const auto& building : buildings) {
        if (building.type == 303) continue;
        if (building.type >= 400 && building.type < 500) continue;
        if (building.state != BuildingInstance::State::BUILT) continue;

        if (!building.isDestroyed && building.currentHP > 0) {
            currentTotalHP += building.currentHP;
        }
    }

    float progress = ((_totalBuildingHP - currentTotalHP) / (float)_totalBuildingHP) * 100.0f;

    if (progress < 0.0f) progress = 0.0f;
    if (progress > 100.0f) progress = 100.0f;

    return progress;
}

int BattleProcessController::getCurrentStars() {
    return _currentStars;
}

// ==========================================
// 陷阱系统实现
// ==========================================

bool BattleProcessController::isUnitInTrapRange(const BuildingInstance& trap, BattleUnitSprite* unit) {
    if (!unit || unit->isDead()) return false;
    
    // 获取兵种网格位置
    Vec2 unitGridPos = unit->getGridPosition();
    int unitGridX = static_cast<int>(std::floor(unitGridPos.x));
    int unitGridY = static_cast<int>(std::floor(unitGridPos.y));
    
    int trapX = trap.gridX;
    int trapY = trap.gridY;
    
    // 401: 炸弹 - 1x1 格子，只检查陷阱所在的格子
    if (trap.type == 401) {
        return (unitGridX == trapX && unitGridY == trapY);
    }
    // 404: 巨型炸弹 - 2x2 格子，检查陷阱所在的4个格子
    else if (trap.type == 404) {
        // 巨型炸弹占据 (trapX, trapY) 到 (trapX+1, trapY+1) 的范围
        return (unitGridX >= trapX && unitGridX <= trapX + 1 &&
                unitGridY >= trapY && unitGridY <= trapY + 1);
    }
    
    return false;
}

void BattleProcessController::updateTrapDetection(BattleTroopLayer* troopLayer) {
    if (!troopLayer) return;
    
    auto dataManager = VillageDataManager::getInstance();
    auto& buildings = const_cast<std::vector<BuildingInstance>&>(dataManager->getAllBuildings());
    float deltaTime = Director::getInstance()->getDeltaTime();
    
    auto allUnits = troopLayer->getAllUnits();
    if (allUnits.empty()) return;
    
    // 遍历所有陷阱
    for (auto& building : buildings) {
        // 只处理陷阱（401: 炸弹, 404: 巨型炸弹）
        if (building.type != 401 && building.type != 404) continue;
        
        // 跳过已摧毁或已触发的陷阱
        if (building.isDestroyed || building.currentHP <= 0) continue;
        
        int trapId = building.id;
        
        // 检查陷阱是否已经被触发（正在倒计时）
        if (_triggeredTraps.find(trapId) != _triggeredTraps.end()) {
            // 更新计时器
            _trapTimers[trapId] -= deltaTime;
            
            if (_trapTimers[trapId] <= 0.0f) {
                // 时间到，执行爆炸
                CCLOG("BattleProcessController: Trap %d exploding!", trapId);
                explodeTrap(&building, troopLayer);
                
                // 清除触发状态
                _triggeredTraps.erase(trapId);
                _trapTimers.erase(trapId);
            }
            continue;
        }
        
        // 检查是否有兵种进入陷阱范围
        for (auto unit : allUnits) {
            if (!unit || unit->isDead()) continue;
            
            // 气球兵是飞行单位，不会触发地面陷阱
            if (unit->getUnitTypeID() == UnitTypeID::BALLOON) continue;
            
            if (isUnitInTrapRange(building, unit)) {
                // 触发陷阱，开始0.5秒倒计时
                CCLOG("BattleProcessController: Trap %d (type=%d) triggered by unit at grid(%d, %d)!",
                      trapId, building.type,
                      static_cast<int>(unit->getGridPosition().x),
                      static_cast<int>(unit->getGridPosition().y));
                
                _triggeredTraps.insert(trapId);
                _trapTimers[trapId] = 0.5f;  // 0.5秒延迟
                
                // ✅ 新增：让陷阱显示出来
                auto mapLayer = troopLayer->getParent();
                if (mapLayer) {
                    std::string spriteName = "Building_" + std::to_string(trapId);
                    auto trapSprite = mapLayer->getChildByName(spriteName);
                    if (trapSprite) {
                        trapSprite->setVisible(true);
                        CCLOG("BattleProcessController: Trap %d now VISIBLE!", trapId);
                    }
                }
                
                break;  // 一个陷阱只能被触发一次
            }
        }
    }
}

void BattleProcessController::explodeTrap(BuildingInstance* trap, BattleTroopLayer* troopLayer) {
    if (!trap || !troopLayer) return;
    
    auto config = BuildingConfig::getInstance()->getConfig(trap->type);
    if (!config) return;
    
    int damage = config->damagePerSecond;  // 对于陷阱，这个字段存储爆炸伤害
    
    CCLOG("BattleProcessController: Trap %d (type=%d) exploding with %d damage!",
          trap->id, trap->type, damage);
    
    // 获取所有在范围内的兵种
    auto allUnits = troopLayer->getAllUnits();
    std::vector<BattleUnitSprite*> affectedUnits;
    
    for (auto unit : allUnits) {
        if (!unit || unit->isDead()) continue;
        
        // 气球兵是飞行单位，不会受到地面陷阱伤害
        if (unit->getUnitTypeID() == UnitTypeID::BALLOON) continue;
        
        if (isUnitInTrapRange(*trap, unit)) {
            affectedUnits.push_back(unit);
        }
    }
    
    CCLOG("BattleProcessController: %zu units affected by trap explosion", affectedUnits.size());
    
    // 对范围内的所有兵种造成伤害
    for (auto unit : affectedUnits) {
        unit->takeDamage(damage);
        CCLOG("BattleProcessController: Unit %s took %d damage from trap, HP: %d",
              unit->getUnitType().c_str(), damage, unit->getCurrentHP());
        
        // 检查是否死亡
        if (unit->isDead()) {
            unit->stopAllActions();
            unit->playDeathAnimation([troopLayer, unit]() {
                troopLayer->removeUnit(unit);
            });
        }
    }
    
    // 播放爆炸特效
    Vec2 trapPixelPos = GridMapUtils::gridToPixelCenter(trap->gridX, trap->gridY);
    
    // 对于巨型炸弹，爆炸位置在2x2的中心
    if (trap->type == 404) {
        trapPixelPos = GridMapUtils::gridToPixelCenter(trap->gridX, trap->gridY + 1);
    }
    
    auto explosion = ParticleExplosion::create();
    explosion->setPosition(trapPixelPos);
    explosion->setDuration(0.3f);
    
    // 巨型炸弹爆炸更大
    float scale = (trap->type == 404) ? 0.6f : 0.3f;
    explosion->setScale(scale);
    explosion->setAutoRemoveOnFinish(true);
    troopLayer->getParent()->addChild(explosion, 1000);
    
    // 标记陷阱为已摧毁（消失）
    trap->isDestroyed = true;
    trap->currentHP = 0;
    
    // 发送陷阱摧毁事件（用于隐藏精灵）
    Director::getInstance()->getEventDispatcher()->dispatchCustomEvent(
        "EVENT_BUILDING_DESTROYED",
        static_cast<void*>(trap)
    );
    
    CCLOG("BattleProcessController: Trap %d destroyed after explosion", trap->id);
}
