#pragma once

#include <cstdint>
#include <string>
#include <string_view>

struct BFloat16 {
    bool sign{};
    std::uint8_t exponent{};
    std::uint8_t mantissa{};
};

class BFloat16Converter {
public:
    static constexpr int EXPONENT_BITS = 8;
    static constexpr int MANTISSA_BITS = 7;
    // бит в числе: 1 - знаковый бит
    // 2-8 мантисса
    // 9-16 - экспонента
    static constexpr int TOTAL_BITS = 16;
    static constexpr int BIAS = 127; // сдвиг экпоненты

    static BFloat16 encode(double value);
    static double decode(const BFloat16& value);

    static BFloat16 fromBitString(std::string_view bits);
    static std::string toBitString(const BFloat16& value);
    static std::string describe(const BFloat16& value);

private:
    static std::string unsignedIntegerToBinary(std::uint64_t value);
    static std::string fractionalPartToBinary(double value);
    static void normalize(const std::string& integerBits,
                          const std::string& fractionalBits,
                          int& exponent,
                          std::string& mantissaSource);
    static std::uint8_t roundMantissa(const std::string& bits,
                                      bool& exponentCarry);
    static double mantissaToFraction(std::uint8_t mantissa);
};
