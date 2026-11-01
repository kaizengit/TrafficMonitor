#include "stdafx.h"
#include "GpuMemoryUsage.h"

#pragma comment(lib, "DXGI.lib")

CGpuMemoryUsage::CGpuMemoryUsage()
{
}

CGpuMemoryUsage::~CGpuMemoryUsage()
{
}

void CGpuMemoryUsage::ResetAdapter()
{
    m_adapter.Reset();
    m_init_tried = false;
}

bool CGpuMemoryUsage::InitAdapter()
{
    if (m_init_tried)
        return m_adapter.Get() != nullptr;
    m_init_tried = true;

    //创建DXGI工厂并枚举所有显卡
    Microsoft::WRL::ComPtr<IDXGIFactory1> p_factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&p_factory))) || p_factory.Get() == nullptr)
        return false;

    unsigned __int64 max_budget = 0;
    for (UINT i = 0; ; ++i)
    {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> p_adapter1;
        if (p_factory->EnumAdapters1(i, &p_adapter1) == DXGI_ERROR_NOT_FOUND)
            break;
        if (p_adapter1.Get() == nullptr)
            continue;

        //跳过软件适配器（虚拟显示器、远程桌面等）
        DXGI_ADAPTER_DESC1 adapte_desc{};
        if (FAILED(p_adapter1->GetDesc1(&adapte_desc)))
            continue;
        if ((adapte_desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0)
            continue;

        //IDXGIAdapter3只在Windows 10及以上版本中可用，在更低版本中此处会失败
        Microsoft::WRL::ComPtr<IDXGIAdapter3> p_adapter3;
        if (FAILED(p_adapter1.As(&p_adapter3)) || p_adapter3.Get() == nullptr)
            continue;

        DXGI_QUERY_VIDEO_MEMORY_INFO memory_info{};
        if (FAILED(p_adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &memory_info)))
            continue;

        //选取显存预算最大的显卡，这样在同时有核显和独显的电脑上会取到独显
        if (memory_info.Budget > max_budget)
        {
            max_budget = memory_info.Budget;
            m_adapter = p_adapter3;
        }
    }

    return m_adapter.Get() != nullptr;
}

bool CGpuMemoryUsage::GetGpuMemory(unsigned __int64& used_bytes, unsigned __int64& total_bytes)
{
    used_bytes = 0;
    total_bytes = 0;

    if (!InitAdapter() || m_adapter.Get() == nullptr)
        return false;

    DXGI_QUERY_VIDEO_MEMORY_INFO memory_info{};
    if (FAILED(m_adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &memory_info)))
    {
        //获取失败时清除已保存的显卡，下次重新枚举（例如显卡热插拔后）
        ResetAdapter();
        return false;
    }

    if (memory_info.Budget == 0)
        return false;

    used_bytes = memory_info.CurrentUsage;
    total_bytes = memory_info.Budget;
    return true;
}
