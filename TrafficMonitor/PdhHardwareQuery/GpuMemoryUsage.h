#pragma once
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <Pdh.h>
#include <PdhMsg.h>

//显存占用监控类
//说明：
//1. 显存总容量通过DXGI的IDXGIAdapter1::GetDesc1获取（DedicatedVideoMemory，即专用显存）；
//2. 显存占用量不能使用IDXGIAdapter3::QueryVideoMemoryInfo，因为它返回的CurrentUsage
//   是“当前进程”占用的显存，而本程序几乎不使用显存，所以这个值总是接近0。
//   因此这里改用性能计数器“\GPU Adapter Memory(*)\Dedicated Usage”获取整个系统的显存占用量，
//   该计数器的实例名中包含了显卡的LUID，通过LUID即可把计数器和DXGI枚举到的显卡对应起来。
//   这与Windows任务管理器中“专用GPU内存”的统计方式一致。
class CGpuMemoryUsage
{
public:
    CGpuMemoryUsage();
    ~CGpuMemoryUsage();

    // 获取显存占用
    // used_bytes:  整个系统已使用的显存（单位：字节）
    // total_bytes: 显存总量（单位：字节）
    // 返回值: true=成功, false=失败（性能计数器首次读取需要预热，此时可能返回false）
    bool GetGpuMemory(unsigned __int64& used_bytes, unsigned __int64& total_bytes);

    // 重置状态，下次调用GetGpuMemory时重新枚举显卡和性能计数器
    void Reset();

private:
    bool Init();                                                //初始化（打开性能计数器并枚举显卡）
    bool InitPdh();                                             //打开性能计数器查询
    bool InitAdapter();                                         //通过DXGI枚举显卡，选择要统计的显卡
    bool GetInstanceValue(HCOUNTER counter, unsigned __int64& value);   //获取所选显卡对应的计数器值
    void GetInstanceNames(HCOUNTER counter, std::vector<std::wstring>& names);
    static bool IsLuidMatch(const wchar_t* instance_name, const std::wstring& luid);

private:
    HQUERY m_query{};                   //性能计数器查询句柄
    HCOUNTER m_dedicated_counter{};     //专用显存占用量计数器
    HCOUNTER m_shared_counter{};        //共享内存占用量计数器
    bool m_pdh_inited{};                //性能计数器是否已初始化
    bool m_init_tried{};                //是否已经尝试过初始化
    bool m_use_shared{};                //没有独立显存（例如只有核显）时改为统计共享内存
    std::wstring m_luid_str;            //所选显卡的LUID字符串（用于匹配计数器实例名）
    unsigned __int64 m_total_bytes{};   //显存总容量
};
