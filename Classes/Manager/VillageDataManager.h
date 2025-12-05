#pragma once
#include "../Model/VillageData.h"

class VillageDataManager {
public:
  static VillageDataManager* getInstance();
  static void destroyInstance();

  // 资源接口
  int getGold() const;
  int getElixir() const;
  void addGold(int amount);
  void addElixir(int amount);
  bool spendGold(int amount);
  bool spendElixir(int amount);

  // 建筑接口
  const std::vector<BuildingInstance>& getAllBuildings() const;
  BuildingInstance* getBuildingById(int id);
  int addBuilding(int type, int level, int gridX, int gridY, BuildingInstance::State state, long long finishTime = 0);
  void upgradeBuilding(int id, int newLevel, long long finishTime);
  void setBuildingPosition(int id, int gridX, int gridY);
  void setBuildingState(int id, BuildingInstance::State state, long long finishTime = 0);

  // 存档/读档
  void loadFromFile(const std::string& filename);
  void saveToFile(const std::string& filename);

private:
  VillageDataManager();
  ~VillageDataManager();
  static VillageDataManager* _instance;
  VillageData _data;
  int _nextBuildingId;
};