// FileSignature.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include <algorithm>
#include "FileHashCounter.h"

bool is_number(const std::string& s)
{
    return !s.empty() && std::all_of(s.begin(), s.end(), ::isdigit);
}

constexpr unsigned long long MB = 1024 * 1024;

int main(int argc, char* argv[])
{
    if (argc < 3) {
        std::cout << "Parameters should be: FileSignature.exe <input file> <output file> <block size>\n";
        return 1;
    }

    const std::string input_file = argv[1];
    const std::string output_file = argv[2];
    unsigned long long block_size = MB;
    if (argc >= 4)
    {
        if (!is_number(argv[3])) {
            std::cout << "Parameter <block size> should be integer\n";
            return 1;
        }
        block_size = atoll(argv[3]);
    }

    try
    {
        auto ptr = std::make_unique<char[]>(block_size);
        auto buf = ptr.get();
    }
    catch (std::bad_alloc& /*ex*/)
    {
        std::cout << "Cannot allocate memory. Maybe the block size is too big.\n";
        return 1;
    }

    CThreadFileHasher file_hasher(input_file, output_file, block_size);
    if (!file_hasher.CalculateHash())
    {
        std::cout << file_hasher.GetLastError() << std::endl;
        return 1;
    }
    std::cout << "Hash has been counted.\n";
    return 0;
}

