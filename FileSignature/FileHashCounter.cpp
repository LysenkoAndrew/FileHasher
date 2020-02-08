#include "FileHashCounter.h"

/// compute CRC32 (half-byte algoritm)
uint32_t crc32_halfbyte(const void* data, size_t length, uint32_t previousCrc32)
{
    uint32_t crc = ~previousCrc32; // same as previousCrc32 ^ 0xFFFFFFFF
    const uint8_t* current = (const uint8_t*)data;

    /// look-up table for half-byte, same as crc32Lookup[0][16*i]
    static const uint32_t Crc32Lookup16[16] =
    {
      0x00000000,0x1DB71064,0x3B6E20C8,0x26D930AC,0x76DC4190,0x6B6B51F4,0x4DB26158,0x5005713C,
      0xEDB88320,0xF00F9344,0xD6D6A3E8,0xCB61B38C,0x9B64C2B0,0x86D3D2D4,0xA00AE278,0xBDBDF21C
    };

    while (length-- != 0)
    {
        crc = Crc32Lookup16[(crc ^ *current) & 0x0F] ^ (crc >> 4);
        crc = Crc32Lookup16[(crc ^ (*current >> 4)) & 0x0F] ^ (crc >> 4);
        current++;
    }

    return ~crc; // same as crc ^ 0xFFFFFFFF
}

CThreadFileHasher::CThreadFileHasher(const std::string& inputFilePath, const std::string& outputFilePath, ull blockSize)
    : m_nMaxThread(std::thread::hardware_concurrency()), m_nHashListCheckQuantity(m_nMaxThread * 2), m_nCurrThread(0), m_nextId(0), m_blockSize(blockSize),
    m_isDone(m_nMaxThread), m_buffers(m_nMaxThread), m_threads(m_nMaxThread)
{
    for (auto& item : m_isDone)
    {
        item.store(true);
    }
    m_bError.store(false);
    m_inFile.open(inputFilePath);
    m_outFile.open(outputFilePath);
}

CThreadFileHasher::~CThreadFileHasher()
{
    if (m_inFile.is_open())
    {
        m_inFile.close();
    }
    if (m_outFile.is_open())
    {
        m_outFile.close();
    }
}

void CThreadFileHasher::CalculateHashThread(int num_of_thread, ull len, ull id)
{
    if (auto pBuf = m_buffers[num_of_thread].get())
    {
        try
        {
            auto crc = crc32_halfbyte(pBuf, len, 0);
            WriteHash(id, crc);
            m_isDone[num_of_thread].store(true);
        }
        catch (std::bad_alloc& /*ex*/)
        {
            m_LastErrorStr = "Cannot allocate memory.";
            m_bError.store(true);
        }
    }
}

void CThreadFileHasher::CalculateMultiThread(ull id, ull len)
{
    try
    {
        auto ptr = std::make_unique<char[]>(len);
        m_inFile.read(ptr.get(), len);
        while (true)
        {
            m_nCurrThread++;
            if (m_nCurrThread == m_nMaxThread)
            {
                m_nCurrThread = 0;
            }
            if (m_isDone[m_nCurrThread].load())
            {
                if (m_threads[m_nCurrThread].joinable())
                {
                    m_threads[m_nCurrThread].join();
                }
                m_isDone[m_nCurrThread].store(false);
                m_buffers[m_nCurrThread] = std::move(ptr);
                m_threads[m_nCurrThread] = std::thread(&CThreadFileHasher::CalculateHashThread, this, m_nCurrThread, len, id);
                break;
            }
        }
    }
    catch (std::bad_alloc& /*ex*/)
    {
        throw std::length_error("Cannot allocate memory. Try to reduce the block size.");
    }
}

void CThreadFileHasher::WriteHash(ull id, ull hash)
{
    std::lock_guard<std::mutex> lock(m_mtx);
    if (id == m_nextId)
    {
        m_outFile << std::hex << hash << std::endl;
        m_nextId++;
        return;
    }
    else
    {
        m_HashList.emplace_back(id, hash);

        if (m_HashList.size() > m_nHashListCheckQuantity)
        {
            m_HashList.sort();
        }

        // Check first N values in list
        unsigned i = 0;
        for (auto ptr = m_HashList.cbegin(); ptr != m_HashList.cend() && i < m_nHashListCheckQuantity; ++i)
        {
            if (ptr->first == m_nextId)
            {
                m_outFile << std::hex << ptr->second << std::endl;
                ptr = m_HashList.erase(ptr);
                m_nextId++;
            }
            else
            {
                ++ptr;
            }
        }
    }
}

void CThreadFileHasher::CalculateCurrentThread(ull id, ull len)
{
    try
    {
        auto ptr = std::make_unique<char[]>(len);
        m_inFile.read(ptr.get(), len);
        if (const char* pBuf = ptr.get())
        {
            auto crc = crc32_halfbyte(pBuf, len, 0);
            WriteHash(id, crc);
        }
    }
    catch (std::bad_alloc& /*ex*/)
    {
        throw std::length_error("Ñannot allocate memory. Try to reduce the block size.");
    }
}

bool CThreadFileHasher::CalculateHash()
{
    bool bResult = false;
    if (!m_outFile.is_open())
    {
        m_LastErrorStr = "Cannot open output file.";
    }
    else if (!m_inFile.is_open())
    {
        m_LastErrorStr = "Cannot open input file.";
    }
    else
    {
        m_inFile.seekg(0, m_inFile.end);
        const auto length = m_inFile.tellg();
        if (!length)
        {
            m_LastErrorStr = "Input file is empty.";
            return false;
        }
        m_inFile.seekg(0, m_inFile.beg);

        m_nextId = 0;
        m_HashList.clear();
        m_LastErrorStr.clear();
        m_bError.store(false);

        bResult = true;
        try
        {
            ull id = 0;
            for (ull i = 0; i < static_cast<ull>(length); i = i + m_blockSize)
            {
                CalculateMultiThread(id, m_blockSize);
                id++;
                if (m_bError.load())
                {
                    bResult = false;
                    break;
                }
            }
            if (bResult)
            {
                if (const auto ost = length % m_blockSize)
                {
                    CalculateMultiThread(id, ost);
                }
            }
        }
        catch (std::length_error& ex)
        {
            bResult = false;
            m_LastErrorStr = ex.what();
        }

        for (unsigned i = 0; i < m_nMaxThread; i++)
        {
            if (m_threads[i].joinable())
            {
                m_threads[i].join();
            }
        }

        if (!m_HashList.empty() && bResult)
        {
            m_HashList.sort();
            if (m_HashList.begin()->first != m_nextId)
            {
                bResult = false;
            }
            else
            {
                for (const auto& elem : m_HashList)
                {
                    m_outFile << std::hex << elem.second << std::endl;
                }
            }
        }
    }
    return bResult;
}