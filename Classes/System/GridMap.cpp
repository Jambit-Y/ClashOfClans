#include "GridMap.h"
#include <cmath> // 需要用到 floor 或 round

// 获取地图的原点偏移量
// 在 CoC 视角中，(0,0) 格子通常位于屏幕上方中间，或者整个地图菱形的顶点
cocos2d::Vec2 GridMap::getMapOriginOffset() {
    // 获取当前屏幕可见区域大小
    cocos2d::Size visibleSize = cocos2d::Director::getInstance()->getVisibleSize();

    // 我们把地图的顶点 (Grid 0,0) 放在屏幕宽度的中心，高度的 90% 处
    // 这样地图会呈金字塔状向下铺开
    return cocos2d::Vec2(visibleSize.width / 2, visibleSize.height * 0.9f);
}

// 核心公式 1：网格 -> 屏幕像素
// 输入：Grid(0, 0) -> 输出：屏幕顶端中心点
// 输入：Grid(1, 0) -> 输出：向右下移动半个格子宽，向下移动半个格子高
cocos2d::Vec2 GridMap::gridToPixel(int gridX, int gridY) {
    cocos2d::Vec2 origin = getMapOriginOffset();

    // 等距投影变换矩阵 (Isometric Projection)
    // ScreenX = OriginX + (GridX - GridY) * (TileWidth / 2)
    // ScreenY = OriginY - (GridX + GridY) * (TileHeight / 2)  <-- 注意这里是减号，因为Cocos Y轴向上，而物理上格子是往下延伸的

    float halfW = TILE_WIDTH / 2.0f;
    float halfH = TILE_HEIGHT / 2.0f;

    float screenX = origin.x + (gridX - gridY) * halfW;
    float screenY = origin.y - (gridX + gridY) * halfH;

    return cocos2d::Vec2(screenX, screenY);
}

// 核心公式 2：屏幕像素 -> 网格
// 这是上面的公式的逆运算（解二元一次方程组得到的）
cocos2d::Vec2 GridMap::pixelToGrid(cocos2d::Vec2 pixelPos) {
    cocos2d::Vec2 origin = getMapOriginOffset();

    // 1. 先计算相对于原点的偏移
    float diffX = pixelPos.x - origin.x;
    float diffY = origin.y - pixelPos.y; // 注意这里反过来减，因为 Y 轴相反

    float halfW = TILE_WIDTH / 2.0f;
    float halfH = TILE_HEIGHT / 2.0f;

    // 2. 逆向解方程
    // gridX = (diffX / halfW + diffY / halfH) / 2
    // gridY = (diffY / halfH - diffX / halfW) / 2

    // 使用 std::round 四舍五入，确保点击格子边缘时也能归属到最近的格子
    // 也可以用 floor，看你希望鼠标偏向哪边
    float gridX = (diffX / halfW + diffY / halfH) / 2.0f;
    float gridY = (diffY / halfH - diffX / halfW) / 2.0f;

    // 返回 float 类型的 Vec2，方便调用者判断是否点偏了
    // 实际使用时通常强转为 int
    return cocos2d::Vec2(std::round(gridX), std::round(gridY));
}