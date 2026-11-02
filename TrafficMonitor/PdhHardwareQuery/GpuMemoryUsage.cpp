#include "stdafx.h"
#include "GpuMemoryUsage.h"

#pragma comment(lib, "DXGI.lib")
#pragma comment(lib, "Pdh.lib")

//专用显存小于这个值时，认为没有独立显卡（例如只有核显），此时统计共享内存的占用量
#define MIN_DEDICATED_VRAM (256ull << 20)

CGpuMemoryUsage::CGpuMemoryUsage()
{
}

CGpuMemoryUsage::~CGpuMemoryUsage()
{
    if (m_query != nullptr)
        ::PdhCloseQuery(m_query);
}

void CGpuMemoryUsage::Reset()
{
    if (m_query != nullptr)
    {
        ::PdhCloseQuery(m_query);
        m_query = nullptr;
    }
    m_dedicated_counter = nullptr;
    m_shared_counter = nullptr;
    m_pdh_inited = false;
    m_init_tried = false;
    m_use_shared = false;
    m_luid_str.clear();
    m_total_bytes = 0;
}

bool CGpuMemoryUsage::IsLuidMatch(const wchar_t* instance_name, const std::wstring& luid)
{
    if (instance_name == nullptr || luid.empty())
        return false;
    return _wcsnicmp(instance_name, luid.c_str(), luid.length()) == 0;
}

bool CGpuMemoryUsage::InitPdh()
{
    if (m_pdh_inited)
        return true;

    if (::PdhOpenQuery(nullptr, 0, &m_query) != ERROR_SUCCESS)
    {
        m_query = nullptr;
        return false;
    }

    //注意：性能计数器的名称在中文系统下是本地化的，因此这里使用PdhAddEnglishCounter
    if (::PdhAddEnglishCounter(m_query, L"\\GPU Adapter Memory(*)\\Dedicated Usage", 0, &m_dedicated_counter) != ERROR_SUCCESS)
    {
        ::PdhCloseQuery(m_query);
        m_query = nullptr;
        m_dedicated_counter = nullptr;
        return false;
    }
    //共享内存的计数器在部分系统上可能不存在，获取失败不影响专用显存的统计
    if (::PdhAddEnglishCounter(m_query, L"\\GPU Adapter Memory(*)\\Shared Usage", 0, &m_shared_counter) != ERROR_SUCCESS)
        m_shared_counter = nullptr;

    //第一次收集数据后，计数器的实例名才可用
    ::PdhCollectQueryData(m_query);
    m_pdh_inited = true;
    return true;
}

void CGpuMemoryUsage::GetInstanceNames(HCOUNTER counter, std::vector<std::wstring>& names)
{
    names.clear();
    if (counter == nullptr)
        return;

    DWORD buffer_size = 0;
    DWORD item_count = 0;
    PDH_STATUS status = ::PdhGetFormattedCounterArray(counter, PDH_FMT_LARGE, &buffer_size, &item_count, nullptr);
    if (status != PDH_MORE_DATA || buffer_size == 0)
        return;

    std::vector<BYTE> buffer(buffer_size);
    auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM*>(buffer.data());
    status = ::PdhGetFormattedCounterArray(counter, PDH_FMT_LARGE, &buffer_size, &item_count, items);
    if (status != ERROR_SUCCESS)
        return;

    for (DWORD i = 0; i < item_count; i++)
    {
        if (items[i].szName != nullptr)
            names.push_back(items[i].szName);
    }
}

bool CGpuMemoryUsage::InitAdapter()
{
    Microsoft::WRL::ComPtr<IDXGIFactory1> p_factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&p_factory))) || p_factory.Get() == nullptr)
        return false;

    //性能计数器中已经存在的显卡LUID（实例名形如luid_0x00000000_0x00019d9c_phys_0）
    std::vector<std::wstring> instance_names;
    GetInstanceNames(m_dedicated_counter, instance_names);

    DXGI_ADAPTER_DESC1 best_desc{};
    bool found{ false };
    bool found_in_pdh{ false };
    for (UINT i = 0; ; i++)
    {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> p_adapter;
        if (p_factory->EnumAdapters1(i, &p_adapter) == DXGI_ERROR_NOT_FOUND)
            break;
        if (p_adapter.Get() == nullptr)
            continue;

        DXGI_ADAPTER_DESC1 adapter_desc{};
        if (FAILED(p_adapter->GetDesc1(&adapter_desc)))
            continue;
        //跳过软件适配器（虚拟显示器、远程桌面等）
        if ((adapter_desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0)
            continue;

        CString luid_str;
        luid_str.Format(L"luid_0x%08x_0x%08x", static_cast<unsigned int>(adapter_desc.AdapterLuid.HighPart), static_cast<unsigned int>(adapter_desc.AdapterLuid.LowPart));
        std::wstring luid{ luid_str.GetString() };
        //该显卡在性能计数器中是否有对应的实例
        bool in_pdh{ false };
        for (const auto& name : instance_names)
        {
            if (IsLuidMatch(name.c_str(), luid))
            {
                in_pdh = true;
                break;
            }
        }

        //优先选择在性能计数器中能找到、且专用显存最大的显卡，
        //这样在同时有核显和独显（以及由Mosaic产生的多个虚拟LUID）的电脑上能选到真正的独立显卡
        bool better{ false };
        if (!found)
            better = true;
        else if (in_pdh != found_in_pdh)
            better = in_pdh;        //能取到占用量的显卡优先
        else if (adapter_desc.DedicatedVideoMemory > best_desc.DedicatedVideoMemory)
            better = true;

        if (better)
        {
            best_desc = adapter_desc;
            m_luid_str = luid;
            found = true;
            found_in_pdh = in_pdh;
        }
    }

    if (!found)
        return false;

    //专用显存很小的显卡（例如核显）没有独立的显存，改为统计共享内存的占用量
    m_use_shared = (best_desc.DedicatedVideoMemory < MIN_DEDICATED_VRAM) && (m_shared_counter != nullptr);
    m_total_bytes = m_use_shared ? best_desc.SharedSystemMemory : best_desc.DedicatedVideoMemory;
    return m_total_bytes != 0;
}

bool CGpuMemoryUsage::Init()
{
    if (m_init_tried)
        return m_pdh_inited && !m_luid_str.empty() && m_total_bytes != 0;
    m_init_tried = true;

    //先打开性能计数器，枚举显卡时需要用它的实例名来筛选
    if (!InitPdh())
        return false;
    return InitAdapter();
}

bool CGpuMemoryUsage::GetInstanceValue(HCOUNTER counter, unsigned __int64& value)
{
    value = 0;
    if (counter == nullptr)
        return false;

    DWORD buffer_size = 0;
    DWORD item_count = 0;
    PDH_STATUS status = ::PdhGetFormattedCounterArray(counter, PDH_FMT_LARGE, &buffer_size, &item_count, nullptr);
    if (status != PDH_MORE_DATA || buffer_size == 0)
        return false;

    std::vector<BYTE> buffer(buffer_size);
    auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM*>(buffer.data());
    status = ::PdhGetFormattedCounterArray(counter, PDH_FMT_LARGE, &buffer_size, &item_count, items);
    if (status != ERROR_SUCCESS)
        return false;

    for (DWORD i = 0; i < item_count; i++)
    {
        if (items[i].FmtValue.CStatus != ERROR_SUCCESS)
            continue;
        if (IsLuidMatch(items[i].szName, m_luid_str))
        {
            value = static_cast<unsigned __int64>(items[i].FmtValue.largeValue);
            return true;
        }
    }
    return false;
}

bool CGpuMemoryUsage::GetGpuMemory(unsigned __int64& used_bytes, unsigned __int64& total_bytes)
{
    used_bytes = 0;
    total_bytes = 0;

    if (!Init())
        return false;

    //重新收集一次数据
    ::PdhCollectQueryData(m_query);

    unsigned __int64 value{};
    bool success{ false };
    HCOUNTER counter = m_use_shared ? m_shared_counter : m_dedicated_counter;
    success = GetInstanceValue(counter, value);
    if (!success && counter != m_dedicated_counter)
        success = GetInstanceValue(m_dedicated_counter, value);
    if (!success)
    {
        //获取失败时清除状态，下次重新枚举（例如显卡热插拔、驱动更新后LUID发生了变化）
        Reset();
        return false;
    }

    used_bytes = value;
    total_bytes = m_total_bytes;
    return true;
}
