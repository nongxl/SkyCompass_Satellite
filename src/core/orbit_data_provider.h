#pragma once
#include <Arduino.h>
#include <vector>
#include "orbit_record.h"
#include "recent_launch_item.h"

class OrbitDataProvider {
public:
    // 从缓存或网络加载指定 Catalog Number 卫星
    static bool loadByCatalogNumber(uint32_t catNum, OrbitRecord& record);
    
    // 下载 Recent Launches 并以 JSONL 形式流式保存，并在内存中流式建构 RecentLaunchItem 列表
    static bool downloadRecentLaunches(std::vector<RecentLaunchItem>& tempLaunches);
    
    // 从本地 JSONL 分页加载指定 batchId 下的卫星并填充 g_level3Objects 列表
    static bool loadLevel3ObjectsPage(const RecentLaunchItem& item, int page);
};
