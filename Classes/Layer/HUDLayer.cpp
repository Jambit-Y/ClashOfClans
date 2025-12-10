#pragma execution_character_set("utf-8")
#include "HUDLayer.h"
#include "Layer/ShopLayer.h"
#include "Manager/VillageDataManager.h"
#include "Model/BuildingConfig.h"

USING_NS_CC;
using namespace ui;

// 字体路径常量
const std::string FONT_PATH = "fonts/simhei.ttf";

bool HUDLayer::init() {
  if (!Layer::init()) {
    return false;
  }
  // 获取单例实例
  auto dataManager = VillageDataManager::getInstance();

  // 获取资源
  int gold = dataManager->getGold();
  int elixir = dataManager->getElixir();

  auto visibleSize = Director::getInstance()->getVisibleSize();
  Vec2 origin = Director::getInstance()->getVisibleOrigin();

  // 1. 创建金币标签 (左上角)
  _goldLabel = Label::createWithTTF("Gold: 0", "fonts/Marker Felt.ttf", 24); // 假设fonts/Marker Felt.ttf存在
  _goldLabel->setPosition(Vec2(origin.x + 100, origin.y + visibleSize.height - 30));
  this->addChild(_goldLabel);

  // 2. 创建圣水标签 (金币标签旁边)
  _elixirLabel = Label::createWithTTF("Elixir: 0", "fonts/Marker Felt.ttf", 24);
  _elixirLabel->setPosition(Vec2(origin.x + 300, origin.y + visibleSize.height - 30));
  this->addChild(_elixirLabel);

  // 初始更新显示
  updateResourceDisplay(gold, elixir);

  // 监听资源变化事件
  auto resource_update_listener = EventListenerCustom::create("EVENT_RESOURCE_CHANGED",
                                              [this](EventCustom* event) {
    // 当接收到资源变化事件时，重新获取最新数据
    auto dataManager = VillageDataManager::getInstance();
    int gold = dataManager->getGold();
    int elixir = dataManager->getElixir();

    // 更新显示
    this->updateResourceDisplay(gold, elixir);

    CCLOG("HUD 接收到资源变化通知，已更新：Gold=%d, Elixir=%d", gold, elixir);
  });
  // 将监听器添加到事件分发器
  _eventDispatcher->addEventListenerWithSceneGraphPriority(resource_update_listener, this);

  // 商店入口按钮开始
  // 1. 创建按钮
  auto shopBtn = ui::Button::create("UI/Shop/Shop-button.png");
  // 2. 设置位置 (左下角)
  shopBtn->setAnchorPoint(Vec2(1, 0)); // 锚点设为左下角
  shopBtn->setPosition(Vec2(origin.x + visibleSize.width - 20, origin.y + 20));  // 距离左边20，距离下边20
  // 3. 点击回调 
  shopBtn->addClickEventListener([=](Ref* sender) {
      // 调试日志
      CCLOG("Member C: 点击了商店图标，准备打开商店...");
      // 创建在 ShopLayer.cpp 里写的那个界面
      auto shopLayer = ShopLayer::create();
      // 将商店层添加到当前的 Scene (场景) 中
      // zOrder 设为 100，确保它盖在所有东西上面
      this->getScene()->addChild(shopLayer, 100);
      });
  // 4. 加到 HUD 层上
  this->addChild(shopBtn);


  // 初始化底部建筑操作菜单 
  initActionMenu();

  
  // 进攻按钮
  auto battleBtn = ui::Button::create("UI/battle/battle-icon/battle-icon.png");

  if (battleBtn) {
      battleBtn->setAnchorPoint(Vec2(0, 0));
      battleBtn->setPosition(Vec2(origin.x + 20, origin.y + 20));
      battleBtn->setScale(0.8f);

      battleBtn->addClickEventListener([=](Ref* sender) {
          CCLOG("点击了进攻按钮！TODO: 进入战斗场景");
          });

      this->addChild(battleBtn);
  }
  else {
      CCLOG("错误：无法加载进攻按钮图片，请检查路径 UI/battle/battle-icon/battle-icon.png");
  }

  return true;
}

void HUDLayer::updateResourceDisplay(int gold, int elixir) {
  // 更新标签显示
  _goldLabel->setString(StringUtils::format("Gold: %d", gold));
  _elixirLabel->setString(StringUtils::format("Elixir: %d", elixir));
}

// --- 新增：初始化菜单布局 ---
void HUDLayer::initActionMenu() {
    auto visibleSize = Director::getInstance()->getVisibleSize();

    // 1. 创建容器节点，位置在屏幕底部中心
    _actionMenuNode = Node::create();
    _actionMenuNode->setPosition(Vec2(visibleSize.width / 2, 100)); // Y=100 稍微留点底边距
    _actionMenuNode->setVisible(false); // 默认隐藏
    this->addChild(_actionMenuNode, 20); // Z=20 确保在最上层

    // 2. 顶部建筑名称 (例如 "训练营 (6级)")
    _buildingNameLabel = Label::createWithTTF("建筑名称", FONT_PATH, 24);
    _buildingNameLabel->enableOutline(Color4B::BLACK, 2);
    _buildingNameLabel->setPosition(Vec2(0, 110)); // 在按钮上方
    _actionMenuNode->addChild(_buildingNameLabel);

    // 按钮通用配置
    float btnSize = 140.0f; // 统一按钮大小
    float spacing = 20.0f;  // 间距
    std::string imgPath = "UI/training-camp/building-icon/"; // 图片目录前缀

    // ================= [信息按钮] =================
    _btnInfo = Button::create(imgPath + "info.png");
    _btnInfo->ignoreContentAdaptWithSize(false); // 允许缩放
    _btnInfo->setContentSize(Size(btnSize, btnSize));
    _btnInfo->setPosition(Vec2(-(btnSize + spacing), 0)); // 放左边
    _btnInfo->addClickEventListener([=](Ref*) {
        CCLOG("点击了信息");
        });
    _actionMenuNode->addChild(_btnInfo);

    // ================= [升级按钮] =================
    _btnUpgrade = Button::create(imgPath + "upgrade.png");
    _btnUpgrade->ignoreContentAdaptWithSize(false);
    _btnUpgrade->setContentSize(Size(btnSize, btnSize));
    _btnUpgrade->setPosition(Vec2(0, 0)); // 放中间
    _btnUpgrade->addClickEventListener([=](Ref*) {
        CCLOG("点击了升级");
        // TODO: 调用升级逻辑
        });
    _actionMenuNode->addChild(_btnUpgrade);

    // --- 升级费用标签 (紫色字体) ---
    // 位置：放在按钮图片的顶部区域，避免遮挡中间的锤子
    _upgradeCostLabel = Label::createWithTTF("270000 圣水", FONT_PATH, 20);
    _upgradeCostLabel->setColor(Color3B::MAGENTA); // 紫色/洋红色
    _upgradeCostLabel->enableOutline(Color4B::BLACK, 2); // 加黑边保证清晰
    // 锚点设为(0.5, 1) -> 顶部中心对齐
    _upgradeCostLabel->setAnchorPoint(Vec2(0.5f, 1.0f));
    // 坐标：X居中，Y在按钮顶部稍微往下一点
    _upgradeCostLabel->setPosition(Vec2(btnSize / 2, btnSize - 10));
    _btnUpgrade->addChild(_upgradeCostLabel); // 加到按钮上，随按钮移动

    // ================= [训练按钮] =================
    _btnTrain = Button::create(imgPath + "training.png");
    _btnTrain->ignoreContentAdaptWithSize(false);
    _btnTrain->setContentSize(Size(btnSize, btnSize));
    _btnTrain->setPosition(Vec2(btnSize + spacing, 0)); // 放右边
    _btnTrain->addClickEventListener([=](Ref*) {
        CCLOG("点击了训练部队");
        // TODO: 弹出训练界面
        });
    _actionMenuNode->addChild(_btnTrain);
}

void HUDLayer::showBuildingActions(int buildingId) {
    if (_currentSelectedBuildingId == buildingId && _actionMenuNode->isVisible()) {
        return; // 已经是这个建筑了，不用刷新
    }

    _currentSelectedBuildingId = buildingId;

    // 刷新数据
    updateActionButtons(buildingId);

    // 显示菜单 (简单的弹入动画)
    _actionMenuNode->setVisible(true);
    _actionMenuNode->setScale(0.1f);
    _actionMenuNode->runAction(EaseBackOut::create(ScaleTo::create(0.2f, 1.0f)));
}

void HUDLayer::hideBuildingActions() {
    if (_actionMenuNode->isVisible()) {
        _actionMenuNode->setVisible(false);
        _currentSelectedBuildingId = -1;
    }
}

void HUDLayer::updateActionButtons(int buildingId) {
    auto dataManager = VillageDataManager::getInstance();
    auto buildingInstance = dataManager->getBuildingById(buildingId);
    if (!buildingInstance) return;

    auto configMgr = BuildingConfig::getInstance();
    auto config = configMgr->getConfig(buildingInstance->type);
    if (!config) return;

    // 1. 更新标题: "名称 (等级)"
    std::string title = config->name + " (" + std::to_string(buildingInstance->level) + "级)";
    _buildingNameLabel->setString(title);

    // 2. 更新升级费用
    // 逻辑：获取下一级的费用。如果满级了，显示“已满级”
    if (configMgr->canUpgrade(buildingInstance->type, buildingInstance->level)) {
        int cost = configMgr->getUpgradeCost(buildingInstance->type, buildingInstance->level);
        _upgradeCostLabel->setString(std::to_string(cost) + " 圣水");
        _upgradeCostLabel->setVisible(true);
        _btnUpgrade->setTouchEnabled(true);
        _btnUpgrade->setOpacity(255);
    }
    else {
        _upgradeCostLabel->setString("已满级");
        _btnUpgrade->setTouchEnabled(false); // 满级不能点
        _btnUpgrade->setOpacity(128); // 变灰
    }

    // 3. 控制“训练”按钮的显示
    // 只有 兵营(101) 和 训练营(102) 显示训练按钮
    // 根据你的具体Type ID调整
    bool canTrain = (buildingInstance->type == 101 || buildingInstance->type == 102);

    _btnTrain->setVisible(canTrain);

    // 4. 动态调整位置
    // 如果没有训练按钮，另外两个按钮居中显示
    float btnSize = 140.0f;
    float spacing = 20.0f;

    if (canTrain) {
        // 三个按钮布局
        _btnInfo->setPosition(Vec2(-(btnSize + spacing), 0));
        _btnUpgrade->setPosition(Vec2(0, 0));
        _btnTrain->setPosition(Vec2(btnSize + spacing, 0));
    }
    else {
        // 两个按钮布局 (信息 | 升级)
        _btnInfo->setPosition(Vec2(-(btnSize / 2 + spacing / 2), 0));
        _btnUpgrade->setPosition(Vec2(btnSize / 2 + spacing / 2, 0));
    }
}