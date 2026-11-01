#pragma once
#include <dxgi1_4.h>
#include <wrl/client.h>

//显存占用监控类
//使用DXGI（IDXGIAdapter3::QueryVideoMemoryInfo）获取显存的当前占用和预算。
//预算（Budget）是系统认为当前程序可用的显存上限，用它来计算占用百分比，
//与Windows任务管理器中“专用GPU内存”的统计方式一致。
//注意：IDXGIAdapter3要求Windows 10及以上版本，在更低版本的系统上会返回false。
class CGpuMemoryUsage
{
public:
    CGpuMemoryUsage();
    ~CGpuMemoryUsage();

    // 获取显存占用
    // used_bytes:  当前已使用的显存（单位：字节）
    // total_bytes: 显存总量（预算，单位：字节）
    // 返回值: true=成功, false=失败（系统不支持或获取失败）
    bool GetGpuMemory(unsigned __int64& used_bytes, unsigned __int64& total_bytes);

private:
    // 枚举显卡，选取显存预算最大的一个（通常是独立显卡）
    bool InitAdapter();
    // 清除已获取的显卡，以便下次重新枚举
    void ResetAdapter();

private:
    Microsoft::WRL::ComPtr<IDXGIAdapter3> m_adapter;
    bool m_init_tried{ false };
};
