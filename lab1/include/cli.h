#pragma once

#include "bfloat16.h"

#include <string>

class CLI {
public:
    void run();

private:
    void m_printMenu() const;
    void m_printHeader(const std::string& title) const;
    void m_printInfo(const std::string& message) const;
    void m_printError(const std::string& message) const;

    int m_readInt(const std::string& prompt, int min, int max) const;
    double m_readDouble(const std::string& prompt) const;
    std::string m_readBitString(const std::string& prompt) const;

    void m_menuDecimalToBFloat16() const;
    void m_menuBFloat16ToDecimal() const;
    void m_printNumber(const BFloat16& value) const;
};
