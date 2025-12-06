#include "VillageDataManager.h"
#include "cocos2d.h"
#include <ctime>
#include <algorithm>

USING_NS_CC;

// 静态成员初始化
VillageDataManager* VillageDataManager::_instance = nullptr;

VillageDataManager::VillageDataManager()
  : _nextBuildingId(1) {
  // 初始化默认资源
  _data.gold = 1000;
  _data.elixir = 1000;

  CCLOG("VillageDataManager created");
}

VillageDataManager::~VillageDataManager() {
  CCLOG("VillageDataManager destroyed");
}

#pragma region 单例管理
VillageDataManager* VillageDataManager::getInstance() {
  if (!_instance) {
    _instance = new VillageDataManager();
  }
  return _instance;
}

void VillageDataManager::destroyInstance() {
  if (_instance) {
    delete _instance;
    _instance = nullptr;
  }
}
#pragma endregion

#pragma region 资源接口
int VillageDataManager::getGold() const {
  return _data.gold;
}

int VillageDataManager::getElixir() const {
  return _data.elixir;
}

void VillageDataManager::addGold(int amount) {
  if (amount <= 0) return;
  _data.gold += amount;
  // 触发资源变化事件
  notifyResourceChanged();
  CCLOG("Gold added: +%d (Total: %d)", amount, _data.gold);
}

void VillageDataManager::addElixir(int amount) {
  if (amount <= 0) return;
  _data.elixir += amount;
  // 触发资源变化事件
  notifyResourceChanged();
  CCLOG("Elixir added: +%d (Total: %d)", amount, _data.elixir);
}

bool VillageDataManager::spendGold(int amount) {
  if (amount <= 0) return false;
  if (_data.gold < amount) {
    CCLOG("Not enough gold! Need: %d, Have: %d", amount, _data.gold);
    return false;
  }
  _data.gold -= amount;
  // 触发资源变化事件
  notifyResourceChanged();
  CCLOG("Gold spent: -%d (Remaining: %d)", amount, _data.gold);
  return true;
}

bool VillageDataManager::spendElixir(int amount) {
  if (amount <= 0) return false;
  if (_data.elixir < amount) {
    CCLOG("Not enough elixir! Need: %d, Have: %d", amount, _data.elixir);
    return false;
  }
  _data.elixir -= amount;
  // 触发资源变化事件
  notifyResourceChanged();
  CCLOG("Elixir spent: -%d (Remaining: %d)", amount, _data.elixir);
  return true;
}
// 通知资源变化
void VillageDataManager::notifyResourceChanged() {
  cocos2d::EventCustom event("EVENT_RESOURCE_CHANGED");
  cocos2d::Director::getInstance()->getEventDispatcher()->dispatchEvent(&event);
}
#pragma endregion

#pragma region 建筑接口
const std::vector<BuildingInstance>& VillageDataManager::getAllBuildings() const {
  return _data.buildings;
}

BuildingInstance* VillageDataManager::getBuildingById(int id) {
  auto it = std::find_if(_data.buildings.begin(), _data.buildings.end(),
                         [id](const BuildingInstance& building) {
    return building.id == id;
  });

  if (it != _data.buildings.end()) {
    return &(*it);
  }
  return nullptr;
}

int VillageDataManager::addBuilding(int type, int level, int gridX, int gridY,
                                    BuildingInstance::State state, long long finishTime) {
  BuildingInstance newBuilding;
  newBuilding.id = _nextBuildingId++;
  newBuilding.type = type;
  newBuilding.level = level;
  newBuilding.gridX = gridX;
  newBuilding.gridY = gridY;
  newBuilding.state = state;
  newBuilding.finishTime = finishTime;

  _data.buildings.push_back(newBuilding);

  CCLOG("Building added: ID=%d, Type=%d, Level=%d, Pos=(%d,%d), State=%d",
        newBuilding.id, type, level, gridX, gridY, (int)state);

  return newBuilding.id;
}

void VillageDataManager::upgradeBuilding(int id, int newLevel, long long finishTime) {
  BuildingInstance* building = getBuildingById(id);
  if (!building) {
    CCLOG("Error: Building ID %d not found!", id);
    return;
  }

  building->level = newLevel;
  building->state = BuildingInstance::CONSTRUCTING;
  building->finishTime = finishTime;

  CCLOG("Building upgraded: ID=%d, NewLevel=%d, FinishTime=%lld",
        id, newLevel, finishTime);
}

void VillageDataManager::setBuildingPosition(int id, int gridX, int gridY) {
  BuildingInstance* building = getBuildingById(id);
  if (!building) {
    CCLOG("Error: Building ID %d not found!", id);
    return;
  }

  building->gridX = gridX;
  building->gridY = gridY;

  CCLOG("Building moved: ID=%d, NewPos=(%d,%d)", id, gridX, gridY);
}

void VillageDataManager::setBuildingState(int id, BuildingInstance::State state, long long finishTime) {
  BuildingInstance* building = getBuildingById(id);
  if (!building) {
    CCLOG("Error: Building ID %d not found!", id);
    return;
  }

  building->state = state;
  building->finishTime = finishTime;

  CCLOG("Building state changed: ID=%d, State=%d, FinishTime=%lld",
        id, (int)state, finishTime);
}
#pragma endregion

#pragma region 存档/读档
void VillageDataManager::loadFromFile(const std::string& filename) {
  // TODO: 实现从文件加载数据
  // 可以使用 Cocos2d-x 的 FileUtils 和 JSON 解析
  CCLOG("Loading from file: %s (Not implemented yet)", filename.c_str());
}

void VillageDataManager::saveToFile(const std::string& filename) {
  // TODO: 实现保存数据到文件
  // 可以使用 Cocos2d-x 的 FileUtils 和 JSON 序列化
  CCLOG("Saving to file: %s (Not implemented yet)", filename.c_str());
}
#pragma endregion