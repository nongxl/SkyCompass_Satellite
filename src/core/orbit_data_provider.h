#pragma once
#include <Arduino.h>
#include <vector>
#include "orbit_record.h"
#include "recent_launch_item.h"

class OrbitDataProvider {
public:
    // 从缓存或网络加载指定 Catalog Number 卫星
    static bool loadByCatalogNumber(uint32_t catNum, OrbitRecord& record, bool forceRefresh = false, int* outHttpCode = nullptr);
    
    // 批量从网络加载多颗卫星 GP 数据（单次 HTTP 请求），结果按卫星分别缓存。
    // 返回成功获取的卫星数量（0 = 全部失败）。
    static int loadByCatalogNumbers(const std::vector<uint32_t>& catNums, std::vector<OrbitRecord>& records, int* outHttpCode = nullptr);
    
    // 下载 Recent Launches 并以 JSONL 形式流式保存，并在内存中流式建构 RecentLaunchItem 列表
    static bool downloadRecentLaunches(std::vector<RecentLaunchItem>& tempLaunches, int* outHttpCode = nullptr);
    
    // 从本地 JSONL 缓存文件加载 Recent Launches 并还原 RecentLaunchItem 列表
    static bool loadRecentLaunchesFromCache(std::vector<RecentLaunchItem>& tempLaunches);
    
    // 从本地 JSONL 分页加载指定 batchId 下的卫星并填充 g_level3Objects 列表
    static bool loadLevel3ObjectsPage(const RecentLaunchItem& item, int page);
};
