#pragma once
#include <mutex>
#include <fstream>
#include <memory>
#include <vector>
#include <list>
#include <thread>
#include <atomic>
#include <string>

class CThreadFileHasher
{
    using ull = unsigned long long;
public:
    CThreadFileHasher(const std::string& inputFilePath, const std::string& outputFilePath, ull blockSize);
    ~CThreadFileHasher();
    CThreadFileHasher(const CThreadFileHasher&) = delete;
    CThreadFileHasher& operator=(CThreadFileHasher&) = delete;

    void CalculateHashThread(int num_of_thread, ull len, ull id);
    bool CalculateHash();
    std::string GetLastError() const { return m_LastErrorStr; }
private:
    const unsigned m_nMaxThread;
    const unsigned m_nHashListCheckQuantity;
    unsigned m_nCurrThread;
    std::vector<std::atomic_bool> m_isDone;
    std::vector<std::unique_ptr<char[]>> m_buffers;
    std::vector<std::thread> m_threads;
    std::mutex m_mtx;
    std::ifstream m_inFile;
    std::ofstream m_outFile;
    const ull m_blockSize;
    using CListHashPair = std::pair<ull, ull>;
    std::list<CListHashPair> m_HashList;
    ull m_nextId;
    std::string m_LastErrorStr;
    std::atomic_bool m_bError;

    void CalculateMultiThread(ull id, ull len);
    void WriteHash(ull id, ull hash);

    // Function for testing
    void CalculateCurrentThread(ull id, ull len);  
};
