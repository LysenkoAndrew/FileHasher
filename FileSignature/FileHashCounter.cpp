#include "FileHashCounter.h"


CThreadFileHasher::CThreadFileHasher(const std::string& inputFilePath, const std::string& outputFilePath, ull blockSize)
    : m_nMaxThread(std::thread::hardware_concurrency()), m_nCurrThread(0), m_nextId(0), m_blockSize(blockSize),
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
            std::hash<const char*> hash_fn;
            auto crc = hash_fn(pBuf);
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
                m_threads[m_nCurrThread] = std::thread(&CThreadFileHasher::CalculateHashThread, this, m_nCurrThread, m_blockSize, id);
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

        // Check first 10 values in list
        int i = 0;
        for (auto ptr = m_HashList.cbegin(); ptr != m_HashList.cend() && i < 10; ++i)
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
            std::hash<const char*> hash_fn;
            auto crc = hash_fn(pBuf);
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