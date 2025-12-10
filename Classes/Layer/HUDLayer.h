#ifndef __HUDLAYER_H__
#define __HUDLAYER_H__

#include "cocos2d.h"
#include "ui/CocosGUI.h"

class HUDLayer : public cocos2d::Layer {
public:
  virtual bool init();
  CREATE_FUNC(HUDLayer);

  void updateResourceDisplay(int gold, int elixir);

  /**
     * @brief 显示建筑操作菜单
     * @param buildingId 选中的建筑唯一ID
     */
  void showBuildingActions(int buildingId);

  /** @brief 隐藏操作菜单 */
  void hideBuildingActions();

private:
  cocos2d::Label* _goldLabel;
  cocos2d::Label* _elixirLabel;

  //-- -底部菜单组件- --
  void initActionMenu();   // 初始化菜单UI
  void updateActionButtons(int buildingId); // 刷新按钮状态和数据

  cocos2d::Node* _actionMenuNode;       // 菜单总容器
  cocos2d::Label* _buildingNameLabel;   // 顶部显示的 "训练营 (6级)"

  // 三个核心按钮
  cocos2d::ui::Button* _btnInfo;
  cocos2d::ui::Button* _btnUpgrade;
  cocos2d::ui::Button* _btnTrain;

  // 升级按钮上的特殊标签
  cocos2d::Label* _upgradeCostLabel;    // 显示 "500 圣水"

  int _currentSelectedBuildingId;       // 当前选中的ID
};

#endif // __HUDLAYER_H__